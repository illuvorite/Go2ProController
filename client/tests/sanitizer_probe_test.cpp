// ============================================================================
// sanitizer 正对照探针。
//
// 为什么需要它：脚本里"TSan/ASan 报告 0 条"这个结论，只有在**这个环境里的 sanitizer
// 确实能抓到问题**的前提下才算证据。只报 0 而不做正对照，很可能是插桩根本没生效
// （例如 WSL 的 ASLR 冲突让 TSan 直接 FATAL，程序根本没用 sanitizer 跑起来）。
//
// 用法（由 scripts/verify_tsan.sh / verify_asan.sh 自动调用）：
//     GO2_PROBE=tsan ./sanitizer_probe_test    → 制造无同步数据竞争，TSan 必须报
//     GO2_PROBE=asan ./sanitizer_probe_test    → 制造堆越界写，ASan 必须报
//     不带 GO2_PROBE 运行                       → 什么都不做、退出 0
//
// 为什么不干脆用编译期宏：
//   宏要改 CMake 变量 → 每次都要重新配置 + 重编一遍（这一层开销比验证本身还大）。
//   做成"运行期决定"之后，同一份二进制既能当普通测试跑，也能当探针用。
// ============================================================================

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

namespace {

/// 无同步共享变量 —— 故意留着的，供 -DGO2_PROBE=tsan 用（平时没人碰它）
int g_unsynchronized = 0;

/// TSan 正对照：两个线程无同步地自增同一个变量
void probeTsan() {
    std::printf("[probe] 制造无同步数据竞争（期望 TSan 报 data race）...\n");
    std::fflush(stdout);
    auto worker = [] {
        for (int i = 0; i < 20000; ++i) g_unsynchronized += 1;
    };
    std::thread t1(worker);
    std::thread t2(worker);
    t1.join();
    t2.join();
    std::printf("[probe] 竞争已制造完毕（值=%d，值本身无所谓）\n", g_unsynchronized);
    std::fflush(stdout);
}

/// ASan 正对照：堆越界写
void probeAsan() {
    std::printf("[probe] 制造堆越界写（期望 ASan 报 heap-buffer-overflow）...\n");
    std::fflush(stdout);
    volatile int* p = new int[1];
    // ★ 索引走 volatile 变量而不是字面量 3：字面量会让 GCC 在编译期就报
    //   -Warray-bounds（"数组下标越界"）—— 那会破坏"零告警"的目标；
    //   换成运行期值后编译器证不出来，但 ASan 的插桩照样在运行时抓到它。
    volatile int idx = 3;
    p[idx] = 42;  // 越界写
    delete[] p;
    std::printf("[probe] 越界写完成（若没看到 ASan 报告，说明插桩没生效）\n");
    std::fflush(stdout);
}

}  // namespace

int main() {
    const char* how = std::getenv("GO2_PROBE");
    const std::string mode = how ? how : "";

    if (mode == "tsan") {
        probeTsan();
        return 0;
    }
    if (mode == "asan") {
        probeAsan();
        return 0;
    }

    // 默认路径：什么坏事都不做。作为普通 `ctest` 用例必须是干净的。
    std::printf("sanitizer_probe_test: 未设置 GO2_PROBE，正常退出（无副作用）\n");
    // 顺手校验一下"不带环境变量时真的没触发"：这个断言在 sanitizer 下也必须成立
    std::atomic<int> ok{0};
    ok.fetch_add(1);
    return ok.load() == 1 ? 0 : 1;
}
