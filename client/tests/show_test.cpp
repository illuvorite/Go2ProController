// ============================================================================
// 演出数据模型（编队 / 时间轴 / 开环位移）的离线自测。
//
// 这块错了**不会崩**，只会"演出看着不对"——最难查的那一类：
//   · 拍点算错   → 动作整体错拍，跟音乐对不上；
//   · 坐标系搞反 → 队形是镜像的，狗往反方向走；
//   · 速度×时长 ≠ 距离 → 狗走不到位，停在一个没人预期的位置；
//   · 高危动作混进演出 → 现场空翻，摔。
// 所以每一条都用**可手算的数**钉死，不靠"跑起来看着差不多"。
//
// ★ 最重要的一条是 testAbsoluteTimelineNoDrift：
//   同步的前提是"cue 的时刻由 t0 绝对决定"，而不是靠累加 sleep 攒出来。
//   这条测试把这条性质写成断言：第 N 拍的时刻恒等于 N × 一拍，
//   与前面实际花了多久**完全无关**。
// ============================================================================

#include "show.hpp"
#include "test_util.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <string>

using namespace go2;

namespace {

constexpr double kEps = 1e-6;

bool nearly(double a, double b, double eps = kEps) { return std::fabs(a - b) <= eps; }

/// 造一个只有一台狗的编队：站在 (x,y)，面朝 yawDeg。
Formation makeFormation(int id, const std::string& name, const std::string& robotKey, double x,
                         double y, double yawDeg) {
    Formation f;
    f.id = id;
    f.name = name;
    StageSlot s;
    s.id = "s1";
    s.name = "主位";
    s.x = x;
    s.y = y;
    s.yawDeg = yawDeg;
    f.slots.push_back(s);
    f.assign[robotKey] = "s1";
    return f;
}

}  // namespace

// ---------------------------------------------------------------- 拍点与时间轴

void testBeatMath() {
    CHECK(beatMs(120) == 500);
    CHECK(beatMs(60) == 1000);
    CHECK(beatMs(240) == 250);
    // bpm=0 会让一拍变成无穷大 → 必须有兜底
    CHECK(beatMs(0) == 500);
    CHECK(beatMs(-30) == 500);
}

void testAbsoluteTimelineNoDrift() {
    // ★ 同步的根本性质：cue 的时刻由 t0 + beat×一拍 决定，**不累积**。
    // 如果实现改成"上一条发完 sleep 多久再发下一条"，这条测试会失败。
    Show s;
    s.bpm = 120;  // 一拍 500ms
    s.beats = 16;
    for (int b = 0; b < 16; ++b) {
        ShowCue c;
        c.beat = b;
        c.kind = CueKind::Action;
        c.actionKey = "Hello";
        s.cues.push_back(c);
    }
    for (const auto& c : s.cues) {
        CHECK_MSG(nearly(cueStartSec(s, c), c.beat * 0.5),
                  "第 " + std::to_string(c.beat) + " 拍的绝对时刻应恒等于 beat×0.5 秒");
    }
    CHECK(nearly(showDurationSec(s), 8.0));  // 16 拍 × 0.5 秒

    // 拍号不连续也必须按绝对值算（跳过的拍不产生"额外等待"）
    ShowCue jump;
    jump.beat = 8;
    jump.kind = CueKind::Action;
    jump.actionKey = "Hello";
    CHECK(nearly(cueStartSec(s, jump), 4.0));
}

void testCueDurations() {
    Show s;
    s.bpm = 120;

    // Action：与舞蹈编排同一口径（defaultDanceDelayMs）
    ShowCue a;
    a.kind = CueKind::Action;
    a.actionKey = "Hello";
    const double helloSec = cueDurationSec(s, a);
    CHECK_MSG(helloSec > 3.0 && helloSec < 4.0, "Hello 应留约 3.5 秒做完");

    // Move：平移段 + 转向段
    ShowCue m;
    m.kind = CueKind::Move;
    m.move.forwardM = 2.0;
    m.move.speedMs = 0.5;    // 2.0 / 0.5 = 4 秒
    m.move.turnDeg = 90.0;
    m.move.turnRateDegS = 90.0;  // 90 / 90 = 1 秒
    CHECK(nearly(moveTranslateSec(m.move), 4.0));
    CHECK(nearly(moveTurnSec(m.move), 1.0));
    CHECK(nearly(moveDurationSec(m.move), 5.0));
    CHECK(nearly(cueDurationSec(s, m), 5.0));

    // Light：瞬时事件
    ShowCue l;
    l.kind = CueKind::Light;
    l.lightId = "warm";
    CHECK(nearly(cueDurationSec(s, l), 0.0));
}

// ---------------------------------------------------------------- 开环位移

void testMoveVelocity() {
    MoveSpec m;
    m.forwardM = 2.0;
    m.leftM = 0.0;
    m.turnDeg = 90.0;
    m.speedMs = 0.5;
    m.turnRateDegS = 90.0;

    float vx = 0, vy = 0, vz = 0;
    // 平移段：只给平移，转向归零
    moveVelocity(m, 0, &vx, &vy, &vz);
    CHECK_MSG(nearly(vx, 0.5f), "2 米 / 4 秒 = 0.5 m/s");
    CHECK(nearly(vy, 0.0f));
    CHECK(nearly(vz, 0.0f));

    // 转向段：只给 vz（协议 z = 左转为正）
    moveVelocity(m, 1, &vx, &vy, &vz);
    CHECK(nearly(vx, 0.0f));
    CHECK(nearly(vy, 0.0f));
    CHECK_MSG(vz > 0.0f, "左转 90° 应给出正的 vz");
    // 90° / 1 秒 = π/2 rad/s
    CHECK(nearly(static_cast<double>(vz), 3.14159265358979 / 2.0, 1e-5));

    // ★ 往返一致：速度 × 时长 必须等于位移（不然狗走不到位）
    MoveSpec back;
    back.forwardM = -1.5;
    back.speedMs = 0.3;
    moveVelocity(back, 0, &vx, &vy, &vz);
    const double t = moveTranslateSec(back);
    CHECK_MSG(nearly(static_cast<double>(vx) * t, -1.5, 1e-5),
              "速度 × 时长 应等于位移（后退 1.5 米）");
}

void testNormalizeDeg() {
    CHECK(nearly(normalizeDeg(0), 0));
    CHECK(nearly(normalizeDeg(90), 90));
    CHECK(nearly(normalizeDeg(-90), -90));
    CHECK(nearly(normalizeDeg(180), 180));
    CHECK(nearly(normalizeDeg(181), -179));   // 绕回
    CHECK(nearly(normalizeDeg(360), 0));
    CHECK(nearly(normalizeDeg(-190), 170));
}

// ---------------------------------------------------------------- 编队换位

void testSlotTransferDirections() {
    // 机器人站在原点、面朝北（yaw=0）
    const std::string key = "ip:192.168.2.111";
    const Formation from = makeFormation(1, "A", key, 0, 0, 0);

    struct Case {
        double x, y;
        const char* what;
        double fwd, left;
    };
    const Case cases[] = {
        {0.0, 2.0, "正前方 2 米 → 前进 2", 2.0, 0.0},
        {-1.0, 0.0, "正西（左）1 米 → 左移 1", 0.0, 1.0},
        {0.0, -2.0, "正南（后）2 米 → 前进 −2（后退）", -2.0, 0.0},
        {1.0, 0.0, "正东（右）1 米 → 左移 −1（右移）", 0.0, -1.0},
    };
    for (const auto& c : cases) {
        const Formation to = makeFormation(2, "B", key, c.x, c.y, 0);
        MoveSpec m;
        CHECK_MSG(slotTransfer(from, to, key, &m), std::string("应算出位移：") + c.what);
        CHECK_MSG(nearly(m.forwardM, c.fwd, 1e-6), std::string("前进量错：") + c.what);
        CHECK_MSG(nearly(m.leftM, c.left, 1e-6), std::string("左移量错：") + c.what);
    }
}

void testSlotTransferTurn() {
    const std::string key = "ip:192.168.2.111";
    const Formation from = makeFormation(1, "A", key, 0, 0, 0);
    // 目标位面朝西（yaw=90 = 左转 90°）
    const Formation to = makeFormation(2, "B", key, 0, 0, 90);
    MoveSpec m;
    CHECK(slotTransfer(from, to, key, &m));
    CHECK_MSG(nearly(m.turnDeg, 90.0), "从北转到西 = 左转 90°");

    // 反向：西 → 北 = 右转 90° → 归一化后是 −90
    // （注意目标是 yaw=0 那个编队，别又传成 `to` —— 那是同一朝向，转向量应为 0）
    const Formation fromW = makeFormation(1, "A", key, 0, 0, 90);
    MoveSpec m2;
    CHECK(slotTransfer(fromW, from, key, &m2));
    CHECK_MSG(nearly(m2.turnDeg, -90.0), "西转到北 = 右转 90°（负）");

    // 同一朝向 → 转向量应为 0
    MoveSpec m3;
    CHECK(slotTransfer(fromW, to, key, &m3));
    CHECK_MSG(nearly(m3.turnDeg, 0.0), "同朝向换位不需要转向");
}

void testSlotTransferMissing() {
    const std::string key = "ip:192.168.2.111";
    const Formation from = makeFormation(1, "A", key, 0, 0, 0);
    const Formation to = makeFormation(2, "B", "ip:192.168.2.999", 0, 0, 0);
    MoveSpec m;
    CHECK_MSG(!slotTransfer(from, to, key, &m), "目标编队里没有这台狗 → 应失败");
    CHECK_MSG(!slotTransfer(from, to, "ip:1.2.3.4", &m), "两套编队都没有这台狗 → 应失败");
}

// ---------------------------------------------------------------- 校验

void testValidateCatchesRiskyAndUnknown() {
    Show s;
    s.bpm = 120;
    s.beats = 8;

    // 未知动作 key
    {
        Show s2 = s;
        ShowCue c;
        c.beat = 0;
        c.kind = CueKind::Action;
        c.actionKey = "ThisActionDoesNotExist";
        s2.cues.push_back(c);
        const auto p = validateShow(s2);
        CHECK_MSG(!p.empty(), "未知动作应被拦下");
    }
    // 高危动作（空翻那一类）—— 演出是连着播的，混进去会摔
    {
        Show s2 = s;
        ShowCue c;
        c.beat = 0;
        c.kind = CueKind::Action;
        c.actionKey = "FrontFlip";
        s2.cues.push_back(c);
        const auto p = validateShow(s2);
        bool hit = false;
        for (const auto& x : p) {
            if (x.find("高危") != std::string::npos) hit = true;
        }
        CHECK_MSG(hit, "高危动作应被拦下（FrontFlip）");
    }
    // 超出总拍数
    {
        Show s2 = s;
        ShowCue c;
        c.beat = 99;
        c.kind = CueKind::Action;
        c.actionKey = "Hello";
        s2.cues.push_back(c);
        const auto p = validateShow(s2);
        CHECK_MSG(!p.empty(), "超出总拍数的 cue 应被拦下");
    }
    // 空时间轴
    {
        Show s2 = s;
        CHECK_MSG(!validateShow(s2).empty(), "空时间轴应被拦下");
    }
    // 合法的演出不该报错
    {
        Show s2 = s;
        ShowCue c;
        c.beat = 0;
        c.kind = CueKind::Action;
        c.actionKey = "Hello";
        s2.cues.push_back(c);
        CHECK_MSG(validateShow(s2).empty(), "合法演出不该报错");
    }
}

void testValidateCatchesDistanceMismatch() {
    // 编队换位写了 1 米，但舞台位要求 2.3 米 → 必须报出来
    // （开环执行，走不到位不会报错，只会站歪 —— 只能靠静态校验拦）
    Show s;
    s.bpm = 120;
    s.beats = 8;
    const std::string key = "ip:192.168.2.111";
    s.formations.push_back(makeFormation(1, "A", key, 0, 0, 0));
    s.formations.push_back(makeFormation(2, "B", key, 0, 2.3, 0));

    ShowCue c;
    c.beat = 0;
    c.kind = CueKind::Move;
    c.roles.push_back(key);
    c.move.forwardM = 1.0;   // 实际要 2.3 米
    s.cues.push_back(c);

    const auto p = validateShow(s);
    bool hit = false;
    for (const auto& x : p) {
        if (x.find("位移量") != std::string::npos) hit = true;
    }
    CHECK_MSG(hit, "位移量与舞台位对不上时应报错");

    // 写对了就不该报
    s.cues[0].move.forwardM = 2.3;
    const auto p2 = validateShow(s);
    bool mismatch = false;
    for (const auto& x : p2) {
        if (x.find("位移量") != std::string::npos) mismatch = true;
    }
    CHECK_MSG(!mismatch, "位移量写对后不该再报");
}

// ---------------------------------------------------------------- JSON 往返

void testJsonRoundTrip() {
    Show s;
    s.id = 7;
    s.name = "开幕式";
    s.desc = "测试";
    s.bpm = 100;
    s.beats = 32;

    ShowCue a;
    a.beat = 4;
    a.kind = CueKind::Action;
    a.actionKey = "Hello";
    a.roles.push_back("ip:192.168.2.111");
    s.cues.push_back(a);

    ShowCue m;
    m.beat = 8;
    m.kind = CueKind::Move;
    m.move.forwardM = 1.25;
    m.move.leftM = -0.5;
    m.move.turnDeg = 45.0;
    m.move.speedMs = 0.4;
    m.move.turnRateDegS = 80.0;
    s.cues.push_back(m);

    ShowCue l;
    l.beat = 12;
    l.kind = CueKind::Light;
    l.lightId = "spot-3";
    s.cues.push_back(l);

    s.formations.push_back(makeFormation(1, "横排", "ip:192.168.2.111", 1.0, 2.0, 90));

    const Show back = showFromJson(showToJson(s));
    CHECK(back.id == s.id);
    CHECK(back.name == s.name);
    CHECK(back.desc == s.desc);
    CHECK(back.bpm == s.bpm);
    CHECK(back.beats == s.beats);
    CHECK(back.cues.size() == 3);
    CHECK(back.formations.size() == 1);

    CHECK(back.cues[0].beat == 4);
    CHECK(back.cues[0].kind == CueKind::Action);
    CHECK(back.cues[0].actionKey == "Hello");
    CHECK(back.cues[0].roles.size() == 1);

    CHECK(back.cues[1].kind == CueKind::Move);
    CHECK(nearly(back.cues[1].move.forwardM, 1.25));
    CHECK(nearly(back.cues[1].move.leftM, -0.5));
    CHECK(nearly(back.cues[1].move.turnDeg, 45.0));
    CHECK(nearly(back.cues[1].move.speedMs, 0.4));
    CHECK(nearly(back.cues[1].move.turnRateDegS, 80.0));

    CHECK(back.cues[2].kind == CueKind::Light);
    CHECK(back.cues[2].lightId == "spot-3");

    CHECK(back.formations[0].id == 1);
    CHECK(back.formations[0].slots.size() == 1);
    CHECK(nearly(back.formations[0].slots[0].x, 1.0));
    CHECK(nearly(back.formations[0].slots[0].y, 2.0));
    CHECK(nearly(back.formations[0].slots[0].yawDeg, 90.0));
    CHECK(back.formations[0].assign.at("ip:192.168.2.111") == "s1");
}

void testJsonBadInputSurvives() {
    // 坏数据不能让整份演出加载失败（与舞蹈编排同一条纪律）
    const Show s = showFromJson(nlohmann::json::parse(R"({"name":123,"cues":"not-an-array"})"));
    CHECK(s.cues.empty());
    const Show s2 = showFromJson(nlohmann::json::parse("[]"));
    CHECK(s2.name.empty());
    const Show s3 = showFromJson(nlohmann::json::parse(R"({"cues":[{"beat":1,"kind":1}]})"));
    CHECK(s3.cues.size() == 1);
    CHECK(s3.cues[0].kind == CueKind::Move);
}

int main() {
    testBeatMath();
    testAbsoluteTimelineNoDrift();
    testCueDurations();
    testMoveVelocity();
    testNormalizeDeg();
    testSlotTransferDirections();
    testSlotTransferTurn();
    testSlotTransferMissing();
    testValidateCatchesRiskyAndUnknown();
    testValidateCatchesDistanceMismatch();
    testJsonRoundTrip();
    testJsonBadInputSurvives();
    return go2test::summary("show_test");
}
