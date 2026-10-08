#include "command_service.hpp"

#include "robot_client.hpp"
#include "robot_manager.hpp"
#include "ui.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

namespace go2 {

// ---------------------------------------------------------------- ManagerSink

std::vector<std::string> ManagerSink::readyIps() const {
    std::vector<std::string> out;
    // 走 snapshot()：管理器保证它是加锁的一致快照
    for (const auto& s : mgr_.snapshot())
        if (s.state == ConnState::Ready) out.push_back(s.ip);
    return out;
}

std::vector<std::string> ManagerSink::selectedReadyIps() const {
    std::vector<std::string> out;
    for (const auto& ip : ui_.selectedIps()) {
        if (auto* c = mgr_.find(ip); c && c->isReady()) out.push_back(ip);
    }
    return out;
}

bool ManagerSink::sendSport(const std::string& ip, int apiId, const nlohmann::json& param) {
    if (apiId == 0) return false;
    auto* c = mgr_.find(ip);
    return c && c->isReady() && c->sendSportCommand(apiId, param);
}

bool ManagerSink::stopMove(const std::string& ip) {
    auto* c = mgr_.find(ip);
    return c && c->isReady() && c->stopMove();
}

int ManagerSink::disablePersistent(const std::string& ip, const std::vector<int>& ids) {
    auto* c = mgr_.find(ip);
    if (!c || !c->isReady()) return 0;
    return c->disablePersistentModes(ids);
}

// ---------------------------------------------------------------- 群控语义

namespace cmd {

namespace {
/// 服务层限幅的**硬上限**（与界面滑条的范围一致）。
/// 即便有人把 maxLinSpeed / yawRate 配成了离谱的值，下发出去也不会超过这两个数。
constexpr float kHardMaxLin = 1.5f;  // m/s
constexpr float kHardMaxYaw = 2.0f;  // rad/s
}  // namespace

int resolveApiId(const UiState& ui, const SportAction& a, bool* fellBack) {
    const bool mcf = ui.mcfMode.load();
    int id = apiIdFor(a, mcf);
    bool fb = false;
    if (id == 0) {
        const int alt = mcf ? a.normalId : a.mcfId;
        if (alt != 0) {
            id = alt;
            fb = true;
        }
    }
    if (fellBack) *fellBack = fb;
    return id;
}

nlohmann::json packParam(const UiState& ui, const SportAction& a, bool flagValue) {
    // 开关型：{"data": true|false}（on/off 语义，StopMove 停不掉，必须带 false 关）
    if (a.param == SportParam::Flag) {
        nlohmann::json p = nlohmann::json::object();
        p["data"] = flagValue;
        return p;
    }
    const std::string key = a.key;
    float realVal = ui.bodyHeight.load();
    if (key == "FootRaiseHeight") realVal = ui.footRaise.load();
    const int intVal = (key == "SwitchGait") ? ui.gaitType.load() : ui.speedLevel.load();
    // euler* 是 atomic（界面线程每帧写，这里可能是 HTTP 线程在读）、rawJson 走锁取快照 ——
    // 直接裸读就是数据竞争，读出来的角度会原样发给狗。
    return buildSportParam(a, intVal, realVal, ui.eulerX.load(), ui.eulerY.load(),
                           ui.eulerZ.load(), ui.rawJsonSnapshot());
}

int forEachSelected(CommandSink& sink, const std::function<bool(const std::string&)>& fn) {
    int n = 0;
    for (const auto& ip : sink.selectedReadyIps())
        if (fn(ip)) ++n;
    return n;
}

int moveSelected(CommandSink& sink, const UiState& ui, float x, float y, float z) {
    // 安全语义：急停锁定期间绝不下发运动（见 hpp 里的说明）
    if (ui.estop.load()) return 0;
    nlohmann::json p = nlohmann::json::object();
    p["x"] = x;
    p["y"] = y;
    p["z"] = z;
    return forEachSelected(sink, [&](const std::string& ip) {
        return sink.sendSport(ip, kApiMove, p);
    });
}

Motion clampMotion(const UiState& ui, float x, float y, float z) {
    // 限幅值 = min(界面上的设置, 硬上限)。设置本身异常（非有限 / <=0）时直接用硬上限 ——
    // 否则 std::clamp 的 lo > hi 是 UB，限幅反而成了崩溃源。
    const float rawLin = ui.maxLinSpeed.load();
    const float rawYaw = ui.yawRate.load();
    const float lin =
        (std::isfinite(rawLin) && rawLin > 0.0f) ? std::min(rawLin, kHardMaxLin) : kHardMaxLin;
    const float yaw =
        (std::isfinite(rawYaw) && rawYaw > 0.0f) ? std::min(rawYaw, kHardMaxYaw) : kHardMaxYaw;
    Motion m;
    m.vx = std::isfinite(x) ? x : 0.0f;
    m.vy = std::isfinite(y) ? y : 0.0f;
    const float mag = std::hypot(m.vx, m.vy);
    if (mag > lin && mag > 0.0f) {  // 斜推时按比例缩回，保持方向
        m.vx *= lin / mag;
        m.vy *= lin / mag;
    }
    m.vz = std::clamp(std::isfinite(z) ? z : 0.0f, -yaw, yaw);
    return m;
}

int moveSelectedClamped(CommandSink& sink, const UiState& ui, float x, float y, float z,
                        Motion* sent) {
    const Motion m = clampMotion(ui, x, y, z);
    if (sent) *sent = m;
    return moveSelected(sink, ui, m.vx, m.vy, m.vz);
}

int stopSelected(CommandSink& sink) {
    return forEachSelected(sink, [&](const std::string& ip) { return sink.stopMove(ip); });
}

int stopDeselected(CommandSink& sink, const std::vector<std::string>& before) {
    const std::vector<std::string> after = sink.selectedReadyIps();
    int n = 0;
    for (const auto& ip : before) {
        // 还在新的受控集合里 → 不归这里管（由 stopSelected 统一处理）
        if (std::find(after.begin(), after.end(), ip) != after.end()) continue;
        if (sink.stopMove(ip)) ++n;
    }
    return n;
}

bool unestop(UiState& ui, bool extraCentered) {
    if (!extraCentered) return false;
    // 判据 2：最近一次下发的速度仍非零 = 摇杆还推着 → 拒绝解锁
    if (std::fabs(ui.cmdVx.load()) > 1e-3f || std::fabs(ui.cmdVy.load()) > 1e-3f ||
        std::fabs(ui.cmdVz.load()) > 1e-3f)
        return false;
    ui.estop = false;
    ui.movingSent = false;
    ui.cmdVx = 0.0f;
    ui.cmdVy = 0.0f;
    ui.cmdVz = 0.0f;
    return true;
}

int quickMove(CommandSink& sink, const UiState& ui, const std::string& dir) {
    const float v = ui.speedScale.load();
    // 速度语义：vx 正 = 前进；vz 正 = 左转（与协议一致）
    float vx = 0.0f, vz = 0.0f;
    if (dir == "fwd") vx = v;
    else if (dir == "back") vx = -v;
    else if (dir == "left") vz = v;
    else if (dir == "right") vz = -v;
    else return -1;
    return moveSelected(sink, ui, vx, 0.0f, vz);
}

int dispatchToggle(CommandSink& sink, UiState& ui, const SportAction& a, bool on) {
    const int id = resolveApiId(ui, a);
    if (id == 0) return 0;
    nlohmann::json p = nlohmann::json::object();
    p["data"] = on;
    const int n = forEachSelected(sink, [&](const std::string& ip) {
        return sink.sendSport(ip, id, p);
    });
    ui.setToggle(a.key, on);
    // 记录"真正开过的开关"（含另一套指令集的 id）—— 急停只关这些，避免把通道灌爆
    std::vector<int> ids{id};
    for (int alt : alternateApiIds(id)) ids.push_back(alt);
    for (int tid : ids) ui.setToggleActive(tid, on);
    return n;
}

int sendAction(CommandSink& sink, UiState& ui, const SportAction& a, bool flagValue) {
    if (a.toggle) return dispatchToggle(sink, ui, a, flagValue);
    const int id = resolveApiId(ui, a);
    if (id == 0) return 0;
    const nlohmann::json p = packParam(ui, a, flagValue);
    return forEachSelected(sink, [&](const std::string& ip) {
        return sink.sendSport(ip, id, p);
    });
}

int resetEuler(CommandSink& sink, UiState& ui) {
    // api_id 从指令表里查，不写死 1007 —— 表改了这里跟着改
    const SportAction* euler = nullptr;
    for (const SportAction& a : sportActions()) {
        if (std::string(a.key) == "Euler") {
            euler = &a;
            break;
        }
    }
    if (!euler) return 0;
    const int id = resolveApiId(ui, *euler);
    if (id == 0) return 0;
    nlohmann::json p = nlohmann::json::object();
    p["x"] = 0.0;
    p["y"] = 0.0;
    p["z"] = 0.0;
    // 无论有没有设备收到，界面上的滑条都要归零 —— 它显示的是"狗现在是什么姿态"，
    // 留着非零值会让人以为狗还歪着（其实下一条动作就已经把姿态覆盖了）
    ui.eulerX = 0.0f;
    ui.eulerY = 0.0f;
    ui.eulerZ = 0.0f;
    ui.eulerResetAt = -1.0;
    return forEachSelected(sink, [&](const std::string& ip) {
        return sink.sendSport(ip, id, p);
    });
}

int dispatchAction(CommandSink& sink, UiState& ui, const std::string& key) {
    for (const SportAction& a : sportActions()) {
        if (key != a.key) continue;
        // 开关型：按"当前状态的相反值"下发（一个按钮即开即关）
        const bool flag = a.toggle ? !ui.toggleState(a.key) : true;
        return sendAction(sink, ui, a, flag);
    }
    return -1;  // key 不在指令表里
}

int dampAll(CommandSink& sink) {
    nlohmann::json none;  // Damp(1001) 无参数 → 空字符串
    int n = 0;
    for (const auto& ip : sink.readyIps())
        if (sink.sendSport(ip, 1001, none)) ++n;
    return n;
}

EstopResult estop(const std::shared_ptr<CommandSink>& sink, UiState& ui, bool asyncClose) {
    EstopResult r;
    if (!sink) return r;

    // 1) 先锁定：此后摇杆/快捷/Web 的 move 一律不下发（各前端都要读这个标志）
    ui.estop = true;

    // 2) 立刻停车 —— 对**全部就绪**设备，不限于勾选（最安全）
    const std::vector<std::string> all = sink->readyIps();
    for (const auto& ip : all)
        if (sink->stopMove(ip)) ++r.stopped;

    // 3) 只关"我们真正打开过"的持续模式开关：
    //    StopMove 只停速度，停不掉"自由行走 / 领航跟随 / 交叉步 / 经济步态"这类一直生效的开关。
    //    连按急停也不会把通道灌爆（上一版每次关 20 个，连按十几次把 SCTP 队列打满，指令反被丢）。
    const std::vector<int> toClose = ui.activeToggleIdsSnapshot();
    r.toggles = static_cast<int>(toClose.size());

    if (asyncClose) {
        if (!ui.estopBusy.exchange(true)) {
            UiState* uip = &ui;  // ui 由入口持有，活到进程结束
            std::thread([sink, all, toClose, uip] {
                // 狗在执行动作时可能吞掉第一条 StopMove → 补发两次
                for (int round = 0; round < 2; ++round) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(330));
                    for (const auto& ip : all) sink->stopMove(ip);
                }
                if (!toClose.empty())
                    for (const auto& ip : all) sink->disablePersistent(ip, toClose);
                uip->estopBusy = false;
            }).detach();
        }
    } else if (!toClose.empty()) {
        for (const auto& ip : all) sink->disablePersistent(ip, toClose);
    }

    // 4) 复位开关状态：界面上的"开"必须跟着变成"关"
    ui.clearToggles();
    ui.clearActiveToggles();
    ui.movingSent = false;
    return r;
}

}  // namespace cmd
}  // namespace go2
