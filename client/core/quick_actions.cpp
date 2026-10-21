#include "quick_actions.hpp"

namespace go2 {

const std::vector<QuickAction>& quickActions() {
    static const std::vector<QuickAction> kQuick = {
        // ---------------- 姿态微调（排进「基础姿态」组）----------------
        // ★ 角度值逐条对齐参考实现：左/右扭 0.5、左/右倾 0.75、低/抬头 0.5。
        // ★ 它们本质上是「姿态角(1007) + 一个固定参数」—— 但动作表里那条「姿态角」
        //   在界面上被撤下了（整组「步态/速度/身高」），所以这几个预设是**唯一能点到**
        //   姿态角的入口；不是"和原来重复"，是"把够不到的重新露出来"。
        {"TwistLeft",   "左扭",     QuickKind::EulerPreset, SportGroup::Basic,  0.0f,   0.0f,  0.5f, 0.0f,
         "先平衡站立，再向左扭转（姿态角 z=+0.5）"},
        {"TwistRight",  "右扭",     QuickKind::EulerPreset, SportGroup::Basic,  0.0f,   0.0f, -0.5f, 0.0f,
         "先平衡站立，再向右扭转（姿态角 z=-0.5）"},
        {"LeanLeft",    "左倾",     QuickKind::EulerPreset, SportGroup::Basic, -0.75f,  0.0f,  0.0f, 0.0f,
         "先平衡站立，再向左倾斜（姿态角 x=-0.75）"},
        {"LeanRight",   "右倾",     QuickKind::EulerPreset, SportGroup::Basic,  0.75f,  0.0f,  0.0f, 0.0f,
         "先平衡站立，再向右倾斜（姿态角 x=+0.75）"},
        {"TiltDown",    "低头",     QuickKind::EulerPreset, SportGroup::Basic,  0.0f,   0.5f,  0.0f, 0.0f,
         "先平衡站立，再低头（姿态角 y=+0.5）"},
        {"TiltUp",      "抬头",     QuickKind::EulerPreset, SportGroup::Basic,  0.0f,  -0.5f,  0.0f, 0.0f,
         "先平衡站立，再抬头（姿态角 y=-0.5）"},
        {"ShiftLeft",   "左移",     QuickKind::MoveBurst,   SportGroup::Basic,  0.0f,   0.4f,  0.0f, 0.0f,
         "按遥控节奏连发 1 秒移动指令，向左平移一小步后自动停下"},
        {"ShiftRight",  "右移",     QuickKind::MoveBurst,   SportGroup::Basic,  0.0f,  -0.4f,  0.0f, 0.0f,
         "按遥控节奏连发 1 秒移动指令，向右平移一小步后自动停下"},

        // ---------------- 高级技能 ----------------
        // ★ 2026-10-20 删除「机身最低 / 机身正常 / 机身最高」三条：
        //   它们发的是 BodyHeight(1013)，而 **1013 是 normal 专属 id** ——
        //   固件 ≥1.1.7 的 MCF 模式不受理（实测 code=3203），按下去只会报错。
        //   机身高度在固件里由 `kApiBodyHeight` 常量保留（回执日志仍要能翻出名字），
        //   但**不再有按钮**；要改机身高度得走别的途径。
        {"AdjustYaw",   "方向回正", QuickKind::AdjustOrientation, SportGroup::Advanced, 0, 0, 0, 0,
         "开连续步态 → 平衡站立 → 按姿态角的偏航闭环回转 → 停车 → 关连续步态（最多 30 秒）"},
        {"StepInPlace", "原地踏步", QuickKind::InPlaceStepping,   SportGroup::Advanced, 0, 0, 0, 0,
         "开连续步态 → 平衡站立 → 前后来回踏步约 5 秒 → 停车 → 关连续步态"},
        // ⚠ 这里**不再有**「恢复移动」：它就是 StopMove(1003)，
        //   动作库「基础姿态」里已经有「停止移动」这一条（2026-10-11 用户指出后删除）。
    };
    return kQuick;
}

const QuickAction* findQuickAction(const std::string& key) {
    for (const auto& q : quickActions())
        if (key == q.key) return &q;
    return nullptr;
}

}  // namespace go2
