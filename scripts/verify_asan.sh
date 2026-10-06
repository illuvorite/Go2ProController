#!/usr/bin/env bash
# ============================================================================
# AddressSanitizer 验证（对应 docs/optimization_plan.md 指标 3）
#
# 做什么：
#   1. ASan 构建里跑全部纯逻辑自测（越界 / use-after-free / 泄漏）；
#   2. 两个"退出路径"场景 —— 最容易出悬垂引用：
#        · 起服务跑几秒再 SIGTERM（此时连接监督线程 / 监听线程都活着）
#        · 起来就立刻退出（全局 std::thread 在静态析构期的收尾）
#   3. **正对照**：GO2_PROBE=asan 跑 sanitizer_probe_test（故意堆越界写），必须报出来。
#
# 用法（WSL / Ubuntu）：
#     cmake --preset linux-asan && cmake --build client/build/linux-asan --parallel 2
#     bash scripts/verify_asan.sh
#
# 注 1：go2_remote 需要 GL/GLFW，无显示环境不一定能跑 → 这里只跑不依赖图形栈的
#       go2_serve + 自测（图形部分由 GO2_SHOT 截图自测覆盖）。
# 注 2：**刻意不触发"扫描中退出"**：那会真去扫局域网并 connectAll 连上机器狗，
#       会占用狗唯一的 WebRTC 会话。扫描线程的并发安全由 ui_state_thread_test（TSan）覆盖。
# 注 3：正对照用**运行期** GO2_PROBE 而不是编译期宏，所以不需要重新配置/重编。
# ============================================================================
set -u
cd "$(dirname "$0")/.." || exit 1
SRC=$(pwd)/client
B=$SRC/build/linux-asan
ITERS="${GO2_STRESS_ITERS:-200}"
FAIL=0

if [ ! -d "$B" ]; then
  echo "没有 $B。先执行："
  echo "    cmake --preset linux-asan && cmake --build client/build/linux-asan --parallel 2"
  exit 2
fi

export ASAN_OPTIONS=halt_on_error=1:detect_leaks=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1

# $1 = 展示名, $2 = 在 $B 下执行的命令
step() {
  local name="$1" cmd="$2"
  local log="/tmp/go2_asan_${name}.log"
  echo
  echo "== $name =="
  local code=0
  ( cd "$B" && eval "$cmd" ) > "$log" 2>&1 || code=$?
  local n
  n=$(grep -cE 'ERROR: AddressSanitizer|runtime error:|LeakSanitizer' "$log")
  echo "  退出码=$code  ASan 报告=$n"
  if [ "$n" -gt 0 ]; then
    grep -E 'ERROR: AddressSanitizer|runtime error:|LeakSanitizer|SUMMARY' "$log" | head -6 | sed 's/^/    /'
    FAIL=$((FAIL + 1))
    return
  fi
  if [ "$code" -ne 0 ] && [ "$code" -ne 124 ]; then
    echo "    !! 非零退出但无 ASan 报告，看 /tmp/go2_asan_${name}.log"
    tail -5 "$log" | sed 's/^/    /'
    FAIL=$((FAIL + 1))
    return
  fi
  tail -1 "$log" | sed 's/^/    /'
}

echo "迭代数 = $ITERS"

# ---- 先增量构建：必须跑**当前源码**的二进制（踩过"陈旧二进制导致误报"的坑）----
echo
echo "== 增量构建 =="
if ( cd "$B" && ninja crypto_test motion_test layout_test sport_library_test \
       command_service_test ui_state_thread_test sanitizer_probe_test go2_serve ) \
     >/tmp/go2_asan_build.log 2>&1; then
  echo "  构建成功"
else
  grep -E 'error:' /tmp/go2_asan_build.log | head -10
  exit 1
fi

echo
echo "===== 正式场景（期望 0 报告）====="
step ctest "GO2_STRESS_ITERS=$ITERS ctest --output-on-failure | tail -12"
step exit_with_live_threads 'timeout 30 ./go2_serve --quiet & p=$!; sleep 4; kill -TERM $p; wait $p; echo "TERM 后已退出"'
step exit_immediately 'timeout 8 ./go2_serve --quiet | tail -2; true'

echo
echo "===== 正对照（故意越界，期望报出来）====="
step probe_asan 'GO2_PROBE=asan ./sanitizer_probe_test'
# 正对照"报了"才算成功：这里把上面那个 FAIL 计数反向折算
if grep -q 'ERROR: AddressSanitizer' /tmp/go2_asan_probe_asan.log 2>/dev/null; then
  FAIL=$((FAIL - 1))
  echo "  [ok] ASan 报出了故意注入的越界 → 工具确实在干活"
else
  echo "  !! 正对照没报错 → 前面的 0 报告不可信"
fi

echo
echo "=================== 结论 ==================="
if [ "$FAIL" -eq 0 ]; then
  echo "  [PASS] ASan 有效，且正式场景 0 报告"
  exit 0
fi
echo "  [FAIL] 失败场景数 = $FAIL，见 /tmp/go2_asan_*.log"
exit 1
