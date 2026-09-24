#include "sport_library.hpp"

#include <algorithm>

namespace go2 {

const std::vector<SportAction>& sportActions() {
    static const std::vector<SportAction> kActions = {
        // ---------------- 基础姿态 ----------------
        {"Damp",           "阻尼",         1001, 1001, SportParam::None, SportGroup::Basic, false},
        {"BalanceStand",   "平衡站立",     1002, 1002, SportParam::None, SportGroup::Basic, false},
        {"StopMove",       "停止移动",     1003, 1003, SportParam::None, SportGroup::Basic, false},
        {"StandUp",        "站立",         1004, 1004, SportParam::None, SportGroup::Basic, false},
        {"StandDown",      "趴下",         1005, 1005, SportParam::None, SportGroup::Basic, false},
        {"RecoveryStand",  "恢复站立",     1006, 1006, SportParam::None, SportGroup::Basic, false},
        {"Sit",            "坐下",         1009, 1009, SportParam::None, SportGroup::Basic, false},
        {"RiseSit",        "起立(坐姿)",   1010, 1010, SportParam::None, SportGroup::Basic, false},

        // ---------------- 步态 / 速度 / 身高 ----------------
        {"Euler",          "姿态角",       1007, 1007, SportParam::Euler, SportGroup::Gait, false},
        {"SwitchGait",     "切换步态",     1011, 0,    SportParam::Int,   SportGroup::Gait, false},
        {"BodyHeight",     "机身高度",     1013, 0,    SportParam::Real,  SportGroup::Gait, false},
        {"FootRaiseHeight","抬腿高度",     1014, 0,    SportParam::Real,  SportGroup::Gait, false},
        {"SpeedLevel",     "速度档位",     1015, 1015, SportParam::Int,   SportGroup::Gait, false},
        {"ContinuousGait", "持续步态",     1019, 1019, SportParam::Flag,  SportGroup::Gait, false, true},
        {"EconomicGait",   "经济步态",     1035, 1063, SportParam::Flag,  SportGroup::Gait, false, true},
        {"StaticWalk",     "静态行走",     0,    1061, SportParam::Flag,  SportGroup::Gait, false, true},
        {"TrotRun",        "小跑",         0,    1062, SportParam::Flag,  SportGroup::Gait, false, true},
        {"SwitchJoystick", "手柄接管",     1027, 1027, SportParam::Flag,  SportGroup::Gait, false},
        {"Trigger",        "扳机",         1012, 0,    SportParam::Json,  SportGroup::Gait, false},

        // ---------------- 表演动作 ----------------
        {"Hello",          "打招呼",       1016, 1016, SportParam::None, SportGroup::Show, false},
        {"Stretch",        "伸懒腰",       1017, 1017, SportParam::None, SportGroup::Show, false},
        {"Content",        "满意",         1020, 1020, SportParam::None, SportGroup::Show, false},
        {"Wallow",         "撒娇打滚",     1021, 0,    SportParam::None, SportGroup::Show, false},
        {"Dance1",         "舞蹈 1",       1022, 1022, SportParam::None, SportGroup::Show, false},
        {"Dance2",         "舞蹈 2",       1023, 1023, SportParam::None, SportGroup::Show, false},
        {"Pose",           "摆姿势",       1028, 1028, SportParam::Flag, SportGroup::Show, false},
        {"Scrape",         "拜年(作揖)",   1029, 1029, SportParam::None, SportGroup::Show, false},
        {"WiggleHips",     "扭屁股",       1033, 0,    SportParam::None, SportGroup::Show, false},
        {"FingerHeart",    "比心",         1036, 1036, SportParam::None, SportGroup::Show, false},
        {"MoonWalk",       "太空步",       1305, 0,    SportParam::None, SportGroup::Show, false},
        {"OnesidedStep",   "单边踏步",     1303, 0,    SportParam::None, SportGroup::Show, false},
        {"CrossStep",      "交叉步",       1302, 2051, SportParam::Flag, SportGroup::Show, false, true},
        {"StandOut",       "站立展示",     1039, 0,    SportParam::None, SportGroup::Show, false},
        {"LeadFollow",     "领航跟随",     1045, 2056, SportParam::Flag, SportGroup::Show, false, true},
        {"FreeWalk",       "自由行走",     1045, 2045, SportParam::Flag, SportGroup::Show, false, true},

        // ---------------- 跳跃特技（危险：需要足够空间） ----------------
        {"FrontJump",      "前跳",         1031, 1031, SportParam::None, SportGroup::Stunt, true},
        {"FrontPounce",    "前扑",         1032, 1032, SportParam::None, SportGroup::Stunt, true},
        {"FrontFlip",      "前空翻",       1030, 1030, SportParam::Flag, SportGroup::Stunt, true},
        {"LeftFlip",       "左空翻",       1042, 2041, SportParam::Flag, SportGroup::Stunt, true},
        {"RightFlip",      "右空翻",       1043, 0,    SportParam::Flag, SportGroup::Stunt, true},
        {"BackFlip",       "后空翻",       1044, 2043, SportParam::Flag, SportGroup::Stunt, true},
        {"Handstand",      "倒立",         1301, 2044, SportParam::Flag, SportGroup::Stunt, true, true},
        {"Bound",          "跳跃奔跑",     1304, 2046, SportParam::Flag, SportGroup::Stunt, true, true},
        {"FreeJump",       "自由跳跃",     0,    2047, SportParam::Flag, SportGroup::Stunt, true, true},

        // ---------------- 状态查询 ----------------
        {"GetBodyHeight",     "查机身高度", 1024, 0,    SportParam::None, SportGroup::Query, false},
        {"GetFootRaiseHeight","查抬腿高度", 1025, 0,    SportParam::None, SportGroup::Query, false},
        {"GetSpeedLevel",     "查速度档位", 1026, 1026, SportParam::None, SportGroup::Query, false},
        {"GetState",          "查运动状态", 1034, 1034, SportParam::None, SportGroup::Query, false},
        {"GetAutoRecovery",   "查自动恢复", 0,    2055, SportParam::None, SportGroup::Query, false},

        // ---------------- 其他 / 进阶 ----------------
        {"TrajectoryFollow",  "轨迹跟随",   1018, 0,    SportParam::Json, SportGroup::Advanced, false},
        {"Standup",           "起立(兼容)", 1050, 0,    SportParam::None, SportGroup::Advanced, false},
        {"CrossWalk",         "横向行走",   1051, 0,    SportParam::None, SportGroup::Advanced, false},
        {"ClassicWalk",       "经典步态",   0,    2049, SportParam::Flag, SportGroup::Advanced, false, true},
        {"BackStand",         "后仰站立",   0,    2050, SportParam::Flag, SportGroup::Advanced, false, true},
        {"SetAutoRecovery",   "设自动恢复", 0,    2054, SportParam::Flag, SportGroup::Advanced, false, true},
        {"FreeAvoid",         "自由避障",   0,    2048, SportParam::Flag, SportGroup::Advanced, false, true},
        {"SwitchAvoidMode",   "避障模式",   0,    2058, SportParam::Flag, SportGroup::Advanced, false, true},
    };
    return kActions;
}

int apiIdFor(const SportAction& a, bool mcfMode) {
    if (!mcfMode) return a.normalId;
    return a.mcfId != 0 ? a.mcfId : a.normalId;
}

bool differsInMcf(const SportAction& a) {
    return a.mcfId != 0 && a.mcfId != a.normalId;
}

std::string labelForApiId(int apiId) {
    if (apiId == 0) return {};
    for (const auto& a : sportActions())
        if (a.normalId == apiId || a.mcfId == apiId) return a.label;
    if (apiId == 1008) return "移动";
    return {};
}

std::vector<int> persistentModeIds() {
    std::vector<int> out;
    for (const auto& a : sportActions()) {
        if (!a.toggle) continue;
        const int ids[2] = {a.normalId, a.mcfId};
        for (int id : ids)
            if (id != 0 && std::find(out.begin(), out.end(), id) == out.end())
                out.push_back(id);
    }
    return out;
}

std::vector<int> alternateApiIds(int apiId) {
    std::vector<int> out;
    if (apiId == 0) return out;
    for (const auto& a : sportActions()) {
        const bool isNormal = (a.normalId == apiId);
        const bool isMcf = (a.mcfId == apiId);
        if (!isNormal && !isMcf) continue;
        const int other = isNormal ? a.mcfId : a.normalId;
        if (other != 0 && other != apiId &&
            std::find(out.begin(), out.end(), other) == out.end())
            out.push_back(other);
    }
    return out;
}

nlohmann::json buildSportParam(const SportAction& a, int intVal, float realVal,
                               float x, float y, float z, const std::string& rawJson) {
    switch (a.param) {
        case SportParam::None:
            return nlohmann::json();
        case SportParam::Flag: {
            nlohmann::json p;
            p["data"] = true;
            return p;
        }
        case SportParam::Int: {
            nlohmann::json p;
            p["data"] = intVal;
            return p;
        }
        case SportParam::Real: {
            nlohmann::json p;
            p["data"] = realVal;
            return p;
        }
        case SportParam::Euler: {
            nlohmann::json p;
            p["x"] = x;
            p["y"] = y;
            p["z"] = z;
            return p;
        }
        case SportParam::Move: {
            nlohmann::json p;
            p["x"] = x;
            p["y"] = y;
            p["z"] = z;
            return p;
        }
        case SportParam::Json: {
            try {
                return nlohmann::json::parse(rawJson);
            } catch (...) {
                return nlohmann::json();
            }
        }
    }
    return nlohmann::json();
}

}  // namespace go2
