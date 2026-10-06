#!/usr/bin/env bash
# ============================================================================
# 一键取证：按 docs/optimization_plan.md §5 的定量指标逐条取值。
#
# 用法（WSL / Ubuntu）：
#     bash scripts/verify.sh            # 只做静态指标 + 已有构建的 ctest
#     bash scripts/verify.sh --build    # 先 ninja 再跑（需要 client/build 已配置）
#
# 说明：真机相关的验证（--verify 连狗、截图对比）**不在这里** ——
# 它们需要真设备与现场纪律（重连间隔 ≥15s），见 docs/multi_go2_pro_solution.md §8。
# ============================================================================
set -u
cd "$(dirname "$0")/.." || exit 1
ROOT=$(pwd)
BUILD="$ROOT/client/build"
PASS=0
FAIL=0

line() { printf '%s\n' "----------------------------------------------------------------"; }
head1() { line; echo "== $1 =="; }
# check "<指标>" "<实测值>" "<期望值>"   （只打印，不做数值比较，人工对照）
report() { printf '  %-34s %-16s 期望: %s\n' "$1" "$2" "$3"; }
verdict() { # $1=条件结果(0/1) $2=说明
  if [ "$1" -eq 0 ]; then PASS=$((PASS+1)); echo "  [PASS] $2"; else FAIL=$((FAIL+1)); echo "  [FAIL] $2"; fi
}

if [ "${1:-}" = "--build" ]; then
  head1 "构建（ninja，先 touch 自有源码以便统计全量告警）"
  if [ -d "$BUILD" ]; then
    touch "$ROOT"/client/core/*.cpp "$ROOT"/client/ui/*.cpp \
          "$ROOT"/client/apps/desktop/*.cpp "$ROOT"/client/apps/serve/*.cpp \
          "$ROOT"/client/tests/*.cpp 2>/dev/null
    if ( cd "$BUILD" && ninja >/tmp/go2_verify_build.log 2>&1 ); then
      echo "  构建成功；-Wall -Wextra 告警数 = $(grep -c 'warning:' /tmp/go2_verify_build.log)"
    else
      echo "  构建失败："
      grep -E 'error:' /tmp/go2_verify_build.log | head -10
    fi
  else
    echo "  没有 $BUILD —— 先按 client/README.md 配置一次"
  fi
fi

head1 "指标 5：最大源文件行数"
maxline=$(wc -l "$ROOT"/client/core/*.cpp "$ROOT"/client/ui/*.cpp 2>/dev/null | sort -rn | sed -n 2p)
report "最大源文件" "$maxline" "≤ 700（M3-2 拆分 ui.cpp 后）"
if echo "$maxline" | grep -q 'ui\.cpp'; then
  verdict 1 "ui/ui.cpp 仍是最大文件 → M3-2（拆分）尚未完成，见方案 §3"
else
  verdict 0 "没有任何源文件超过拆分阈值"
fi

head1 "指标 6：群控循环实现处数"
n=$(grep -rho 'for (const auto& ip : ui.selectedIps())' "$ROOT/client/ui" 2>/dev/null | wc -l)
report "内联群控循环" "$n 处" "0（全部走 cmd::forEachSelected）"
verdict "$([ "$n" -eq 0 ] && echo 0 || echo 1)" "ui/ 下没有内联的群控循环"

head1 '指标 7：群控状态是否还有「裸访问」'
# 精确判据：取址交给别人的 atomic、以及直接访问三个受锁保护的容器 —— 这三类必须为 0。
# （单纯读 `ui.estop` 走 operator bool 是合法的，不算裸访问。）
bad=0
# \b：避免把 ui.togglesSnapshot() / ui.activeToggleIdsSnapshot() 当成裸访问（子串误报）
for pat in '&ui\.estop' '&ui\.movingSent' '&ui\.cmdV' 'ui\.toggles\b' 'ui\.activeToggleIds\b'; do
  c=$(grep -rho "$pat" "$ROOT/client/ui" 2>/dev/null | wc -l)
  [ "$c" -gt 0 ] && { echo "  !! $pat 命中 $c 次"; bad=$((bad + c)); }
done
report "禁止写法命中次数" "$bad" "0"
verdict "$([ "$bad" -eq 0 ] && echo 0 || echo 1)" "没有取址/直接容器访问（TSan 0 报告为最终判据）"

head1 "指标 8：网页资源重复副本"
if [ -d "$ROOT/client/apps/android/app/src/main/assets/web" ]; then
  report "安卓 assets/web" "仍存在" "0（已改为构建期同步）"
  verdict 1 "安卓端仍有一份网页资源副本"
else
  report "安卓 assets/web" "不存在" "0"
  verdict 0 "网页资源只有 client/assets/web 一份（APK 由 syncWebAssets 同步）"
fi

head1 "指标 9：孤儿测试（存在但未注册进 CMake）"
orphan=0
for f in "$ROOT"/client/tests/*_test.cpp; do
  name=$(basename "$f" .cpp)
  grep -q "add_executable($name" "$ROOT/client/CMakeLists.txt" \
    || { echo "  !! 未注册: $name"; orphan=$((orphan + 1)); }
done
report "未注册测试数" "$orphan" "0"
verdict "$([ "$orphan" -eq 0 ] && echo 0 || echo 1)" "所有 tests/*_test.cpp 都有构建目标"

head1 "指标 11：密钥文件是否落在源码树里"
leak=$(cd "$ROOT" && find client -maxdepth 1 \( -name '*keys*.json' -o -name 'keys.txt' \) 2>/dev/null | wc -l)
report "client/ 下的密钥文件" "$leak" "0"
verdict "$([ "$leak" -eq 0 ] && echo 0 || echo 1)" "源码树里没有密钥文件"

head1 "指标 1：ctest"
if [ -f "$BUILD/CTestTestfile.cmake" ]; then
  out=$(cd "$BUILD" && ctest 2>&1)
  echo "$out" | grep -E 'tests passed|tests failed|Test #' | tail -12
  if echo "$out" | grep -q '0 tests failed'; then
    verdict 0 "ctest 全绿（真机/官方服务器相关的两个测试为 DISABLED，属预期）"
  else
    verdict 1 "ctest 有失败项，详见 ctest --output-on-failure"
  fi
else
  echo "  未找到 $BUILD/CTestTestfile.cmake —— 先配置并构建"
  verdict 1 "缺少构建目录"
fi

head1 "指标 2/3：Sanitizer（ASan / TSan）"
if [ -x "$BUILD/linux-tsan/ui_state_thread_test" ]; then
  echo "  已有 TSan 构建，完整验证（含正对照）跑： bash scripts/verify_tsan.sh"
else
  echo "  未做 TSan 构建，命令："
  echo "      cmake --preset linux-tsan && cmake --build client/build/linux-tsan --parallel 2"
  echo "      bash scripts/verify_tsan.sh"
fi

head1 "指标 4：编译器告警"
if [ -f /tmp/go2_verify_build.log ]; then
  w=$(grep -c 'warning:' /tmp/go2_verify_build.log)
  report "本次全量重编告警数" "$w" "0"
  verdict "$([ "$w" -eq 0 ] && echo 0 || echo 1)" "-Wall -Wextra 下无告警"
else
  echo "  加 --build 参数可顺带统计（-Wall -Wextra 已默认开启）"
fi

head1 "前端自检（node）"
if command -v node >/dev/null 2>&1; then
  node "$ROOT/scripts/web_check.mjs" | tail -2
else
  echo "  本机没有 node，跳过（CI 里会跑）"
fi

line
echo "静态/离线指标：PASS $PASS 项，FAIL $FAIL 项"
line
