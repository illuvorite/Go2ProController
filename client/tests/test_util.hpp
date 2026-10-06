#pragma once

// ============================================================================
// 极简断言工具。
//
// 为什么不上 gtest / Catch2：本项目的自测目标是"离线、秒级、零依赖" ——
// 拉一个测试框架进来会让 CI 首次构建变慢、也可能因网络失败。
// 这里只需要"断言 + 末尾汇总 + 非零退出码"，30 行就够。
//
// 用法：
//     CHECK(expr);
//     CHECK_EQ(actual, expected, "说明");
//     ...
//     return go2test::summary("xxx_test");
// ============================================================================

#include <cstdio>
#include <string>

namespace go2test {

inline int g_checks = 0;
inline int g_failures = 0;

inline void report(bool ok, const char* expr, const char* file, int line,
                   const std::string& note) {
    ++g_checks;
    if (ok) return;
    ++g_failures;
    std::printf("  [FAIL] %s:%d\n         %s%s%s\n", file, line, expr,
                note.empty() ? "" : "\n         ", note.c_str());
}

inline int summary(const char* name) {
    std::printf("%s: %d 项检查, %d 项失败%s\n", name, g_checks, g_failures,
                g_failures == 0 ? "  → PASS" : "  → FAIL");
    return g_failures == 0 ? 0 : 1;
}

}  // namespace go2test

#define CHECK(expr) ::go2test::report((expr) ? true : false, #expr, __FILE__, __LINE__, "")
#define CHECK_MSG(expr, note) \
    ::go2test::report((expr) ? true : false, #expr, __FILE__, __LINE__, (note))
