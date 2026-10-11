// ============================================================================
// 群控语义自测（不需要机器狗、不需要图形环境、不需要网络）。
//
// 这个测试存在的意义：`cmd::` 这一层是**安全动作的唯一实现**（急停、锁定、群控目标集合、
// 持续模式开关）。改造前这套语义在桌面端与网页端各有一份、而且已经分叉过：
//   · 网页端打包参数时把 speedLevel / 姿态角 / 自定义 JSON 全丢了（固定传 0）；
//   · 桌面端急停是异步的、网页端是同步的；
//   · 网页端的群控循环内联了 6 次。
// 现在用假 sink 把行为钉死，任何人改动这里都会立刻看到红。
// ============================================================================

#include "command_service.hpp"

#include "dance.hpp"
#include "fake_sink.hpp"
#include "quick_actions.hpp"
#include "robot_client.hpp"
#include "sport_library.hpp"
#include "test_util.hpp"
#include "ui.hpp"

#include <chrono>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace go2;
using go2test::FakeSink;
using go2test::kNullParam;
using go2test::borrow;

namespace {

/// 解析最后一条下发的 parameter（浮点比较走数值 ——
/// float → double 的十进制展开不是人类写的那一串）
nlohmann::json lastParam(const FakeSink& s) {
    std::lock_guard<std::mutex> lock(s.mtx);
    return nlohmann::json::parse(s.sent.back().param);
}

bool near(double a, double b) { return std::fabs(a - b) < 1e-6; }

}  // namespace

int main() {
    // ---------------------------------------------------------------- 目标集合
    // 群控只作用于「受控 ∩ 就绪」；急停才作用于「全部就绪」
    {
        UiState ui;
        FakeSink sink;
        sink.ready = {"10.0.0.1", "10.0.0.2", "10.0.0.3"};
        sink.selected = {"10.0.0.2", "10.0.0.3", "10.0.0.9"};  // .9 未就绪

        CHECK(cmd::forEachSelected(sink, [](const std::string&) { return true; }) == 2);
        CHECK(cmd::moveSelected(sink, ui, 0.5f, 0.0f, 0.0f) == 2);
        CHECK(sink.countApi(kApiMove) == 2);
        CHECK_MSG(sink.countTo("10.0.0.1") == 0, "未勾选的设备不应收到指令");
        CHECK_MSG(sink.countTo("10.0.0.9") == 0, "未就绪的设备不应收到指令");

        const auto p = lastParam(sink);
        CHECK(near(p["x"].get<double>(), 0.5));
        CHECK(near(p["y"].get<double>(), 0.0));
        CHECK(near(p["z"].get<double>(), 0.0));
    }

    // ---------------------------------------------------------------- 急停锁定
    // "锁定期间不许动"只在服务层实现一处（改造前靠两个前端各检查一遍）
    {
        UiState ui;
        FakeSink sink;
        sink.ready = {"10.0.0.1"};
        sink.selected = {"10.0.0.1"};
        ui.estop = true;

        CHECK(cmd::moveSelected(sink, ui, 1.0f, 0.0f, 0.0f) == 0);
        CHECK(cmd::quickMove(sink, ui, "fwd") == 0);
        CHECK_MSG(sink.sentCount() == 0, "急停锁定后不得下发任何运动指令");

        CHECK_MSG(cmd::stopSelected(sink) == 1, "停车是安全方向，锁定时仍要能发");
        CHECK(sink.stoppedCount() == 1);
    }

    // ---------------------------------------------------------------- 急停全流程
    {
        UiState ui;
        FakeSink sink;
        sink.ready = {"10.0.0.1", "10.0.0.2"};
        sink.selected = {"10.0.0.1"};  // 只勾了 1 台

        // 先真的开一个持续模式（CrossStep：normal 1302 / mcf 2051）
        CHECK(cmd::dispatchAction(sink, ui, "CrossStep") == 1);
        CHECK(ui.toggleState("CrossStep"));
        CHECK_MSG(ui.activeToggleIdsSnapshot().size() == 2,
                  "两套指令集的 id 都要记下 —— 急停时无论固件认哪套都能关掉");

        sink.reset();
        const auto r = cmd::estop(borrow(sink), ui, /*asyncClose=*/false);
        CHECK(ui.estop.load());
        CHECK_MSG(r.stopped == 2, "急停要停**全部就绪**设备，不限于勾选");
        CHECK(sink.stoppedCount() == 2);
        CHECK_MSG(sink.closedCount() == 2, "每台就绪设备都要关持续模式");
        {
            std::lock_guard<std::mutex> lock(sink.mtx);
            CHECK(sink.closed[0].ids.size() == 2);
        }
        CHECK_MSG(ui.togglesSnapshot().empty(), "急停后开关状态必须复位");
        CHECK(ui.activeToggleIdsSnapshot().empty());
        CHECK(!ui.movingSent.load());
        CHECK(r.toggles == 2);
    }

    // ---------------------------------------------------------------- 开关型动作
    {
        UiState ui;
        FakeSink sink;
        sink.ready = {"10.0.0.1"};
        sink.selected = {"10.0.0.1"};

        CHECK(cmd::dispatchAction(sink, ui, "ContinuousGait") == 1);
        CHECK(lastParam(sink)["data"].get<bool>() == true);
        CHECK(ui.toggleState("ContinuousGait"));
        CHECK(sink.countApi(1019) == 1);  // ContinuousGait 的 normalId

        // 再点一次 = 关闭（同一个 api_id 带 {"data": false} —— StopMove 停不掉它）
        CHECK(cmd::dispatchAction(sink, ui, "ContinuousGait") == 1);
        CHECK(lastParam(sink)["data"].get<bool>() == false);
        CHECK(!ui.toggleState("ContinuousGait"));
        CHECK(ui.activeToggleIdsSnapshot().empty());
    }

    // ---------------------------------------------------------------- 指令集兜底与参数
    {
        UiState ui;
        FakeSink sink;
        sink.ready = {"10.0.0.1"};
        sink.selected = {"10.0.0.1"};
        ui.mcfMode = false;
        ui.speedLevel = 2;

        // StaticWalk 只有 MCF 有 → 非 MCF 下必须兜底到 1061，否则这条动作永远点不到
        CHECK(cmd::dispatchAction(sink, ui, "StaticWalk") == 1);
        {
            std::lock_guard<std::mutex> lock(sink.mtx);
            CHECK(sink.sent.back().apiId == 1061);
        }

        // 参数必须取界面里的值 —— 改造前网页端固定传 0，等于"档位调了没用"
        sink.reset();
        CHECK(cmd::dispatchAction(sink, ui, "SpeedLevel") == 1);
        CHECK(lastParam(sink)["data"].get<int>() == 2);

        // 未知 key
        CHECK(cmd::dispatchAction(sink, ui, "NoSuchAction") == -1);
    }

    // ---------------------------------------------------------------- 快捷方向
    {
        UiState ui;
        FakeSink sink;
        sink.ready = {"10.0.0.1"};
        sink.selected = {"10.0.0.1"};
        ui.speedScale = 0.7f;

        CHECK(cmd::quickMove(sink, ui, "fwd") == 1);
        CHECK(near(lastParam(sink)["x"].get<double>(), 0.7));

        sink.reset();
        CHECK(cmd::quickMove(sink, ui, "right") == 1);
        CHECK(near(lastParam(sink)["z"].get<double>(), -0.7));

        CHECK(cmd::quickMove(sink, ui, "nope") == -1);
    }

    // ---------------------------------------------------------------- 强制阻尼
    {
        UiState ui;
        FakeSink sink;
        sink.ready = {"10.0.0.1", "10.0.0.2"};
        sink.selected = {"10.0.0.1"};  // 阻尼是"全员安全"，不受勾选限制
        CHECK(cmd::dampAll(sink) == 2);
        CHECK(sink.countApi(1001) == 2);
        {
            std::lock_guard<std::mutex> lock(sink.mtx);
            CHECK_MSG(sink.sent[0].param == kNullParam, "Damp 无参数 → parameter 为空");
        }
    }

    // ---------------------------------------------------------------- 姿态角自动归零
    // 姿态角是"保持型"参数：发出去一直生效，不存在"发完即失效"。
    // 不自动归零 = 狗带着歪斜姿态一直走下去。所以归零这条语义也用假 sink 钉死。
    {
        UiState ui;
        FakeSink sink;
        sink.ready = {"10.0.0.1", "10.0.0.2"};
        sink.selected = {"10.0.0.1"};

        // 发一条非零姿态
        ui.eulerX = 0.3f;
        ui.eulerY = -0.2f;
        ui.eulerZ = 0.4f;
        CHECK(cmd::dispatchAction(sink, ui, "Euler") == 1);
        CHECK(sink.countApi(1007) == 1);
        CHECK(near(lastParam(sink)["x"].get<double>(), 0.3));
        CHECK(near(lastParam(sink)["z"].get<double>(), 0.4));

        // 到点归零：下发全零 + 界面三个轴清零 + 撤掉待归零任务（否则每帧都会重发）
        CHECK(cmd::resetEuler(sink, ui) == 1);
        {
            const auto p = lastParam(sink);
            CHECK(near(p["x"].get<double>(), 0.0));
            CHECK(near(p["y"].get<double>(), 0.0));
            CHECK(near(p["z"].get<double>(), 0.0));
        }
        CHECK(ui.eulerX == 0.0f);
        CHECK(ui.eulerY == 0.0f);
        CHECK(ui.eulerZ == 0.0f);
        CHECK_MSG(ui.eulerResetAt < 0.0, "归零后必须撤掉计时任务，否则每帧重复下发");

        // 急停锁定时也要能归零 —— 这恰恰是最需要它的场景（狗停在半歪的姿态上）
        ui.estop = true;
        ui.eulerX = 0.25f;
        CHECK_MSG(cmd::moveSelected(sink, ui, 1.0f, 0.0f, 0.0f) == 0,
                  "急停锁定后不得下发运动指令");
        CHECK_MSG(cmd::resetEuler(sink, ui) == 1, "归零是安全方向，锁定时仍要能发");
        CHECK(ui.eulerX == 0.0f);
    }

    // ---------------------------------------------------------------- 受控集合收尾
    // 取消勾选 / 切单控时，被移出集合的那台必须停车 ——
    // 速度是保持型的，而 stopSelected 只遍历**新的**集合，不收尾就再也停不到它。
    {
        UiState ui;
        FakeSink sink;
        sink.ready = {"10.0.0.1", "10.0.0.2", "10.0.0.3"};
        sink.selected = {"10.0.0.1", "10.0.0.2"};

        // 从两台改成一台：只有被移出去的那台该收停车
        const std::vector<std::string> before = {"10.0.0.1", "10.0.0.2"};
        sink.selected = {"10.0.0.1"};
        CHECK(cmd::stopDeselected(sink, before) == 1);
        {
            std::lock_guard<std::mutex> lock(sink.mtx);
            CHECK_MSG(sink.stopped.size() == 1 && sink.stopped[0] == "10.0.0.2",
                      "只应停被移出受控集合的那台，仍在集合里的不动");
        }

        // 全部取消：两台都要停
        sink.reset();
        sink.selected = {};
        CHECK(cmd::stopDeselected(sink, {"10.0.0.1", "10.0.0.2", "10.0.0.9"}) == 2);
        CHECK_MSG(sink.countTo("10.0.0.9") == 0, "未就绪的设备不产生下发，也不该计入");

        // 集合没变 → 什么都不做（不能顺手把还在受控的狗停掉）
        sink.reset();
        sink.selected = {"10.0.0.1"};
        CHECK(cmd::stopDeselected(sink, {"10.0.0.1"}) == 0);
        CHECK(sink.stoppedCount() == 0);
    }

    // ---------------------------------------------------------------- 解除急停的回中闸门
    // 桌面端按钮要求双杆回中；网页端那个检查原本只在浏览器里 → 换客户端就能
    // 在摇杆还推着的时候解锁。判据必须落在服务层。
    {
        UiState ui;
        FakeSink sink;
        sink.ready = {"10.0.0.1"};
        sink.selected = {"10.0.0.1"};
        cmd::estop(borrow(sink), ui, /*asyncClose=*/false);

        // 杆还推着（最近一次下发的速度非零）→ 拒绝
        ui.cmdVx = 0.6f;
        CHECK_MSG(!cmd::unestop(ui), "摇杆未回中时必须拒绝解除急停");
        CHECK_MSG(ui.estop.load(), "被拒绝时锁必须保持不动");

        // 调用方明确说"没回中"（桌面端的 centered 闸门）→ 也拒绝
        ui.cmdVx = 0.0f;
        CHECK_MSG(!cmd::unestop(ui, /*extraCentered=*/false), "调用方判定未回中时必须拒绝");

        // 回中了 → 放行，并顺手清干净速度状态
        ui.cmdVx = 0.0f; ui.cmdVy = 0.0f; ui.cmdVz = 0.0f;
        ui.movingSent = true;
        CHECK(cmd::unestop(ui, /*extraCentered=*/true));
        CHECK(!ui.estop.load());
        CHECK(!ui.movingSent.load());
        CHECK(near(ui.cmdVx.load(), 0.0) && near(ui.cmdVz.load(), 0.0));
    }

    // ---------------------------------------------------------------- 网页端摇杆的服务层限幅
    // 限幅原本只存在于浏览器里的 motion.js：换个客户端 / 写个脚本 / 前端算错一次，
    // 就能让狗以任意速度冲出去。安全边界必须在服务端。
    {
        UiState ui;
        FakeSink sink;
        sink.ready = {"10.0.0.1"};
        sink.selected = {"10.0.0.1"};
        ui.maxLinSpeed = 0.6f;
        ui.yawRate = 1.2f;

        // 超出线速度上限 → 夹回上限
        cmd::Motion sent;
        CHECK(cmd::moveSelectedClamped(sink, ui, 5.0f, 0.0f, 0.0f, &sent) == 1);
        CHECK_MSG(near(sent.vx, 0.6), "超速的摇杆输入必须被夹到 maxLinSpeed");
        CHECK(near(lastParam(sink)["x"].get<double>(), 0.6));
        CHECK_MSG(near(sent.vx, lastParam(sink)["x"].get<double>()),
                  "sent 必须是真正发出去的值：调用方要拿它回填 cmdV*（界面显示与 "
                  "cmd::unestop 的回中判据都靠那个）；服务层只负责限幅，不改 UiState");

        // 斜推：合成幅值受限，方向不变
        {
            const cmd::Motion d = cmd::clampMotion(ui, 3.0f, 3.0f, 0.0f);
            CHECK_MSG(near(std::hypot(d.vx, d.vy), 0.6), "斜推的合成幅值也必须受限");
            CHECK(d.vx > 0.0f && d.vy > 0.0f);
        }

        // 转向单独限幅
        {
            const cmd::Motion d = cmd::clampMotion(ui, 0.0f, 0.0f, 9.0f);
            CHECK_MSG(near(d.vz, 1.2), "转向角速度必须夹到 yawRate");
            const cmd::Motion n = cmd::clampMotion(ui, 0.0f, 0.0f, -9.0f);
            CHECK(near(n.vz, -1.2));
        }

        // 硬上限：即使用户把限幅值配成离谱的大，也不得超过 1.5 / 2.0
        {
            ui.maxLinSpeed = 100.0f;
            ui.yawRate = 100.0f;
            const cmd::Motion d = cmd::clampMotion(ui, 50.0f, 0.0f, 50.0f);
            CHECK_MSG(near(d.vx, 1.5) && near(d.vz, 2.0), "必须还有一层与界面滑条一致的硬上限");
        }

        // 限幅值本身被配坏（非有限 / <=0）→ 退回硬上限，而不是让 clamp 的 lo>hi 变 UB
        {
            ui.maxLinSpeed = 0.0f;
            ui.yawRate = std::numeric_limits<float>::quiet_NaN();
            const cmd::Motion d = cmd::clampMotion(ui, 9.0f, 0.0f, 9.0f);
            CHECK_MSG(near(d.vx, 1.5) && near(d.vz, 2.0), "限幅值异常时退回硬上限");
        }

        // 非有限输入一律当 0（JSON 里塞得进来的极端数字）
        {
            ui.maxLinSpeed = 0.6f;
            ui.yawRate = 1.2f;
            const float inf = std::numeric_limits<float>::infinity();
            const cmd::Motion d = cmd::clampMotion(ui, inf, -inf, inf);
            CHECK_MSG(near(d.vx, 0.0) && near(d.vy, 0.0) && near(d.vz, 0.0),
                      "NaN/Inf 输入不得变成 NaN 发给狗");
        }
    }

    // ---------------------------------------------------------------- 摆姿势：摇杆改成调姿态
    // 行为对齐参考实现「天树探界遥控」：摆姿势开着时，摇杆下发的是 **Euler(1007)**
    // 而不是 Move(1008) —— 狗不走，只上下左右摆姿势。这条必须在服务层，
    // 否则桌面 / 网页 / 安卓三个入口各判一次、迟早分叉。
    {
        const SportAction* pose = nullptr;
        const SportAction* euler = nullptr;
        for (const auto& a : sportActions()) {
            if (std::string(a.key) == "Pose") pose = &a;
            if (std::string(a.key) == "Euler") euler = &a;
        }
        CHECK_MSG(pose != nullptr && euler != nullptr, "指令表里必须有 Pose 与 Euler");
        CHECK_MSG(pose != nullptr && pose->toggle, "Pose 必须是持续模式开关（否则关不掉）");
        const int eulerId = apiIdFor(*euler, false);   // 1007
        const int moveId = kApiMove;                   // 1008
        CHECK_MSG(eulerId != moveId, "Euler 与 Move 不能是同一个 api_id");

        UiState ui;
        FakeSink sink;
        sink.ready = {"10.0.0.1"};
        sink.selected = {"10.0.0.1"};
        ui.maxLinSpeed = 1.0f;  // 归一化成好算的数
        ui.yawRate = 1.0f;

        // 没开摆姿势 → 照旧发 Move
        CHECK(cmd::moveSelected(sink, ui, 0.5f, 0.0f, 0.0f) == 1);
        CHECK_MSG(sink.countApi(moveId) == 1, "未开摆姿势时摇杆仍应下发 Move");
        CHECK(sink.countApi(eulerId) == 0);

        // 开摆姿势 → 改发 Euler，且不再发 Move
        sink.reset();
        ui.setToggle("Pose", true);
        // x=1.0(前进满推) y=0.5(左半推) z=1.0(左转满推)
        CHECK(cmd::moveSelected(sink, ui, 1.0f, 0.5f, 1.0f) == 1);
        CHECK_MSG(sink.countApi(moveId) == 0, "摆姿势时**不得**再下发 Move（否则狗会走）");
        CHECK_MSG(sink.countApi(eulerId) == 1, "摆姿势时摇杆应下发 Euler");
        {
            const auto p = lastParam(sink);
            CHECK_MSG(near(p["x"].get<double>(), -0.5), "左推 → 左右倾斜 roll 为负（左倾为负）");
            CHECK_MSG(near(p["y"].get<double>(), -0.8), "前推满 → 抬头为负 pitch");
            CHECK_MSG(near(p["z"].get<double>(), 0.8), "左推右杆 → 左扭为正 yaw");
        }

        // 超出满量程要夹住（不能把 3 倍推力原样当角度发出去）
        sink.reset();
        CHECK(cmd::moveSelected(sink, ui, 9.0f, -9.0f, 9.0f) == 1);
        {
            const auto p = lastParam(sink);
            CHECK_MSG(near(p["x"].get<double>(), 1.0) && near(p["y"].get<double>(), -0.8) &&
                          near(p["z"].get<double>(), 0.8),
                      "摆姿势的归一化必须夹到 ±满推力");
        }

        // 急停锁定优先于摆姿势：锁着就什么都不发
        sink.reset();
        ui.estop = true;
        CHECK(cmd::moveSelected(sink, ui, 1.0f, 1.0f, 1.0f) == 0);
        CHECK_MSG(sink.sentCount() == 0, "急停锁定期间摆姿势也不得下发");
        ui.estop = false;

        // 关掉摆姿势时额外补一条 StopMove（参考实现 commandsNeedStopMove 的做法）
        sink.reset();
        CHECK(cmd::dispatchToggle(sink, ui, *pose, false) == 1);
        CHECK_MSG(sink.stoppedCount() == 1, "关闭摆姿势要顺带停车，别留下摆动余速");
        // 开启时**不**补 StopMove（否则会把刚起来的姿势打断）
        sink.reset();
        CHECK(cmd::dispatchToggle(sink, ui, *pose, true) == 1);
        CHECK_MSG(sink.stoppedCount() == 0, "开启摆姿势不应额外发 StopMove");
    }

    // ---------------------------------------------------------------- 开关：回执成功才算真的开
    // 乐观置位 + 待确认：机器人拒绝（且自动换 id 重试也没成功）时必须回滚，
    // 否则会在不支持的机型上谎报"已开启" —— 摆姿势还会顺手把摇杆换成 Euler。
    {
        const SportAction* pose = nullptr;
        const SportAction* freeWalk = nullptr;
        for (const auto& a : sportActions()) {
            if (std::string(a.key) == "Pose") pose = &a;
            if (std::string(a.key) == "FreeWalk") freeWalk = &a;
        }
        CHECK_MSG(pose != nullptr && freeWalk != nullptr, "指令表里必须有 Pose 与 FreeWalk");
        const int poseId = apiIdFor(*pose, false);
        const int fwId = apiIdFor(*freeWalk, false);
        CHECK_MSG(!alternateApiIds(fwId).empty(), "FreeWalk 应当有另一套指令集的备用 id");

        UiState ui;
        FakeSink sink;
        sink.ready = {"10.0.0.1"};
        sink.selected = {"10.0.0.1"};

        // 1) 发送后**立刻**是"开"（界面要即时响应，不能等回执），且挂着待确认
        CHECK(cmd::dispatchToggle(sink, ui, *pose, true) == 1);
        CHECK_MSG(ui.toggleState("Pose"), "发送后应乐观置位");
        CHECK_MSG(ui.reapUnconfirmedToggles(3600.0) == 0, "窗口内不得回滚");
        CHECK(ui.toggleState("Pose"));
        // 超时仍无成功回执 → 回滚成"关"，并把它从"真正开过的开关"里摘掉
        CHECK_MSG(ui.reapUnconfirmedToggles(0.0) == 1, "超时未确认应回滚一条");
        CHECK_MSG(!ui.toggleState("Pose"), "没被确认的开关必须回到发送前的状态");
        CHECK_MSG(ui.activeToggleIdsSnapshot().empty(), "回滚后不该再留着'开过的开关 id'");

        // 2) 收到成功回执 → 确认，之后即使超时也不回滚
        sink.reset();
        CHECK(cmd::dispatchToggle(sink, ui, *pose, true) == 1);
        ui.noteApiResult("10.0.0.1", poseId, 0, "");
        CHECK_MSG(ui.reapUnconfirmedToggles(0.0) == 0, "已被确认的开关不得回滚");
        CHECK_MSG(ui.toggleState("Pose"), "确认后应保持开启");

        // 3) **备用指令集** id 的成功回执同样算确认（被拒后自动换 id 重试成功的情形）
        ui.clearToggles();
        ui.clearActiveToggles();
        CHECK(cmd::dispatchToggle(sink, ui, *freeWalk, true) == 1);
        for (int alt : alternateApiIds(fwId)) ui.noteApiResult("10.0.0.1", alt, 0, "");
        CHECK_MSG(ui.reapUnconfirmedToggles(0.0) == 0, "备用 api_id 的成功回执也要认");
        CHECK(ui.toggleState("FreeWalk"));

        // 4) 关闭方向同理：关闭没被确认 → 回滚成"开"
        ui.clearToggles();
        ui.clearActiveToggles();
        ui.setToggle("Pose", true);
        CHECK(cmd::dispatchToggle(sink, ui, *pose, false) == 1);
        CHECK_MSG(!ui.toggleState("Pose"), "关闭也应乐观置位");
        CHECK(ui.reapUnconfirmedToggles(0.0) == 1);
        CHECK_MSG(ui.toggleState("Pose"), "关闭没被确认 → 回滚成'开'");

        // 5) 急停清开关时待确认一并清掉，否则过期回滚会把开关"复活"
        ui.clearToggles();
        ui.clearActiveToggles();
        CHECK(cmd::dispatchToggle(sink, ui, *pose, true) == 1);
        ui.clearToggles();
        CHECK_MSG(ui.reapUnconfirmedToggles(0.0) == 0, "急停清过之后不该再有回滚");
        CHECK(!ui.toggleState("Pose"));
    }

    // ---------------------------------------------------------------- 群控混机型：只标注、不隐藏
    // 最危险的场景：Air + Pro 一起控，Air 拒绝了某条而 Pro 还没试过 ——
    // 此时**绝不能隐藏**，否则 Pro 明明能做的动作会从动作库里消失。
    {
        UiState ui;
        ui.addOrUpdate("10.0.0.1", false, "B210123456789ABC");  // 下标 4 = '1' → Air
        ui.addOrUpdate("10.0.0.2", false, "B210223456789ABC");  // 下标 4 = '2' → Pro
        ui.setSelected("10.0.0.1", true);
        ui.setSelected("10.0.0.2", true);
        CHECK_MSG(ui.controlModel().empty(), "混机型时'单一机型'判据应为空");
        CHECK(ui.controlModels().size() == 2);

        const int api = 1030;  // 前空翻

        // (1) 只有 Air 有记录且失败 → 只标注，**不得**隐藏（Pro 还没试过）
        ui.noteApiResult("10.0.0.1", api, 3203, "不支持");
        {
            const auto s = ui.modelSupport(api);
            CHECK(s.total == 2 && s.recorded == 1 && s.failed == 1);
            CHECK_MSG(s.anyReject(), "应标注：有机型不支持");
            CHECK_MSG(!s.allReject(), "只有一台试过 → 绝不能隐藏");
            CHECK(s.rejectModels.size() == 1 && s.rejectModels[0] == "Air");
        }

        // (2) Pro 也失败 → 两台都确认不支持，这时才允许隐藏
        ui.noteApiResult("10.0.0.2", api, 3203, "不支持");
        CHECK_MSG(ui.modelSupport(api).allReject(), "两台都确认不支持 → 可以隐藏");

        // (3) Pro 其实能做 → 退回"部分不支持"，仍然不隐藏
        ui.noteApiResult("10.0.0.2", api, 0, "");
        {
            const auto s = ui.modelSupport(api);
            CHECK_MSG(s.anyReject() && !s.allReject(), "部分机型不支持 → 只标注不隐藏");
        }

        // (4) 两边都成功 → ✓
        ui.noteApiResult("10.0.0.1", 1003, 0, "");
        ui.noteApiResult("10.0.0.2", 1003, 0, "");
        CHECK(ui.modelSupport(1003).allOk());

        // (5) 群里混进一台**机型未知**的设备（手动填 IP，没有 SN）→ 整条判断作废
        ui.addOrUpdate("10.0.0.3", true);
        ui.setSelected("10.0.0.3", true);
        {
            const auto s = ui.modelSupport(api);
            CHECK_MSG(s.total == 0 && !s.allReject(), "有机型未知的设备在群里 → 一律不隐藏");
            CHECK_MSG(!s.anyReject(), "机型未知时不标注（没有可靠依据）");
        }
    }

    // ---------------------------------------------------------------- 快捷动作
    // 来源：参考实现「天树探界遥控」Go2 页的复合按钮（姿态预设 / 平移 / 机身高度 / 恢复移动）
    {
        UiState ui;
        FakeSink sink;
        sink.ready = {"10.0.0.1", "10.0.0.2"};
        sink.selected = {"10.0.0.1", "10.0.0.2"};

        // 未知 key → -1（界面据此报"没有这条快捷动作"，而不是静默什么都不做）
        CHECK(cmd::runQuickAction(borrow(sink), ui, "Nope") == -1);

        // 「恢复移动」= 一条 StopMove，打到两台
        CHECK(cmd::runQuickAction(borrow(sink), ui, "ResumeMove") == 2);
        CHECK(sink.stoppedCount() == 2);

        // 「机身最低」= BodyHeight(1013) 带 data=-0.18（对齐参考实现的取值）
        sink.reset();
        CHECK(cmd::runQuickAction(borrow(sink), ui, "BodyLowest") == 2);
        CHECK(sink.countApi(1013) == 2);
        CHECK(near(lastParam(sink)["data"].get<double>(), -0.18));

        // 没有受控设备 → 0，且什么都不发
        sink.reset();
        sink.selected.clear();
        CHECK(cmd::runQuickAction(borrow(sink), ui, "BodyHighest") == 0);
        CHECK(sink.sentCount() == 0);

        // 急停锁定：除「恢复移动」外一律不下发（恢复移动是安全方向，仍允许）
        ui.estop = true;
        sink.selected = {"10.0.0.1", "10.0.0.2"};
        sink.reset();
        CHECK(cmd::runQuickAction(borrow(sink), ui, "BodyLowest") == 0);
        CHECK(sink.sentCount() == 0);
        CHECK(cmd::runQuickAction(borrow(sink), ui, "ResumeMove") == 2);
        CHECK(sink.stoppedCount() == 2);
        ui.estop = false;
    }

    // ---------------------------------------------------------------- 运动模式
    // 走 rt/api/motion_switcher（与运动指令是两套），只发给受控且就绪的设备
    {
        UiState ui;
        FakeSink sink;
        sink.ready = {"10.0.0.1", "10.0.0.2", "10.0.0.3"};
        sink.selected = {"10.0.0.2"};  // 只有一台受控

        CHECK(cmd::setMotionMode(borrow(sink), ui, "ai") == 1);
        CHECK(sink.lastMode() == "ai");
        CHECK_MSG(sink.modes.size() == 1 && sink.modes[0].first == "10.0.0.2",
                  "模式切换只发给受控设备");
        CHECK(ui.motionMode() == "ai");  // 乐观置位，供 /api/state 立即回显

        CHECK(cmd::setMotionMode(borrow(sink), ui, "") == 0);  // 空模式名不发
    }

    // ---------------------------------------------------------------- 舞蹈编排播放
    // 顺序下发 + 每步延时；急停 / 再点播放 / dance_stop 都能中止（代际号机制）
    {
        UiState ui;
        FakeSink sink;
        sink.ready = {"10.0.0.1"};
        sink.selected = {"10.0.0.1"};

        Dance d;
        d.name = "测试编排";
        d.steps.push_back({"Sit", 0});      // 立即下发
        d.steps.push_back({"wait", 0});     // 空等待步
        d.steps.push_back({"Dance1", 0});
        const int id = ui.upsertDance(d);
        CHECK(id > 0);

        CHECK(cmd::playDance(borrow(sink), ui, id));
        // 脚本在后台线程跑；等到它收尾（或超时）
        for (int i = 0; i < 200 && ui.scriptRunning.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        CHECK_MSG(!ui.scriptRunning.load(), "编排播放应已结束");
        CHECK_MSG(sink.countApi(1009) == 1, "第 1 步 Sit(1009) 应下发");
        CHECK_MSG(sink.countApi(1022) == 1, "第 3 步 Dance1(1022) 应下发");

        // 找不到的编排 → false
        CHECK(!cmd::playDance(borrow(sink), ui, 99999));
        // 急停锁定 → false（不乱发）
        ui.estop = true;
        CHECK(!cmd::playDance(borrow(sink), ui, id));
        ui.estop = false;
        // 删掉就查不到了
        ui.removeDance(id);
        Dance tmp;
        CHECK(!ui.danceById(id, &tmp));
    }

    return go2test::summary("command_service_test");
}
