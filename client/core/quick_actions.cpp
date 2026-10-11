#include "quick_actions.hpp"

namespace go2 {

const std::vector<QuickAction>& quickActions() {
    static const std::vector<QuickAction> kQuick = {
        // ---------------- 姿态微调（天树探界「基础动作」里的那排） ----------------
        // ★ 角度值逐条对齐参考实现：左/右扭 0.5、左/右倾 0.75、低/抬头 0.5。
        {"TwistLeft",   "左扭",     QuickKind::EulerPreset, QuickGroup::Pose,  0.0f,   0.0f,  0.5f, 0.0f,
         "先平衡站立，再向左扭转（Euler z=+0.5）"},
        {"TwistRight",  "右扭",     QuickKind::EulerPreset, QuickGroup::Pose,  0.0f,   0.0f, -0.5f, 0.0f,
         "先平衡站立，再向右扭转（Euler z=-0.5）"},
        {"LeanLeft",    "左倾",     QuickKind::EulerPreset, QuickGroup::Pose, -0.75f,  0.0f,  0.0f, 0.0f,
         "先平衡站立，再向左倾斜（Euler x=-0.75）"},
        {"LeanRight",   "右倾",     QuickKind::EulerPreset, QuickGroup::Pose,  0.75f,  0.0f,  0.0f, 0.0f,
         "先平衡站立，再向右倾斜（Euler x=+0.75）"},
        {"TiltDown",    "低头",     QuickKind::EulerPreset, QuickGroup::Pose,  0.0f,   0.5f,  0.0f, 0.0f,
         "先平衡站立，再低头（Euler y=+0.5）"},
        {"TiltUp",      "抬头",     QuickKind::EulerPreset, QuickGroup::Pose,  0.0f,  -0.5f,  0.0f, 0.0f,
         "先平衡站立，再抬头（Euler y=-0.5）"},
        {"ShiftLeft",   "左移",     QuickKind::MoveBurst,   QuickGroup::Pose,  0.0f,   0.4f,  0.0f, 0.0f,
         "按遥控节奏连发 1 秒 Move，向左平移一小步后自动停下"},
        {"ShiftRight",  "右移",     QuickKind::MoveBurst,   QuickGroup::Pose,  0.0f,  -0.4f,  0.0f, 0.0f,
         "按遥控节奏连发 1 秒 Move，向右平移一小步后自动停下"},

        // ---------------- 机身高度 ----------------
        {"BodyLowest",  "机身最低", QuickKind::BodyHeight,  QuickGroup::Body,  0.0f, 0.0f, 0.0f, -0.18f,
         "机身降到最低（BodyHeight 1013, data=-0.18）"},
        {"BodyNormal",  "机身正常", QuickKind::BodyHeight,  QuickGroup::Body,  0.0f, 0.0f, 0.0f,  0.0f,
         "机身回到正常高度（BodyHeight 1013, data=0）"},
        {"BodyHighest", "机身最高", QuickKind::BodyHeight,  QuickGroup::Body,  0.0f, 0.0f, 0.0f,  0.03f,
         "机身升到最高（BodyHeight 1013, data=+0.03）"},

        // ---------------- 高级技能 ----------------
        {"AdjustYaw",   "方向回正", QuickKind::AdjustOrientation, QuickGroup::Skill, 0, 0, 0, 0,
         "开连续步态 → 平衡站立 → 按偏航角闭环回转 → 停车 → 关连续步态（最多 30 秒）"},
        {"StepInPlace", "原地踏步", QuickKind::InPlaceStepping,   QuickGroup::Skill, 0, 0, 0, 0,
         "开连续步态 → 平衡站立 → 前后来回踏步约 5 秒 → 停车 → 关连续步态"},
        {"ResumeMove",  "恢复移动", QuickKind::StopMove,          QuickGroup::Skill, 0, 0, 0, 0,
         "发一条 StopMove(1003)：清掉当前速度指令，让狗停下来接受新的动作"},
    };
    return kQuick;
}

const QuickAction* findQuickAction(const std::string& key) {
    for (const auto& q : quickActions())
        if (key == q.key) return &q;
    return nullptr;
}

const char* quickGroupName(QuickGroup g) {
    switch (g) {
        case QuickGroup::Pose:  return "姿态微调";
        case QuickGroup::Body:  return "机身高度";
        case QuickGroup::Skill: return "高级技能";
    }
    return "快捷动作";
}

}  // namespace go2
