#!/usr/bin/env bash
# ============================================================================
# ThreadSanitizer 验证（对应 docs/optimization_plan.md 指标 2）
#
# 做什么：
#   1. 在 TSan 构建里跑 command_service_test 与 ui_state_thread_test，期望 0 条 data race；
#   2. 打开正对照（-DGO2_RACE_POSITIVE_CONTROL=ON，故意注入一处无同步的竞争）再跑一次，
#      期望 TSan **报出来**。
#
# 为什么必须有第 2 步：一个"0 报告"的结论，只有在"这个工具在本环境里确实抓得到竞争"
# 的前提下才算证据。只报 0 而不做正对照，很可能是 sanitizer 根本没生效。
#
# 用法（WSL / Ubuntu）：
#     cmake --preset linux-tsan && cmake --build client/build/linux-tsan --parallel 2
#     bash scripts/verify_tsan.sh
#
# 已知环境坑：WSL 的 vm.mmap_rnd_bits=32 超过 TSan 能接受的 28，直接跑会
#   "FATAL: ThreadSanitizer: unexpected memory mapping"。
# 绕法：setarch -R（只对本进程关 ASLR，不改系统设置）—— 本脚本已内置。
# ============================================================================
set -u
cd "$(dirname "$0")/.." || exit 1
SRC=$(pwd)/client
B=$SRC/build/linux-tsan
ARCH=$(uname -m)
RACE_REAL=0
RACE_POS=0

if [ ! -d "$B" ]; then
  echo "没有 $B。先执行："
  echo "    cmake --preset linux-tsan && cmake --build client/build/linux-tsan --parallel 2"
  exit 2
fi

echo "mmap_rnd_bits = $(cat /proc/sys/vm/mmap_rnd_bits 2>/dev/null)   arch = $ARCH"

run_one() {  # $1=可执行名 $2=日志 $3=超时秒
  TSAN_OPTIONS=halt_on_error=0:second_deadlock_stack=1 \
    timeout "$3" setarch "$ARCH" -R "./$1" > "$2" 2>&1
  local code=$?
  local n
  n=$(grep -c 'WARNING: ThreadSanitizer' "$2")
  echo "  $1: 退出码=$code  TSan 报告=$n"
  if grep -q 'unexpected memory mapping' "$2"; then
    echo "    !! ASLR 冲突：试试 sysctl -w vm.mmap_rnd_bits=28 后重跑"
  fi
  if [ "$n" -gt 0 ]; then
    grep -E 'WARNING: ThreadSanitizer|SUMMARY: ThreadSanitizer' "$2" | head -4 | sed 's/^/    /'
  fi
  tail -1 "$2" | sed 's/^/    /'
  echo "$n"
}

cd "$B" || exit 1

echo
echo "===== 第一步：正式结论（正对照关闭）====="
cmake "$SRC" -DGO2_RACE_POSITIVE_CONTROL=OFF >/tmp/go2_tsan_cfg.log 2>&1 \
  || { tail -5 /tmp/go2_tsan_cfg.log; exit 1; }
ninja command_service_test ui_state_thread_test >/tmp/go2_tsan_build.log 2>&1 \
  || { grep -E 'error:' /tmp/go2_tsan_build.log | head -10; exit 1; }
run_one command_service_test /tmp/go2_tsan_cs.log 300 >/dev/null
RACE_REAL=$(run_one ui_state_thread_test /tmp/go2_tsan_thread.log 900 | tail -1)

echo
echo "===== 第二步：正对照（故意注入竞争，必须报出来）====="
cmake "$SRC" -DGO2_RACE_POSITIVE_CONTROL=ON >/tmp/go2_tsan_cfg2.log 2>&1 \
  || { tail -5 /tmp/go2_tsan_cfg2.log; exit 1; }
ninja ui_state_thread_test >/tmp/go2_tsan_build2.log 2>&1 \
  || { grep -E 'error:' /tmp/go2_tsan_build2.log | head -10; exit 1; }
RACE_POS=$(run_one ui_state_thread_test /tmp/go2_tsan_positive.log 900 | tail -1)

# 收尾：把开关恢复默认，免得影响后续构建
cmake "$SRC" -DGO2_RACE_POSITIVE_CONTROL=OFF >/dev/null 2>&1

echo
echo "===== 结论 ====="
echo "  正式构建的竞争报告数 = $RACE_REAL   （期望 0）"
echo "  正对照的竞争报告数   = $RACE_POS   （期望 >= 1）"
if [ "$RACE_REAL" -eq 0 ] && [ "$RACE_POS" -ge 1 ]; then
  echo "  [PASS] TSan 有效，且当前代码在 4 线程并发压力下无数据竞争"
  exit 0
fi
echo "  [FAIL] 需要人工确认（0 报告可能是工具没生效，或确实还有竞争）"
exit 1
