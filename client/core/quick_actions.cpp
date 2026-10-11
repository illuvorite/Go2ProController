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

        // ---------------- 机身高度（参考实现放在「高级技能」）----------------
        {"BodyLowest",  "机身最低", QuickKind::BodyHeight,  SportGroup::Advanced, 0, 0, 0, -0.18f,
         "机身降到最低（机身高度 1013, data=-0.18）"},
        {"BodyNormal",  "机身正常", QuickKind::BodyHeight,  SportGroup::Advanced, 0, 0, 0,  0.0f,
         "机身回到正常高度（机身高度 1013, data=0）"},
        {"BodyHighest", "机身最高", QuickKind::BodyHeight,  SportGroup::Advanced, 0, 0, 0,  0.03f,
         "机身升到最高（机身高度 1013, data=+0.03）"},

        // ---------------- 高级技能 ----------------
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
