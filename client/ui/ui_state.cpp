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
#include "robot_model.hpp"
#include "sport_library.hpp"  // labelForApiId：回滚日志要写中文动作名

#include <algorithm>
#include <cctype>
#include <cstdlib>
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

bool UiState::addOrUpdate(const std::string& ip, bool manual, const std::string& sn) {
    const std::string model = sn.empty() ? std::string() : std::string(modelName(modelFromSn(sn)));
    std::lock_guard<std::mutex> lock(robotsMutex);
    for (auto& r : robots) {
        if (r.ip != ip) continue;
        // 已存在：补一次机型。典型场景 —— 先手动填 IP 加进来（没有 SN），
        // 后一轮扫描/多播又拿到了 SN → 这里把机型补上，不用重加一台。
        if (r.sn.empty() && !sn.empty()) {
            r.sn = sn;
            r.model = model;
            saveDevicesLocked();
        }
        return false;
    }
    RobotEntry e;
    e.ip = ip;
    e.manual = manual;
    e.sn = sn;
    e.model = model;
    robots.push_back(std::move(e));
    saveDevicesLocked();  // 列表要落盘，否则原生侧重启后"扫描到的狗就没了"
    return true;
}

bool UiState::remove(const std::string& ip) {
    std::lock_guard<std::mutex> lock(robotsMutex);
    for (auto it = robots.begin(); it != robots.end(); ++it)
        if (it->ip == ip) {
            robots.erase(it);
            saveDevicesLocked();
            return true;
        }
    return false;
}

// ---------------------------------------------------------------- 设备列表持久化
// 与名称文件同一套规矩：只读写**本应用目录**下的文件（安卓启动时已 chdir 到应用目录）。
// 没有它的话，只要原生侧被重建，设备列表就清空 —— 用户看到的是"刚扫描到的狗没了"。
void UiState::loadDevices() {
    std::lock_guard<std::mutex> lock(robotsMutex);
    std::ifstream f("robot_devices.json");
    if (!f) return;
    try {
        nlohmann::json j;
        f >> j;
        if (!j.is_array()) return;
        for (const auto& e : j) {
            if (!e.is_object()) continue;
            const std::string ip = e.value("ip", std::string());
            if (ip.empty()) continue;
            bool dup = false;
            for (const auto& r : robots)
                if (r.ip == ip) { dup = true; break; }
            if (dup) continue;  // 本次启动已经加过了（比如刚扫描到）→ 不重复
            RobotEntry re;
            re.ip = ip;
            re.manual = e.value("manual", false);
            re.sn = e.value("sn", std::string());
            re.model = e.value("model", std::string());
            // 老版本的 robot_devices.json 没有 sn/model 字段 → 这里补一次推导，
            // 免得升级后所有历史设备都退化成"机型未知"
            if (re.model.empty() && !re.sn.empty())
                re.model = std::string(modelName(modelFromSn(re.sn)));
            robots.push_back(std::move(re));
        }
    } catch (...) {
        // 文件损坏就当没存过 —— 绝不能因为一个列表文件让界面起不来
    }
}

void UiState::saveDevicesLocked() const {
    nlohmann::json j = nlohmann::json::array();
    for (const auto& r : robots) {
        if (r.ip.empty()) continue;
        nlohmann::json o = {{"ip", r.ip}, {"manual", r.manual}};
        if (!r.sn.empty()) o["sn"] = r.sn;        // 有才写：不拿空字段污染文件
        if (!r.model.empty()) o["model"] = r.model;
        j.push_back(std::move(o));
    }
    std::ofstream f("robot_devices.json", std::ios::trunc);
    if (f) f << j.dump(2) << "\n";
}

std::vector<RobotEntry> UiState::robotsSnapshot() {
    std::lock_guard<std::mutex> lock(robotsMutex);
    return robots;
}

std::string UiState::rawJsonSnapshot() const {
    // 持锁拷一份出来。裸读这个 char[] 的话，界面线程正在 InputText 里改它，
    // 拷出来的可能是半截字符串 —— 好在下游 parse 有 try/catch，但不该靠那个兜。
    std::lock_guard<std::mutex> lock(rawJsonMutex);
    return std::string(rawJson);
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

void UiState::updateTelemetry(const std::string& ip, const Telemetry& t) {
    {
        std::lock_guard<std::mutex> lock(robotsMutex);
        for (auto& r : robots)
            if (r.ip == ip) {
                if (t.hasSoc) r.battery = t.soc;
                if (!t.mode.empty()) r.modeName = t.mode;
                if (t.hasTemp) r.temperature = t.tempC;
                if (t.hasYaw) {
                    r.yaw = t.yaw;
                    r.hasYaw = true;
                }
                break;
            }
    }
    // 机器人自己上报的模式名（部分固件会给 "normal"/"ai"/"mcf"）也要回显到模式页
    if (!t.mode.empty()) setMotionMode(t.mode);
}

float UiState::yawOf(const std::string& ip, bool* ok) const {
    std::lock_guard<std::mutex> lock(robotsMutex);
    for (const auto& r : robots)
        if (r.ip == ip) {
            if (ok) *ok = r.hasYaw;
            return r.yaw;
        }
    if (ok) *ok = false;
    return 0.0f;
}

float UiState::temperatureMin() const {
    std::lock_guard<std::mutex> lock(robotsMutex);
    float best = -1000.0f;
    for (const auto& r : robots) {
        if (!r.selected) continue;
        if (r.temperature <= -999.0f) continue;  // 这台还没报过温度
        best = (best <= -999.0f) ? r.temperature : std::min(best, r.temperature);
    }
    return best;
}

void UiState::setMotionMode(const std::string& mode) {
    if (mode.empty()) return;
    std::lock_guard<std::mutex> lock(modeMutex);
    motionMode_ = mode;
}

std::string UiState::motionMode() const {
    std::lock_guard<std::mutex> lock(modeMutex);
    return motionMode_;
}

std::string UiState::scriptNameSnap() const {
    std::lock_guard<std::mutex> lock(scriptMutex);
    return scriptName_;
}

void UiState::setScriptName(const std::string& n) {
    std::lock_guard<std::mutex> lock(scriptMutex);
    scriptName_ = n;
}

void UiState::abortScript() {
    scriptGen.fetch_add(1);   // 让正在跑的脚本发现代际号变了 → 自行退出
    scriptRunning.store(false);
}

// ---------------------------------------------------------------- 舞蹈编排
// 存本应用目录下的 robot_dances.json（与设备列表/名称同一套"只碰自己目录"的规矩）。
void UiState::loadDancesLocked() {
    std::ifstream f("robot_dances.json");
    if (!f) return;
    try {
        nlohmann::json j;
        f >> j;
        if (!j.is_array()) return;
        for (const auto& e : j) {
            Dance d = danceFromJson(e);
            if (d.name.empty() && d.steps.empty()) continue;
            if (d.id <= 0) d.id = nextDanceId_++;
            nextDanceId_ = std::max(nextDanceId_, d.id + 1);
            dances.push_back(std::move(d));
        }
    } catch (...) {
        // 文件坏了就当没存过 —— 绝不能因为一个编排文件让界面起不来
    }
}

void UiState::saveDancesLocked() const {
    nlohmann::json j = nlohmann::json::array();
    for (const auto& d : dances) j.push_back(danceToJson(d));
    std::ofstream f("robot_dances.json", std::ios::trunc);
    if (f) f << j.dump(2) << "\n";
}

void UiState::loadDances() {
    std::lock_guard<std::mutex> lock(dancesMutex);
    if (!dances.empty()) return;  // 已经加载过
    loadDancesLocked();
}

std::vector<Dance> UiState::dancesSnapshot() const {
    std::lock_guard<std::mutex> lock(dancesMutex);
    return dances;
}

int UiState::upsertDance(const Dance& d) {
    std::lock_guard<std::mutex> lock(dancesMutex);
    if (d.id > 0) {
        for (auto& e : dances)
            if (e.id == d.id) {
                e = d;
                saveDancesLocked();
                return e.id;
            }
    }
    Dance n = d;
    n.id = nextDanceId_++;
    dances.push_back(std::move(n));
    saveDancesLocked();
    return dances.back().id;
}

void UiState::removeDance(int id) {
    std::lock_guard<std::mutex> lock(dancesMutex);
    for (auto it = dances.begin(); it != dances.end(); ++it)
        if (it->id == id) {
            dances.erase(it);
            saveDancesLocked();
            return;
        }
}

bool UiState::danceById(int id, Dance* out) const {
    std::lock_guard<std::mutex> lock(dancesMutex);
    for (const auto& e : dances)
        if (e.id == id) {
            if (out) *out = e;
            return true;
        }
    return false;
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
    // 待确认也一并清掉：急停已经把开关全关了，别再过期回滚一次去"复活"某个开关
    pendingToggles.clear();
}

void UiState::noteToggleSent(const std::string& key, const std::vector<int>& ids, bool want,
                             bool prev, int sentCount) {
    std::lock_guard<std::mutex> lock(toggleMutex);
    PendingToggle p;
    p.want = want;
    p.prev = prev;
    p.sent = sentCount;
    p.ids = ids;
    p.sentAt = std::chrono::steady_clock::now();
    pendingToggles[key] = std::move(p);
}

int UiState::reapUnconfirmedToggles(double timeoutSeconds) {
    std::vector<std::pair<std::string, PendingToggle>> expired;
    {
        std::lock_guard<std::mutex> lock(toggleMutex);
        const auto now = std::chrono::steady_clock::now();
        for (auto it = pendingToggles.begin(); it != pendingToggles.end();) {
            const double age = std::chrono::duration<double>(now - it->second.sentAt).count();
            if (age < timeoutSeconds) {
                ++it;
                continue;
            }
            // 回滚：状态回到发送前，"真正开过的开关"集合也一起还原
            // （否则急停会去关一个从来没打开成功过的开关）
            toggles[it->first] = it->second.prev;
            for (int id : it->second.ids) {
                if (it->second.prev)
                    activeToggleIds.insert(id);
                else
                    activeToggleIds.erase(id);
            }
            expired.emplace_back(it->first, it->second);
            it = pendingToggles.erase(it);
        }
    }
    for (const auto& kv : expired) {
        std::string name = kv.first;
        if (!kv.second.ids.empty()) {
            const std::string cn = labelForApiId(kv.second.ids.front());
            if (!cn.empty()) name = cn;
        }
        // 回滚原因要分清：一种是"根本没发出去"（没有受控/就绪设备），
        // 另一种是"发了但机器人没确认"。混成一句话会把排查方向带偏。
        const std::string why = kv.second.sent > 0
                                    ? "机器人没确认（未收到成功回执）"
                                    : "没发给任何就绪设备";
        addLog("[开关] " + name + " " + why + "，已回滚为" + (kv.second.prev ? "开" : "关"));
    }
    return static_cast<int>(expired.size());
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

// ---------------------------------------------------------------- 机型

std::string UiState::modelOf(const std::string& ip) const {
    std::lock_guard<std::mutex> lock(robotsMutex);
    for (const auto& r : robots)
        if (r.ip == ip) return r.model;
    return {};
}

void UiState::setKeySnTable(std::vector<std::pair<std::string, std::string>> pairs) {
    std::lock_guard<std::mutex> lock(keySnMutex);
    keySn.clear();
    for (auto& kv : pairs)
        if (!kv.first.empty() && !kv.second.empty()) keySn[kv.first] = kv.second;
}

std::string UiState::noteHandshakeKey(const std::string& ip, const std::string& keyHex) {
    if (ip.empty() || keyHex.empty()) return {};
    std::string sn;
    {
        std::lock_guard<std::mutex> lock(keySnMutex);
        auto it = keySn.find(keyHex);
        if (it == keySn.end()) {
            // 大小写不敏感地再找一次（用过的 key 由握手回传，大小写不保证）
            std::string low = keyHex;
            for (char& c : low)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            it = keySn.find(low);
        }
        if (it != keySn.end()) sn = it->second;
    }
    if (sn.empty()) return {};
    const std::string model = modelName(modelFromSn(sn));
    if (model.empty()) return {};  // SN 认不出机型（不是 Go2 三款）→ 不写、不猜
    {
        std::lock_guard<std::mutex> lock(robotsMutex);
        for (auto& r : robots) {
            if (r.ip != ip) continue;
            if (r.sn == sn && r.model == model) return model;
            r.sn = sn;
            r.model = model;
            saveDevicesLocked();
            break;
        }
    }
    return model;
}

std::string UiState::controlModel() const {
    std::lock_guard<std::mutex> lock(robotsMutex);
    std::string model;
    bool any = false;
    for (const auto& r : robots) {
        if (!r.selected) continue;
        any = true;
        if (r.model.empty()) return {};        // 有一台机型未知 → 不做机型过滤
        if (model.empty()) model = r.model;
        else if (model != r.model) return {};  // 群控混机型 → 同样不过滤
    }
    return any ? model : std::string();
}

std::vector<std::string> UiState::controlModels() const {
    std::lock_guard<std::mutex> lock(robotsMutex);
    std::vector<std::string> out;
    for (const auto& r : robots) {
        if (!r.selected || r.model.empty()) continue;
        if (std::find(out.begin(), out.end(), r.model) == out.end()) out.push_back(r.model);
    }
    return out;
}

UiState::ModelSupport UiState::modelSupport(int apiId) const {
    UiState::ModelSupport s;
    if (apiId == 0) return s;
    std::vector<std::string> models;
    {
        std::lock_guard<std::mutex> lock(robotsMutex);
        for (const auto& r : robots) {
            if (!r.selected) continue;
            // ★ 有任意一台**机型未知**的受控设备 → 整条判断作废（total 保持 0）：
            //   宁可什么都不标，也不能按一半设备的信息去隐藏动作。
            if (r.model.empty()) return s;
            if (std::find(models.begin(), models.end(), r.model) == models.end())
                models.push_back(r.model);
        }
    }
    s.total = static_cast<int>(models.size());
    if (s.total == 0) return s;
    // 收完机型名再加 apiMutex（两把锁不嵌套，避免与 noteApiResult 形成反序）
    std::lock_guard<std::mutex> lock(apiMutex);
    for (const auto& m : models) {
        auto mit = modelApiCode.find(m);
        if (mit == modelApiCode.end()) continue;
        auto it = mit->second.find(apiId);
        if (it == mit->second.end()) continue;
        ++s.recorded;
        if (it->second != 0) {
            ++s.failed;
            s.rejectModels.push_back(m);
        }
    }
    return s;
}

// ---------------------------------------------------------------- 动作回执

void UiState::noteApiResult(const std::string& ip, int apiId, int code, const std::string& note) {
    if (apiId == 0) return;
    // 先查机型（内部只持 robotsMutex，与下面的 apiMutex 无嵌套，不会死锁）
    const std::string model = ip.empty() ? std::string() : modelOf(ip);
    {
        std::lock_guard<std::mutex> lock(apiMutex);
        apiCode[apiId] = code;
        apiNote[apiId] = note;
        if (!model.empty()) {
            auto& codes = modelApiCode[model];
            auto it = codes.find(apiId);
            const bool changed = (it == codes.end() || it->second != code);
            codes[apiId] = code;
            modelApiNote[model][apiId] = note;
            // ★ 只在**结果变化**时落盘：摇杆的移动指令 (1008) 会以 10Hz 回执，
            //   每次 ack 都写一次文件会把磁盘打满（而且内容根本没变）。
            if (changed) saveModelCapsLocked();
        }
    }
    // ★ 成功的回执 = 对应的「持续模式开关」真的生效了 → 撤掉它的"待确认"。
    //   注意两条指令集都要认：被拒后客户端会自动换另一套 api_id 重试，
    //   成功的那次用的是备用 id（pendingToggles 里两个 id 都记着）。
    if (code == 0) {
        std::lock_guard<std::mutex> lock(toggleMutex);
        for (auto it = pendingToggles.begin(); it != pendingToggles.end();) {
            const auto& ids = it->second.ids;
            if (std::find(ids.begin(), ids.end(), apiId) != ids.end())
                it = pendingToggles.erase(it);  // 确认：保持现状，不再回滚
            else
                ++it;
        }
    }
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

bool UiState::apiResultForModel(const std::string& model, int apiId, int* code, std::string* note) {
    if (!model.empty()) {
        bool hit = false;
        int c = -1;
        std::string n;
        {
            std::lock_guard<std::mutex> lock(apiMutex);
            auto mit = modelApiCode.find(model);
            if (mit != modelApiCode.end()) {
                auto it = mit->second.find(apiId);
                if (it != mit->second.end()) {
                    hit = true;
                    c = it->second;
                    auto nmit = modelApiNote.find(model);
                    if (nmit != modelApiNote.end()) {
                        auto nt = nmit->second.find(apiId);
                        if (nt != nmit->second.end()) n = nt->second;
                    }
                }
            }
        }
        if (hit) {
            if (code) *code = c;
            if (note) *note = n;
            return true;  // ★ 机型有记录就用它，**不回退**全局 —— 否则 Air 的失败会污染 Pro
        }
    }
    // 机型未知 / 该机型还没试过 → 退回全局记录（不能在上面持锁时调用 apiResult）
    return apiResult(apiId, code, note);
}

void UiState::loadModelCaps() {
    std::ifstream f("robot_model_caps.json");
    if (!f) return;
    try {
        nlohmann::json j;
        f >> j;
        if (!j.is_object()) return;
        std::lock_guard<std::mutex> lock(apiMutex);
        for (auto mit = j.begin(); mit != j.end(); ++mit) {
            if (!mit.value().is_object()) continue;
            const std::string model = mit.key();
            if (modelFromName(model) == RobotModel::Unknown) continue;  // 只认三款机型
            for (auto ait = mit.value().begin(); ait != mit.value().end(); ++ait) {
                const int apiId = std::atoi(ait.key().c_str());
                if (apiId == 0) continue;
                if (ait.value().is_object()) {
                    modelApiCode[model][apiId] = ait.value().value("code", -1);
                    modelApiNote[model][apiId] = ait.value().value("note", std::string());
                } else if (ait.value().is_number_integer()) {
                    modelApiCode[model][apiId] = ait.value().get<int>();
                }
            }
        }
    } catch (...) {
        // 文件损坏就当没学过 —— 丢了只是重新标一次，不该让界面起不来
    }
}

void UiState::saveModelCapsLocked() const {
    nlohmann::json j = nlohmann::json::object();
    for (const auto& mk : modelApiCode) {
        if (modelFromName(mk.first) == RobotModel::Unknown) continue;
        nlohmann::json m = nlohmann::json::object();
        for (const auto& ak : mk.second) {
            nlohmann::json o = {{"code", ak.second}};
            auto nit = modelApiNote.find(mk.first);
            if (nit != modelApiNote.end()) {
                auto nt = nit->second.find(ak.first);
                if (nt != nit->second.end() && !nt->second.empty()) o["note"] = nt->second;
            }
            m[std::to_string(ak.first)] = std::move(o);
        }
        j[mk.first] = std::move(m);
    }
    std::ofstream f("robot_model_caps.json", std::ios::trunc);
    if (f) f << j.dump(2) << "\n";
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
            // scanSubnet 拿不到 SN（只有多播能拿到）→ 这里的 r.sn 通常为空，
            // 机型要等多播那一轮补上（addOrUpdate 会对已有条目补机型）
            if (ui.addOrUpdate(r.ip, false, r.sn)) {
                ++added;
                ui.addLog("[扫描] 发现 Go2: " + r.ip + " [" + r.note + "]");
                toConnect.push_back(r.ip);
            }
        }
    }
    // 多播 SN 发现：补上跨网段/多网卡时的漏网设备，并给出 SN（**机型就靠它**）
    for (const auto& kv : Discovery::multicastSnScan(
             1500, [&](const std::string& line) { ui.addLog("[扫描] " + line); })) {
        if (ui.addOrUpdate(kv.second, false, kv.first)) {
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
