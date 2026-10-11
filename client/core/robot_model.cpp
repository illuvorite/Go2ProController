#include "robot_model.hpp"

#include <cctype>

namespace go2 {

RobotModel modelFromSn(const std::string& sn) {
    // 序列号第 5 个字符（下标 4）编码机型 —— 长度不够就是拿不到 SN，按 Unknown 处理
    if (sn.size() < 5) return RobotModel::Unknown;
    switch (sn[4]) {
        case '1': return RobotModel::Air;
        case '2': return RobotModel::Pro;
        case '4': return RobotModel::Edu;
        default:  return RobotModel::Unknown;
    }
}

const char* modelName(RobotModel m) {
    switch (m) {
        case RobotModel::Air: return "Air";
        case RobotModel::Pro: return "Pro";
        case RobotModel::Edu: return "Edu";
        case RobotModel::Unknown: break;
    }
    return "";  // Unknown：空串 = 界面不显示机型
}

RobotModel modelFromName(const std::string& name) {
    std::string low;
    low.reserve(name.size());
    for (char ch : name) low.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    if (low == "air") return RobotModel::Air;
    if (low == "pro") return RobotModel::Pro;
    if (low == "edu") return RobotModel::Edu;
    return RobotModel::Unknown;
}

}  // namespace go2
