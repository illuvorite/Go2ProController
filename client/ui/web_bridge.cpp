#include "web_bridge.hpp"

#include "command_service.hpp"
#include "robot_client.hpp"
#include "robot_manager.hpp"
#include "sport_library.hpp"
#include "ui.hpp"

#include "key_probe.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

namespace go2 {
namespace {

std::atomic<bool> g_running{false};
httplib::Server* g_svr = nullptr;
std::thread g_thread;

/// 是否是"只有本机能访问"的绑定地址 —— 决定是否需要访问令牌。
bool isLoopbackHost(const std::string& h) {
    return h == "127.0.0.1" || h == "localhost" || h == "::1" || h == "[::1]";
}

/// 请求携带的令牌：优先 `X-Go2-Token` 头，其次 `?token=` 查询参数
/// （查询参数这条是为了让浏览器直接打开 `http://host:端口/?token=xxx` 就能用）。
std::string requestToken(const httplib::Request& req) {
    if (auto it = req.headers.find("X-Go2-Token"); it != req.headers.end()) return it->second;
    if (req.has_param("token")) return req.get_param_value("token");
    return std::string();
}


const char* stateTextOf(ConnState s) {
    switch (s) {
        case ConnState::Disconnected: return "未连接";
        case ConnState::Signaling:    return "信令中";
        case ConnState::Connecting:   return "连接中";
        case ConnState::Validating:   return "校验中";
        case ConnState::Ready:        return "就绪";
        case ConnState::Failed:       return "失败";
    }
    return "?";
}

/// 按英文 key 发一条运动指令给**当前勾选**的设备。
///
/// 语义（指令集 id 兜底、参数打包、持续模式开关维护）统一在 `cmd::dispatchAction` 里 ——
/// 改造前这里是第二份实现，而且**打包参数时把 speedLevel / gaitType / 姿态角 / 自定义 JSON
/// 全丢了**（固定传 0），与桌面端口径不一致。现在两边共用同一份，这类漂移不会再发生。
bool sendActionByKey(RobotManager& mgr, UiState& ui, const std::string& key) {
    ManagerSink sink(mgr, ui);
    const int n = cmd::dispatchAction(sink, ui, key);
    if (n < 0) return false;  // key 不在指令表里
    ui.addLog("[WebUI] 动作 " + key + " → " + std::to_string(n) + " 台");
    return true;
}

nlohmann::json stateJson(RobotManager& mgr, UiState& ui) {
    nlohmann::json j = nlohmann::json::object();
    // ★ 这些成员是 std::atomic（跨线程读写）→ 构造 json 前必须显式 .load()：
    //   atomic 不是 nlohmann::json 的兼容类型，直接塞进去编译不过。
    j["estop"] = ui.estop.load();
    j["mcf"] = ui.mcfMode.load();

    std::vector<RobotEntry> snap;
    {
        std::lock_guard<std::mutex> lock(ui.robotsMutex);
        snap = ui.robots;
    }
    nlohmann::json robots = nlohmann::json::array();
    for (const auto& e : snap) {
        nlohmann::json r = nlohmann::json::object();
        r["ip"] = e.ip;
        r["name"] = ui.nameOf(e.ip);
        r["label"] = ui.labelOf(e.ip);
        r["selected"] = ui.isSelected(e.ip);
        r["battery"] = e.battery;
        r["mode"] = e.modeName;
        if (auto* c = mgr.find(e.ip); c) {
            r["state"] = stateTextOf(c->state());
            r["ready"] = c->isReady();
        } else {
            r["state"] = "未连接";
            r["ready"] = false;
        }
        robots.push_back(r);
    }
    j["robots"] = robots;
    j["selectedCount"] = ui.selectedCount();
    j["target"] = ui.controlTargetText();

    // 电量胶囊取**受控设备里最低的那台**（与 ImGui 顶栏一致）
    float battMin = -1.0f;
    for (const auto& r : robots) {
        if (!r["selected"].get<bool>()) continue;
        const float b = r["battery"].get<float>();
        if (b < 0.0f) continue;
        battMin = (battMin < 0.0f) ? b : std::min(battMin, b);
    }
    j["batteryMin"] = battMin;

    // 网页端要看的遥控参数 / 开关状态 / 最近一次下发速度
    // （全部 .load()：这些是 atomic，见上面的说明）
    j["params"] = {{"maxLinSpeed", ui.maxLinSpeed.load()}, {"yawRate", ui.yawRate.load()},
                   {"speedScale", ui.speedScale.load()},   {"bodyHeight", ui.bodyHeight.load()},
                   {"footRaise", ui.footRaise.load()},     {"speedLevel", ui.speedLevel.load()},
                   {"gaitType", ui.gaitType.load()},       {"mcf", ui.mcfMode.load()},
                   {"privacy", ui.privacyMode.load()},     {"hideUnsupported", ui.hideUnsupported.load()}};
    j["cmd"] = {{"vx", ui.cmdVx.load()}, {"vy", ui.cmdVy.load()}, {"vz", ui.cmdVz.load()}};
    nlohmann::json tg = nlohmann::json::object();
    {
        // 走快照：不能在锁外直接遍历 toggles（HTTP 线程与界面线程会同时访问）
        for (const auto& kv : ui.togglesSnapshot()) tg[kv.first] = kv.second;
    }
    j["toggles"] = tg;

    // 「日志」按钮角标：problemCount − problemSeen（没人打开日志也能发现新异常）
    {
        const int total = ui.problemCount.load();
        const int seen = ui.problemSeen.load();
        j["problemCount"] = total;
        j["problemUnread"] = total > seen ? (total - seen) : 0;
    }
    j["keyCount"] = ui.localKeyCount.load();
    j["scanning"] = ui.scanning.load();
    {
        std::lock_guard<std::mutex> lock(ui.keyScanMutex);
        j["keyScan"] = {{"running", ui.keyScanning.load()},
                        {"note", ui.keyScanNote},
                        {"candidates", ui.keyCandidates}};
    }

    nlohmann::json acts = nlohmann::json::array();
    for (const SportAction& a : sportActions()) {
        nlohmann::json o = nlohmann::json::object();
        o["key"] = a.key;
        o["label"] = a.label;
        o["group"] = static_cast<int>(a.group);
        o["risky"] = a.risky;
        o["toggle"] = a.toggle;
        // 可用性标注（与 ImGui 端一致）：上次回执 code（0=成功，3203=该固件没有此指令）
        int id = apiIdFor(a, ui.mcfMode.load());
        if (id == 0) id = apiIdFor(a, !ui.mcfMode.load());
        int code = -1;
        std::string note;
        if (id != 0 && ui.apiResult(id, &code, &note)) o["ack"] = code;
        acts.push_back(o);
    }
    j["actions"] = acts;

    nlohmann::json log = nlohmann::json::array();
    {
        std::lock_guard<std::mutex> lock(ui.logMutex);
        const size_t from = ui.logs.size() > 40 ? ui.logs.size() - 40 : 0;
        for (size_t i = from; i < ui.logs.size(); ++i) log.push_back(ui.logs[i]);
    }
    j["log"] = log;
    return j;
}

}  // namespace

bool startWebUi(RobotManager& mgr, UiState& ui, int port) {
    if (g_running.load()) return true;

    const std::string dir = "assets/web";
    auto* svr = new httplib::Server();
    svr->set_base_dir(dir);  // 静态页面（index.html / app.js / vendor/...）

    // ---------------------------------------------------------------- 访问控制
    // ⚠ 这个服务**没有账号体系**：谁连上谁就能让机器狗动。所以：
    //   · 默认只绑回环（127.0.0.1）—— 本机浏览器 / 本机 WebView 都够用；
    //   · 一旦绑到非回环地址（例如 WSL 里为了让 Windows 侧访问而设 0.0.0.0），
    //     必须同时给出 GO2_WEB_TOKEN，否则**拒绝启动** —— 宁可起不来，
    //     也不要出现"同一 Wi-Fi 下任何人都能控狗"。
    //   （httplib 的静态文件不在 /api/ 下，所以页面本身不校验；令牌只管 API。）
    const char* hostEnv = std::getenv("GO2_WEB_HOST");
    const std::string bindHost = (hostEnv && *hostEnv) ? hostEnv : "127.0.0.1";
    std::string token;
    if (const char* t = std::getenv("GO2_WEB_TOKEN"); t && *t) token = t;
    if (!isLoopbackHost(bindHost) && token.empty()) {
        std::printf("[WebUI] 拒绝启动：GO2_WEB_HOST=%s 属于对外监听，但服务没有账号体系。\n"
                    "        请同时设置 GO2_WEB_TOKEN=<随机串>（或改回 127.0.0.1）。\n",
                    bindHost.c_str());
        std::fflush(stdout);
        delete svr;
        return false;
    }
    // 统一在**每个**响应上加两条头（用 pre-routing 保证连错误响一起覆盖）：
    //   · Cache-Control: no-store —— 这套前端是"免构建"的（改文件即生效，没有打包哈希），
    //     浏览器把 app.js / components/*.js 缓存住的话，改完界面刷新看不到变化，
    //     人会以为是代码没生效（真实踩过：换了 logo 但页面还是旧的文字品牌）。
    //   · X-Content-Type-Options: nosniff —— 顺手，别让浏览器猜 MIME。
    svr->set_pre_routing_handler([token](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Cache-Control", "no-store, must-revalidate");
        res.set_header("X-Content-Type-Options", "nosniff");
        if (token.empty()) return httplib::Server::HandlerResponse::Unhandled;
        // 只拦 /api/*：静态页面不拦（页面本身没有控制能力，权限在 API 上）
        if (req.path.rfind("/api/", 0) != 0) return httplib::Server::HandlerResponse::Unhandled;
        if (requestToken(req) == token) return httplib::Server::HandlerResponse::Unhandled;
        res.status = 401;
        res.set_content("{\"ok\":false,\"error\":\"缺少或错误的访问令牌\"}", "application/json");
        return httplib::Server::HandlerResponse::Handled;
    });

    svr->Get("/api/state", [&mgr, &ui](const httplib::Request&, httplib::Response& res) {
        res.set_content(stateJson(mgr, ui).dump(), "application/json");
    });

    svr->Post("/api/command", [&mgr, &ui](const httplib::Request& req, httplib::Response& res) {
        nlohmann::json out = nlohmann::json::object();
        try {
            const nlohmann::json in = nlohmann::json::parse(req.body);
            const std::string cmd = in.value("cmd", "");
            if (cmd == "action") {
                out["ok"] = sendActionByKey(mgr, ui, in.value("key", ""));
            } else if (cmd == "select") {
                const std::string mode = in.value("mode", "");
                if (mode == "all") {
                    ui.selectAll();
                    ui.addLog("[WebUI] 群控：全选");
                } else if (mode == "none") {
                    ui.selectOnly("");
                    ui.addLog("[WebUI] 取消受控：不控制任何设备");
                } else if (mode == "multi") {
                    // 勾选式多选：把列表设置成**精确这个集合**（没列出的全部取消）
                    std::vector<std::string> want;
                    if (in.contains("ips"))
                        for (const auto& v : in["ips"]) want.push_back(v.get<std::string>());
                    std::vector<RobotEntry> snap;
                    {
                        std::lock_guard<std::mutex> lock(ui.robotsMutex);
                        snap = ui.robots;
                    }
                    for (const auto& e : snap) {
                        const bool on = std::find(want.begin(), want.end(), e.ip) != want.end();
                        ui.setSelected(e.ip, on);
                    }
                    ui.addLog("[WebUI] 勾选受控 → " + std::to_string(want.size()) + " 台");
                } else {
                    ui.selectOnly(in.value("ip", ""));
                    ui.addLog("[WebUI] 单控：" + in.value("ip", std::string()));
                }
                out["ok"] = true;
            } else if (cmd == "estop") {
                // 与桌面端**同一份**急停语义（cmd::estop）：锁定 → 全部就绪设备停车 →
                // 逐个关掉"真正开过"的持续模式开关 → 复位开关状态。
                // asyncClose=false：HTTP 线程本来就该等它做完，前端要的是"确已停住"。
                const auto sink = std::make_shared<ManagerSink>(mgr, ui);
                const cmd::EstopResult r = cmd::estop(sink, ui, /*asyncClose=*/false);
                ui.addLog("[WebUI] 急停：停车 " + std::to_string(r.stopped) + " 台、关闭 " +
                          std::to_string(r.toggles) + " 个持续模式");
                out["ok"] = true;
            } else if (cmd == "logseen") {
                ui.problemSeen = ui.problemCount.load();  // 打开日志就算"看过了"，角标随之清掉
                out["ok"] = true;
            } else if (cmd == "add") {
                const std::string ip = in.value("ip", "");
                out["ok"] = !ip.empty() && (ui.addOrUpdate(ip, true), true);
                if (out["ok"]) {
                    mgr.connectAll({ip}, 600);
                    ui.addLog("[WebUI] 添加并连接 " + ip);
                }
            } else if (cmd == "connect") {
                const std::string ip = in.value("ip", "");
                mgr.connectAll({ip}, 600);
                ui.addLog("[WebUI] 连接 " + ip);
                out["ok"] = true;
            } else if (cmd == "rename") {
                ui.setName(in.value("ip", ""), in.value("name", ""));
                ui.addLog("[WebUI] 改名：" + in.value("ip", std::string()) + " → " +
                          ui.labelOf(in.value("ip", "")));
                out["ok"] = true;
            } else if (cmd == "move") {
                // 摇杆：网页端自己按 10Hz 节拍 POST，这里只管发一次。
                // 群控语义统一走 cmd::moveSelected / cmd::stopSelected。
                ManagerSink sink(mgr, ui);
                if (in.value("stop", false)) {
                    const int n = cmd::stopSelected(sink);
                    ui.movingSent = false;
                    if (n > 0)
                        ui.addLog(std::string(ui.estop ? "[WebUI][急停] 停车 → " : "[WebUI] 松手停车 → ") +
                                  std::to_string(n) + " 台");
                    out["ok"] = true;
                } else if (ui.estop) {
                    out["ok"] = false;
                    out["error"] = "急停锁定中，不下发运动指令";
                } else {
                    const float vx = in.value("x", 0.0f);
                    const float vy = in.value("y", 0.0f);
                    const float vz = in.value("z", 0.0f);
                    const int n = cmd::moveSelected(sink, ui, vx, vy, vz);
                    ui.cmdVx = vx; ui.cmdVy = vy; ui.cmdVz = vz;
                    ui.movingSent = n > 0;
                    out["ok"] = true;
                }
            } else if (cmd == "quick") {
                // 快捷方向键：一次下发，速度持续到下一条指令（步速取 ui.speedScale）
                ManagerSink sink(mgr, ui);
                const std::string dir = in.value("dir", "");
                const int n = cmd::quickMove(sink, ui, dir);
                if (n < 0) {
                    out["ok"] = false;
                    out["error"] = "未知方向：" + dir;
                } else {
                    ui.addLog("[WebUI] 快捷 " + dir + " → " + std::to_string(n) + " 台");
                    out["ok"] = true;
                }
            } else if (cmd == "param") {
                const std::string name = in.value("name", "");
                // ★ 参数成员都是 atomic（界面线程每帧读）→ 按值取默认、夹紧后写回。
                //   范围夹紧只是"防手滑"，真正的安全边界在机器人侧。
                auto setF = [&](std::atomic<float>& dst, float lo, float hi) {
                    dst = std::clamp(in.value("value", dst.load()), lo, hi);
                };
                auto setI = [&](std::atomic<int>& dst, int lo, int hi) {
                    dst = std::clamp(in.value("value", dst.load()), lo, hi);
                };
                auto setB = [&](std::atomic<bool>& dst) {
                    dst = in.value("value", dst.load());
                };
                if (name == "maxLinSpeed") setF(ui.maxLinSpeed, 0.05f, 1.5f);
                else if (name == "yawRate") setF(ui.yawRate, 0.2f, 2.0f);
                else if (name == "speedScale") setF(ui.speedScale, 0.05f, 1.5f);
                else if (name == "bodyHeight") setF(ui.bodyHeight, -0.18f, 0.12f);
                else if (name == "footRaise") setF(ui.footRaise, 0.0f, 0.15f);
                else if (name == "speedLevel") setI(ui.speedLevel, 0, 2);
                else if (name == "gaitType") setI(ui.gaitType, 0, 4);
                else if (name == "mcf") setB(ui.mcfMode);
                else if (name == "privacy") setB(ui.privacyMode);
                else if (name == "hideUnsupported") setB(ui.hideUnsupported);
                else { out["ok"] = false; out["error"] = "未知参数：" + name; }
                if (!out.contains("ok")) out["ok"] = true;
            } else if (cmd == "damp") {
                // 强制阻尼：对**全部就绪**设备（不限于勾选），语义与桌面端同一份
                ManagerSink sink(mgr, ui);
                const int n = cmd::dampAll(sink);
                ui.addLog("[WebUI][急停] 强制阻尼 → " + std::to_string(n) + " 台");
                out["ok"] = true;
            } else if (cmd == "unestop") {
                ui.estop = false;
                ui.movingSent = false;
                ui.addLog("[WebUI][急停] 已解除，可以继续遥控");
                out["ok"] = true;
            } else if (cmd == "remove") {
                out["ok"] = ui.remove(in.value("ip", ""));
            } else if (cmd == "keyprobe") {
                // 从机器狗内网找钥匙（不连电脑）：扫端口 + Web 服务抓 32 位 hex，后台线程跑
                std::string ip = in.value("ip", "");
                if (ip.empty()) {
                    const auto sel = ui.selectedIps();
                    if (!sel.empty()) ip = sel.front();
                    else {
                        std::lock_guard<std::mutex> lock(ui.robotsMutex);
                        if (!ui.robots.empty()) ip = ui.robots.front().ip;
                    }
                }
                if (ip.empty()) {
                    out["ok"] = false;
                    out["error"] = "没有可探测的 IP —— 先在设备里添加一台狗，或手动填 IP";
                } else if (ui.keyScanning.load()) {
                    out["ok"] = false;
                    out["error"] = "上一次探测还在进行";
                } else {
                    std::thread(runKeyProbe, std::ref(mgr), std::ref(ui), ip).detach();
                    out["ok"] = true;
                }
            } else if (cmd == "keyapply") {
                // 采用一把候选钥匙：存进本地 keys.txt + 加入候选 + 绑定到该 IP（握手成功即为正确）
                out["ok"] = applyKeyCandidate(mgr, ui, in.value("key", ""), in.value("ip", ""));
                if (!out["ok"]) out["error"] = "不是合法的 32 位 hex";
            } else if (cmd == "connectall") {
                // 与 ImGui 设备弹窗的「全部连接」同一语义：列表内全部错峰连接
                std::vector<std::string> ips;
                {
                    std::lock_guard<std::mutex> lock(ui.robotsMutex);
                    for (const auto& e : ui.robots) ips.push_back(e.ip);
                }
                if (!ips.empty()) mgr.connectAll(ips, 600);
                ui.addLog("[WebUI] 全部连接 → " + std::to_string(ips.size()) + " 台");
                out["ok"] = true;
            } else if (cmd == "disconnectall") {
                mgr.disconnectAll();
                ui.addLog("[WebUI] 全部断开");
                out["ok"] = true;
            } else if (cmd == "clearlog") {
                {
                    std::lock_guard<std::mutex> lock(ui.logMutex);
                    ui.logs.clear();
                }
                out["ok"] = true;
            } else if (cmd == "scan") {
                // 与桌面端同一份实现（ui.cpp 的 startScan：网段 TCP 探测 + SN 多播 + 自动连接）
                startScan(mgr, ui);
                out["ok"] = true;
            } else {
                out["ok"] = false;
                out["error"] = "未知指令：" + cmd;
            }
        } catch (const std::exception& e) {
            out["ok"] = false;
            out["error"] = e.what();
        }
        res.set_content(out.dump(), "application/json");
    });

    g_svr = svr;
    // 后台线程跑服务（listen 是阻塞的）
    // ★ 端口可能被别的程序占着（8080 尤其常见）→ 从给定端口开始往后试 5 个，用第一个能绑定的
    // ★ 默认监听 127.0.0.1（见上面的访问控制说明）。要在 WSL 里让 Windows 侧访问，
    //   设 GO2_WEB_HOST=0.0.0.0 **并**给出 GO2_WEB_TOKEN，然后用
    //   http://<WSL的IP>:端口/?token=<令牌> 打开页面。
    const bool needToken = !token.empty();
    g_thread = std::thread([svr, port, bindHost, token, needToken] {
        for (int p = port; p < port + 5; ++p) {
            if (svr->bind_to_port(bindHost.c_str(), p)) {
                std::printf("[WebUI] 界面已就绪 → 浏览器打开 http://%s:%d%s\n",
                            isLoopbackHost(bindHost) ? "localhost" : bindHost.c_str(), p,
                            needToken ? "/?token=" : "");
                if (needToken) std::printf("%s\n", token.c_str());
                std::fflush(stdout);
                svr->listen_after_bind();
                return;
            }
        }
        std::printf("[WebUI] 端口 %d~%d 都被占用 → 网页界面未启动\n", port, port + 4);
        std::fflush(stdout);
    });
    g_running = true;
    return true;
}

void stopWebUi() {
    if (!g_running.load()) return;
    if (g_svr) g_svr->stop();
    if (g_thread.joinable()) g_thread.join();
    delete g_svr;
    g_svr = nullptr;
    g_running = false;
}

/// 静态析构兜底。
///
/// 背景：`g_thread` 是个**全局** `std::thread`。只要入口忘了调 `stopWebUi()`，
/// 它就会在静态析构期带着 joinable 状态被销毁 → 直接 `std::terminate()`
/// （现象就是"正常退出却 dump core：terminate called without an active exception"）。
/// 这里用一个守卫对象做最后一道保险；它定义在 g_thread 之后，所以**先于** g_thread 析构
/// （同一 TU 内静态对象按定义逆序析构），此时 g_svr / g_running 仍然有效。
/// 入口显式调用 stopWebUi() 依然是推荐做法，这里只是别再崩。
namespace {
struct WebUiStaticGuard {
    ~WebUiStaticGuard() { stopWebUi(); }
};
[[maybe_unused]] WebUiStaticGuard g_webUiGuard;
}  // namespace

}  // namespace go2
