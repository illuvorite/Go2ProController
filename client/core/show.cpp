#include "show.hpp"

#include "dance.hpp"
#include "sport_library.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace go2 {

namespace {

/// 按 key 在动作表里查一条动作（找不到返回 nullptr）。
///
/// sport_library.hpp 只导出了 sportActions() 这张表本身，没导出按 key 查找的函数
/// （command_service.cpp 里那个 actionByKey 是它自己的匿名命名空间函数）。
/// 这里自己查一份 —— 免得为一个 5 行的查找去动公共头文件。
const SportAction* findAction(const std::string& key) {
    if (key.empty()) return nullptr;
    for (const auto& a : sportActions()) {
        if (key == a.key) return &a;
    }
    return nullptr;
}

double deg2rad(double d) { return d * 3.14159265358979323846 / 180.0; }

}  // namespace

int beatMs(int bpm) {
    if (bpm <= 0) bpm = 120;  // 除零防护：bpm=0 会让一拍变成无穷大
    return static_cast<int>(std::lround(60000.0 / static_cast<double>(bpm)));
}

double showDurationSec(const Show& s) {
    const int total = std::max(0, s.beats);
    return static_cast<double>(total) * static_cast<double>(beatMs(s.bpm)) / 1000.0;
}

double cueStartSec(const Show& s, const ShowCue& c) {
    const int b = std::max(0, c.beat);
    return static_cast<double>(b) * static_cast<double>(beatMs(s.bpm)) / 1000.0;
}

double moveTranslateSec(const MoveSpec& m) {
    const double dist = std::max(std::fabs(m.forwardM), std::fabs(m.leftM));
    if (dist < 1e-6) return 0.0;
    const double sp = (m.speedMs > 1e-3) ? m.speedMs : 0.35;
    return dist / sp;
}

double moveTurnSec(const MoveSpec& m) {
    const double deg = std::fabs(m.turnDeg);
    if (deg < 1e-6) return 0.0;
    const double rate = (m.turnRateDegS > 1e-3) ? m.turnRateDegS : 90.0;
    return deg / rate;
}

double moveDurationSec(const MoveSpec& m) { return moveTranslateSec(m) + moveTurnSec(m); }

double cueDurationSec(const Show& s, const ShowCue& c) {
    (void)s;
    switch (c.kind) {
        case CueKind::Action: {
            if (c.actionKey.empty() || c.actionKey == "wait") return 0.0;
            // 与舞蹈编排同一个口径：动作要留够做完的时间，否则下一个动作会打断上一个。
            return static_cast<double>(defaultDanceDelayMs(c.actionKey)) / 1000.0;
        }
        case CueKind::Move:
            return moveDurationSec(c.move);
        case CueKind::Light:
            return 0.0;  // 灯光是瞬时事件
    }
    return 0.0;
}

void moveVelocity(const MoveSpec& m, int phase, float* vx, float* vy, float* vz) {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    if (phase == 0) {
        // 平移段：按整段时长摊平，速度 = 位移 / 时长（保证走完刚好到位）。
        const double t = moveTranslateSec(m);
        if (t > 1e-6) {
            x = static_cast<float>(m.forwardM / t);
            y = static_cast<float>(m.leftM / t);
        }
    } else {
        // 转向段：只给 vz（协议 z = 左转为正）。
        const double t = moveTurnSec(m);
        if (t > 1e-6) z = static_cast<float>(deg2rad(m.turnDeg) / t);
    }
    if (vx) *vx = x;
    if (vy) *vy = y;
    if (vz) *vz = z;
}

double normalizeDeg(double deg) {
    if (!std::isfinite(deg)) return 0.0;
    double d = std::fmod(deg + 180.0, 360.0);
    if (d <= 0.0) d += 360.0;
    return d - 180.0;
}

bool slotTransfer(const Formation& from, const Formation& to, const std::string& robotKey,
                  MoveSpec* out) {
    if (!out) return false;
    const auto pick = [&robotKey](const Formation& f, const StageSlot** slot) -> bool {
        auto it = f.assign.find(robotKey);
        if (it == f.assign.end()) return false;
        for (const auto& s : f.slots) {
            if (s.id == it->second) {
                *slot = &s;
                return true;
            }
        }
        return false;
    };

    const StageSlot* a = nullptr;
    const StageSlot* b = nullptr;
    if (!pick(from, &a) || !pick(to, &b)) return false;

    // 舞台位移（x 右 / y 前）→ 机器人自身坐标系（x 前 / y 左）。
    const double dx = b->x - a->x;
    const double dy = b->y - a->y;
    const double th = deg2rad(a->yawDeg);
    // 朝向向量 = (−sin θ, cos θ)：θ=0 面朝 +y（北），θ=+90 面朝 −x（西）——
    //   也就是"左转 90°"，与协议里 z=左转为正、StageSlot 的 yaw 逆时针为正一致。
    // 左方向 = 朝向再逆时针转 90° = (−cos θ, −sin θ)。
    const double forward = dx * (-std::sin(th)) + dy * std::cos(th);
    const double left = dx * (-std::cos(th)) + dy * (-std::sin(th));

    out->forwardM = forward;
    out->leftM = left;
    out->turnDeg = normalizeDeg(b->yawDeg - a->yawDeg);
    out->speedMs = 0.35;
    out->turnRateDegS = 90.0;
    return true;
}

std::vector<std::string> validateShow(const Show& s) {
    std::vector<std::string> problems;

    if (s.bpm <= 0) problems.push_back("bpm 必须为正数（当前 " + std::to_string(s.bpm) + "）");
    if (s.beats <= 0) problems.push_back("总拍数必须为正数");
    if (s.cues.empty()) problems.push_back("时间轴是空的");

    for (const auto& c : s.cues) {
        const std::string where = "第 " + std::to_string(c.beat) + " 拍";
        if (c.beat < 0 || c.beat >= std::max(1, s.beats)) {
            problems.push_back(where + "：超出总拍数（0.." + std::to_string(s.beats - 1) + "）");
        }
        switch (c.kind) {
            case CueKind::Action: {
                if (c.actionKey.empty() || c.actionKey == "wait") break;
                if (!findAction(c.actionKey)) {
                    problems.push_back(where + "：动作 key 不存在 —— " + c.actionKey);
                    break;
                }
                // ★ 高危动作不进演出：演出是连着播的，一个空翻甩出去就可能摔。
                //   （与舞蹈编排同一条纪律，见 sport_library.hpp 的 isRiskyAction 说明）
                if (isRiskyAction(c.actionKey)) {
                    problems.push_back(where + "：高危动作不许进演出 —— " + c.actionKey);
                }
                break;
            }
            case CueKind::Move: {
                const double d = std::max(std::fabs(c.move.forwardM), std::fabs(c.move.leftM));
                if (d > 5.0) {
                    problems.push_back(where + "：单段位移 " + std::to_string(d) +
                                       " 米太长，请拆成多段（开环走太远会累积误差）");
                }
                if (std::fabs(c.move.turnDeg) > 180.0) {
                    problems.push_back(where + "：转向超过 180 度");
                }
                break;
            }
            case CueKind::Light:
                if (c.lightId.empty()) problems.push_back(where + "：灯光 cue 没有灯号");
                break;
        }
    }

    // 编队换位的静态校验：位移量与舞台位对不上时提示。
    // ★ 这是**开环**的必然要求：写 1 米但实际要 2.3 米，机器人会停在错的位置上，
    //   而现场没人看得出来（它不会报错，只是站歪了）。
    for (const auto& c : s.cues) {
        if (c.kind != CueKind::Move) continue;
        // 找一对舞台位：roles 为空时无法静态比对，跳过。
        if (c.roles.empty()) continue;
        for (const auto& from : s.formations) {
            for (const auto& to : s.formations) {
                if (from.id == to.id) continue;
                MoveSpec need;
                if (!slotTransfer(from, to, c.roles.front(), &need)) continue;
                const double needDist = std::max(std::fabs(need.forwardM), std::fabs(need.leftM));
                const double haveDist =
                    std::max(std::fabs(c.move.forwardM), std::fabs(c.move.leftM));
                if (haveDist > 1e-6 && std::fabs(needDist - haveDist) > 0.25) {
                    std::ostringstream os;
                    os << "第 " << c.beat << " 拍：位移量与编队位对不上 —— 舞台位要求约 "
                       << needDist << " 米，编排里写的是 " << haveDist << " 米";
                    problems.push_back(os.str());
                }
                break;  // 每个 from 只比对第一个匹配的 to，避免刷屏
            }
        }
    }
    return problems;
}

// ---------------------------------------------------------------- JSON

nlohmann::json formationToJson(const Formation& f) {
    nlohmann::json j = nlohmann::json::object();
    j["id"] = f.id;
    j["name"] = f.name;
    nlohmann::json slots = nlohmann::json::array();
    for (const auto& s : f.slots) {
        nlohmann::json o = nlohmann::json::object();
        o["id"] = s.id;
        o["name"] = s.name;
        o["x"] = s.x;
        o["y"] = s.y;
        o["yaw"] = s.yawDeg;
        slots.push_back(o);
    }
    j["slots"] = slots;
    nlohmann::json assign = nlohmann::json::object();
    for (const auto& kv : f.assign) assign[kv.first] = kv.second;
    j["assign"] = assign;
    return j;
}

Formation formationFromJson(const nlohmann::json& j) {
    Formation f;
    try {
        if (!j.is_object()) return f;
        f.id = j.value("id", 0);
        f.name = j.value("name", std::string());
        auto it = j.find("slots");
        if (it != j.end() && it->is_array()) {
            for (const auto& e : *it) {
                if (!e.is_object()) continue;
                StageSlot s;
                s.id = e.value("id", std::string());
                s.name = e.value("name", std::string());
                s.x = e.value("x", 0.0);
                s.y = e.value("y", 0.0);
                s.yawDeg = e.value("yaw", 0.0);
                if (s.id.empty()) continue;
                f.slots.push_back(std::move(s));
            }
        }
        auto ia = j.find("assign");
        if (ia != j.end() && ia->is_object()) {
            for (auto it2 = ia->begin(); it2 != ia->end(); ++it2) {
                if (it2.value().is_string()) f.assign[it2.key()] = it2.value().get<std::string>();
            }
        }
    } catch (...) {
        // 单条坏数据不能让整份演出加载失败
    }
    return f;
}

nlohmann::json showToJson(const Show& s) {
    nlohmann::json j = nlohmann::json::object();
    j["id"] = s.id;
    j["name"] = s.name;
    j["desc"] = s.desc;
    j["bpm"] = s.bpm;
    j["beats"] = s.beats;
    nlohmann::json cues = nlohmann::json::array();
    for (const auto& c : s.cues) {
        nlohmann::json o = nlohmann::json::object();
        o["beat"] = c.beat;
        o["kind"] = static_cast<int>(c.kind);
        if (!c.actionKey.empty()) o["action"] = c.actionKey;
        if (c.kind == CueKind::Move) {
            nlohmann::json mv = nlohmann::json::object();
            mv["forward"] = c.move.forwardM;
            mv["left"] = c.move.leftM;
            mv["turn"] = c.move.turnDeg;
            mv["speed"] = c.move.speedMs;
            mv["turnRate"] = c.move.turnRateDegS;
            o["move"] = mv;
        }
        if (!c.lightId.empty()) o["light"] = c.lightId;
        if (!c.roles.empty()) {
            nlohmann::json r = nlohmann::json::array();
            for (const auto& x : c.roles) r.push_back(x);
            o["roles"] = r;
        }
        cues.push_back(o);
    }
    j["cues"] = cues;
    nlohmann::json fs = nlohmann::json::array();
    for (const auto& f : s.formations) fs.push_back(formationToJson(f));
    j["formations"] = fs;
    return j;
}

Show showFromJson(const nlohmann::json& j) {
    Show s;
    try {
        if (!j.is_object()) return s;
        s.id = j.value("id", 0);
        s.name = j.value("name", std::string());
        s.desc = j.value("desc", std::string());
        s.bpm = j.value("bpm", 120);
        s.beats = j.value("beats", 32);
        auto it = j.find("cues");
        if (it != j.end() && it->is_array()) {
            for (const auto& e : *it) {
                if (!e.is_object()) continue;
                ShowCue c;
                c.beat = e.value("beat", 0);
                const int kind = e.value("kind", 0);
                c.kind = (kind == 1) ? CueKind::Move : (kind == 2) ? CueKind::Light : CueKind::Action;
                c.actionKey = e.value("action", std::string());
                c.lightId = e.value("light", std::string());
                auto im = e.find("move");
                if (im != e.end() && im->is_object()) {
                    c.move.forwardM = im->value("forward", 0.0);
                    c.move.leftM = im->value("left", 0.0);
                    c.move.turnDeg = im->value("turn", 0.0);
                    c.move.speedMs = im->value("speed", 0.35);
                    c.move.turnRateDegS = im->value("turnRate", 90.0);
                }
                auto ir = e.find("roles");
                if (ir != e.end() && ir->is_array()) {
                    for (const auto& x : *ir) {
                        if (x.is_string()) c.roles.push_back(x.get<std::string>());
                    }
                }
                s.cues.push_back(std::move(c));
            }
        }
        auto ifm = j.find("formations");
        if (ifm != j.end() && ifm->is_array()) {
            for (const auto& e : *ifm) s.formations.push_back(formationFromJson(e));
        }
    } catch (...) {
        // 单条坏数据不能让整份演出加载失败
    }
    return s;
}

}  // namespace go2
