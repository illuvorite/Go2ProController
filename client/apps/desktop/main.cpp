#include "local_keys.hpp"
#include "robot_manager.hpp"
#include "sport_library.hpp"
#include "theme.hpp"
#include "ui.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <GLFW/glfw3.h>

#include <nlohmann/json.hpp>

#include <atomic>
#include <csignal>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {
// 收到 SIGINT/SIGTERM 时请求优雅退出，保证 SCTP 正常关闭、
// 否则机器人侧会残留“僵尸连接”，导致后续连不上（同一时刻只允许一条连接）
volatile std::sig_atomic_t g_stopRequested = 0;
void handleSignal(int) { g_stopRequested = 1; }

/// 命令行选项
struct Options {
    std::vector<std::string> ips;
    std::vector<std::string> keys;
    bool verify = false;      // 无界面验证模式（不需要图形环境）
    bool actions = false;     // 验证时对每台下发安全动作，检查指令回执
    bool probe = false;       // 动作可用性探测（只发原地不动的安全指令）
    bool bindRoute = false;   // 按路由绑定本机网卡（多网卡/虚拟网卡场景）
    bool ipv4Only = false;    // 只保留 IPv4 ICE 候选
    bool dumpState = false;   // 打印机器人状态帧（诊断用）
    std::string setMode;      // 连接就绪后切换运动模式（normal / ai 等）
    int seconds = 60;         // 验证时长
    int staggerMs = 600;      // 错峰连接间隔
    int settleMs = 2500;      // 就绪后等多久再下发动作（刚就绪时会被拒）
};

void printUsage() {
    std::printf(R"(Unitree Go2 控制管理台

用法:
  go2_remote [ip ...]                     图形界面，参数为预连接的目标
  go2_remote --verify ip1,ip2,ip3 [选项]  无界面验证模式（三台一起验证用）

选项:
  --verify             无界面验证模式：连接全部目标并输出稳定性报告
  --seconds N          验证时长（默认 60 秒）
  --stagger MS         错峰连接间隔（默认 600ms；机器狗信令服务单线程）
  --settle MS          就绪后等多久再动作（默认 2500ms；刚就绪时指令会被拒）
  --actions            验证时对每台执行安全动作并校验指令回执
  --probe              动作可用性探测：逐条下发"原地不动脚"的安全指令并报告回执
                       （查询类 / 步态·速度·身高 / 打招呼；含 MCF 与普通指令集自动回退验证）
  --set-mode NAME      就绪后切换运动模式（normal / ai），排查动作被拒
  --dump-state         打印机器人状态帧（运动模式/步态/错误码，诊断用）
  --keys HEX,HEX       直接提供每设备 AES-128 钥匙（data2=3 新固件）
  --bind-route         按路由绑定本机网卡（多网卡 / WSL / Hyper-V 环境）
  --ipv4               只保留 IPv4 ICE 候选
  -h, --help           显示本帮助

环境变量:
  GO2_BIND_ROUTE=1     等价于 --bind-route
  GO2_IPV4_ONLY=1      等价于 --ipv4
  GO2_KEEP_CANDIDATES=addr1,addr2   只保留这些本地地址的 ICE 候选

退出码（--verify）: 0 = 全部通过；2 = 有设备未达标
)");
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

Options parseArgs(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s 需要一个参数\n", name);
                std::exit(1);
            }
            return argv[++i];
        };
        if (a == "--verify") o.verify = true;
        else if (a == "--actions") o.actions = true;
        else if (a == "--probe") o.probe = true;
        else if (a == "--bind-route") o.bindRoute = true;
        else if (a == "--ipv4") o.ipv4Only = true;
        else if (a == "--dump-state") o.dumpState = true;
        else if (a == "--set-mode") o.setMode = next("--set-mode");
        else if (a == "--seconds") o.seconds = std::atoi(next("--seconds").c_str());
        else if (a == "--stagger") o.staggerMs = std::atoi(next("--stagger").c_str());
        else if (a == "--settle") o.settleMs = std::atoi(next("--settle").c_str());
        else if (a == "--keys") {
            for (auto& k : splitList(next("--keys"))) o.keys.push_back(k);
        } else if (a == "-h" || a == "--help") {
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

/// 延时辅助
void sleepFor(std::chrono::milliseconds ms) { std::this_thread::sleep_for(ms); }

// 字体与主题在 theme.cpp（三级字号 + 现代深色配色）

// ============================================================ 无界面验证模式

struct VerifyRecord {
    int acksOk = 0;
    int acksFail = 0;
    long long lastStateRxMs = 0;   // 最近一次收到状态主题
    long long stateRxCount = 0;
    std::map<std::string, int> stateDumps;  // topic -> 已打印帧数（--dump-state）
};

int runVerify(const Options& opt) {
    if (opt.ips.empty()) {
        std::fprintf(stderr, "用法: go2_remote --verify ip1,ip2,ip3 [--seconds 60]\n");
        return 1;
    }

    go2::RobotManager mgr;
    mgr.loadKeyCache();
    if (!opt.keys.empty()) mgr.addAesKeys(opt.keys);
    if (!mgr.aesKeys().empty())
        std::printf("[钥匙] 已装载 %zu 把（含本机缓存 go2_keys_cache.json）\n",
                    mgr.aesKeys().size());

    std::mutex recMutex;
    std::map<std::string, VerifyRecord> recs;
    std::atomic<long long> firstReadyMs{0};  // 首个设备就绪时刻（重试耗时不计入稳定判定）
    std::mutex printMutex;
    auto printLine = [&printMutex](const std::string& line) {
        std::lock_guard<std::mutex> lock(printMutex);
        std::printf("%s\n", line.c_str());
        std::fflush(stdout);
    };

    const long long startMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now().time_since_epoch())
                                  .count();

    if (opt.bindRoute) {
        ::setenv("GO2_BIND_ROUTE", "1", 1);
        std::printf("[选项] 按路由绑定本机网卡（多网卡/虚拟网卡环境）\n");
    }
    if (opt.ipv4Only) {
        ::setenv("GO2_IPV4_ONLY", "1", 1);
        std::printf("[选项] 只保留 IPv4 ICE 候选\n");
    }

    mgr.onCreated = [&](go2::RobotClient& c) {
        // 注意：client 创建时 ip() 还是空的（connect 时才赋值），
        // 因此这里捕获对象指针、在日志里再取 ip()。
        go2::RobotClient* pc = &c;
        c.setRawAckLogging(opt.dumpState);
        c.onLog = [&printLine, pc](const std::string& line) {
            printLine("[" + pc->ip() + "] " + line);
        };
        c.onStateChanged = [pc, &printLine, &recMutex, &recs, &firstReadyMs](go2::ConnState s) {
            if (s != go2::ConnState::Ready) return;
            printLine("[" + pc->ip() + "] 通道就绪，订阅状态主题");
            // 不同固件发布的状态主题不同：高频 / 低频两套都订上（PowerPro 等固件只发 rt/lf/*）
            pc->subscribe("rt/sportmodestate");
            pc->subscribe("rt/lf/sportmodestate");
            pc->subscribe("rt/lf/lowstate");
            long long expect = 0;
            const long long nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::steady_clock::now().time_since_epoch())
                                        .count();
            firstReadyMs.compare_exchange_strong(expect, nowMs);
            std::lock_guard<std::mutex> lock(recMutex);
            recs[pc->ip()];  // 建立记录
        };
        c.onTopicData = [&recMutex, &recs, &printLine, pc, dumpState = opt.dumpState](
                            const std::string& topic, const nlohmann::json& msg) {
            const std::string ip = pc->ip();
            {
                std::lock_guard<std::mutex> lock(recMutex);
                auto& r = recs[ip];
                if (topic.find("sportmodestate") != std::string::npos ||
                    topic.find("lowstate") != std::string::npos) {
                    r.stateRxCount++;
                    int& dumped = r.stateDumps[topic];
                    if (dumpState && dumped < 2) {
                        ++dumped;
                        std::string body = msg.contains("data") ? msg["data"].dump() : msg.dump();
                        if (body.size() > 700) body = body.substr(0, 700) + " ...";
                        printLine("[" + ip + "] [状态帧 " + topic + "] " + body);
                    }
                }
            }
            // 指令回执（--actions 的动作验证靠它判定）
            if (topic.find("/response") == std::string::npos) return;
            int code = -1, apiId = -1;
            try {
                code = msg.at("data").at("header").at("status").at("code").get<int>();
                apiId = msg.at("data").at("header").at("identity").at("api_id").get<int>();
            } catch (...) {}
            {
                std::lock_guard<std::mutex> lock(recMutex);
                if (code == 0) recs[ip].acksOk++;
                else recs[ip].acksFail++;
            }
            const std::string nm = go2::labelForApiId(apiId);
            printLine("[" + ip + "] [回执] " + (nm.empty() ? std::string() : nm + " ") +
                      "(api " + std::to_string(apiId) + ") code=" + std::to_string(code) +
                      (code == 0 ? " 成功" : " 失败"));
        };
    };

    go2::RobotProfile base;
    base.ipv4Only = opt.ipv4Only;

    std::printf("===== Go2 多机连接验证 =====\n");
    std::printf("目标 %zu 台，验证 %d 秒，错峰 %d ms%s\n", opt.ips.size(), opt.seconds,
                opt.staggerMs, opt.actions ? "，含动作回执校验" : "");
    for (size_t i = 0; i < opt.ips.size(); ++i)
        std::printf("  %zu) %s\n", i + 1, opt.ips[i].c_str());
    std::printf("\n");

    // 每台的档案：Air = STA 默认；Pro 若走热点需自行改为 AP（192.168.12.1）
    for (size_t i = 0; i < opt.ips.size(); ++i) {
        go2::RobotProfile p = base;
        p.ip = opt.ips[i];
        mgr.connect(opt.ips[i], p);
        if (i + 1 < opt.ips.size()) sleepFor(std::chrono::milliseconds(opt.staggerMs));
    }

    // ---- 动作验证：每台就绪后按「平衡站立 → 打招呼 → 停止」逐步验证 ----
    std::atomic<bool> actionsDone{false};
    std::thread actionsThread;
    if (opt.actions || opt.probe || !opt.setMode.empty()) {
        actionsThread = std::thread([&mgr, &opt, &actionsDone, &printLine] {
            for (int waited = 0; waited < 60 * 1000 && !g_stopRequested; waited += 500) {
                bool allReady = true;
                for (const auto& ip : opt.ips) {
                    if (auto* c = mgr.find(ip); !c || !c->isReady()) { allReady = false; break; }
                }
                if (allReady) break;
                sleepFor(std::chrono::milliseconds(500));
            }
            if (!opt.setMode.empty()) {
                for (const auto& ip : opt.ips) {
                    if (auto* c = mgr.find(ip); c && c->isReady()) {
                        printLine("[" + ip + "] 查询当前运动模式 ...");
                        c->queryMotionMode();
                        sleepFor(std::chrono::milliseconds(800));
                        printLine("[" + ip + "] 切换运动模式 -> " + opt.setMode);
                        c->setMotionMode(opt.setMode);
                        sleepFor(std::chrono::milliseconds(1200));
                    }
                }
            }
            if (opt.probe) {
                // 动作可用性探测：只发"原地不动脚"的指令，逐条报告回执，
                // 其中 1035（经济步态）故意用 normal 的 id —— 若自动回退到 MCF 1063 成功，
                // 说明"指令集不匹配导致动作用不了"的自动兜底生效。
                printLine("等待链路稳定 " + std::to_string(opt.settleMs) + " ms ...");
                sleepFor(std::chrono::milliseconds(opt.settleMs));

                struct Item {
                    int apiId;
                    const char* label;
                    const char* param;
                };
                static const Item kItems[] = {
                    {1002, "平衡站立", ""},
                    {1024, "查机身高度", ""},
                    {1025, "查抬腿高度", ""},
                    {1026, "查速度档位", ""},
                    {1034, "查运动状态", ""},
                    {2055, "查自动恢复（MCF 专属）", ""},
                    {1011, "切换步态=1", "{\"data\":1}"},
                    {1015, "速度档位=1", "{\"data\":1}"},
                    {1019, "持续步态", "{\"data\":true}"},
                    {1035, "经济步态（故意用 normal 的 1035）", "{\"data\":true}"},
                    {1013, "机身高度 0.28m", "{\"data\":0.28}"},
                    {1014, "抬腿高度 0.06m", "{\"data\":0.06}"},
                    {1016, "打招呼", ""},
                    {1003, "停止移动", ""},
                };
                constexpr size_t kCount = sizeof(kItems) / sizeof(kItems[0]);

                for (size_t i = 0; i < opt.ips.size(); ++i) {
                    auto* c = mgr.find(opt.ips[i]);
                    if (!c || !c->isReady()) {
                        printLine("[" + opt.ips[i] + "] 未就绪，跳过动作探测");
                        continue;
                    }
                    printLine("[" + opt.ips[i] + "] —— 动作可用性探测开始（" +
                              std::to_string(kCount) + " 条安全指令）——");
                    c->balanceStand();
                    sleepFor(std::chrono::milliseconds(1500));
                    for (const auto& it : kItems) {
                        nlohmann::json param;
                        if (it.param[0] != '\0') {
                            try {
                                param = nlohmann::json::parse(it.param);
                            } catch (...) {
                            }
                        }
                        printLine("[" + opt.ips[i] + "] 探测: " + it.label + " (api " +
                                  std::to_string(it.apiId) + ")");
                        c->sendSportCommand(it.apiId, param);
                        sleepFor(std::chrono::milliseconds(1600));
                    }
                    printLine("[" + opt.ips[i] + "] —— 动作探测结束 ——");
                }
            } else if (!opt.actions) {
                printLine("（未启用 --actions/--probe，只做了模式切换）");
            } else {
                // 关键：WebRTC 校验刚通过时立刻下发运动指令会被拒（回执 code=-1），
                // 稳定 2 秒后再发即可成功（实测 Go2 Pro / mcf 模式固件）。
                printLine("等待链路稳定 " + std::to_string(opt.settleMs) +
                          " ms（刚就绪时下发指令会被拒）...");
                sleepFor(std::chrono::milliseconds(opt.settleMs));
                printLine("—— 动作验证开始（每台：平衡站立 -> 打招呼 -> 停止）——");
                for (size_t i = 0; i < opt.ips.size(); ++i) {
                    if (auto* c = mgr.find(opt.ips[i]); c && c->isReady()) {
                        printLine("[" + opt.ips[i] + "] BalanceStand");
                        c->balanceStand();
                        sleepFor(std::chrono::milliseconds(1500));
                        printLine("[" + opt.ips[i] + "] Hello");
                        c->hello();
                        sleepFor(std::chrono::milliseconds(2000));
                        printLine("[" + opt.ips[i] + "] StopMove");
                        c->stopMove();
                    } else {
                        printLine("[" + opt.ips[i] + "] 未就绪，跳过动作验证");
                    }
                    if (i + 1 < opt.ips.size()) sleepFor(std::chrono::milliseconds(400));
                }
                printLine("—— 动作验证结束 ——");
            }
            actionsDone = true;
        });
    }

    // ---- 等待验证时长，期间每 10 秒打印一次进度 ----
    const long long endMs = startMs + (long long)opt.seconds * 1000;
    long long nextProgress = startMs + 10000;
    bool actionsEarlyStop = false;
    long long earlyStopAt = 0;
    while (!g_stopRequested) {
        const long long now = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now().time_since_epoch())
                                  .count();
        if ((opt.actions || opt.probe) && actionsDone.load() && !actionsEarlyStop) {
            // 动作验证完成后，继续观测 10 秒数据流即可收尾
            actionsEarlyStop = true;
            earlyStopAt = now + 10000;
        }
        if (actionsEarlyStop && now >= earlyStopAt) break;
        if (now >= endMs) break;
        if (now >= nextProgress) {
            nextProgress += 10000;
            int ready = 0;
            for (const auto& s : mgr.snapshot())
                if (s.state == go2::ConnState::Ready) ++ready;
            printLine("[进度] " + std::to_string((now - startMs) / 1000) + "s: 就绪 " +
                      std::to_string(ready) + "/" + std::to_string(opt.ips.size()));
        }
        sleepFor(std::chrono::milliseconds(200));
    }
    if (actionsThread.joinable()) actionsThread.join();

    // ---- 汇总报告 ----
    const auto snaps = mgr.snapshot();
    const long long endAll = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now().time_since_epoch())
                                 .count();
    const long long totalSeconds = (endAll - startMs) / 1000;
    // 稳定率从「首台就绪」开始算，避免限流重试耗时把达标率拉低造成误判
    const long long readyBase = firstReadyMs.load() > 0 ? firstReadyMs.load() : startMs;
    const long long measured = (endAll - readyBase) / 1000;

    std::printf("\n===== 多机连接验证报告 =====\n");
    std::printf("%-16s %-6s %-6s %-6s %-16s %-9s %-8s %-9s %-9s %-6s\n", "IP", "状态", "端口",
                "data2", "钥匙来源", "稳定(s)", "收包", "最近(ms)", "心跳ok/发", "重连");
    int pass = 0;
    int measuredOk = 0;
    for (const auto& s : snaps) {
        const char* keyInfo = s.stats.keySource.empty() ? "-" : s.stats.keySource.c_str();
        const std::string hb =
            std::to_string(s.stats.heartbeatsAcked) + "/" + std::to_string(s.stats.heartbeatsSent);
        std::printf("%-16s %-6s %-6d %-6d %-16s %-9lld %-8zu %-9lld %-9s %-6d\n",
                    s.ip.c_str(), go2::toString(s.state),
                    s.stats.signalingPort, s.stats.data2, keyInfo,
                    (long long)s.stats.readySeconds, s.stats.rxMessages,
                    s.stats.lastRxAgeMs, hb.c_str(), s.stats.reconnects);
        {
            std::lock_guard<std::mutex> lock(recMutex);
            auto it = recs.find(s.ip);
            if (it != recs.end()) {
                std::printf("%-16s   状态主题帧 %lld 条", "", it->second.stateRxCount);
                if (opt.actions)
                    std::printf("，动作回执 成功 %d / 失败 %d", it->second.acksOk,
                                it->second.acksFail);
                std::printf("\n");
            }
        }
        const bool ready = s.state == go2::ConnState::Ready;
        const bool streamOk = s.stats.lastRxAgeMs >= 0 && s.stats.lastRxAgeMs < 5000;
        const bool stable = s.stats.readySeconds >= (measured * 8) / 10;
        if (ready && streamOk && stable) ++pass;
        if (ready && streamOk) ++measuredOk;
        if (!s.lastError.empty()) {
            // 错误信息可能多行，压成一行便于阅读
            std::string err = s.lastError;
            for (char& ch : err)
                if (ch == '\n' || ch == '\r') ch = ' ';
            if (err.size() > 150) err = err.substr(0, 150) + "...";
            std::printf("%-16s   最近错误: %s\n", "", err.c_str());
        }
    }

    std::printf("\n判定标准: 就绪 + 最近 5 秒内有数据 + 在线时长 >= 测量时长的 80%%\n");
    std::printf("测量口径: 总时长 %lld 秒，首台就绪后计时 %lld 秒\n", totalSeconds, measured);
    std::printf("结论: %s —— %d/%zu 台达标，另有 %d 台在线且有数据流\n",
                pass == (int)opt.ips.size() ? "PASS" : "FAIL", pass, snaps.size(), measuredOk);
    if (opt.actions)
        std::printf("提示: 动作回执 code=0 为成功；若无回执说明指令通道有问题。\n");
    std::printf("钥匙缓存: go2_keys_cache.json（IP -> 每设备 AES-128 key，下次启动自动复用）\n");

    mgr.shutdown();
    for (const auto& ip : opt.ips) mgr.disconnect(ip);
    return pass == (int)opt.ips.size() ? 0 : 2;
}

// ============================================================ 图形界面模式

int runGui(const Options& opt) {
    if (!glfwInit()) {
        std::fprintf(stderr, "GLFW 初始化失败（无图形环境？可改用 --verify 无界面验证）\n");
        return 1;
    }

    const char* glslVersion = "#version 330";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glslVersion = "#version 150";
#endif

    GLFWwindow* window = glfwCreateWindow(1440, 880, "Unitree Go2 控制管理台", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "创建窗口失败（无显示环境？）\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    // ---- 诊断：窗口尺寸 / OpenGL 上下文是否健康 ----
    {
        int ww = 0, wh = 0, fw = 0, fh = 0;
        glfwGetWindowSize(window, &ww, &wh);
        glfwGetFramebufferSize(window, &fw, &fh);
        const char* glVer = reinterpret_cast<const char*>(glGetString(GL_VERSION));
        const char* glRen = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        std::printf("[窗口] size=%dx%d framebuffer=%dx%d\n", ww, wh, fw, fh);
        std::printf("[GL] version=%s | renderer=%s\n",
                    glVer ? glVer : "(null)", glRen ? glRen : "(null)");
        std::fflush(stdout);
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    go2::applyTheme();
    go2::loadUiFonts();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glslVersion);

    go2::RobotManager manager;
    manager.loadKeyCache();
    if (!opt.keys.empty()) manager.addAesKeys(opt.keys);
    go2::UiState ui;

    // 日志：UI 列表 + stdout 双输出
    static std::mutex printMutex;
    auto printLine = [&ui](const std::string& line) {
        ui.addLog(line);
        std::lock_guard<std::mutex> lock(printMutex);
        std::printf("%s\n", line.c_str());
        std::fflush(stdout);
    };

    // 兼容旧自检入口：环境变量触发一次（对第一台就绪的机器狗执行）
    const char* mvEnv = std::getenv("GO2_MOVE_TEST");
    const bool wantMoveTest = mvEnv && *mvEnv && *mvEnv != '0';
    const char* demoEnv = std::getenv("GO2_DEMO");
    const bool wantDemo = demoEnv && *demoEnv && *demoEnv != '0';
    std::atomic<bool> selfTestDone{false};

    // ---- 每创建一个机器狗客户端，就接线它的回调 ----
    manager.onCreated = [&](go2::RobotClient& c) {
        go2::RobotClient* pc = &c;  // 捕获对象指针（ip() 在 connect 后才有值）
        c.onLog = [&printLine, pc](const std::string& line) {
            printLine("[" + pc->ip() + "] " + line);
        };

        c.onStateChanged = [pc, &printLine, wantMoveTest, wantDemo, &selfTestDone](go2::ConnState s) {
            if (s != go2::ConnState::Ready) return;
            printLine("[" + pc->ip() + "] 通道就绪，订阅机器人状态");
            pc->subscribe("rt/sportmodestate");
            pc->subscribe("rt/lf/sportmodestate");  // 部分固件只有低频版
            pc->subscribe("rt/lf/lowstate");

            // 兼容旧自检入口：环境变量触发一次（对第一台就绪的机器狗执行）
            if ((wantMoveTest || wantDemo) && !selfTestDone.exchange(true)) {
                std::thread([pc, &printLine, wantMoveTest] {
                    printLine("[" + pc->ip() + "] —— 自动自检开始 ——");
                    printLine("[" + pc->ip() + "] 平衡站立");
                    pc->balanceStand();
                    sleepFor(std::chrono::seconds(3));
                    if (wantMoveTest) {
                        printLine("[" + pc->ip() + "] Move 前进 0.12 m/s，1.5 秒");
                        pc->move(0.12f, 0, 0);
                        sleepFor(std::chrono::milliseconds(1500));
                        printLine("[" + pc->ip() + "] StopMove");
                        pc->stopMove();
                    } else {
                        printLine("[" + pc->ip() + "] 打招呼");
                        pc->hello();
                        sleepFor(std::chrono::seconds(4));
                        printLine("[" + pc->ip() + "] 停止");
                        pc->stopMove();
                    }
                    printLine("[" + pc->ip() + "] —— 自动自检结束 ——");
                }).detach();
            }
        };

        // 动作回执 → 界面标注"这条动作能不能用"（✓ / ✗ + 失败原因）
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
                return;  // 状态字段结构可能随固件变化，忽略解析失败
            }
            ui.updateStatus(pc->ip(), battery, mode);
        };
    };

    // ---- 本地钥匙（data2=3 新固件用；纯本地，不联网）----
    {
        const auto lk = go2::loadLocalAesKeys();
        ui.localKeyCount = static_cast<int>(lk.keys.size());
        if (lk.keys.empty()) {
            printLine("[钥匙] 未找到本地钥匙 —— 新固件机器狗需要：把 32 位 hex key 写进 keys.json");
        } else {
            manager.setAesKeys(lk.keys);
            // 不输出钥匙文件路径（界面/日志可能被他人看到，只需要知道装了几把）
            printLine("[钥匙] 已加载 " + std::to_string(lk.keys.size()) + " 把（本地安全存储）");
            if (std::getenv("GO2_VERBOSE_KEYS"))
                printLine("[钥匙] 来源: " + lk.source);
        }
    }

    // 命令行传入 IP：作为初始设备加入并错峰连接
    if (!opt.ips.empty()) {
        std::snprintf(ui.manualIp, sizeof(ui.manualIp), "%s", opt.ips[0].c_str());
        for (const auto& ip : opt.ips) ui.addOrUpdate(ip, true);
        printLine("[UI] 初始设备 " + std::to_string(opt.ips.size()) + " 台，开始连接 ...");
        manager.connectAll(opt.ips, opt.staggerMs);
    }

    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    while (!glfwWindowShouldClose(window) && !g_stopRequested) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        go2::drawUi(manager, ui);

        ImGui::Render();
        int fbW = 0, fbH = 0;
        glfwGetFramebufferSize(window, &fbW, &fbH);
        glViewport(0, 0, fbW, fbH);
        glClearColor(0.09f, 0.09f, 0.11f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // ---- 调试截图：GO2_SHOT=1 时把渲染好的帧缓冲落盘（诊断"窗口空白"用）----
        static bool shotDone = false;
        if (!shotDone && std::getenv("GO2_SHOT") && fbW > 0 && fbH > 0) {
            static int frames = 0;
            if (++frames > 60) {  // 60 帧后界面已稳定
                std::vector<unsigned char> px(size_t(fbW) * fbH * 3);
                glPixelStorei(GL_PACK_ALIGNMENT, 1);
                glReadPixels(0, 0, fbW, fbH, GL_RGB, GL_UNSIGNED_BYTE, px.data());
                if (FILE* f = std::fopen("/tmp/frame.ppm", "wb")) {
                    std::fprintf(f, "P6\n%d %d\n255\n", fbW, fbH);
                    std::fwrite(px.data(), 1, px.size(), f);
                    std::fclose(f);
                    // 采样统计：变色点越多说明界面真的画出来了
                    int uniq = 0;
                    for (size_t i = 0; i + 2 < px.size(); i += 997)
                        if (px[i] != px[0] || px[i + 1] != px[1] || px[i + 2] != px[2]) ++uniq;
                    std::printf("[SHOT] /tmp/frame.ppm %dx%d, 采样变色数=%d\n", fbW, fbH, uniq);
                }
                shotDone = true;
            }
        }
        glfwSwapBuffers(window);
    }

    // 退出前：全部停车并优雅断开（避免僵尸连接）
    manager.disconnectAll();
    manager.shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const Options opt = parseArgs(argc, argv);
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);
    if (opt.verify) return runVerify(opt);
    return runGui(opt);
}
