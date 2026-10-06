// ============================================================================
// go2_serve —— 无界面的群控服务
//
// 为什么需要它：Web 界面（assets/web 那一套，以及移植到别的项目里的版本）
// 只是皮肤。真正的控制逻辑 —— WebRTC 数据通道、9991/8081 信令握手、
// AES 钥匙、动作指令表 —— 全在 core/。桌面版 go2_remote 把这些能力和
// ImGui 界面编进了同一个可执行文件，于是"只想用网页控制"也不得不把整个
// 图形程序跑起来。
//
// 这个目标把两者拆开：core + HTTP 服务 + 一个不碰图形库的 main。
// **不链接 ImGui / GLFW / OpenGL**，服务器上、容器里、CI 里都能跑。
//
// 用法：
//   ./go2_serve 192.168.123.161,192.168.123.162
//   ./go2_serve --port 8123 --keys <32位hex>,<32位hex> --ipv4
//   ./go2_serve --host 127.0.0.1          # 只允许本机访问（默认）
//
// 启动后会打印实际监听地址；网页端连 /api/state 与 /api/command 即可。
// 设备也可以在服务起来之后由网页端添加 / 扫描，不必写在命令行里。
// ============================================================================

#include "local_keys.hpp"
#include "robot_client.hpp"
#include "robot_manager.hpp"
#include "ui.hpp"
#include "web_bridge.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

std::atomic<bool> g_stopRequested{false};

void handleSignal(int) { g_stopRequested = true; }

/// setenv 在 Windows 上不存在，统一走这个包装。
void setEnvVar(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    ::setenv(name, value, 1);
#endif
}

std::vector<std::string> splitList(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char ch : s) {
        if (ch == ',' || ch == ';' || ch == ' ' || ch == '\t') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur += ch;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

struct Options {
    std::vector<std::string> ips;
    std::vector<std::string> keys;
    int port = 8123;
    int staggerMs = 600;
    bool ipv4Only = false;
    bool bindRoute = false;
    /// 绑定地址。默认只监听本机 —— 这个服务**没有鉴权**，别随手暴露到局域网。
    std::string host = "127.0.0.1";
    bool quiet = false;
};

void printUsage() {
    std::printf(R"(go2_serve —— 无界面群控服务（Web 界面的后端）

用法: go2_serve [选项] [ip1,ip2,...]

选项:
  --port N              HTTP 服务端口（默认 8123；被占用会自动往后试 5 个）
  --host ADDR           绑定地址（默认 127.0.0.1，只允许本机访问）
  --keys HEX,HEX        直接提供每设备 AES-128 钥匙（data2=3 新固件）
  --stagger MS          错峰连接间隔（默认 600ms；机器狗信令服务单线程）
  --bind-route          按路由绑定本机网卡（多网卡 / WSL / Hyper-V 环境）
  --ipv4                只保留 IPv4 ICE 候选
  --quiet               不在 stdout 重复输出运行日志（仍可通过 /api/state 读取）
  -h, --help            显示本帮助

环境变量:
  GO2_BIND_ROUTE=1      等价于 --bind-route
  GO2_IPV4_ONLY=1       等价于 --ipv4

钥匙来源（按优先级）：--keys → 环境变量 GO2_KEYS_FILE / GO2_AES_KEYS
                    → 本机安全存储 ~/.go2/keys.json

说明: 设备也可以等 HTTP 服务起来之后由网页端添加或扫描，不必写在命令行里。
)");
}

Options parseArgs(int argc, char** argv) {
    Options o;
    // 监听地址也认环境变量（与桌面端读的 GO2_WEB_HOST 同一个名字）。
    // 否则会出现"设了 GO2_WEB_HOST=0.0.0.0 却被 --host 的默认值静默覆盖"的坑
    // —— 命令行 --host 仍然优先。
    if (const char* h = std::getenv("GO2_WEB_HOST"); h && *h) o.host = h;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s 需要一个参数\n", name);
                std::exit(1);
            }
            return argv[++i];
        };
        if (a == "--port") o.port = std::atoi(next("--port").c_str());
        else if (a == "--host") o.host = next("--host");
        else if (a == "--stagger") o.staggerMs = std::atoi(next("--stagger").c_str());
        else if (a == "--keys") {
            for (auto& k : splitList(next("--keys"))) o.keys.push_back(k);
        } else if (a == "--bind-route") o.bindRoute = true;
        else if (a == "--ipv4") o.ipv4Only = true;
        else if (a == "--quiet") o.quiet = true;
        else if (a == "-h" || a == "--help") {
            printUsage();
            std::exit(0);
        } else if (!a.empty() && a[0] == '-') {
            std::fprintf(stderr, "未知选项: %s（用 --help 查看帮助）\n", a.c_str());
            std::exit(1);
        } else {
            for (auto& ip : splitList(a)) o.ips.push_back(ip);
        }
    }
    return o;
}

}  // namespace

int main(int argc, char** argv) {
    const Options opt = parseArgs(argc, argv);

    // 这两个开关必须在建连接之前设好：它们被 core 在握手/收集候选时读取。
    if (opt.bindRoute) setEnvVar("GO2_BIND_ROUTE", "1");
    if (opt.ipv4Only) setEnvVar("GO2_IPV4_ONLY", "1");
    // 服务无鉴权，默认收紧到本机；web_bridge 会读这个变量决定 bind 地址。
    setEnvVar("GO2_WEB_HOST", opt.host.c_str());

    go2::RobotManager manager;
    go2::UiState ui;
    ui.loadNames();  // 设备名映射（robot_names.json）—— 界面版是在绘制时惰性加载的

    std::mutex printMutex;
    auto printLine = [&ui, &opt, &printMutex](const std::string& line) {
        ui.addLog(line);
        if (opt.quiet) return;
        std::lock_guard<std::mutex> lock(printMutex);
        std::printf("%s\n", line.c_str());
        std::fflush(stdout);
    };

    // ---- 每创建一个机器狗客户端，就接线它的回调 ----
    // 桌面版这段在 apps/desktop/main.cpp 里，和窗口循环写在一起；这里只保留
    // 控制必需的部分：日志、就绪后订阅状态主题、动作回执、状态更新。
    manager.onCreated = [&printLine, &ui](go2::RobotClient& c) {
        go2::RobotClient* pc = &c;  // ip() 要 connect 之后才有值，捕获指针而非值
        c.onLog = [&printLine, pc](const std::string& line) {
            printLine("[" + pc->ip() + "] " + line);
        };

        c.onStateChanged = [pc, &printLine](go2::ConnState s) {
            if (s != go2::ConnState::Ready) return;
            printLine("[" + pc->ip() + "] 通道就绪，订阅机器人状态");
            // 不同固件发布的状态主题不同：高频 / 低频两套都订上
            pc->subscribe("rt/sportmodestate");
            pc->subscribe("rt/lf/sportmodestate");
            pc->subscribe("rt/lf/lowstate");
        };

        // 动作回执 → 网页端据此标注"这条动作能不能用"（✓ / ✗ + 失败原因）
        c.onSportAck = [&ui](int apiId, int code, const std::string& note) {
            ui.noteApiResult(apiId, code, note);
        };

        c.onTopicData = [&ui, pc](const std::string& topic, const nlohmann::json& msg) {
            float battery = -1.0f;
            std::string mode;
            try {
                const auto& d = msg.contains("data") ? msg["data"] : nlohmann::json();
                if (topic.find("sportmodestate") != std::string::npos) {
                    if (d.contains("mode_name")) mode = d["mode_name"].get<std::string>();
                    else if (d.contains("mode")) mode = d["mode"].dump();
                    if (d.contains("battery_level")) battery = d["battery_level"].get<float>();
                } else if (topic.find("lowstate") != std::string::npos) {
                    if (d.contains("bms_state") && d["bms_state"].contains("soc"))
                        battery = d["bms_state"]["soc"].get<float>();
                }
            } catch (...) {
                return;  // 状态字段结构可能随固件变化，解析失败就跳过这一帧
            }
            ui.updateStatus(pc->ip(), battery, mode);
        };
    };

    // ---- 钥匙：命令行 → 本机安全存储（~/.go2/keys.json）----
    manager.loadKeyCache();
    if (!opt.keys.empty()) manager.addAesKeys(opt.keys);
    {
        const auto lk = go2::loadLocalAesKeys();
        ui.localKeyCount = static_cast<int>(lk.keys.size());
        if (!lk.keys.empty()) manager.setAesKeys(lk.keys);
        printLine("[钥匙] 已装载 " + std::to_string(manager.aesKeys().size()) +
                  " 把（含本机缓存）");
        if (lk.keys.empty() && opt.keys.empty())
            printLine("[钥匙] 提示：新固件机器狗需要 AES key —— 写进 ~/.go2/keys.json，"
                      "或用 --keys 传入");
    }

    // ---- 命令行传入的初始设备 ----
    if (!opt.ips.empty()) {
        for (const auto& ip : opt.ips) ui.addOrUpdate(ip, true);
        printLine("[服务] 初始设备 " + std::to_string(opt.ips.size()) + " 台，开始连接 ...");
        manager.connectAll(opt.ips, opt.staggerMs);
    } else {
        printLine("[服务] 未指定初始设备 —— 打开网页后可用「设备管理」添加或扫描局域网");
    }

    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    // ---- 起 HTTP 服务（后台线程；被占用会自动往后试端口）----
    if (!go2::startWebUi(manager, ui, opt.port)) {
        std::fprintf(stderr, "启动 HTTP 服务失败（端口 %d~%d 都被占用？）\n", opt.port,
                     opt.port + 4);
        return 1;
    }
    printLine("[服务] 群控服务已启动，按 Ctrl+C 退出");

    // ---- 主循环：只等信号。真正的活儿都在 core 的线程里 ----
    int tick = 0;
    while (!g_stopRequested) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (++tick % 60 != 0) continue;  // 每 30 秒打一行存活摘要
        int devices = 0;
        {
            // robots 由 manager 的线程在写，读长度也要走同一把锁。
            std::lock_guard<std::mutex> lock(ui.robotsMutex);
            devices = static_cast<int>(ui.robots.size());
        }
        const int selected = ui.selectedCount();
        printLine("[服务] 存活中：设备 " + std::to_string(devices) + " 台，受控 " +
                  std::to_string(selected) + " 台" + (ui.estop ? "，急停锁定中" : ""));
    }

    printLine("[服务] 正在退出 ...");
    go2::stopWebUi();
    // 扫描线程按引用持有 manager/ui → 必须等它结束，否则可能在析构后访问（use-after-free）
    go2::joinScans();
    return 0;
}
