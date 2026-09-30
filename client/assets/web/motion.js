// 摇杆 → 速度：与 C++ 端 core/motion.hpp 的 planMotion() **同一套语义**（急停语义有 motion_test 单测）
// 左杆 = 平移（上推 = 前进、右推 = 右移）；右杆横向 = 原地转向（右推 = 右转）；右杆纵向不启用。
export function planMotion(lx, ly, rx, estop, maxLinSpeed, yawRate, movingSent) {
  const leftOn = Math.abs(lx) > 1e-3 || Math.abs(ly) > 1e-3
  const rightOn = Math.abs(rx) > 1e-3
  const active = leftOn || rightOn
  if (estop || !active) {
    return { send: false, stop: !!movingSent, active, vx: 0, vy: 0, vz: 0 }
  }
  return {
    send: true,
    stop: false,
    active: true,
    vx: -ly * maxLinSpeed,
    vy: -lx * maxLinSpeed,
    vz: -rx * yawRate,
  }
}
