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
int moveSelected(CommandSink& sink, const UiState& ui, float x, float y, float z);

/// 松手 / 急停停车：对受控设备发 StopMove（任何状态下都允许）
int stopSelected(CommandSink& sink);

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

/// 强制阻尼：对**全部就绪**设备（不限于勾选）发 Damp
int dampAll(CommandSink& sink);

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
