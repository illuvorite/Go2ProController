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

#include "fake_sink.hpp"
#include "sport_library.hpp"
#include "test_util.hpp"
#include "ui.hpp"

#include <cmath>
#include <limits>
#include <memory>
#include <string>
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

    return go2test::summary("command_service_test");
}
