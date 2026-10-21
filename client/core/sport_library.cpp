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

        // ★ 「步态 / 速度 / 身高」整组（11 条）2026-10-11 **删除**。
        //   它 2026-09-28 就被要求从界面撤下（那批滑条最占地方），界面上一直够不到、
        //   只剩 ui_actions.cpp 里一段渲染代码；预设（姿态角 / 机身高度）已经覆盖了实际用法。
        //   原条目：Euler 1007 / SwitchGait 1011 / BodyHeight 1013 / FootRaiseHeight 1014 /
        //          SpeedLevel 1015 / ContinuousGait 1019 / EconomicGait 1035,1063 /
        //          StaticWalk 1061 / TrotRun 1062 / SwitchJoystick 1027 / Trigger 1012。
        //   仍在用的三个 id（1007 / 1013 / 1019）以常量形式留在 sport_library.hpp：
        //   kApiEuler / kApiBodyHeight / kApiContinuousGait。

        // ---------------- 表演动作 ----------------
        {"Hello",          "打招呼",       1016, 1016, SportParam::None, SportGroup::Show, false},
        {"Stretch",        "伸懒腰",       1017, 1017, SportParam::None, SportGroup::Show, false},
        {"Content",        "满意",         1020, 1020, SportParam::None, SportGroup::Show, false},
        {"Wallow",         "撒娇打滚",     1021, 0,    SportParam::None, SportGroup::Show, false},
        {"Dance1",         "舞蹈 1",       1022, 1022, SportParam::None, SportGroup::Show, false},
        {"Dance2",         "舞蹈 2",       1023, 1023, SportParam::None, SportGroup::Show, false},
        // ★ 摆姿势是**持续模式开关**（不是一次性动作）：`{"data":true}` 开启后狗会一直摆姿势、
        //   不接受行走；必须用同一条 api_id 带 `{"data":false}` 关闭 —— StopMove(1003) 停不掉它。
        //   改造前它是 toggle=false 的普通 Flag → 界面只发得出 true、**没有任何关闭入口**，
        //   而急停的 persistentModeIds() 也收不到它 → 一旦开启就只能重启狗（2026-10-11 用户实测）。
        {"Pose",           "摆姿势",       1028, 1028, SportParam::Flag, SportGroup::Show, false, true},
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

        // ★ 「状态查询」整组（5 条）2026-10-11 **删除**：查机身高度 / 查抬腿高度 /
        //   查速度档位 / 查运动状态 / 查自动恢复（1024 / 1025 / 1026 / 1034 / 2055）。
        //   删的理由不是"没用"，而是**它们在说谎**：我们只解析回执的 code，
        //   查询返回的数据从头到尾没人读 —— 界面上点一下只会出现一条
        //   「[回执] 查机身高度 (api 1024) —— 成功 ✔」，身高一个数字都不显示。
        //   参考实现「天树探界遥控」的动作页里也**根本没有**这几条。
        //   真要用，正确做法是把返回的 data 接到状态区（和刚加的电量/温度/姿态角一样），
        //   而不是留一个"点了像查到了、其实什么也没查"的按钮。
        //   → 届时连同 sport_library.hpp 的 kApiGet* 常量一起加回来。

        // ---------------- 其他 / 进阶 ----------------
        // ★ 2026-10-20 精简：这组原来有 7 条，现在**只剩「避障」两条**。
        //   删掉的 5 条，理由各不相同（不是笼统的"没用"）：
        //     · 轨迹跟随 1018 / 横向行走 1051 —— **normal 专属**。
        //       固件 ≥1.1.7 的 MCF 模式只受理 MCF 那套 id，实测这类 id 一律 3203
        //       （1011 / 1013 / 1014 / 1024 / 1025 都实测过）。轨迹跟随还得手填
        //       JSON 轨迹，本来就是最难用的一个。
        //     · 经典步态 2049 / 后仰站立 2050 —— 能受理，但它们是**步态/姿态开关**，
        //       放在「模式设置」页更合适，混在动作库里是分类错位。
        //     · 设自动恢复 2054 —— 能受理，但它是**安全功能的开关**（狗摔了自己站起来）。
        //       删按钮 ≠ 关功能（固件里默认是开的，2055 查得到）；而且它不该出现在
        //       动作库里 —— 急停也不该去关它（关掉 = 摔了起不来，更危险）。
        //   ★ 保留「避障」两条：避障是**真功能**（实测 2048 / 2058 都受理），
        //     而且安全相关，必须留在一个显眼、能一键关的位置。
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
    // 不在动作表里、但仍会下发的几条（见 sport_library.hpp 的常量说明）——
    // 回执日志要靠它把 api_id 翻成中文名，否则会打出一串裸 id
    switch (apiId) {
        case kApiMove: return "移动";
        case kApiEuler: return "姿态角";
        case kApiBodyHeight: return "机身高度";
        case kApiContinuousGait: return "持续步态";
        // ★ 2026-10-20 从动作表里删掉、但**回执里仍可能出现**的 id（老客户端 / 网页端
        //   缓存的编排 / 别的 App 发的）：名字要留着，否则回执日志只能打出一串裸 id。
        case 1018: return "轨迹跟随";
        case 1051: return "横向行走";
        case 2049: return "经典步态";
        case 2050: return "后仰站立";
        case 2054: return "设自动恢复";
        default: return {};
    }
}

bool isRiskyAction(const std::string& key) {
    if (key.empty()) return false;
    for (const auto& a : sportActions())
        if (key == a.key) return a.risky;
    return false;  // 未知 key：不拦（快捷动作 / "wait" 之类的伪动作都走这条路）
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
