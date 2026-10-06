#!/usr/bin/env bash
# ============================================================================
# 界面视觉回归基线（对应 docs/optimization_plan.md M3-3）
#
# 为什么需要：`ui.cpp` 要拆成多个 TU（M3-2），拆分过程**不能改变界面长相** ——
# 但 ImGui 的绘制顺序改动经常是"编译通过、界面悄悄变了"。所以先固化几个断点尺寸的
# 截图基线，拆完再逐像素比一遍。
#
# 覆盖的断点（阈值见 ui/layout.hpp：宽 600/900/1280，高 480/800）：
#     large-tall         1440x880   默认窗口尺寸（宽 L 档 + 高 T 档）
#     expanded-regular   1100x720   小桌面 / 横屏平板
#     medium-short        820x460   窄窗 + 矮屏（顶栏拆两行、摇杆半径压到 70）
#     compact-short       540x440   最窄档（品牌位让给按钮）
#
# 用法：
#     bash scripts/screenshot.sh capture    # 抓当前界面 → 覆盖/建立基线
#     bash scripts/screenshot.sh compare    # 抓当前 + 与基线逐像素比差（有差异退出码 1）
#     bash scripts/screenshot.sh            # 默认 compare
#
# 说明：
#   · 比差有容差（单通道差 > 8 才算不同、不同像素占比 <= 0.5%），因为不同 GPU/驱动
#     渲染同一界面的抗锯齿会有极小差异；真正的布局改动远超这个量级。
#   · 需要图形环境（Linux 桌面 / WSLg）。无显示环境时脚本会明确报出来而不是假装通过。
# ============================================================================
set -u
cd "$(dirname "$0")/.." || exit 1
ROOT=$(pwd)
OUT="$ROOT/client/build/shots"
BASE="$ROOT/docs/screenshots"
MODE="${1:-compare}"

# 尺寸:名称
CASES=("1440x880:large-tall" "1100x720:expanded-regular" "820x460:medium-short" "540x440:compact-short")

# 构建目录两种叫法都认：手工 `-B build`，或 `--preset linux`（→ build/linux）
BIN=""
for cand in "$ROOT/client/build/linux/go2_remote" "$ROOT/client/build/go2_remote"; do
  if [ -x "$cand" ]; then BIN="$cand"; break; fi
done
if [ -z "$BIN" ]; then
  echo "找不到 go2_remote —— 先构建："
  echo "    cmake --preset linux && cmake --build client/build/linux --parallel 2"
  exit 2
fi
echo "使用可执行文件: $BIN"
mkdir -p "$OUT" "$BASE"

capture() {  # $1=WxH $2=名称 → 输出 $OUT/<名称>.ppm
  local size="$1" name="$2"
  local w="${size%x*}" h="${size#*x}"
  rm -f "$OUT/$name.ppm"
  ( cd "$ROOT/client" && GO2_WIN_W="$w" GO2_WIN_H="$h" GO2_SHOT=1 GO2_SHOT_EXIT=1 \
      GO2_SHOT_PATH="$OUT/$name.ppm" timeout 60 "$BIN" ) > "$OUT/$name.log" 2>&1
  local code=$?
  if [ ! -s "$OUT/$name.ppm" ]; then
    echo "  [FAIL] $name（$size）没抓到帧，退出码=$code"
    tail -3 "$OUT/$name.log" | sed 's/^/         /'
    return 1
  fi
  grep -E '\[SHOT\]' "$OUT/$name.log" | sed 's/^/  /'
  return 0
}

echo "== 抓帧（4 个断点尺寸）=="
FAILED=0
for c in "${CASES[@]}"; do
  capture "${c%%:*}" "${c##*:}" || FAILED=$((FAILED + 1))
done
if [ "$FAILED" -gt 0 ]; then
  echo
  echo "[FAIL] $FAILED 个尺寸没抓到帧 —— 大概率是没有图形环境（无 DISPLAY/WSLg）"
  echo "       （本机若在无头环境跑，用 ctest 那套离线自测代替，不要假装这里通过）"
  exit 1
fi

echo
if [ "$MODE" = "capture" ]; then
  echo "== 写入基线 → $BASE =="
  for c in "${CASES[@]}"; do
    name="${c##*:}"
    python3 "$ROOT/scripts/imgtool.py" ppm2png "$OUT/$name.ppm" "$BASE/$name.png" \
      || .venv/bin/python "$ROOT/scripts/imgtool.py" ppm2png "$OUT/$name.ppm" "$BASE/$name.png"
  done
  echo
  echo "[PASS] 基线已更新：$(ls "$BASE" | tr '\n' ' ')"
  exit 0
fi

echo "== 与基线逐像素比差 =="
if [ ! -f "$BASE/large-tall.png" ]; then
  echo "还没有基线。先跑： bash scripts/screenshot.sh capture"
  exit 2
fi
DIFFS=0
for c in "${CASES[@]}"; do
  name="${c##*:}"
  if [ ! -f "$BASE/$name.png" ]; then
    echo "  [FAIL] 缺基线 $name.png"
    DIFFS=$((DIFFS + 1))
    continue
  fi
  # 基线是 PNG（自上而下），新帧是 PPM（GL 自下而上）—— imgtool 内部会统一方向
  if python3 "$ROOT/scripts/imgtool.py" diff "$BASE/$name.png" "$OUT/$name.ppm" \
       "$OUT/$name.diff.png"; then
    echo "  [ok]   $name 与基线一致"
  else
    echo "  [DIFF] $name 与基线不一致（差异图：$OUT/$name.diff.png）"
    DIFFS=$((DIFFS + 1))
  fi
done

echo
if [ "$DIFFS" -eq 0 ]; then
  echo "[PASS] 4 个断点尺寸与基线一致（视觉无回归）"
  exit 0
fi
echo "[FAIL] $DIFFS 个尺寸与基线不一致 —— 确认是预期改动后跑 capture 更新基线"
exit 1
