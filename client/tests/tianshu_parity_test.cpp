// ============================================================================
// 天树探界对齐部分的纯逻辑自测：状态帧解析 / 快捷动作表 / 舞蹈编排数据模型。
//
// 这几块都是"离线可判定"的：
//   · telemetry  —— 字段位置随固件变，解析错了界面就永远显示 "—"（温度）或
//                   把"没收到姿态角"当成"已回正"（方向回正会直接空转一圈）；
//   · quickActions —— 角度/速度是照参考实现抄的，抄错一位狗的动作方向就反了；
//   · dance      —— 落盘/读回必须无损，否则用户编排的动作序列会静默丢步。
// ============================================================================

#include "dance.hpp"
#include "quick_actions.hpp"
#include "telemetry.hpp"
#include "test_util.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <string>

using namespace go2;

namespace {

nlohmann::json parse(const std::string& s) { return nlohmann::json::parse(s); }

// ---------------------------------------------------------------- 状态帧解析

void testLowState() {
    // 真实 rt/lf/lowstate 的形状（天树探界 go2-robot-plugin.js 里解析的就是这几个字段）
    const auto msg = parse(R"({
        "type": "msg",
        "topic": "rt/lf/lowstate",
        "data": {
            "bms_state": {"soc": 78, "current": -1200, "mcu_ntc": 41.5, "bq_ntc": 39.0},
            "temperature_ntc1": 36.5,
            "imu": {"rpy": [0.01, -0.02, 0.35], "gyroscope": [0, 0, 0.12]},
            "motor_state": [{"temperature": 45}]
        }
    })");
    const Telemetry t = parseTelemetry("rt/lf/lowstate", msg);
    CHECK(t.hasSoc);
    CHECK(t.soc == 78.0f);
    CHECK(t.hasTemp);
    // 温度优先取 mcu_ntc（与参考实现 `mcu_ntc ?? bq_ntc ?? ntc1` 同序）
    CHECK_MSG(t.tempC == 41.5f, "应优先取 bms_state.mcu_ntc");
    CHECK(t.hasYaw);
    CHECK_MSG(std::fabs(t.yaw - 0.35f) < 1e-6, "yaw 应取 imu.rpy[2]");
    CHECK(t.hasYawRate);
    CHECK(std::fabs(t.yawRate - 0.12f) < 1e-6);
}

void testLowStateFallbacks() {
    // 没有 bms_state.mcu_ntc → 退到 bq_ntc
    {
        const auto t = parseTelemetry("rt/lf/lowstate",
                                      parse(R"({"data":{"bms_state":{"bq_ntc":33.0}}})"));
        CHECK(t.hasTemp);
        CHECK(t.tempC == 33.0f);
    }
    // 再退到平铺的 temperature_ntc1
    {
        const auto t = parseTelemetry("rt/lf/lowstate", parse(R"({"data":{"temperature_ntc1":29.5}})"));
        CHECK(t.hasTemp);
        CHECK(t.tempC == 29.5f);
    }
    // imu_state.rpy（另一版固件的字段名）也要认
    {
        const auto t = parseTelemetry("rt/lf/lowstate",
                                      parse(R"({"data":{"imu_state":{"rpy":[0,0,-1.25]}}})"));
        CHECK(t.hasYaw);
        CHECK(std::fabs(t.yaw + 1.25f) < 1e-6);
    }
    // 多包一层 data.data（参考实现写的是 `message.data?.data || message.data`）
    {
        const auto t =
            parseTelemetry("rt/lf/lowstate", parse(R"({"data":{"data":{"bms_state":{"soc":55}}}})"));
        CHECK(t.hasSoc);
        CHECK(t.soc == 55.0f);
    }
}

void testSportModeState() {
    const auto msg = parse(R"({
        "topic": "rt/sportmodestate",
        "data": {"mode_name": "ai", "error_code": 0, "battery_level": 91}
    })");
    const Telemetry t = parseTelemetry("rt/sportmodestate", msg);
    CHECK_MSG(t.mode == "ai", "模式名应取 mode_name");
    CHECK(t.hasSoc);
    CHECK(t.soc == 91.0f);
    // 运动状态话题里没有 imu → 绝不能凭空给一个 yaw=0（那会被当成"已回正"）
    CHECK(!t.hasYaw);
}

void testTelemetryGarbage() {
    // 坏数据必须退化成"什么都没有"，而不是抛异常 / 给出假值
    const char* bad[] = {
        "null",
        "[]",
        R"({"data": 3})",
        R"({"data": {"bms_state": "x", "imu": {"rpy": "y"}}})",
        R"({"data": {"bms_state": {"soc": "not-a-number"}}})",
        R"({"data": {"imu": {"rpy": [1, 2]}}})",  // 数组太短，取不到 [2]
    };
    for (const char* s : bad) {
        const Telemetry t = parseTelemetry("rt/lf/lowstate", parse(s));
        CHECK(!t.hasSoc);
        CHECK(!t.hasTemp);
        CHECK(!t.hasYaw);
    }
}

// ---------------------------------------------------------------- 快捷动作表

void testQuickTable() {
    const QuickAction* q = findQuickAction("TwistLeft");
    CHECK(q != nullptr);
    CHECK_MSG(q->kind == QuickKind::EulerPreset, "左扭应是「先平衡站立再发姿态角」的脚本");
    CHECK(q->z == 0.5f);
    // 分组复用动作库那一套 —— 界面据此把它们排进**同一个网格**（不另起一条）
    CHECK_MSG(q->group == SportGroup::Basic, "姿态预设应排在「基础姿态」组");

    q = findQuickAction("LeanLeft");
    CHECK(q != nullptr && q->x == -0.75f);  // 左倾为负（与参考实现一致，符号错了会往右倒）
    q = findQuickAction("ShiftRight");
    CHECK(q != nullptr && q->y == -0.4f);
    q = findQuickAction("BodyLowest");
    CHECK(q != nullptr && q->kind == QuickKind::BodyHeight && q->data == -0.18f);
    CHECK(q != nullptr && q->group == SportGroup::Advanced);
    q = findQuickAction("AdjustYaw");
    CHECK(q != nullptr && q->kind == QuickKind::AdjustOrientation);

    // ★ 不设「恢复移动」：它就是 StopMove(1003)，动作库「基础姿态」里已经有「停止移动」。
    //   再摆一个按钮 = 同一个功能两个入口（2026-10-11 用户指出后删除）。
    CHECK_MSG(findQuickAction("ResumeMove") == nullptr, "不该再有「恢复移动」这个重复入口");

    CHECK(findQuickAction("NoSuchThing") == nullptr);
    CHECK(findQuickAction("") == nullptr);

    // key 不能重复（网页端按 key 派发，重了会点错动作）
    for (const auto& a : quickActions())
        for (const auto& b : quickActions())
            if (&a != &b) CHECK_MSG(std::string(a.key) != b.key, std::string("key 重复: ") + a.key);

    // 也不能和动作库的 key 撞（两边共用同一张图标表与同一个网格）
    for (const auto& q : quickActions())
        for (const auto& a : sportActions())
            CHECK_MSG(std::string(q.key) != a.key, std::string("与动作库 key 撞了: ") + q.key);
}

// ---------------------------------------------------------------- 舞蹈编排

void testDanceRoundTrip() {
    Dance d;
    d.id = 7;
    d.name = "开场舞";
    d.desc = "测试";
    d.steps.push_back({"StandUp", 2500});
    d.steps.push_back({"wait", 800});
    d.steps.push_back({"Dance1", 5000});

    const Dance back = danceFromJson(danceToJson(d));
    CHECK(back.id == 7);
    CHECK(back.name == "开场舞");
    CHECK(back.steps.size() == 3);
    CHECK(back.steps[1].key == "wait");
    CHECK(back.steps[1].delayMs == 800);
    CHECK(back.steps[2].delayMs == 5000);
}

void testDanceCompat() {
    // 参考实现导出的字段名是 duration，我们也要能读进来
    const Dance d = danceFromJson(parse(R"({
        "name": "导入",
        "steps": [{"command": "sit", "duration": 1200}]
    })"));
    // "command" 不是我们的字段名 → 这一步应被跳过（而不是塞一条空 key 的步进）
    CHECK(d.steps.empty());
    const Dance d2 = danceFromJson(parse(R"({"name":"x","steps":[{"key":"Sit","duration":1200}]})"));
    CHECK(d2.steps.size() == 1);
    CHECK(d2.steps[0].delayMs == 1200);
}

void testDanceDefaults() {
    CHECK(defaultDanceDelayMs("Dance1") == 5000);
    CHECK(defaultDanceDelayMs("BackFlip") == 6000);
    CHECK(defaultDanceDelayMs("StandUp") == 2500);
    CHECK(defaultDanceDelayMs("未知动作") == 3500);  // 默认 3.5s
    CHECK(defaultDanceDelayMs("wait") == 0);
}

}  // namespace

int main() {
    testLowState();
    testLowStateFallbacks();
    testSportModeState();
    testTelemetryGarbage();
    testQuickTable();
    testDanceRoundTrip();
    testDanceCompat();
    testDanceDefaults();
    return go2test::summary("tianshu_parity_test");
}
