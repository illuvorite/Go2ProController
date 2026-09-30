#include "web_bridge.hpp"

#include "robot_client.hpp"
#include "robot_manager.hpp"
#include "sport_library.hpp"
#include "ui.hpp"

#include "../platform/net.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace go2 {
namespace {

std::atomic<bool> g_running{false};
httplib::Server* g_svr = nullptr;
std::thread g_thread;

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

/// 按英文 key 发一条运动指令给**当前勾选**的设备
bool sendActionByKey(RobotManager& mgr, UiState& ui, const std::string& key) {
    for (const SportAction& a : sportActions()) {
        if (key != a.key) continue;
        int id = apiIdFor(a, ui.mcfMode);
        if (id == 0) id = apiIdFor(a, !ui.mcfMode);  // 这套指令集没有 → 用另一套兜底
        nlohmann::json p;
        if (a.param == SportParam::Flag) {
            p = nlohmann::json::object();
            // 开关型（持续模式）：记住当前状态再翻转 —— 这类指令 StopMove 停不掉，
            // 只能靠同一个 api_id 带 {"data": false} 关掉
            const bool on = a.toggle && ui.toggles[a.key];
            p["data"] = !on;
            ui.toggles[a.key] = !on;
        } else {
            p = buildSportParam(a, 0, ui.bodyHeight, 0.0f, 0.0f, 0.0f, std::string());
        }
        int n = 0;
        for (const auto& ip : ui.selectedIps())
            if (auto* c = mgr.find(ip); c && c->isReady() && c->sendSportCommand(id, p)) ++n;
        ui.addLog("[WebUI] " + std::string(a.label) + " (api " + std::to_string(id) + ") → " +
                  std::to_string(n) + " 台");
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- 钥匙探测（不连电脑）
/// 原理（同 tools/key_extract.py，但移植进 app，平板上直接跑）：
/// 固件 ≥1.1.15 的狗本地必须持有**明文** AES 钥匙（信令服务要用它加密握手 data1），
/// 所以只要狗的内网服务暴露，就有机会把钥匙捞出来。
/// ① TCP 扫常见端口 → ② 对 Web 端口做 HTTP GET 抓 32 位 hex。
/// SSH/ADB/NFS 这几条路需要外部工具（sshpass/adb/mount），App 里没有 → 只能提示用电脑。

/// 非阻塞 TCP 端口探测（跨平台：POSIX / Winsock）
bool tcpOpen(const std::string& ip, uint16_t port, int timeoutMs) {
    platform::initSockets();
    go2_socket_t s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s == GO2_INVALID_SOCKET) return false;
#ifdef _WIN32
    u_long nb = 1;
    ::ioctlsocket(s, FIONBIO, &nb);
#else
    int flags = ::fcntl(s, F_GETFL, 0);
    ::fcntl(s, F_SETFL, flags | O_NONBLOCK);
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = ::htons(port);
    ::inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
    int rc = ::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    bool ok = (rc == 0);
    if (!ok) {
        const int err = platform::lastSocketError();
#ifdef _WIN32
        if (platform::wouldBlock(err)) {
#else
        if (platform::wouldBlock(err) || err == EINPROGRESS) {
#endif
            ok = platform::waitSocket(s, true, timeoutMs) > 0;
            if (ok) {
                int soErr = 0;
                socklen_t len = sizeof(soErr);
                ::getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soErr), &len);
                ok = (soErr == 0);
            }
        }
    }
    platform::closeSocket(s);
    return ok;
}

/// 阻塞式 HTTP GET（短超时；只拿小响应，够用）
std::string httpGet(const std::string& ip, uint16_t port, const std::string& path, int timeoutMs) {
    platform::initSockets();
    go2_socket_t s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s == GO2_INVALID_SOCKET) return {};
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = ::htons(port);
    ::inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
    timeval tv{};
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&tv), sizeof(tv));
    ::setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<char*>(&tv), sizeof(tv));
    if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        platform::closeSocket(s);
        return {};
    }
    const std::string req = "GET " + path +
                            " HTTP/1.0\r\nHost: " + ip +
                            "\r\nUser-Agent: go2-remote-keyprobe\r\nConnection: close\r\n\r\n";
    std::string out;
    if (::send(s, req.c_str(), static_cast<int>(req.size()), 0) > 0) {
        char buf[4096];
        for (;;) {
            const int n = static_cast<int>(
                ::recv(s, buf, sizeof(buf), 0));
            if (n <= 0) break;
            out.append(buf, static_cast<size_t>(n));
            if (out.size() > 256 * 1024) break;  // 防御：别把内存吃爆
        }
    }
    platform::closeSocket(s);
    return out;
}

/// 从文本里捞 32 位 hex 候选（判据与 key_extract.py 一致：去重 + 字符种类 > 4，滤掉 aaaa…这种）
std::vector<std::string> hexCandidates(const std::string& text) {
    static const std::regex kHex("\\b[0-9a-fA-F]{32}\\b");
    std::vector<std::string> out;
    std::set<std::string> seen;
    auto it = std::sregex_iterator(text.begin(), text.end(), kHex);
    for (auto end = std::sregex_iterator(); it != end; ++it) {
        std::string k = it->str();
        std::transform(k.begin(), k.end(), k.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const int distinct = static_cast<int>(std::set<char>(k.begin(), k.end()).size());
        if (distinct > 4 && seen.insert(k).second) out.push_back(k);
    }
    return out;
}

/// 探测线程主体（后台跑，结果写 ui.keyScan*，前端从 /api/state 轮询）
void runKeyProbe(UiState& ui, std::string ip) {
    ui.keyScanning = true;
    {
        std::lock_guard<std::mutex> lock(ui.keyScanMutex);
        ui.keyCandidates.clear();
        ui.keyScanNote = "探测中：" + ip;
    }
    ui.addLog("[钥匙] 开始探测 " + ip + " 的内网服务 ...");

    static const int kPorts[] = {22, 23, 80, 443, 5555, 2049, 8080, 8081, 8888, 8000, 9090, 9991};
    std::vector<int> open;
    for (int p : kPorts)
        if (tcpOpen(ip, static_cast<uint16_t>(p), 400)) open.push_back(p);

    std::string note;
    if (open.empty()) {
        note = "没有开放端口 —— 狗不在线 / IP 不对 / 不在同一网段";
    } else {
        note = "开放端口:";
        for (size_t i = 0; i < open.size(); ++i)
            note += (i ? "," : " ") + std::to_string(open[i]);
    }
    ui.addLog("[钥匙] " + note);

    std::vector<std::string> found;
    const auto harvest = [&](const std::string& text) {
        for (const auto& k : hexCandidates(text))
            if (std::find(found.begin(), found.end(), k) == found.end()) found.push_back(k);
    };
    for (int p : open) {
        if (p == 80 || p == 8080 || p == 8081 || p == 8888 || p == 8000 || p == 9090) {
            for (const char* path : {"/", "/config", "/debug/vars"})
                harvest(httpGet(ip, static_cast<uint16_t>(p), path, 1500));
        }
    }

    std::string hint;
    for (int p : open) {
        if (p == 5555) hint += " 5555(ADB)";
        if (p == 22) hint += " 22(SSH)";
        if (p == 2049) hint += " 2049(NFS)";
    }
    if (!hint.empty())
        hint = "这些入口需要外部工具（adb/sshpass/mount），目前只能用电脑：" + hint + "；";

    {
        std::lock_guard<std::mutex> lock(ui.keyScanMutex);
        ui.keyCandidates = found;
        ui.keyScanNote = note + (found.empty() ? "" : ("；候选钥匙 " + std::to_string(found.size()) + " 把")) +
                         "。" + hint;
    }
    if (found.empty()) {
        ui.addLog(std::string("[钥匙] 没有直接捞到 32 位 hex。") + hint);
    } else {
        for (const auto& k : found) ui.addLog("[钥匙] 候选: " + k + "（设置页点「采用」后握手验证）");
    }
    ui.keyScanning = false;
}

nlohmann::json stateJson(RobotManager& mgr, UiState& ui) {
    nlohmann::json j = nlohmann::json::object();
    j["estop"] = ui.estop;   // UiState::estop 是普通 bool（不是 atomic）
    j["mcf"] = ui.mcfMode;

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
    j["params"] = {{"maxLinSpeed", ui.maxLinSpeed}, {"yawRate", ui.yawRate},
                   {"speedScale", ui.speedScale},  {"bodyHeight", ui.bodyHeight},
                   {"footRaise", ui.footRaise},    {"speedLevel", ui.speedLevel},
                   {"gaitType", ui.gaitType},      {"mcf", ui.mcfMode},
                   {"privacy", ui.privacyMode},    {"hideUnsupported", ui.hideUnsupported}};
    j["cmd"] = {{"vx", ui.cmdVx}, {"vy", ui.cmdVy}, {"vz", ui.cmdVz}};
    nlohmann::json tg = nlohmann::json::object();
    for (const auto& kv : ui.toggles) tg[kv.first] = kv.second;
    j["toggles"] = tg;

    // 「日志」按钮角标：problemCount − problemSeen（没人打开日志也能发现新异常）
    j["problemCount"] = ui.problemCount;
    j["problemUnread"] = (ui.problemCount > ui.problemSeen) ? (ui.problemCount - ui.problemSeen) : 0;
    j["keyCount"] = ui.localKeyCount;
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
        int id = apiIdFor(a, ui.mcfMode);
        if (id == 0) id = apiIdFor(a, !ui.mcfMode);
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
                ui.estop = true;
                int n = 0;
                std::vector<RobotEntry> snap;
                {
                    std::lock_guard<std::mutex> lock(ui.robotsMutex);
                    snap = ui.robots;
                }
                for (const auto& e : snap)
                    if (auto* c = mgr.find(e.ip); c && c->isReady() && c->stopMove()) ++n;
                // ★ 持续模式（自由行走 / 领航跟随 / 交叉步 …）StopMove 停不掉 ——
                //   必须逐个用同一 api_id 带 {"data": false} 关掉（空表 = 关全部已知开关）
                std::vector<int> ids(ui.activeToggleIds.begin(), ui.activeToggleIds.end());
                for (const auto& e : snap)
                    if (auto* c = mgr.find(e.ip); c && c->isReady()) c->disablePersistentModes(ids);
                ui.toggles.clear();
                ui.addLog("[WebUI] 急停：停车 " + std::to_string(n) + " 台、持续模式已关闭");
                out["ok"] = true;
            } else if (cmd == "logseen") {
                ui.problemSeen = ui.problemCount;  // 打开日志就算"看过了"，角标随之清掉
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
                // 摇杆：网页端自己按 10Hz 节拍 POST，这里只管发一次
                int n = 0;
                if (in.value("stop", false)) {
                    for (const auto& ip : ui.selectedIps())
                        if (auto* c = mgr.find(ip); c && c->isReady() && c->stopMove()) ++n;
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
                    for (const auto& ip : ui.selectedIps())
                        if (auto* c = mgr.find(ip); c && c->isReady() && c->move(vx, vy, vz)) ++n;
                    ui.cmdVx = vx; ui.cmdVy = vy; ui.cmdVz = vz;
                    ui.movingSent = n > 0;
                    out["ok"] = true;
                }
            } else if (cmd == "quick") {
                // 快捷方向键：一次下发，速度持续到下一条指令
                const float v = ui.speedScale;
                const std::string dir = in.value("dir", "");
                float vx = 0.0f, vz = 0.0f;
                if (dir == "fwd") vx = v;
                else if (dir == "back") vx = -v;
                else if (dir == "left") vz = v;
                else if (dir == "right") vz = -v;
                int n = 0;
                for (const auto& ip : ui.selectedIps())
                    if (auto* c = mgr.find(ip); c && c->isReady() && c->move(vx, 0.0f, vz)) ++n;
                ui.addLog("[WebUI] 快捷 " + dir + " → " + std::to_string(n) + " 台");
                out["ok"] = true;
            } else if (cmd == "param") {
                const std::string name = in.value("name", "");
                auto setF = [&](float& dst, float lo, float hi) {
                    dst = std::clamp(in.value("value", dst), lo, hi);
                };
                if (name == "maxLinSpeed") setF(ui.maxLinSpeed, 0.05f, 1.5f);
                else if (name == "yawRate") setF(ui.yawRate, 0.2f, 2.0f);
                else if (name == "speedScale") setF(ui.speedScale, 0.05f, 1.5f);
                else if (name == "bodyHeight") setF(ui.bodyHeight, -0.18f, 0.12f);
                else if (name == "footRaise") setF(ui.footRaise, 0.0f, 0.15f);
                else if (name == "speedLevel") ui.speedLevel = std::clamp(in.value("value", 0), 0, 2);
                else if (name == "gaitType") ui.gaitType = std::clamp(in.value("value", 0), 0, 4);
                else if (name == "mcf") ui.mcfMode = in.value("value", false);
                else if (name == "privacy") ui.privacyMode = in.value("value", false);
                else if (name == "hideUnsupported") ui.hideUnsupported = in.value("value", false);
                else { out["ok"] = false; out["error"] = "未知参数：" + name; }
                if (!out.contains("ok")) out["ok"] = true;
            } else if (cmd == "damp") {
                int n = 0;
                std::vector<RobotEntry> snap;
                {
                    std::lock_guard<std::mutex> lock(ui.robotsMutex);
                    snap = ui.robots;
                }
                for (const auto& e : snap)
                    if (auto* c = mgr.find(e.ip); c && c->isReady() && c->damp()) ++n;
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
                    std::thread(runKeyProbe, std::ref(ui), ip).detach();
                    out["ok"] = true;
                }
            } else if (cmd == "keyapply") {
                // 采用一把候选钥匙：存进本地 keys.txt + 加入候选 + 绑定到该 IP（握手成功即为正确）
                const std::string key = in.value("key", "");
                const std::string ip = in.value("ip", "");
                const bool hexOk = key.size() == 32 &&
                                   std::all_of(key.begin(), key.end(), [](unsigned char c) {
                                       return std::isxdigit(c) != 0;
                                   });
                if (!hexOk) {
                    out["ok"] = false;
                    out["error"] = "不是合法的 32 位 hex";
                } else {
                    {
                        std::ofstream f("keys.txt", std::ios::app);
                        f << key << "\n";
                    }
                    mgr.addAesKeys({key});
                    if (!ip.empty()) mgr.bindKey(ip, key);
                    ui.addLog("[钥匙] 已采用 " + key +
                              (ip.empty() ? "" : (" → 绑定 " + ip)) +
                              "（下次连接生效；握手成功即为正确钥匙）");
                    out["ok"] = true;
                }
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
    // ★ 默认监听 0.0.0.0：Windows 访问不到 WSL 内部的 127.0.0.1（实测即使 mirrored 网络模式，
    //   回环地址也不跨 Windows/WSL）→ 绑 0.0.0.0 后 Windows 用 localhost 就能打开。
    //   想收紧成"只有 WSL 内部能访问"，启动前设 GO2_WEB_HOST=127.0.0.1
    const char* hostEnv = std::getenv("GO2_WEB_HOST");
    const std::string bindHost = (hostEnv && *hostEnv) ? hostEnv : "0.0.0.0";
    g_thread = std::thread([svr, port, bindHost] {
        for (int p = port; p < port + 5; ++p) {
            if (svr->bind_to_port(bindHost.c_str(), p)) {
                std::printf("[WebUI] 界面已就绪 → 浏览器打开 http://localhost:%d\n", p);
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

}  // namespace go2
