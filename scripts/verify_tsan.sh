#!/usr/bin/env bash
# ============================================================================
# ThreadSanitizer 验证（对应 docs/optimization_plan.md 指标 2）
#
# 做什么：
#   1. TSan 构建里跑并发相关自测（command_service_test / ui_state_thread_test），期望 0 条 race；
#   2. **正对照**：GO2_PROBE=tsan 跑 sanitizer_probe_test（故意无同步竞争），必须报出来。
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
# 绕法：setarch -R（只对本进程关 ASLR，不改系统设置）—— 脚本已内置。
#
# 耗时提示：TSan 树要单独编一遍 libdatachannel 等依赖（首次几分钟）；
#   之后是增量的。并发压测的迭代次数用 GO2_STRESS_ITERS 控制（插桩后慢 10~20 倍）。
# ============================================================================
set -u
cd "$(dirname "$0")/.." || exit 1
SRC=$(pwd)/client
B=$SRC/build/linux-tsan
ARCH=$(uname -m)
ITERS="${GO2_STRESS_ITERS:-200}"
FAIL=0

if [ ! -d "$B" ]; then
  echo "没有 $B。先执行："
  echo "    cmake --preset linux-tsan && cmake --build client/build/linux-tsan --parallel 2"
  exit 2
fi

echo "mmap_rnd_bits = $(cat /proc/sys/vm/mmap_rnd_bits 2>/dev/null)   arch = $ARCH   迭代数 = $ITERS"

# ---- 先增量构建：本脚本必须跑**当前源码**的二进制 ----
# 踩过：TSan 树里留着上一次用 -DGO2_RACE_POSITIVE_CONTROL=ON 编出来的 ui_state_thread_test，
# 于是"正式场景"报出 1 条 race —— 其实是旧正对照残留，白查半天。
# 现在每次先 ninja（增量，通常几秒），杜绝跑陈旧产物。
echo
echo "== 增量构建 =="
if ( cd "$B" && ninja command_service_test ui_state_thread_test sanitizer_probe_test ) \
     >/tmp/go2_tsan_build.log 2>&1; then
  echo "  构建成功"
else
  grep -E 'error:' /tmp/go2_tsan_build.log | head -10
  exit 1
fi

# $1 = 可执行名, $2 = 环境变量前缀
run_one() {
  local name="$1" env="$2"
  local log="/tmp/go2_tsan_${name}.log"
  echo
  echo "== $name =="
  local code=0
  # shellcheck disable=SC2086
  ( cd "$B" && env $env TSAN_OPTIONS=halt_on_error=0:second_deadlock_stack=1 \
      timeout 900 setarch "$ARCH" -R "./$name" ) > "$log" 2>&1 || code=$?
  local n
  n=$(grep -c 'WARNING: ThreadSanitizer' "$log")
  echo "  退出码=$code  TSan 报告=$n"
  if grep -q 'unexpected memory mapping' "$log"; then
    echo "    !! ASLR 冲突：试 sysctl -w vm.mmap_rnd_bits=28"
    FAIL=$((FAIL + 1))
    return
  fi
  grep -E 'WARNING: ThreadSanitizer|SUMMARY: ThreadSanitizer' "$log" | head -4 | sed 's/^/    /'
  tail -1 "$log" | sed 's/^/    /'
  echo "$n"
}

# ---- 第一步：正式结论（期望 0 条）----
echo
echo "===== 正式场景（期望 0 条 race）====="
N1=$(run_one command_service_test "GO2_STRESS_ITERS=$ITERS" | tail -1)
N2=$(run_one ui_state_thread_test "GO2_STRESS_ITERS=$ITERS" | tail -1)
[ "${N1:-0}" -eq 0 ] && [ "${N2:-0}" -eq 0 ] || FAIL=$((FAIL + 1))

# ---- 第二步：正对照（期望 >= 1 条）----
echo
echo "===== 正对照（故意制造竞争，期望 >= 1 条）====="
N3=$(run_one sanitizer_probe_test "GO2_PROBE=tsan" | tail -1)
[ "${N3:-0}" -ge 1 ] || FAIL=$((FAIL + 1))

echo
echo "=================== 结论 ==================="
echo "  正式场景报告数 = command_service_test:$N1  ui_state_thread_test:$N2   （期望 0）"
echo "  正对照报告数   = $N3   （期望 >= 1）"
if [ "$FAIL" -eq 0 ]; then
  echo "  [PASS] TSan 有效，且当前代码在并发压力下无数据竞争"
  exit 0
fi
echo "  [FAIL] 见 /tmp/go2_tsan_*.log"
exit 1
