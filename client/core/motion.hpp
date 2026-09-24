#pragma once

#include <cmath>

namespace go2 {

/// 本帧的运动决策（由摇杆位置 + 急停锁定推导；纯函数，便于单元自测）
struct MotionPlan {
    bool  send = false;      ///< 本帧是否下发 Move
    bool  stop = false;      ///< 本帧是否补发一次 StopMove（松手 / 急停）
    bool  active = false;    ///< 是否有摇杆离开中位
    float vx = 0.0f;         ///< 前后（m/s，正 = 前进）
    float vy = 0.0f;         ///< 左右（m/s，正 = 左移，与协议一致）
    float vz = 0.0f;         ///< 转向（rad/s，正 = 左转）
};

/// 双摇杆 + 急停的安全语义（自测见 tests/motion_test.cpp）：
///   - estop=true 时**绝不下发运动指令**，只可能补发一次停车；
///   - 两杆回中 → 若上一帧在运动，则本帧补发一次 StopMove（不重复刷）；
///   - 左杆 = 平移（上 = 前进、右 = 右移）；右杆横向 = 原地转向（右 = 右转），纵向暂不启用；
///   - 单杆回中只把该分量归零（另一杆仍可继续），两杆回中才整体停车。
inline MotionPlan planMotion(float lx, float ly, float rx, float ry, bool estop,
                             float maxLinSpeed, float yawRate, bool movingLastFrame) {
    MotionPlan p;
    (void)ry;  // 右杆纵向未启用（避免"看着有反应其实没指令"的困惑）
    const bool leftOn = std::fabs(lx) > 1e-3f || std::fabs(ly) > 1e-3f;
    const bool rightOn = std::fabs(rx) > 1e-3f;
    p.active = leftOn || rightOn;

    if (estop || !p.active) {
        p.stop = movingLastFrame;
        return p;
    }

    p.send = true;
    p.vx = -ly * maxLinSpeed;   // 屏幕上推 = 前进
    p.vy = -lx * maxLinSpeed;   // 右推 = 右移（协议 y 左为正）
    p.vz = -rx * yawRate;       // 右推 = 右转（协议 z 左转为正）
    return p;
}

}  // namespace go2
