// 双摇杆 + 急停的决策自测（纯逻辑，不需要机器狗、不需要图形环境）
//
// 覆盖用户提出的三条要求里最容易被"看起来能用其实没生效"骗过的部分：
//   1) 急停必须压得住摇杆（锁定期间绝不下发运动指令，且补一次停车）
//   2) 松手即停（两杆回中后补一次 StopMove，不重复刷屏）
//   3) 双摇杆可同时操作、各轴独立回中（单杆回中只归零该分量）
//
// 构建运行：
//   cmake --build build --target motion_test && ./build/motion_test

#include "motion.hpp"

#include <cmath>
#include <cstdio>
#include <string>

namespace {

int g_fail = 0;

void check(const char* name, bool cond) {
    std::printf("%s  %s\n", cond ? "PASS" : "FAIL", name);
    if (!cond) ++g_fail;
}

bool near0(float v) { return std::fabs(v) < 1e-6f; }

constexpr float kLin = 0.60f;   // 线速度上限
constexpr float kYaw = 1.20f;   // 角速度上限

}  // namespace

int main() {
    using go2::planMotion;

    std::printf("=== 双摇杆 / 急停 决策自测 ===\n\n");

    // ---- 1. 急停锁定：任何摇杆位置都不得下发运动 ----
    {
        bool allSilent = true, allNoMove = true;
        const float vals[] = {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f};
        for (float lx : vals)
            for (float ly : vals)
                for (float rx : vals) {
                    const auto p = planMotion(lx, ly, rx, 0.0f, /*estop=*/true, kLin, kYaw,
                                              /*movingLastFrame=*/true);
                    if (p.send) allSilent = false;
                    if (!near0(p.vx) || !near0(p.vy) || !near0(p.vz)) allNoMove = false;
                }
        check("急停：任何杆位都不下发运动指令", allSilent);
        check("急停：速度分量恒为 0", allNoMove);
    }
    {
        const auto p = planMotion(0.5f, -1.0f, 1.0f, 0.0f, true, kLin, kYaw, false);
        check("急停：上一帧没动就不重复发停车", !p.send && !p.stop);
    }
    {
        const auto p = planMotion(0.5f, -1.0f, 0.0f, 0.0f, true, kLin, kYaw, true);
        check("急停：上一帧在动则补发一次停车", !p.send && p.stop);
    }

    // ---- 2. 松手即停 ----
    {
        const auto p = planMotion(0.0f, 0.0f, 0.0f, 0.0f, false, kLin, kYaw, true);
        check("松手（双杆回中）：补发一次停车", !p.send && p.stop && !p.active);
    }
    {
        const auto p = planMotion(0.0f, 0.0f, 0.0f, 0.0f, false, kLin, kYaw, false);
        check("静止状态：不反复发停车", !p.send && !p.stop);
    }

    // ---- 3. 左杆 = 平移 ----
    {
        const auto p = planMotion(0.0f, -1.0f, 0.0f, 0.0f, false, kLin, kYaw, false);
        check("左杆推满向前：vx=+上限，其余为 0",
              p.send && std::fabs(p.vx - kLin) < 1e-4f && near0(p.vy) && near0(p.vz));
    }
    {
        const auto p = planMotion(-1.0f, 0.0f, 0.0f, 0.0f, false, kLin, kYaw, false);
        check("左杆推满向左：vy=+上限（协议 y 左为正），其余为 0",
              p.send && std::fabs(p.vy - kLin) < 1e-4f && near0(p.vx) && near0(p.vz));
    }
    {
        const auto p = planMotion(1.0f, 0.0f, 0.0f, 0.0f, false, kLin, kYaw, false);
        check("左杆推满向右：vy=-上限", p.send && std::fabs(p.vy + kLin) < 1e-4f);
    }
    {
        const auto p = planMotion(0.0f, 1.0f, 0.0f, 0.0f, false, kLin, kYaw, false);
        check("左杆推满向后：vx=-上限", p.send && std::fabs(p.vx + kLin) < 1e-4f);
    }

    // ---- 4. 右杆 = 原地转向 ----
    {
        const auto p = planMotion(0.0f, 0.0f, -1.0f, 0.0f, false, kLin, kYaw, false);
        check("右杆推满向左：vz=+上限（左转），不产生平移",
              p.send && std::fabs(p.vz - kYaw) < 1e-4f && near0(p.vx) && near0(p.vy));
    }
    {
        const auto p = planMotion(0.0f, 0.0f, 1.0f, 0.0f, false, kLin, kYaw, false);
        check("右杆推满向右：vz=-上限（右转）", p.send && std::fabs(p.vz + kYaw) < 1e-4f);
    }
    {
        const auto p = planMotion(0.0f, 0.0f, 0.0f, 1.0f, false, kLin, kYaw, false);
        check("右杆纵向：暂不启用（不误产生运动）", !p.send && !p.active);
    }

    // ---- 5. 双杆同时操作：平移 + 转向叠加 ----
    {
        const auto p = planMotion(-1.0f, -1.0f, 1.0f, 0.0f, false, kLin, kYaw, false);
        check("双杆同时：边走边转，三个分量同时生效",
              p.send && std::fabs(p.vx - kLin) < 1e-4f && std::fabs(p.vy - kLin) < 1e-4f &&
                  std::fabs(p.vz + kYaw) < 1e-4f);
    }

    // ---- 6. 单杆回中：只归零该分量，另一杆继续有效 ----
    {
        const auto p = planMotion(0.0f, 0.0f, 1.0f, 0.0f, false, kLin, kYaw, false);
        check("左杆回中、右杆仍按住：只转向不前进",
              p.send && near0(p.vx) && near0(p.vy) && std::fabs(p.vz + kYaw) < 1e-4f);
    }
    {
        const auto p = planMotion(0.0f, -1.0f, 0.0f, 0.0f, false, kLin, kYaw, false);
        check("右杆回中、左杆仍按住：只前进不转向",
              p.send && std::fabs(p.vx - kLin) < 1e-4f && near0(p.vz));
    }

    // ---- 7. 上限可调 ----
    {
        const auto p = planMotion(0.0f, -1.0f, 0.0f, 0.0f, false, 0.25f, 0.8f, false);
        check("速度上限受滑条控制（0.25 m/s）", std::fabs(p.vx - 0.25f) < 1e-4f);
    }

    std::printf("\n%s（失败 %d 项）\n", g_fail == 0 ? "全部通过 ✔" : "存在失败 ✘", g_fail);
    return g_fail == 0 ? 0 : 1;
}
