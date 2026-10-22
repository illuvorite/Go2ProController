#pragma once

// ============================================================================
// 演出引擎 —— 多机编队表演的运行时（对标智身科技 SomaFlow 多机舞蹈表演系统）。
//
// ============================================================================
// 它解决三件事，现有「舞蹈编排」都做不到：
//
//   ① **绝对时间轴**：每条 cue 的触发时刻 = t0 + beat × 一拍时长。
//      调度线程「等到点了就发」，不靠累加 sleep 对拍 → 误差不累积。
//      （现有 playDance 是「发一条 → sleep → 发下一条」，N 步之后整体越拖越偏。）
//
//   ② **并行群发**：同一条 cue 的多台狗**同时**下发（每台一个线程）。
//      现有 forEachSelected 是串行的 —— 第 N 台比第 1 台晚 N×发送耗时。
//      并行之后错位只剩网络抖动本身（同网段实测 10~40ms）。
//
//   ③ **同步可测量**：ShowSyncMetrics 记录每条 cue 各狗的发出时刻，算出
//      最大错位 / 平均错位 / 迟到条数。**没有这个数字，"同步"就只是宣传语。**
//
// ============================================================================
// 与既有脚本机制的关系（为什么不另起一套中止逻辑）：
//
//   引擎复用 UiState 的**代际号**（scriptGen）作为自己的身份：
//     · `cmd::estop` / `cmd::abortScript` 自增代际号 → 引擎立刻察觉并收尾；
//     · 引擎自己也是「当前脚本」之一 → 界面顶栏显示"演出：xxx"、
//       网页端的"正在播放"状态都自动成立。
//   所以**不存在"演出绕过急停"这种可能**：急停是唯一入口，两边共用同一个代际号。
//
// ============================================================================
// 关于灯光联动：
//
//   本仓库**没有任何灯光硬件驱动**（没有 DMX / Art-Net / 灯光台协议的实现）。
//   所以这里做的是**联动链路**而不是驱动：
//     · LightSink 是窄接口（fireCue(灯号, 参与角色)）；
//     · 默认实现 LoggingLightSink 只记日志 + 把最近事件留给网页端显示；
//     · 要接真灯光时，实现一个自己的 LightSink 塞进去即可（DMX / OSC / MQTT…）。
//
//   这一点必须说清楚：**接上 LightSink ≠ 灯会亮**。目前能保证的是
//   "时间轴上的灯光 cue 在该亮的时刻被准时触发"，下游怎么用由接入方决定。
// ============================================================================

#include "show.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace go2 {

class CommandSink;
class RobotManager;
struct UiState;

/// 灯光联动的出口（窄接口，便于接真灯光 / 记日志 / 转发到外部系统）
class LightSink {
public:
    virtual ~LightSink() = default;

    /// 触发一条灯光 cue。
    /// @param lightId 灯号（演出里编导起的名字，如 "warm" / "spot-3"）
    /// @param roles   参与的角色（robotKey 列表；空 = 全体）
    virtual void fireCue(const std::string& lightId, const std::vector<std::string>& roles) = 0;
};

/// 默认实现：只记日志，并保留最近若干条事件（网页端可以拉出来显示"刚才亮了哪盏"）
class LoggingLightSink final : public LightSink {
public:
    explicit LoggingLightSink(UiState* ui = nullptr) : ui_(ui) {}

    void fireCue(const std::string& lightId, const std::vector<std::string>& roles) override;

    struct Event {
        std::string lightId;
        std::vector<std::string> roles;
        long long atMs = 0;  ///< 相对演出起点的毫秒
    };

    /// 最近的事件（按时间倒序，最多 64 条）
    std::vector<Event> history() const;

private:
    mutable std::mutex m_;
    std::vector<Event> events_;
    UiState* ui_ = nullptr;
};

/// 同步度量 —— 「同步」必须可测，否则只是宣传语。
struct ShowSyncMetrics {
    int cuesTotal = 0;       ///< 时间轴上的 cue 总数
    int cuesFired = 0;       ///< 实际触发过的 cue 数
    int robots = 0;          ///< 参与演出的机器狗台数
    int lateCues = 0;        ///< 触发时已经过点的 cue 数（调度被拖慢了）
    double maxSkewMs = 0.0;  ///< 同一条 cue 内，各狗发出时刻的最大差
    double avgSkewMs = 0.0;  ///< 各 cue 错位的平均
    double maxLatenessMs = 0.0;  ///< 最严重的迟到量
};

/// 演出引擎。
///
/// 用法：
///   ShowEngine eng(sink, ui);
///   eng.start(show, {"192.168.2.111", "192.168.2.112"});
///   ...
///   eng.stop();   // 或由急停 / abortScript 触发
///
/// 线程安全：公开方法内部加锁。内部线程（调度 + 位移 worker + 并行下发）
/// 在 stop() / 析构时全部收敛 —— **绝不把 join 放在持锁状态下**。
class ShowEngine {
public:
    /// @param sink 发指令的出口。用 shared_ptr 是因为引擎会被全局单例持有，
    ///              栈上的引用会悬垂。
    ShowEngine(std::shared_ptr<CommandSink> sink, UiState& ui);
    ~ShowEngine();

    ShowEngine(const ShowEngine&) = delete;
    ShowEngine& operator=(const ShowEngine&) = delete;

    /// 换掉灯光出口（默认是 LoggingLightSink）
    void setLightSink(std::shared_ptr<LightSink> s);

    /// 开始一场演出。
    /// @param ips 参与的机器狗 IP 列表（空 = 用当前「受控且就绪」的集合）
    /// @return false = 没有可用设备 / 急停锁定中 / 时间轴是空的 / 上一场还在跑
    bool start(const Show& show, const std::vector<std::string>& ips);

    /// 中止演出：叫醒所有 worker、给参与的车停车、关掉开过的持续模式开关。
    /// 幂等：没在跑时直接返回。
    void stop();

    bool running() const;

    ShowSyncMetrics metrics() const;

    /// 最近一次出错的说明（空 = 没有）
    std::string lastError() const;

    /// 当前演出的名字（没在跑时为空）
    std::string currentShowName() const;

private:
    /// 演出起点之前的预备时间（毫秒）：给操作员一个"准备"窗口。
    static constexpr int kStartLeadMs = 400;
    /// 位移 worker 的重发周期（毫秒）。Go2 的速度是**保持型**的，
    /// 必须持续重发，否则一次性的速度指令很快会被判为失效。
    static constexpr int kMoveTickMs = 100;
    /// 判定"迟到"的容差（毫秒）。调度线程每 25ms 醒一次，正常抖动在 ±25ms 内。
    static constexpr int kLateToleranceMs = 60;

    struct Target {
        std::string ip;
    };

    /// 一条 cue 的目标集合（受控 ∩ 就绪 ∩ 角色）
    std::vector<std::string> resolveTargets(const ShowCue& cue, const std::vector<std::string>& ips);

    /// 并行下发同一条指令到多台狗；返回各狗的发出时刻（相对 epoch 纳秒）。
    std::vector<std::pair<std::string, long long>> parallelSend(const std::vector<std::string>& ips,
                                                                 int apiId,
                                                                 const nlohmann::json& param);

    /// 触发一条 cue
    void fireCue(const ShowCue& cue, const std::vector<std::string>& ips,
                 const std::chrono::steady_clock::time_point& when);

    /// 起一段位移（每个目标一台一个 worker 线程）
    void startMove(const ShowCue& cue, const std::vector<std::string>& ips,
                   const std::chrono::steady_clock::time_point& when);

    /// 调度线程主体
    void runShow(const Show& show, const std::vector<std::string>& ips, unsigned gen);

    /// 等到某个时刻（分片睡，急停/中止能及时打断）。返回 false = 被中止。
    bool waitUntil(const std::chrono::steady_clock::time_point& when, unsigned gen);

    /// 收干净所有内部线程（调度 + 位移 worker）。
    /// ★ 必须在**开新一场之前**也调一次：上一场演完时调度线程自己退出了、
    ///   但没被 join 过 → 之后给 worker_ 赋值会 std::terminate。
    void joinWorkers();

    /// 是否还该继续（代际号没变 + 没急停 + 没被要求停）
    bool alive(unsigned gen) const;

    /// 给参与的车补一条 StopMove（收尾用）
    void stopAll(const std::vector<std::string>& ips);

    std::shared_ptr<CommandSink> sink_;
    UiState& ui_;
    std::shared_ptr<LightSink> light_;

    std::thread worker_;                 ///< 调度线程
    std::vector<std::thread> movers_;    ///< 位移 worker（收尾时 join）
    mutable std::mutex moversMutex_;

    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> running_{false};
    std::atomic<unsigned> gen_{0};       ///< 引擎自己的代际号（内部 worker 用）

    mutable std::mutex metricsMutex_;
    ShowSyncMetrics metrics_;
    int skewSamples_ = 0;  ///< 真正量到错位的 cue 数（平均错位按它算，不按 cuesFired）
    std::string lastError_;
    std::string showName_;
    std::chrono::steady_clock::time_point t0_{};  ///< 演出起点
    std::atomic<long long> t0EpochMs_{0};          ///< t0 的墙钟毫秒（算 cue 的绝对时刻用）
};

/// 进程内**唯一**的演出引擎（首次调用时创建）。
///
/// 为什么是全局单例：网页端与桌面端是**同一个进程里的两套界面**（安卓端是 WebView，
/// 桌面端是 ImGui + 可选浏览器）。如果各建各的引擎，就会出现"网页端开了演、
/// 桌面端点停止，停不掉"——两套状态各说各话。共用一个实例就没这个问题。
///
/// ★ 生命周期约定：RobotManager 与 UiState 必须是**进程级**对象（活到进程结束）。
///   本项目所有入口（apps/desktop、apps/serve、安卓 native）都满足这一点。
std::shared_ptr<ShowEngine> globalShowEngine(RobotManager& mgr, UiState& ui);

}  // namespace go2
