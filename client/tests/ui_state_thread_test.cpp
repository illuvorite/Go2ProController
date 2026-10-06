// ============================================================================
// UiState 的**并发访问**自测。
//
// 为什么需要它：界面（每帧读）与 Web 后端（`/api/state` 读、`/api/command` 写）是两个线程，
// 改造前它们直接读写同一批裸 `bool/float/int` 和 `std::map/std::set` ——
// 前者是数据竞争（UB），后者会在并发重哈希时**直接崩**。
//
// 这个测试按"两个线程各自最像真实用法的访问序列"反复冲，用来保证：
//   · 普通构建下：不崩（map/set 的并发访问问题会立刻现形）；
//   · TSan 构建下（`-DGO2_SANITIZE=thread`）：报告 0 条 data race。
//
// ★ 它不替代真机验证：这里只覆盖"状态读写的线程安全"，不覆盖协议/网络。
// ============================================================================

#include "command_service.hpp"

#include "fake_sink.hpp"
#include "test_util.hpp"
#include "ui.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

using namespace go2;
using go2test::FakeSink;

namespace {

/// 本次运行实际使用的迭代次数（main 里按环境变量决定）
int g_iters = 1000;

/// 默认迭代次数。可用环境变量 GO2_STRESS_ITERS 调小 ——
/// sanitizer（ASan/TSan）插桩后单次迭代慢 10~20 倍，跑 1000 次要一分多钟；
/// 压到 200 次仍然能覆盖全部共享状态的读写路径（约十几秒），
/// 需要"跑久一点"时把它调回 1000+ 即可。
int stressIters() {
    if (const char* e = std::getenv("GO2_STRESS_ITERS")) {
        const int v = std::atoi(e);
        if (v > 0) return v;
    }
    return 1000;
}

std::string ipAt(int i) {
    return "10.0.0." + std::to_string((i % 200) + 1);
}

/// 模拟 **Web 后端线程**：与 `web_bridge.cpp` 的 `/api/state` + `/api/command` 同形状
void webThread(UiState& ui, FakeSink& sink, std::atomic<bool>& go) {
    while (!go.load()) std::this_thread::yield();
    for (int i = 0; i < g_iters; ++i) {
        // /api/state 侧：读各种状态（改造前这些读都没有同步）
        (void)ui.estop.load();
        (void)ui.mcfMode.load();
        (void)ui.maxLinSpeed.load();
        (void)ui.speedLevel.load();
        (void)ui.cmdVx.load();
        (void)ui.problemCount.load();
        (void)ui.problemSeen.load();
        (void)ui.localKeyCount.load();
        (void)ui.nameOf(ipAt(i));
        (void)ui.labelOf(ipAt(i));
        (void)ui.togglesSnapshot();
        (void)ui.activeToggleIdsSnapshot();
        (void)ui.selectedIps();
        (void)ui.selectedCount();
        (void)ui.controlTargetText();
        (void)ui.isSelected(ipAt(i));

        // /api/command 侧：改名 / 开关 / 参数 / 动作 / 摇杆 / 急停
        ui.setName(ipAt(i), "狗" + std::to_string(i));
        ui.setToggle("CrossStep", (i % 2) == 0);
        ui.setToggleActive(1302, (i % 2) == 0);
        ui.maxLinSpeed = 0.3f + float(i % 10) * 0.05f;
        ui.speedScale = 0.2f + float(i % 5) * 0.1f;
        ui.mcfMode = (i % 2) == 0;
        ui.privacyMode = (i % 3) == 0;
        ui.localKeyCount = i % 7;
        ui.problemSeen = ui.problemCount.load();

        cmd::dispatchAction(sink, ui, "ContinuousGait");
        cmd::moveSelected(sink, ui, 0.2f, 0.0f, 0.0f);
        cmd::quickMove(sink, ui, "fwd");
        cmd::stopSelected(sink);
        cmd::dampAll(sink);

        if ((i % 97) == 0) {
            const auto r = cmd::estop(go2test::borrow(sink), ui, /*asyncClose=*/false);
            (void)r;
            ui.estop = false;  // 下一轮继续跑
        }
    }
}

/// 模拟 **界面线程**：与 `ui.cpp` 每帧的读取序列同形状
void uiThread(UiState& ui, std::atomic<bool>& go) {
    while (!go.load()) std::this_thread::yield();
    for (int i = 0; i < g_iters; ++i) {
        // 顶栏：急停状态 / 角标 / 受控目标
        (void)ui.estop.load();
        (void)ui.movingSent.load();
        const int total = ui.problemCount.load();
        const int seen = ui.problemSeen.load();
        (void)(total > seen ? total - seen : 0);
        (void)ui.controlTargetText();
        (void)ui.selectedCount();
        (void)ui.localKeyCount.load();

        // 设备列表：名称 + 勾选
        for (const auto& ip : ui.selectedIps()) {
            (void)ui.labelOf(ip);
            (void)ui.isSelected(ip);
        }

        // 动作库：瓷砖开关状态 + 回执标注 + 指令集
        bool mcfSnap = ui.mcfMode.load();
        (void)cmd::resolveApiId(ui, sportActions()[size_t(i) % sportActions().size()], nullptr);
        (void)ui.toggleState("CrossStep");
        for (const auto& kv : ui.togglesSnapshot()) (void)kv.second;
        for (int id : ui.activeToggleIdsSnapshot()) {
            int code = 0;
            std::string note;
            (void)ui.apiResult(id, &code, &note);
        }
        (void)mcfSnap;

        // 遥控页：参数 / 命令值 / 键盘输入
        (void)ui.maxLinSpeed.load();
        (void)ui.yawRate.load();
        (void)ui.speedScale.load();
        (void)ui.bodyHeight.load();
        (void)ui.footRaise.load();
        (void)ui.speedLevel.load();
        (void)ui.gaitType.load();
        (void)ui.privacyMode.load();
        (void)ui.hideUnsupported.load();

        ui.noteApiResult(1001 + (i % 30), i % 3 == 0 ? 0 : 3203, "测试回执");
        ui.addLog("[测试] 并发日志 " + std::to_string(i));
        ui.addOrUpdate(ipAt(i), false);
        ui.updateStatus(ipAt(i), float(i % 100), "trot");
        (void)ui.selectedIps();
    }
}

}  // namespace

int main() {
    g_iters = stressIters();
    UiState ui;
    FakeSink sink;
    sink.ready = {"10.0.0.1", "10.0.0.2", "10.0.0.3"};
    sink.selected = {"10.0.0.1", "10.0.0.2", "10.0.0.3"};
    // UiState 自己的设备列表也要照着填：群控的"受控集合"读的是它（sink 只是假的下发口）
    for (const auto& ip : sink.selected) {
        ui.addOrUpdate(ip, true);
        ui.setSelected(ip, true);
    }

    // 顺带覆盖"异步急停"那条路径：它会在后台线程里持有 sink 并读 ui.estopBusy。
    // 必须等它自己收尾（否则测试结束销毁 ui/sink 时后台线程还在跑 —— 那是真 use-after-free）。
    {
        ui.setToggle("CrossStep", true);
        ui.setToggleActive(1302, true);
        const auto r = cmd::estop(go2test::borrow(sink), ui, /*asyncClose=*/true);
        CHECK(r.stopped == 3);
        for (int i = 0; i < 400 && ui.estopBusy.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        CHECK_MSG(!ui.estopBusy.load(), "异步急停的后台线程必须在 10 秒内收尾");
        ui.estop = false;
        sink.reset();
    }

    std::atomic<bool> go{false};
    std::vector<std::thread> threads;
    threads.emplace_back(webThread, std::ref(ui), std::ref(sink), std::ref(go));
    threads.emplace_back(webThread, std::ref(ui), std::ref(sink), std::ref(go));  // 两个 Web 请求并发
    threads.emplace_back(uiThread, std::ref(ui), std::ref(go));
    threads.emplace_back(uiThread, std::ref(ui), std::ref(go));

    const auto t0 = std::chrono::steady_clock::now();
    go = true;
    for (auto& t : threads) t.join();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0)
                        .count();

    // 走到这里就说明没有崩溃（并发访问 std::map/std::set 崩溃是最典型的失败模式）
    std::printf("  并发跑完 %d 次迭代 x4 线程，用时 %lld ms\n", g_iters,
                static_cast<long long>(ms));

    // ---- 收尾后的基本一致性 ----
    CHECK(ui.selectedIps().size() >= 1);
    CHECK(ui.selectedCount() == static_cast<int>(ui.selectedIps().size()));
    for (const auto& kv : ui.togglesSnapshot()) (void)kv.second;  // 快照本身不炸
    // 日志有上限（addLog 里 500 上限 + 批量裁剪），不能无限涨
    {
        std::lock_guard<std::mutex> lock(ui.logMutex);
        CHECK(ui.logs.size() <= 500);
    }
    CHECK(ui.problemCount.load() >= 0);

    return go2test::summary("ui_state_thread_test");
}
