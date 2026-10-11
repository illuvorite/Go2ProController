#pragma once

// ============================================================================
// 「快捷动作」—— 不是单条 api_id 就能完成的按钮。
//
// 来源：参考实现「天树探界遥控」的 Go2 页面（`go2-robot-control.html`）。
// 那个 App 里有一批按钮点下去要**发一串指令**才算完成：
//   · 左扭 / 右扭 / 左倾 / 右倾 / 低头 / 抬头
//       → 先 BalanceStand(1002)，等 500ms，再发 Euler(1007) 带固定角度
//   · 左移 / 右移
//       → 按 150ms 的节奏连发 Move(1008) 共 1 秒，然后补一条 StopMove(1003)
//   · 机身最低 / 正常 / 最高
//       → BodyHeight(1013) 带固定 data
//   · 方向回正
//       → 开连续步态 → 平衡站立 → 用 Move 的 z 轴闭环转到 yaw≈0 → 停车 → 关连续步态
//   · 原地踏步
//       → 开连续步态 → 平衡站立 → 前后来回 Move 若干秒 → 停车 → 关连续步态
//   · 恢复移动
//       → 一条 StopMove(1003)
//
// 为什么不塞进 `sport_library` 的动作表：
//   那张表是「一条 api_id + 一种参数」的**纯指令**，界面按它自动生成控件；
//   而这里有前置动作、有节奏、有闭环，是一段**脚本**。
//   混在一起会让"参数输入控件"的逻辑变得不清不楚。
//
// ★ 但它们在界面上**不另起一条**：分组直接复用 `SportGroup`，
//   动作库页把它们和普通动作画在**同一个网格、同一个分组里**（见 ui_actions.cpp）。
//   参考实现「天树探界遥控」的 Go2 页也是这样 —— 它只有"基础动作 / 高级技能"两块网格，
//   这批按钮就在网格里，没有单独的区块。
//
// 本文件只放**数据**（表 + 分类）；执行逻辑在 ui/command_service.cpp 的
// `cmd::runQuickAction`，因为它要发指令、要计时、要能被急停打断。
// ============================================================================

#include "sport_library.hpp"  // SportGroup：分组复用动作库那一套

#include <string>
#include <vector>

namespace go2 {

/// 快捷动作的执行方式
enum class QuickKind {
    StopMove,           ///< 一条 StopMove(1003)（当前表里没有；保留给将来）
    EulerPreset,        ///< 先 BalanceStand，再 Euler(x,y,z)
    MoveBurst,          ///< 连发 Move(x,y,z)，结束后 StopMove
    BodyHeight,         ///< BodyHeight(1013)，data 见 data 字段
    AdjustOrientation,  ///< 方向回正（闭环）
    InPlaceStepping,    ///< 原地踏步
};

/// 一条快捷动作。
/// ★ `group` 用的是动作库的 SportGroup —— 界面按它把快捷动作**混排进同一个网格**。
///
/// ⚠ 不设「恢复移动」：它就是 `StopMove(1003)`，动作库里已经有「停止移动」这一条，
///   再摆一个按钮等于同一个功能两个入口（2026-10-11 用户指出后删除）。
struct QuickAction {
    const char* key;    ///< 英文标识（界面 ID / 网页 key）
    const char* label;  ///< 中文显示名
    QuickKind kind;
    SportGroup group;   ///< 排进动作库的哪一组
    float x = 0.0f;     ///< EulerPreset 的 roll / MoveBurst 的 vx
    float y = 0.0f;     ///< EulerPreset 的 pitch / MoveBurst 的 vy
    float z = 0.0f;     ///< EulerPreset 的 yaw / MoveBurst 的 vz
    float data = 0.0f;  ///< BodyHeight 的 data
    const char* hint;   ///< 悬停说明（界面用）
};

/// 全部快捷动作
const std::vector<QuickAction>& quickActions();

/// 按 key 查（找不到返回 nullptr）
const QuickAction* findQuickAction(const std::string& key);

// ---- 脚本参数（与天树探界逐条对齐，改这里即可调行为）----

constexpr int kQuickBalanceDelayMs = 500;   ///< Euler 预设：平衡站立后的等待
constexpr int kMoveBurstMs = 1000;          ///< 左移/右移：连发总时长
constexpr int kMoveBurstIntervalMs = 150;   ///< 左移/右移：连发间隔（遥控节奏）

constexpr int kOrientThresholdMilli = 50;   ///< 方向回正：|yaw| 阈值 0.05 rad（千分之一弧度）
constexpr float kOrientMaxAngularVel = 0.5f;///< 方向回正：最大角速度 rad/s
constexpr int kOrientIntervalMs = 100;      ///< 方向回正：检查间隔
constexpr int kOrientStabilizeMs = 1000;    ///< 方向回正：稳身等待
constexpr int kOrientTimeoutMs = 30000;     ///< 方向回正：最长调整时间
constexpr int kGaitSettleMs = 1000;         ///< 切连续步态后的生效等待

constexpr int kStepDurationMs = 5000;       ///< 原地踏步：总时长（与参考实现一致）
constexpr int kStepIntervalMs = 150;        ///< 原地踏步：连发间隔
constexpr float kStepSpeed = 0.15f;         ///< 原地踏步：前后速度

}  // namespace go2
