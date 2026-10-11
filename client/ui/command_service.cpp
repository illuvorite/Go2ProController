#include "command_service.hpp"

#include "dance.hpp"
#include "quick_actions.hpp"
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

bool ManagerSink::setMotionMode(const std::string& ip, const std::string& mode) {
    auto* c = mgr_.find(ip);
    return c && c->isReady() && c->setMotionMode(mode);
}

// ---------------------------------------------------------------- 群控语义

namespace cmd {

namespace {
/// 服务层限幅的**硬上限**（与界面滑条的范围一致）。
/// 即便有人把 maxLinSpeed / yawRate 配成了离谱的值，下发出去也不会超过这两个数。
constexpr float kHardMaxLin = 1.5f;  // m/s
constexpr float kHardMaxYaw = 2.0f;  // rad/s

/// 「摆姿势」模式下摇杆满推对应的最大姿态角（弧度）。
/// 取值对齐参考实现「天树探界遥控」：它把摇杆行程直接当 euler 角用，
/// 左右倾斜最多 1.0、前后俯仰与扭转最多 0.8。这里照抄同样的量级，
/// 想更保守就改这三个数 —— 只此一处。
constexpr float kPoseMaxRoll = 1.0f;
constexpr float kPoseMaxPitch = 0.8f;
constexpr float kPoseMaxYaw = 0.8f;
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
    // ★ 摆姿势开着 → 摇杆只调姿态、不走路（与参考实现「天树探界」一致）。
    //   判断放这里而不是各界面里：桌面 / 网页 / 安卓共用同一处，
    //   不会出现"一个界面在摆姿势、另一个界面还在让狗走"的互抢。
    if (ui.toggleState("Pose")) return poseAdjustSelected(sink, ui, x, y, z);
    nlohmann::json p = nlohmann::json::object();
    p["x"] = x;
    p["y"] = y;
    p["z"] = z;
    return forEachSelected(sink, [&](const std::string& ip) {
        return sink.sendSport(ip, kApiMove, p);
    });
}

/// 指令表里按 key 找一条（找不到返回 nullptr）
const SportAction* actionByKey(const char* key) {
    for (const SportAction& a : sportActions())
        if (std::string(a.key) == key) return &a;
    return nullptr;
}

int poseAdjustSelected(CommandSink& sink, const UiState& ui, float x, float y, float z) {
    const SportAction* euler = actionByKey("Euler");
    if (!euler) return 0;
    const int id = resolveApiId(ui, *euler);
    if (id == 0) return 0;  // 当前指令集没有 Euler 这条 → 不发（不要退化成 Move）

    // 归一化成"摇杆行程比例"：除以上限就是满推时的比例。
    // 上限沿用 clampMotion 的取值口径（界面设置与硬上限取小），保证同一个杆位
    // 在"走路"和"摆姿势"两种模式下的相对力度一致。
    const float rawLin = ui.maxLinSpeed.load();
    const float rawYaw = ui.yawRate.load();
    const float lin =
        (std::isfinite(rawLin) && rawLin > 0.0f) ? std::min(rawLin, kHardMaxLin) : kHardMaxLin;
    const float yaw =
        (std::isfinite(rawYaw) && rawYaw > 0.0f) ? std::min(rawYaw, kHardMaxYaw) : kHardMaxYaw;

    const auto ratio = [](float v, float full) {
        if (!std::isfinite(v) || full <= 0.0f) return 0.0f;
        return std::clamp(v / full, -1.0f, 1.0f);
    };
    const float nx = ratio(x, lin);  // 前后（正=前进）
    const float ny = ratio(y, lin);  // 左右（正=左）
    const float nz = ratio(z, yaw);  // 转向（正=左转）

    nlohmann::json p = nlohmann::json::object();
    // 符号按参考实现定：
    //   roll  = -左右 → 左倾为负（我方正 y = 左，故取负）
    //   pitch = -前后 → 抬头为负（我方正 x = 前进，故取负）
    //   yaw   = +转向 → 左扭为正（我方正 z = 左转，符号一致）
    p["x"] = -ny * kPoseMaxRoll;
    p["y"] = -nx * kPoseMaxPitch;
    p["z"] = nz * kPoseMaxYaw;
    return forEachSelected(sink, [&](const std::string& ip) {
        return sink.sendSport(ip, id, p);
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
    ui.webMovingSent = false;
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
    const bool prev = ui.toggleState(a.key);  // 回滚目标（发送前的状态）
    nlohmann::json p = nlohmann::json::object();
    p["data"] = on;
    const int n = forEachSelected(sink, [&](const std::string& ip) {
        return sink.sendSport(ip, id, p);
    });
    // ★ 关闭「摆姿势」时额外补一条 StopMove —— 参考实现（天树探界遥控）里
    //   `commandsNeedStopMove = ['freejump','freebound','pose']` 就是这么做的：
    //   摆姿势期间摇杆一直在下发 Euler，退出时把最后一次摆动速度收干净，
    //   否则松手/关闭后狗还会带着一点"摆动余速"。
    if (!on && std::string(a.key) == "Pose")
        forEachSelected(sink, [&](const std::string& ip) { return sink.stopMove(ip); });
    ui.setToggle(a.key, on);
    // 记录"真正开过的开关"（含另一套指令集的 id）—— 急停只关这些，避免把通道灌爆
    std::vector<int> ids{id};
    for (int alt : alternateApiIds(id)) ids.push_back(alt);
    for (int tid : ids) ui.setToggleActive(tid, on);
    // ★ 置位是**乐观**的，同时挂一条"待确认"：机器人拒绝这条指令时（回执 code≠0 且
    //   自动换 id 重试也没成功），`reapUnconfirmedToggles` 会把状态回滚回去。
    //   没有这一步，界面会在不支持的机型上谎报"已开启"，摆姿势还会顺手把摇杆换成 Euler。
    ui.noteToggleSent(a.key, ids, on, prev, n);
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
    // 顺带让正在跑的脚本（快捷动作 / 舞蹈播放）立刻退出循环 —— 它们每步都查代际号，
    // 这里自增后它们下一步就会自行收尾，不会继续往通道里灌指令。
    ui.abortScript();

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
    ui.webMovingSent = false;  // 两个界面都不再"正在下发"
    return r;
}

// ---------------------------------------------------------------- 快捷动作 / 脚本

namespace {

/// 脚本的"我还该继续吗"守卫。
///
/// 用**代际号**而不是一个 bool：急停之后用户可能立刻又点了别的脚本，
/// 若旧脚本用 bool 复位就必然把新脚本一起杀掉（旧脚本醒来时新脚本已在跑）。
/// 代际号天然区分"我是哪一次启动的"。
struct ScriptGuard {
    const UiState* ui = nullptr;
    unsigned gen = 0;

    bool alive() const { return ui && ui->scriptGen.load() == gen && !ui->estop.load(); }

    /// 可分片中断的睡眠。返回 false = 中途被中止（调用方应立即收尾退出）。
    bool sleep(int ms) const {
        constexpr int kChunk = 25;  // 25ms 一片：急停响应够快，又不至于空转
        for (int t = 0; t < ms;) {
            if (!alive()) return false;
            const int d = std::min(kChunk, ms - t);
            std::this_thread::sleep_for(std::chrono::milliseconds(d));
            t += d;
        }
        return alive();
    }
};

/// 起一个后台脚本。返回本次代际号（0 = 没启动）。
unsigned startScript(const std::shared_ptr<CommandSink>& sink, UiState& ui, const std::string& name,
                     const std::function<void(const ScriptGuard&)>& body) {
    if (!sink) return 0;
    const unsigned gen = ui.scriptGen.fetch_add(1) + 1;  // 新代际 → 旧脚本自动失效
    ui.scriptRunning.store(true);
    ui.setScriptName(name);
    UiState* uip = &ui;  // ui 由入口持有，活到进程结束（与 estop 的异步收尾同一约定）
    std::thread([sink, uip, gen, body] {
        const ScriptGuard g{uip, gen};
        body(g);
        if (uip->scriptGen.load() == gen) {  // 只有"还是当前脚本"才复位标志
            uip->scriptRunning.store(false);
            uip->setScriptName("");
        }
    }).detach();
    return gen;
}

/// 对"受控且就绪"的设备发同一条指令，返回成功台数
int sendToSelected(CommandSink& sink, int apiId, const nlohmann::json& p) {
    if (apiId == 0) return 0;
    return forEachSelected(sink, [&](const std::string& ip) { return sink.sendSport(ip, apiId, p); });
}

/// 指令表里按 key 解析 api_id（尊重当前指令集；解析不到用 fallback）
int apiKeyId(const UiState& ui, const char* key, int fallback = 0) {
    const SportAction* a = actionByKey(key);
    if (!a) return fallback;
    const int id = resolveApiId(ui, *a);
    return id != 0 ? id : fallback;
}

}  // namespace

/// 姿态预设：先平衡站立，等 500ms，再发 Euler（与参考实现逐条对齐）
unsigned startEulerPreset(const std::shared_ptr<CommandSink>& sink, UiState& ui,
                          const QuickAction& q) {
    const std::string name = std::string(q.label);
    return startScript(sink, ui, name, [sink, &ui, q](const ScriptGuard& g) {
        const int bal = apiKeyId(ui, "BalanceStand", 1002);
        const int euler = apiKeyId(ui, "Euler", 1007);
        sendToSelected(*sink, bal, nlohmann::json());
        if (!g.sleep(kQuickBalanceDelayMs)) return;  // 被急停/新脚本打断 → 不发姿态角
        nlohmann::json p = nlohmann::json::object();
        p["x"] = q.x;
        p["y"] = q.y;
        p["z"] = q.z;
        sendToSelected(*sink, euler, p);
    });
}

/// 平移：按 150ms 节奏连发 1 秒 Move，然后补一条 StopMove
unsigned startMoveBurst(const std::shared_ptr<CommandSink>& sink, UiState& ui, const QuickAction& q) {
    const std::string name = std::string(q.label);
    return startScript(sink, ui, name, [sink, &ui, q](const ScriptGuard& g) {
        nlohmann::json p = nlohmann::json::object();
        p["x"] = q.x;
        p["y"] = q.y;
        p["z"] = q.z;
        for (int t = 0; t < kMoveBurstMs; t += kMoveBurstIntervalMs) {
            if (!g.alive()) return;
            sendToSelected(*sink, kApiMove, p);
            if (!g.sleep(kMoveBurstIntervalMs)) return;
        }
        // 连发结束必须停车：Go2 的速度是保持型的，不发 StopMove 它会一直平移
        for (const auto& ip : sink->selectedReadyIps()) sink->stopMove(ip);
    });
}

/// 方向回正：开连续步态 → 平衡站立 → 按 yaw 闭环回转 → 停车 → 关连续步态
unsigned startAdjustOrientation(const std::shared_ptr<CommandSink>& sink, UiState& ui) {
    return startScript(sink, ui, "方向回正", [sink, &ui](const ScriptGuard& g) {
        const int gait = apiKeyId(ui, "ContinuousGait", 1019);
        const int bal = apiKeyId(ui, "BalanceStand", 1002);
        nlohmann::json on = nlohmann::json::object();
        on["data"] = true;

        // 1) 开连续步态（实现"踏步回正"而不是原地旋转）+ 等生效
        sendToSelected(*sink, gait, on);
        if (!g.sleep(kGaitSettleMs)) return;

        // 2) 平衡站立 + 稳身
        sendToSelected(*sink, bal, nlohmann::json());
        if (!g.sleep(kOrientStabilizeMs)) return;

        // 前面任何一次被急停/新脚本打断，都在这里统一收尾（关掉连续步态）
        const auto finish = [&] {
            for (const auto& ip : sink->selectedReadyIps()) sink->stopMove(ip);
            nlohmann::json off = nlohmann::json::object();
            off["data"] = false;
            sendToSelected(*sink, gait, off);
        };

        // 3) 闭环：只要还有一台 |yaw| 超阈值就继续转
        const float threshold = static_cast<float>(kOrientThresholdMilli) / 1000.0f;
        const auto t0 = std::chrono::steady_clock::now();
        bool sawAnyYaw = false;
        for (;;) {
            if (!g.alive()) { finish(); return; }
            const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::steady_clock::now() - t0)
                                       .count();
            if (elapsedMs > kOrientTimeoutMs) {
                ui.addLog("[方向回正] 超时（30 秒），已停止调整");
                break;
            }
            bool allAligned = true;
            for (const auto& ip : sink->selectedReadyIps()) {
                bool ok = false;
                const float yaw = ui.yawOf(ip, &ok);
                if (!ok) continue;  // 这台没上报姿态角：不拿它当判据（也不要误判成"已回正"）
                sawAnyYaw = true;
                if (std::fabs(yaw) < threshold) continue;
                allAligned = false;
                // z = -clamp(yaw, ±MAX)（与参考实现同一式子：yaw>0 → 逆时针）
                const float z = -std::clamp(yaw, -kOrientMaxAngularVel, kOrientMaxAngularVel);
                nlohmann::json mv = nlohmann::json::object();
                mv["x"] = 0.0f;
                mv["y"] = 0.0f;
                mv["z"] = z;
                sink->sendSport(ip, kApiMove, mv);
            }
            if (!sawAnyYaw) {
                ui.addLog("[方向回正] 收不到姿态角（该固件的 lowstate 不含 imu.rpy），已取消");
                break;
            }
            if (allAligned) {
                ui.addLog("[方向回正] 已完成");
                break;
            }
            if (!g.sleep(kOrientIntervalMs)) { finish(); return; }
        }
        finish();
        g.sleep(kGaitSettleMs);  // 等连续步态关闭生效
    });
}

/// 原地踏步：开连续步态 → 平衡站立 → 前后来回 Move 约 5 秒 → 停车 → 关连续步态
unsigned startInPlaceStepping(const std::shared_ptr<CommandSink>& sink, UiState& ui) {
    return startScript(sink, ui, "原地踏步", [sink, &ui](const ScriptGuard& g) {
        const int gait = apiKeyId(ui, "ContinuousGait", 1019);
        const int bal = apiKeyId(ui, "BalanceStand", 1002);
        nlohmann::json on = nlohmann::json::object();
        on["data"] = true;

        sendToSelected(*sink, gait, on);
        if (!g.sleep(kGaitSettleMs)) return;
        sendToSelected(*sink, bal, nlohmann::json());
        if (!g.sleep(kQuickBalanceDelayMs)) return;

        for (int t = 0, i = 0; t < kStepDurationMs; t += kStepIntervalMs, ++i) {
            if (!g.alive()) break;
            nlohmann::json mv = nlohmann::json::object();
            mv["x"] = (i % 2 == 0) ? kStepSpeed : -kStepSpeed;  // 前进/后退交替
            mv["y"] = 0.0f;
            mv["z"] = 0.0f;
            sendToSelected(*sink, kApiMove, mv);
            if (!g.sleep(kStepIntervalMs)) break;
        }

        // 收尾（无论正常结束还是被打断）：停车 + 关连续步态
        for (const auto& ip : sink->selectedReadyIps()) sink->stopMove(ip);
        nlohmann::json off = nlohmann::json::object();
        off["data"] = false;
        sendToSelected(*sink, gait, off);
        g.sleep(kGaitSettleMs);
    });
}

int runQuickAction(const std::shared_ptr<CommandSink>& sink, UiState& ui, const std::string& key) {
    if (!sink) return 0;
    const QuickAction* q = findQuickAction(key);
    if (!q) return -1;
    // 「恢复移动」本身就是安全方向，急停锁定时也允许（与 stopSelected 同理）
    if (ui.estop.load() && q->kind != QuickKind::StopMove) return 0;
    if (sink->selectedReadyIps().empty()) return 0;

    switch (q->kind) {
        case QuickKind::StopMove:
            return stopSelected(*sink);
        case QuickKind::BodyHeight: {
            const int id = apiKeyId(ui, "BodyHeight", 1013);
            nlohmann::json p = nlohmann::json::object();
            p["data"] = q->data;
            return sendToSelected(*sink, id, p);
        }
        case QuickKind::EulerPreset:
            return startEulerPreset(sink, ui, *q) ? 1 : 0;
        case QuickKind::MoveBurst:
            return startMoveBurst(sink, ui, *q) ? 1 : 0;
        case QuickKind::AdjustOrientation:
            return startAdjustOrientation(sink, ui) ? 1 : 0;
        case QuickKind::InPlaceStepping:
            return startInPlaceStepping(sink, ui) ? 1 : 0;
    }
    return 0;
}

void abortScript(UiState& ui) {
    ui.abortScript();
}

int setMotionMode(const std::shared_ptr<CommandSink>& sink, UiState& ui, const std::string& mode) {
    if (!sink || mode.empty()) return 0;
    int n = 0;
    for (const auto& ip : sink->selectedReadyIps())
        if (sink->setMotionMode(ip, mode)) ++n;
    // 乐观置位：以机器人回执为准的部分交给 onTopicData 里的 mode_name 回显覆盖
    if (n > 0) ui.setMotionMode(mode);
    return n;
}

bool playDance(const std::shared_ptr<CommandSink>& sink, UiState& ui, int danceId) {
    if (!sink) return false;
    Dance d;
    if (!ui.danceById(danceId, &d) || d.steps.empty()) return false;
    if (ui.estop.load() || sink->selectedReadyIps().empty()) return false;
    // 再点一次播放 = 先中止上一套（与参考实现 playDance 里"正在播就先 stop"一致）
    ui.abortScript();
    return startScript(sink, ui, "舞蹈：" + d.name, [sink, &ui, d](const ScriptGuard& g) {
        int i = 0;
        for (const auto& step : d.steps) {
            if (!g.alive()) break;
            ++i;
            if (step.key == "wait") {
                if (!g.sleep(step.delayMs)) break;
                continue;
            }
            const int n = dispatchAction(*sink, ui, step.key);
            if (n < 0) {
                ui.addLog("[舞蹈] 第 " + std::to_string(i) + " 步未知动作：" + step.key);
            } else if (n == 0) {
                ui.addLog("[舞蹈] 第 " + std::to_string(i) + " 步没有可用的受控设备：" + step.key);
                break;  // 没设备了，继续播下去只会白等
            }
            if (!g.sleep(step.delayMs)) break;
        }
        if (ui.scriptGen.load() == g.gen) ui.addLog("[舞蹈] 「" + d.name + "」播放结束");
    }) != 0;
}

}  // namespace cmd
}  // namespace go2
