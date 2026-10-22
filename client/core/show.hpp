#pragma once

// ============================================================================
// 「演出」数据模型 —— 多机编队表演（对标智身科技 SomaFlow 多机舞蹈表演系统）。
//
// ============================================================================
// 为什么单独一个模型（而不是把「舞蹈编排」dance.hpp 改大）：
//
//   core/dance.hpp 是**单机**语义 —— 一串 {动作, 延时} 按顺序播。它复刻的是参考实现
//   「天树探界遥控」的编排页，那边也只有单机。
//
//   SomaFlow 要的是**多机编队**：动作同步 + 空间移动 + 动态编队 + 灯光联动。
//   这四件事都需要 dance.hpp 里根本不存在的数据：
//     · **时间轴**：不是「发完等多久」，而是「第几拍做���么」—— 绝对时间轴是同步的前提。
//     · **角色**：同一条 cue 可能只给队里的两台狗（领队 / 跟队），不能只有「全体」。
//     · **舞台位**：狗站在哪（x/y/朝向）、编队之间怎么换位。
//     · **灯光**：与动作时间轴对齐的灯光 cue。
//
//   所以这是**新模型**，不是旧模型的扩展 —— 旧的 dance 语义保留不动（它另有用途：
//   单机动作串 + 参考实现对齐），两者并存。
//
// ============================================================================
// 关于「空间移动」的一个硬约束（必须先讲清楚，否则会做出假的东西）：
//
//   Go2 的 sport 接口**没有绝对定位**。本仓库解析的状态话题
//   （core/telemetry.hpp：lowstate / sportmodestate）里只有电量、温度、姿态角、
//   偏航角速度、运动模式、进度 —— **没有 x/y 里程计**。
//
//   所以这里**不做闭环导航**，做的是**开环位移**（dead reckoning）：
//     · 编导在可视化界面里指定「这只狗从 A 位走到 B 位」，系统算出
//       「前进几米 / 左移几米 / 原地转几度」，按开环速度指令走完；
//     · 舞台坐标图（StageSlot 的 x/y/yaw）用来**画**队形、**校验**位移量是否合理
//       （例如「从 A 到 B 要走 2.3 米，但你只写了 1.0 米」→ 静态报错），
//       而不是用来实时闭环纠偏。
//
//   这不是妥协，是这类舞台表演的通行做法：几十台机器狗在开阔场地做队形变换，
//   靠的是**事先标定 + 开环执行**，而不是每台都装 SLAM。
//   想要闭环，得换成带里程计的机型（如 Unitree Go2 Pro 的网口 DDS 通道）——
//   那是另一件事，不在本次范围内。
//
// ============================================================================
// 同步的实现口径（为什么"绝对时间轴"是同步的关键）：
//
//   现有 playDance 的节奏是「发一条 → sleep(延时) → 发下一条」。这有两个问题：
//     1. **误差累积**：sleep 的实际时长 ≥ 请求时长（调度抖动 + 发送耗时），
//        N 步之后整体比音乐慢一大截，而且越演越偏。
//     2. **多机不同步**：forEachSelected 是**串行**的 —— 第 N 台狗比第 1 台晚
//        N×(单次发送耗时) 才收到指令。3 台狗、每步 30ms 发送耗时，就是 90ms 的错位。
//
//   这里改成：每条 cue 的触发时刻 = `t0 + beat × 一拍时长`（**绝对时间**），
//   调度线程只负责「等到点了就发」，不靠累加 sleep 来对拍。
//   同一条 cue 的多台狗**并行下发**（每台一个线程），错位就只剩网络抖动本身
//   （同网段实测 10~40ms），不再叠加发送耗时。
//
//   ★ 同步必须**可测量**：ShowSyncMetrics 记录每条 cue 各狗的发出时刻，算出
//     最大错位 / 平均错位 / 迟到条数。没有这个数字，"同步"就只是宣传语。
// ============================================================================

#include <nlohmann/json.hpp>

#include <map>
#include <string>
#include <vector>

namespace go2 {

// ---------------------------------------------------------------- 舞台位

/// 舞台上的一个「站位」。坐标单位是米，原点在舞台中心。
///
/// 坐标系约定：**x 向右（东），y 向前（北）**，yaw 为**逆时针为正**、0 表示面朝 +y。
/// 换算到机器人自身坐标系（协议里 x=前进、y=左移、z=左转）见 `slotTransfer()`。
///
/// ★ 朝向换算是整套编队算术的地基，方向搞反了整场队形就是镜像的：
///   θ=0   → 朝向 (0, 1) = 北，左方 (−1, 0) = 西；
///   θ=+90 → 朝向 (−1, 0) = 西（= 左转 90°），左方 (0, −1) = 南。
struct StageSlot {
    std::string id;    ///< 位号（同一编队内唯一）
    std::string name;  ///< 显示名（"左前" / "C 位"…）
    double x = 0.0;
    double y = 0.0;
    double yawDeg = 0.0;
};

// ---------------------------------------------------------------- 编队

/// 一套编队：若干舞台位 + 「哪台狗站哪个位」的指派。
///
/// 指派用 **robotKey**（`sn:<SN>` 或 `ip:<IP>`，见 ui.hpp 的 deviceKeyFor）而不是裸 IP：
/// IP 会随 WiFi/DHCP 变，编队是**演出资产**，不能因为换网络就散架。
struct Formation {
    int id = 0;
    std::string name;
    std::vector<StageSlot> slots;
    /// robotKey -> slotId
    std::map<std::string, std::string> assign;
};

// ---------------------------------------------------------------- 开环位移

/// 一段「怎么走过去」的开环位移。
///
/// 执行分**两段**（顺序执行，不并行）：
///   ① 平移段：按 `forwardM` / `leftM` 走 `speedMs` 的速度；
///   ② 转向段：原地转 `turnDeg`，角速度 `turnRateDegS`。
///
/// ★ 为什么平移与转向要分开而不是同时给：
///   四足机器人**不能一边横移一边原地转** —— 那等于让它横着走同时自旋，
///   四条腿的落点会打架，姿态闭环会拉胯，表演时大概率摔。
///   舞台上的真实做法也是「先挪到位，再转身面向观众」。
struct MoveSpec {
    double forwardM = 0.0;     ///< 前进（+ 向前）
    double leftM = 0.0;        ///< 左移（+ 向左）
    double turnDeg = 0.0;      ///< 原地左转（+ 左转）
    double speedMs = 0.35;     ///< 平移速度 (m/s)
    double turnRateDegS = 90.0;///< 转向角速度 (deg/s)
};

// ---------------------------------------------------------------- 时间轴 cue

enum class CueKind {
    Action,  ///< 做一个动作
    Move,    ///< 走一段（编队换位 / 入场 / 退场）
    Light,   ///< 灯光联动
};

/// 时间轴上的一条 cue。
struct ShowCue {
    int beat = 0;              ///< 落在第几拍（从 0 开始）
    CueKind kind = CueKind::Action;
    std::string actionKey;     ///< Action：动作 key（sport_library 的 key）
    MoveSpec move;             ///< Move：位移
    std::string lightId;       ///< Light：灯号
    /// 参与的角色（robotKey 列表）。**空 = 全体**。
    ///
    /// 用 robotKey 而不是 IP，理由同 Formation::assign。
    std::vector<std::string> roles;
};

// ---------------------------------------------------------------- 演出

/// 一场演出。
struct Show {
    int id = 0;
    std::string name;
    std::string desc;
    int bpm = 120;             ///< 节拍（拍/分钟）
    int beats = 32;            ///< 总拍数
    std::vector<ShowCue> cues;
    std::vector<Formation> formations;  ///< 本场用到的编队（换位 cue 的 A/B 端）
};

// ---------------------------------------------------------------- 纯计算

/// 一拍多少毫秒。bpm ≤ 0 时按 120 兜底（除零会得到无穷大）。
int beatMs(int bpm);

/// 演出总时长（秒）：总拍数 × 一拍。
double showDurationSec(const Show& s);

/// cue 的绝对开始时刻（秒，从演出起点算）。
double cueStartSec(const Show& s, const ShowCue& c);

/// cue 的持续时长（秒）。
///
/// · Action：取该动作的默认延时（core/dance.hpp 的 defaultDanceDelayMs）——
///   与舞蹈编排同一个口径，两处对不齐会让"动作还没做完下一个就来了"。
/// · Move：平移段 + 转向段（见 moveDurationSec）。
/// · Light：0（灯光是瞬时事件）。
double cueDurationSec(const Show& s, const ShowCue& c);

/// MoveSpec 的总时长（秒）：平移段 + 转向段。
double moveDurationSec(const MoveSpec& m);

/// MoveSpec 的平移段时长（秒）。距离为 0 时返回 0。
double moveTranslateSec(const MoveSpec& m);

/// MoveSpec 的转向段时长（秒）。角度为 0 时返回 0。
double moveTurnSec(const MoveSpec& m);

/// 某个相位（0 = 平移段，1 = 转向段）对应的速度指令。
///
/// 协议口径（core/motion.hpp 与 Unitree 一致）：**x = 前进、y = 左移、z = 左转**，
/// 三者都是**正 = 那个方向**。相位 1 时 vx/vy 归零，只给 vz。
void moveVelocity(const MoveSpec& m, int phase, float* vx, float* vy, float* vz);

/// 从 A 编队换到 B 编队时，某台狗的开环位移。
///
/// @param robotKey 参与演出的狗（用 sn:/ip: 前缀的设备键）
/// @param out      算出来的位移
/// @return false = 这台狗在 A 或 B 里没有指派（或两个编队里都没有它）
///
/// 计算过程：
///   1. 取它在 A、B 里的舞台位；
///   2. 把舞台位移向量转到它**当前朝向**的自身坐标系 → 得到前进量 / 左移量；
///   3. 转向量 = B 位朝向 − A 位朝向（归一化到 (−180, 180]）。
bool slotTransfer(const Formation& from, const Formation& to, const std::string& robotKey,
                  MoveSpec* out);

/// 把角度归一化到 (−180, 180]。
double normalizeDeg(double deg);

/// 校验一条 cue / 一场演出，返回问题清单（空 = 没问题）。
///
/// 校验是**静态**的（不连机器狗）：主要抓
///   · 动作 key 不存在 / 是高危动作（空翻连着甩会摔）；
///   · cue 落在总拍数之外；
///   · 位移量与舞台位对不上（编队换位写了 1 米、实际要 2.3 米）。
/// 这些错在**开演前**必须拦住 —— 演出中才发现就只能停下来重来。
std::vector<std::string> validateShow(const Show& s);

// ---------------------------------------------------------------- JSON

nlohmann::json showToJson(const Show& s);
Show showFromJson(const nlohmann::json& j);

nlohmann::json formationToJson(const Formation& f);
Formation formationFromJson(const nlohmann::json& j);

}  // namespace go2
