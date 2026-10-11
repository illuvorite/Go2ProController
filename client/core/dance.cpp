#include "dance.hpp"

#include <string>

namespace go2 {

int defaultDanceDelayMs(const std::string& key) {
    // 值逐条对齐参考实现的 durationMap（它按"命令名"给默认执行时间）。
    if (key == "wait") return 0;  // 等待步用自己设的延时
    if (key == "StandUp" || key == "StandDown" || key == "Sit" || key == "RiseSit" ||
        key == "BalanceStand" || key == "RecoveryStand" || key == "Damp")
        return 2500;
    if (key == "Stretch") return 3000;
    if (key == "Hello" || key == "FingerHeart" || key == "Scrape" || key == "WiggleHips" ||
        key == "FrontJump" || key == "FrontPounce" || key == "WalkUpright" ||
        key == "CrossStep" || key == "FreeWalk" || key == "LeadFollow")
        return 3500;
    if (key == "Dance1" || key == "Dance2") return 5000;
    if (key == "FrontFlip" || key == "BackFlip" || key == "LeftFlip" || key == "RightFlip" ||
        key == "Bound" || key == "FreeJump")
        return 6000;
    if (key == "Handstand") return 4500;
    // 注：姿态角(1007) / 移动(1008) 不在动作表里了（整组删除），编排也不会有这两步 ——
    //     它们要么是"左扭"那种预设（自带时序），要么是摇杆的连续速度（不是点一下的动作）。
    return 3500;  // 默认 3.5 秒（与参考实现一致）
}

nlohmann::json danceToJson(const Dance& d) {
    nlohmann::json j = nlohmann::json::object();
    j["id"] = d.id;
    j["name"] = d.name;
    j["desc"] = d.desc;
    nlohmann::json steps = nlohmann::json::array();
    for (const auto& s : d.steps) {
        nlohmann::json o = nlohmann::json::object();
        o["key"] = s.key;
        o["delay"] = s.delayMs;
        steps.push_back(o);
    }
    j["steps"] = steps;
    return j;
}

Dance danceFromJson(const nlohmann::json& j) {
    Dance d;
    try {
        if (!j.is_object()) return d;
        d.id = j.value("id", 0);
        d.name = j.value("name", std::string());
        d.desc = j.value("desc", std::string());
        auto it = j.find("steps");
        if (it != j.end() && it->is_array()) {
            for (const auto& e : *it) {
                if (!e.is_object()) continue;
                DanceStep s;
                s.key = e.value("key", std::string());
                // 兼容两种字段名：我们自己写 "delay"，参考实现导出的是 "duration"
                s.delayMs = e.value("delay", e.value("duration", 1000));
                if (s.key.empty()) continue;
                d.steps.push_back(std::move(s));
            }
        }
    } catch (...) {
        // 单条坏数据不能让整份编排加载失败
    }
    return d;
}

}  // namespace go2
