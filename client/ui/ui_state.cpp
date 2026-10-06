// ============================================================================
// UiState 的纯数据实现 —— 从 ui.cpp 搬出来，**刻意不 include imgui.h**
//
// 为什么要单独一个 TU：无界面的群控服务（apps/serve/main.cpp）只需要
// "设备列表 / 受控集合 / 参数 / 日志"这些纯数据逻辑，不该为了几个成员函数
// 把整套 ImGui 拖进来。ui.cpp 里留下的只有绘制代码。
//
// go2_remote（桌面 GUI）和 go2_serve（无界面服务）都编这个文件。
// ============================================================================

#include "ui.hpp"

#include "discovery.hpp"
#include "robot_manager.hpp"

#include <fstream>
#include <thread>

namespace go2 {

// ---------------------------------------------------------------- 日志

bool isProblemLine(const std::string& s) {
    return s.find("失败") != std::string::npos || s.find("[错误]") != std::string::npos ||
           s.find("WARN") != std::string::npos || s.find("异常") != std::string::npos ||
           s.find("超时") != std::string::npos || s.find("掉线") != std::string::npos ||
           s.find("[急停]") != std::string::npos;
}

void UiState::addLog(const std::string& line) {
    std::lock_guard<std::mutex> lock(logMutex);
    logs.push_back(line);
    if (logs.size() > 500) logs.erase(logs.begin(), logs.begin() + 100);
    // 累计异常/失败行数：日志弹窗没打开时，靠顶栏「日志」按钮上的角标提醒
    // （否则出错了用户根本不知道，机器人控制里这种"静默失败"很危险）
    if (isProblemLine(line)) ++problemCount;
}

// ---------------------------------------------------------------- 设备列表

bool UiState::addOrUpdate(const std::string& ip, bool manual) {
    std::lock_guard<std::mutex> lock(robotsMutex);
    for (auto& r : robots)
        if (r.ip == ip) return false;
    robots.push_back({ip, false, manual, -1.0f, "-"});
    return true;
}

bool UiState::remove(const std::string& ip) {
    std::lock_guard<std::mutex> lock(robotsMutex);
    for (auto it = robots.begin(); it != robots.end(); ++it)
        if (it->ip == ip) {
            robots.erase(it);
            return true;
        }
    return false;
}

bool UiState::isSelected(const std::string& ip) {
    std::lock_guard<std::mutex> lock(robotsMutex);
    for (auto& r : robots)
        if (r.ip == ip) return r.selected;
    return false;
}

void UiState::setSelected(const std::string& ip, bool sel) {
    std::lock_guard<std::mutex> lock(robotsMutex);
    for (auto& r : robots)
        if (r.ip == ip) {
            r.selected = sel;
            return;
        }
}

void UiState::updateStatus(const std::string& ip, float battery, const std::string& mode) {
    std::lock_guard<std::mutex> lock(robotsMutex);
    for (auto& r : robots)
        if (r.ip == ip) {
            if (battery >= 0.0f) r.battery = battery;
            if (!mode.empty()) r.modeName = mode;
            return;
        }
}

std::vector<std::string> UiState::selectedIps() {
    std::lock_guard<std::mutex> lock(robotsMutex);
    std::vector<std::string> out;
    for (auto& r : robots)
        if (r.selected) out.push_back(r.ip);
    return out;
}

int UiState::selectedCount() {
    return static_cast<int>(selectedIps().size());
}

// ---------------------------------------------------------------- 机器狗名称
// 规矩与钥匙一致：只读写**本应用目录**下的文件（安卓启动时已 chdir 到应用专属目录）。
//
// ★ names 必须加锁：Web 端的 `rename` 指令在 **HTTP 线程**调用 setName，
//   而界面线程同时在读（drawDeviceList / labelOf）。原实现注释说"只在界面线程读写、
//   不需要加锁"是错的 —— 并发读一个正在重哈希的 std::map 会直接崩。
//   所以：公开方法自己加锁，内部互相调用走 *Locked 版本（避免递归加锁死锁）。
void UiState::loadNames() {
    std::lock_guard<std::mutex> lock(namesMutex);
    std::ifstream f("robot_names.json");
    if (!f) return;
    try {
        nlohmann::json j;
        f >> j;
        if (!j.is_object()) return;
        for (auto it = j.begin(); it != j.end(); ++it)
            if (it.value().is_string() && !it.value().get<std::string>().empty())
                names[it.key()] = it.value().get<std::string>();
    } catch (...) {
        // 文件损坏就当没起过名 —— 绝不能因为一个名字文件让界面起不来
    }
}

/// 落盘。**调用方必须已持有 namesMutex**（saveNames 与 setName 都会走到这里）
void UiState::saveNamesLocked() const {
    nlohmann::json j = nlohmann::json::object();
    for (const auto& kv : names)
        if (!kv.second.empty()) j[kv.first] = kv.second;
    std::ofstream f("robot_names.json", std::ios::trunc);
    if (f) f << j.dump(2) << "\n";
}

void UiState::saveNames() {
    std::lock_guard<std::mutex> lock(namesMutex);
    saveNamesLocked();
}

void UiState::setName(const std::string& ip, const std::string& name) {
    std::lock_guard<std::mutex> lock(namesMutex);
    // 去掉首尾空白：只有空白的名字等于"没起名"（恢复显示 IP）
    std::string clean = name;
    while (!clean.empty() && (clean.back() == ' ' || clean.back() == '\t')) clean.pop_back();
    const size_t b = clean.find_first_not_of(" \t");
    clean = (b == std::string::npos) ? std::string() : clean.substr(b);
    if (clean.empty())
        names.erase(ip);
    else
        names[ip] = clean;
    saveNamesLocked();
}

std::string UiState::nameOfLocked(const std::string& ip) const {
    auto it = names.find(ip);
    return it == names.end() ? std::string() : it->second;
}

std::string UiState::nameOf(const std::string& ip) const {
    std::lock_guard<std::mutex> lock(namesMutex);
    return nameOfLocked(ip);
}

std::string UiState::labelOf(const std::string& ip) const {
    std::lock_guard<std::mutex> lock(namesMutex);
    const std::string n = nameOfLocked(ip);  // 不能调 nameOf（会二次加锁 → 死锁）
    return n.empty() ? ip : n;
}

// ---------------------------------------------------------------- 持续模式开关
// toggles / activeToggleIds 的读写者来自两个线程（界面线程画瓷砖、HTTP 线程急停清空），
// 所以统一走这几个方法；不要在别处直接访问这两个容器。

bool UiState::toggleState(const std::string& key) const {
    std::lock_guard<std::mutex> lock(toggleMutex);
    auto it = toggles.find(key);
    return it != toggles.end() && it->second;
}

void UiState::setToggle(const std::string& key, bool on) {
    std::lock_guard<std::mutex> lock(toggleMutex);
    toggles[key] = on;
}

void UiState::clearToggles() {
    std::lock_guard<std::mutex> lock(toggleMutex);
    toggles.clear();
}

std::map<std::string, bool> UiState::togglesSnapshot() const {
    std::lock_guard<std::mutex> lock(toggleMutex);
    return toggles;
}

void UiState::setToggleActive(int apiId, bool active) {
    std::lock_guard<std::mutex> lock(toggleMutex);
    if (active)
        activeToggleIds.insert(apiId);
    else
        activeToggleIds.erase(apiId);
}

std::vector<int> UiState::activeToggleIdsSnapshot() const {
    std::lock_guard<std::mutex> lock(toggleMutex);
    return std::vector<int>(activeToggleIds.begin(), activeToggleIds.end());
}

void UiState::clearActiveToggles() {
    std::lock_guard<std::mutex> lock(toggleMutex);
    activeToggleIds.clear();
}

// ---------------------------------------------------------------- 受控集合

int UiState::selectAll() {
    std::lock_guard<std::mutex> lock(robotsMutex);
    for (auto& r : robots) r.selected = true;
    return static_cast<int>(robots.size());
}

bool UiState::selectOnly(const std::string& ip) {
    std::lock_guard<std::mutex> lock(robotsMutex);
    bool found = false;
    for (auto& r : robots) {
        r.selected = (r.ip == ip);
        if (r.selected) found = true;
    }
    return found;
}

std::string UiState::controlTargetText() {
    const auto ips = selectedIps();
    if (ips.empty()) return "未选择受控设备";
    if (ips.size() == 1) return "单控 · " + labelOf(ips.front());
    return "群控 · " + std::to_string(ips.size()) + " 台";
}

// ---------------------------------------------------------------- 动作回执

void UiState::noteApiResult(int apiId, int code, const std::string& note) {
    if (apiId == 0) return;
    std::lock_guard<std::mutex> lock(apiMutex);
    apiCode[apiId] = code;
    apiNote[apiId] = note;
}

bool UiState::apiResult(int apiId, int* code, std::string* note) {
    std::lock_guard<std::mutex> lock(apiMutex);
    auto it = apiCode.find(apiId);
    if (it == apiCode.end()) return false;
    if (code) *code = it->second;
    if (note) {
        auto nt = apiNote.find(apiId);
        *note = (nt == apiNote.end()) ? std::string() : nt->second;
    }
    return true;
}

// ---------------------------------------------------------------- 钥匙文件

std::vector<std::string> loadLocalKeysFile(const std::string& path) {
    std::vector<std::string> keys;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' ||
                                 line.back() == '\t'))
            line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        if (line.size() == 32) keys.push_back(line);
    }
    return keys;
}

// ---------------------------------------------------------------- 局域网扫描

namespace {

/// 扫描线程句柄 + 保护它的锁。
/// ★ 这里**不能用 detach**：线程体按引用捕获 mgr/ui，而它们属于调用方的栈对象，
///   进程退出（或 Android 退出界面）时线程可能还没跑完 → use-after-free。
///   所以改成持有 thread，退出前由 joinScans() 收尾。
std::mutex g_scanMutex;
std::vector<std::thread> g_scanThreads;

/// 回收已结束的扫描线程。**调用方必须已持有 g_scanMutex**。
///
/// 安全性依据：`startScanImpl` 靠 `scanning.exchange(true)` 保证同一时刻只有一轮扫描；
/// 能走到这里说明上一轮的 `scanning` 已经回到 false，也就是上一轮线程体已基本结束，
/// 所以 join 只会等待微秒级（线程收尾），不会阻塞界面。
void reapScanThreadsLocked() {
    for (auto& t : g_scanThreads)
        if (t.joinable()) t.join();
    g_scanThreads.clear();
}

/// 扫描线程体：本机所有网段 TCP 探测 + SN 多播，发现即加入列表并错峰连接
void runScan(RobotManager& mgr, UiState& ui) {
    ui.addLog("[扫描] 启动局域网发现 ...");
    const auto subnets = Discovery::localSubnets();
    if (subnets.empty()) {
        ui.addLog("[扫描] 未找到可用的局域网 IPv4 网卡");
        ui.scanning = false;
        return;
    }
    int added = 0;
    std::vector<std::string> toConnect;
    for (const auto& sn : subnets) {
        ui.addLog("[扫描] 本机网段 " + sn);
        for (const auto& r : Discovery::scanSubnet(
                 sn, 400, [&](const std::string& line) { ui.addLog("[扫描] " + line); })) {
            if (ui.addOrUpdate(r.ip, false)) {
                ++added;
                ui.addLog("[扫描] 发现 Go2: " + r.ip + " [" + r.note + "]");
                toConnect.push_back(r.ip);
            }
        }
    }
    // 多播 SN 发现：补上跨网段/多网卡时的漏网设备，并给出 SN
    for (const auto& kv : Discovery::multicastSnScan(
             1500, [&](const std::string& line) { ui.addLog("[扫描] " + line); })) {
        if (ui.addOrUpdate(kv.second, false)) {
            ++added;
            ui.addLog("[扫描] 多播发现 Go2: " + kv.second + " (SN=" + kv.first + ")");
            toConnect.push_back(kv.second);
        }
    }
    if (!toConnect.empty()) {
        ui.addLog("[扫描] 错峰连接 " + std::to_string(toConnect.size()) + " 台 ...");
        mgr.connectAll(toConnect, 600);
    }
    ui.addLog("[扫描] 完成，新增 " + std::to_string(added) + " 台");
    ui.scanning = false;
}

}  // namespace

void startScan(RobotManager& mgr, UiState& ui) {
    if (ui.scanning.exchange(true)) return;  // 已有一轮在跑
    std::lock_guard<std::mutex> lock(g_scanMutex);
    reapScanThreadsLocked();  // 上一轮已结束，先回收它的句柄
    g_scanThreads.emplace_back([&mgr, &ui] { runScan(mgr, ui); });
}

void joinScans() {
    std::lock_guard<std::mutex> lock(g_scanMutex);
    reapScanThreadsLocked();
}

}  // namespace go2
