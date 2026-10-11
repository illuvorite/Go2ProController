#pragma once

// ============================================================================
// 群控语义的**唯一实现**。
//
// 为什么必须集中（改造前的问题）：
//   桌面端（ui.cpp）和网页端（web_bridge.cpp）各写了一遍「急停 / 群控分发 / 动作指令集
//   回退」—— 急停是安全动作，两边口径一旦分叉就是事故；而且 Web 端还把群控循环内联了 6 次，
//   新增一条指令要改好几处。
//
// 现在：前端只负责「画」和「收事件」，所有"发给谁、发什么、怎么收尾"都在这里。
//
// 为什么还要 CommandSink：
//   RobotManager 是具体类（内部跑 WebRTC 连接），测试没法塞假实现。
//   把"发包"抽成一个窄接口后：
//     · 生产代码用 ManagerSink（包住 RobotManager + UiState）；
//     · 测试用 FakeSink 记录"到底发给了谁、发了什么"，把安全语义钉死
//       （见 tests/command_service_test.cpp）。
// ============================================================================

#include "sport_library.hpp"

#include <nlohmann/json.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace go2 {

class RobotManager;
struct UiState;

// 移动指令的 api_id 是 `go2::kApiMove`，定义在 core/sport_library.hpp（指令表的家）。

/// 群控下发的**唯一出口**（窄接口，便于替换成假实现做测试）
class CommandSink {
public:
    virtual ~CommandSink() = default;

    /// 全部**就绪**设备的 IP。急停必须作用于全部就绪设备，**不限于勾选**。
    virtual std::vector<std::string> readyIps() const = 0;

    /// "受控且就绪"的 IP = 勾选集合 ∩ 就绪集合。普通群控指令的目标。
    virtual std::vector<std::string> selectedReadyIps() const = 0;

    /// 下发一条运动指令（api_id + parameter）。
    virtual bool sendSport(const std::string& ip, int apiId,
                           const nlohmann::json& param) = 0;

    /// StopMove（只停速度；停不掉持续模式开关）。
    virtual bool stopMove(const std::string& ip) = 0;

    /// 逐个用同一 api_id 带 `{"data": false}` 关掉持续模式。实现内部每条留间隔 → **可能阻塞**。
    virtual int disablePersistent(const std::string& ip, const std::vector<int>& ids) = 0;

    /// 切换运动模式（普通 / AI / MCF）。
    ///
    /// 注意它**不是运动指令**：走的是 `rt/api/motion_switcher/request`（api_id 1002），
    /// 与 sport 服务是两套东西，所以不能拿 sendSport 凑。
    virtual bool setMotionMode(const std::string& ip, const std::string& mode) = 0;
};

/// 生产实现：RobotManager（连接/就绪状态） + UiState（受控集合）
class ManagerSink final : public CommandSink {
public:
    ManagerSink(RobotManager& mgr, UiState& ui) : mgr_(mgr), ui_(ui) {}

    std::vector<std::string> readyIps() const override;
    std::vector<std::string> selectedReadyIps() const override;
    bool sendSport(const std::string& ip, int apiId, const nlohmann::json& param) override;
    bool stopMove(const std::string& ip) override;
    int disablePersistent(const std::string& ip, const std::vector<int>& ids) override;
    bool setMotionMode(const std::string& ip, const std::string& mode) override;

private:
    RobotManager& mgr_;
    UiState& ui_;
};

namespace cmd {

/// 动作在**当前指令集**下的 api_id。当前指令集没有这条（例如 MCF 专属）时用另一套兜底，
/// 避免"整条动作根本点不到"。
/// @param fellBack 出参：是否用了兜底（界面据此标 †）
int resolveApiId(const UiState& ui, const SportAction& a, bool* fellBack = nullptr);

/// 按参数类型打包 parameter。`flagValue` 只对 Flag（开关）类生效。
nlohmann::json packParam(const UiState& ui, const SportAction& a, bool flagValue = true);

/// 对"受控且就绪"的设备逐台执行，返回成功台数。
/// （取代改造前 ui.cpp 匿名命名空间里的 forEachSelected，以及 Web 端 6 处内联循环）
int forEachSelected(CommandSink& sink,
                    const std::function<bool(const std::string& ip)>& fn);

/// 摇杆速度：x 前后(m/s) / y 左右(m/s) / z 转向(rad/s) → 受控设备，返回成功台数。
///
/// ★ **急停锁定（`ui.estop`）时一律返回 0，不下发任何东西。**
///   这条约束故意放在服务层：改造前"锁定期间不许动"靠界面与网页各检查一遍，
///   只要有一处漏了就变成"点了急停狗还在走"。现在只有一处，且有单元测试。
///   （停车 `stopSelected` 不受此限 —— 它本身就是安全方向。）
///
/// ★ **「摆姿势」开着时改发 `Euler`(1007) 而不是 `Move`(1008)** —— 见 `poseAdjustSelected`。
///   这条也放在服务层：桌面、网页、安卓三个入口共用同一个判断，
///   不会出现"某个界面还在让狗走路、另一个界面在摆姿势"的互抢。
int moveSelected(CommandSink& sink, const UiState& ui, float x, float y, float z);

/// 「摆姿势」模式下的摇杆语义：**不走路，只调姿态**。
///
/// 来源：参考实现「天树探界遥控」（`go2-robot-control.html`）——它在摆姿势开启后
/// 把摇杆指令由 `move` 换成 `euler`，参数按 `(roll, pitch, yaw)` 给：
///   左摇杆左右 → 左右倾斜 roll（左倾为负）
///   左摇杆前后 → 前后俯仰 pitch（抬头为负）
///   右摇杆左右 → 左右扭转 yaw（左扭为正）
/// 用户实测的观感就是"推摇杆狗不走了，只是上下左右摆姿势"。
///
/// 本函数沿用 move 的入参语义（x 前后 m/s、y 左右 m/s、z 转向 rad/s），
/// 先按各自的**满量程**归一化成摇杆行程（-1..1），再乘摆姿势的最大角度
/// （`kPoseMaxRoll/Pitch/Yaw`），所以"推到底"= 最大摆动，与推力成比例。
/// @return 成功台数；当前指令集里没有 Euler 这条时返回 0
int poseAdjustSelected(CommandSink& sink, const UiState& ui, float x, float y, float z);

/// 一次摇杆输入经**服务层限幅**后、实际该下发的速度。
struct Motion {
    float vx = 0.0f, vy = 0.0f, vz = 0.0f;
};

/// 网页端摇杆输入的限幅（纯函数，好测）。
///
/// 桌面端不需要这一步 —— 摇杆值经 `core/motion.hpp` 的 planMotion 归一化后天然落在
/// `maxLinSpeed` / `yawRate` 之内。网页端原本把浏览器给的 x/y/z 原样转发，
/// 限幅只存在于 `assets/web/motion.js`（浏览器里）：换个客户端、写个脚本、或前端算错
/// 一次，就能让狗以任意速度冲出去。**安全边界必须在服务端，不能只靠前端自觉。**
/// 规则：平移按 `hypot(vx,vy)` 合成限幅（斜推也不超），转向单独限幅，
/// 两者都再夹一层硬上限；非有限值（NaN / Inf）一律按 0 处理。
Motion clampMotion(const UiState& ui, float x, float y, float z);

/// 网页端摇杆入口：`clampMotion` 限幅后下发，返回成功台数。
/// @param sent 出参：实际下发的值。**调用方负责**把它回填 `ui.cmdVx/Vy/Vz` ——
///              界面显示与 `cmd::unestop` 的"是否回中"判据都必须用**限幅后**的真值。
///              本函数不改 UiState。
int moveSelectedClamped(CommandSink& sink, const UiState& ui, float x, float y, float z,
                        Motion* sent = nullptr);

/// 松手 / 急停停车：对受控设备发 StopMove（任何状态下都允许）
int stopSelected(CommandSink& sink);

/// 受控集合变更后的**收尾**：把"刚刚被移出集合"的设备停掉，返回成功台数。
/// @param before 变更**之前**的受控集合（调用方在改 `selected` 之前抓一份
///                `ui.selectedIps()` 下来）
///
/// ★ 为什么必须有这一步：Go2 的速度是**保持型**的 —— 唯一的清除手段就是 StopMove。
///   改造前取消勾选 / 切单控只是改了个 bool，没有任何收尾，于是：
///     · 取消勾选一台正在行走的狗 → 它继续走；
///     · 而 `stopSelected` 只遍历**新的**受控集合，从此再也停不到它
///       （唯一补救是按急停全量停车，那不该是"取消勾选"的代价）。
///   仍在新集合里的设备不动（它们归 `stopSelected` 管）。
int stopDeselected(CommandSink& sink, const std::vector<std::string>& before);

/// 解除急停锁定。**摇杆未回中时拒绝**（返回 false，锁保持不动）。
///
/// 桌面端的"解除急停"按钮本来就要求双杆回中（`ui_remote.cpp` 的 centered 闸门），
/// 但网页端那个检查只活在浏览器里 —— 安全闸门不能只放在前端：换个客户端、开第二个
/// 标签页、或一次脚本调用，都能在摇杆还推着的时候把锁解开，紧接着摇杆带的 10Hz
/// 循环立刻以旧值全力输出。
///
/// 判据（两条都过才解锁）：
///   1. `extraCentered` —— 调用方额外知道的"摇杆是否回中"。桌面端每帧都在算真实的双杆
///      状态，传进来；网页端没有摇杆概念，用默认值。
///   2. 最近一次下发的速度 `ui.cmdVx/Vy/Vz` 是否为零。网页端靠它：急停锁定期间 `move`
///      会被服务层挡下、`cmdV*` 停在急停前的值上，所以"锁着但杆还推着"必然被抓到。
/// 成功时顺带把 `cmdVx/Vy/Vz` 与 `movingSent` 清零。
bool unestop(UiState& ui, bool extraCentered = true);

/// 快捷方向：fwd / back / left / right（步速取 `ui.speedScale`）。
/// 未知方向返回 -1，其余返回成功台数；同样受急停锁定约束。
int quickMove(CommandSink& sink, const UiState& ui, const std::string& dir);

/// 按动作 key 下发。开关型会**翻转**状态并维护 toggles / activeToggleIds。
/// @return 成功台数；-1 = 该 key 不在指令表里；0 = 当前指令集没有可用 api_id
int dispatchAction(CommandSink& sink, UiState& ui, const std::string& key);

/// 开关型动作：按 on/off 下发，并维护 toggles / activeToggleIds
int dispatchToggle(CommandSink& sink, UiState& ui, const SportAction& a, bool on);

/// 动作库统一出口（界面用）：开关型走 dispatchToggle，其余走 sendSport。
/// @return 成功台数
int sendAction(CommandSink& sink, UiState& ui, const SportAction& a, bool flagValue = true);

/// 姿态角发送后的**保持时长**（秒）：到点自动下发一次全零（见 `resetEuler`）。
///
/// 为什么不是"发完立刻回正"：Go2 的姿态是闭环跟踪目标，下发后要几百毫秒才真的歪过去。
/// 立刻补一条全零会把动作吃掉，狗根本不会倾斜 —— 看着像"点了没反应"。
/// 1.5s 足够走到目标位姿，又不至于长到让人以为没归零。
constexpr double kEulerHoldSeconds = 1.5;

/// 下发一次姿态角全零（roll/pitch/yaw = 0）并把 `ui.eulerX/Y/Z` 清回 0、清掉待归零任务。
///
/// ★ **不受急停锁定约束** —— 与 `stopSelected` 同理：这是安全方向。
///   最需要它的情况恰恰是"已经按下急停、狗停在半歪的姿态上"，此时归零比锁住更重要。
///   它只发 `{"x":0,"y":0,"z":0}`，不会让狗移动。
///
/// 目标集合与普通动作指令一致（受控 ∩ 就绪），不做全量广播 —— 全量广播是急停的语义。
/// @return 成功台数；0 = 当前指令集里没有 Euler 这条
int resetEuler(CommandSink& sink, UiState& ui);

/// 强制阻尼：对**全部就绪**设备（不限于勾选）发 Damp
int dampAll(CommandSink& sink);

// ---------------------------------------------------------------- 快捷动作
// 「快捷动作」= 一次点击要发一串指令的按钮（见 core/quick_actions.hpp）。
// 与运动指令表分开，因为这里有前置动作 / 节奏 / 闭环。

/// 运行一条快捷动作。
///
/// 执行方式：
///   · StopMove / BodyHeight —— 立即下发，返回成功台数；
///   · EulerPreset / MoveBurst / AdjustOrientation / InPlaceStepping ——
///     起一个**后台脚本线程**按节奏下发，本函数立即返回 1（不代表动作已完成）。
///     脚本每一步都会检查"代际号是否还是自己的" —— 急停、或启动新脚本都会让旧脚本
///     自行退出（见 UiState::scriptGen），所以不会出现两个脚本抢同一个通道。
/// @return -1 = 没有这个 key；0 = 急停锁定或没有受控就绪设备；其余见上
int runQuickAction(const std::shared_ptr<CommandSink>& sink, UiState& ui, const std::string& key);

/// 中止正在跑的脚本（快捷动作 / 舞蹈）。不附带停车 —— 停车由调用方显式做。
void abortScript(UiState& ui);

/// 切换运动模式：normal / ai / mcf（走 `rt/api/motion_switcher/request`，api_id 1002）。
/// 成功与否以**机器人回执**为准，这里只保证指令发出去了；ui.motionMode 先乐观置位。
/// @return 成功下发台数
int setMotionMode(const std::shared_ptr<CommandSink>& sink, UiState& ui, const std::string& mode);

/// 播放一套舞蹈编排（core/dance.hpp）：按顺序下发每一步、按步的延时等待。
/// 同样是后台脚本：急停 / 再点一次播放 / 调用 abortScript 都会中止它。
/// 每一步动作走 dispatchAction（开关型会正确维护 toggles）。
/// @return false = 找不到该编排或没设备可用
bool playDance(const std::shared_ptr<CommandSink>& sink, UiState& ui, int danceId);

/// 急停（锁定式）的执行结果
struct EstopResult {
    int stopped = 0;  ///< StopMove 成功的台数
    int toggles = 0;  ///< 需要关闭的「持续模式」开关 id 个数
};

/// 急停（锁定式）。语义有单元测试保证（tests/command_service_test.cpp）：
///   1. 先把 `ui.estop` 置位（锁定；此后摇杆/快捷一律不下发）；
///   2. 对**全部就绪**设备发 StopMove（不限于勾选）—— 这是最安全的选择；
///   3. 取"我们真正打开过"的开关 id 快照，只关这些（避免把通道灌爆）；
///   4. 复位 UiState 里的开关状态与 movingSent。
///
/// @param asyncClose true = 第 3 步（会阻塞，每条 45ms）放到后台线程；
///                   界面线程必须传 true，HTTP/服务端可以传 false。
/// @note 用 shared_ptr 传 sink：后台线程要持有它，栈上的引用会悬垂。
EstopResult estop(const std::shared_ptr<CommandSink>& sink, UiState& ui, bool asyncClose);

}  // namespace cmd
}  // namespace go2
