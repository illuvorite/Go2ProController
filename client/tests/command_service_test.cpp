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
#include <memory>
#include <string>

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

    return go2test::summary("command_service_test");
}
