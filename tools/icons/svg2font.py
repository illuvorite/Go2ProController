#!/usr/bin/env python3
"""把"天树探界遥控"的 SVG 动作剪影转成 **TTF 图标字体**（私有码位区 U+E000 起）。

为什么转字体而不是直接用 SVG/PNG：
  · C++（ImGui 1.92 动态字体）与网页（@font-face）两边都只能吃**字体**；
  · 字体里的图标能跟随文字颜色、单色渲染、自动跟随字号 —— 和现有 Phosphor 用法完全一致，
    界面层（theme.cpp / style.css）几乎不用改，只是多加载一份字体。

实现：用 fontTools 自带的 `SVGPathPen`（它内部就把 SVG path 的 M/L/C/Q/A 解析成
pen 命令），配 `TransformPen` 做 viewBox→em 映射。**不依赖 svgpathtools**。

依赖：fontTools

用法：
    python tools/icons/svg2font.py <svg目录> <输出ttf> [字体名]

注意（授权）：这些 SVG 来自第三方 App（宇树"天树探界遥控" APK），属其专有美术资源。
**适合本地自用 / 内部部署**；若本项目要对外开源分发，必须替换为自绘或已授权图标。
"""
import os
import re
import sys
import xml.etree.ElementTree as ET

from fontTools import cu2qu
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.recordingPen import RecordingPen
from fontTools.pens.transformPen import TransformPen
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.svgLib.path import parse_path

UPM = 1024          # 与 Phosphor 一致，视觉比例才接近
FIRST_CP = 0xE100  # 私有码位：避开 U+E000 段（留给 Phosphor-Bold 等）
PAD = 40            # 四周留白，剪影通常画满 viewBox
MAX_CU2QU_ERR = 1.0  # 三次→二次的最大允许偏差（1004 UPM 下肉眼不可见）

_SVG_NUM = re.compile(r"[-+]?(?:\d*\.\d+|\d+\.?)(?:[eE][-+]?\d+)?")
_SVG_CMD = re.compile(r"([MmLlHhVvCcSsQqTtAaZz])([^MmLlHhVvCcSsQqTtAaZz]*)")


def _nums(s):
    return [float(x) for x in _SVG_NUM.findall(s)]


def parse_transform(t):
    """把 SVG transform 串解析成 (a,b,c,d,e,f)；支持 translate/scale/rotate/matrix/skewX|Y。"""
    import math
    m = [1.0, 0.0, 0.0, 1.0, 0.0, 0.0]

    def mul(x):
        a1, b1, c1, d1, e1, f1 = m
        a2, b2, c2, d2, e2, f2 = x
        m[:] = [a1 * a2 + c1 * b2, b1 * a2 + d1 * b2,
                a1 * c2 + c1 * d2, b1 * c2 + d1 * d2,
                a1 * e2 + c1 * f2 + e1, b1 * e2 + d1 * f2 + f1]

    for name, args in re.findall(r"(\w+)\s*\(([^)]*)\)", t or ""):
        v = _nums(args)
        if name == "translate":
            mul([1, 0, 0, 1, v[0], v[1] if len(v) > 1 else 0])
        elif name == "scale":
            sx = v[0]
            sy = v[1] if len(v) > 1 else sx
            mul([sx, 0, 0, sy, 0, 0])
        elif name == "rotate":
            a = math.radians(v[0])
            cos, sin = math.cos(a), math.sin(a)
            if len(v) == 3:
                mul([1, 0, 0, 1, v[1], v[2]])
                mul([cos, sin, -sin, cos, 0, 0])
                mul([1, 0, 0, 1, -v[1], -v[2]])
            else:
                mul([cos, sin, -sin, cos, 0, 0])
        elif name == "matrix" and len(v) == 6:
            mul(v)
        elif name == "skewX":
            mul([1, 0, math.tan(math.radians(v[0])), 1, 0, 0])
        elif name == "skewY":
            mul([1, math.tan(math.radians(v[0])), 0, 1, 0, 0])
    return m


def compose(outer, inner):
    """outer ∘ inner（先把 inner 施加到点上，再施加 outer）。"""
    if not inner:
        return outer
    if not outer:
        return inner
    a1, b1, c1, d1, e1, f1 = outer
    a2, b2, c2, d2, e2, f2 = inner
    return [a1 * a2 + c1 * b2, b1 * a2 + d1 * b2,
            a1 * c2 + c1 * d2, b1 * c2 + d1 * d2,
            a1 * e2 + c1 * f2 + e1, b1 * e2 + d1 * f2 + f1]


def collect(svg_path):
    """收集 (path_d, transform)；只处理 path / g，忽略 mask·use（纯裁剪矩形）。"""
    root = ET.parse(svg_path).getroot()
    vb = _nums(root.get("viewBox") or "")
    if len(vb) != 4:
        raise SystemExit("%s 缺 viewBox" % svg_path)
    out = []

    def walk(node, tf):
        for ch in node:
            tag = ch.tag.split("}")[-1]
            ctf = compose(tf, parse_transform(ch.get("transform"))) if ch.get("transform") else tf
            if tag == "path" and ch.get("d"):
                out.append((ch.get("d"), ctf))
            elif tag in ("g", "svg"):
                walk(ch, ctf)

    walk(root, None)
    return vb, out


def cu2qu_convert(rec, out_pen, max_err=1.0):
    """把 RecordingPen 录到的**三次**贝塞尔降阶为**二次**并输出到 out_pen。

    ★ 为什么要降阶：SVG 的曲线是三次(C)，而传统 TrueType `glyf` 表只存二次(Q)。
      曾试过把 head.glyphDataFormat 设成 1（TrueType 2.0 全曲线）—— stb_truetype(ImGui)
      能正常渲染，**但浏览器的 OTS 字体校验器会直接拒绝加载**，网页端全变缺字方框。
      降阶误差在 1000+ UPM 下肉眼不可见，换来全平台兼容，这笔买卖值。
    """
    curves = []          # 每个三次曲线 [(p0,c1,c2,p1), ...]
    idx = []             # 每条曲线对应录制序列里的操作下标（用于原样回放非曲线操作）
    ops = rec.value
    # ★ RecordingPen 记的 curveTo 只有 3 个点（c1,c2,p1）——起点沿用「当前点」，
    #   而 cu2qu 需要完整的 4 点三次曲线。这里显式把当前点补到最前面。
    cur_pt = (0.0, 0.0)
    for op, args in ops:
        if op == "curveTo":
            curves.append([cur_pt] + list(args))
            cur_pt = tuple(args[-1])
            idx.append(("curve", len(idx)))
        else:
            if op == "moveTo" and args:
                cur_pt = tuple(args[0])
            elif op == "lineTo" and args:
                cur_pt = tuple(args[-1])
            elif op == "qCurveTo" and args:
                cur_pt = tuple(args[-1])
            idx.append(("op", (op, args)))

    if not curves:
        for op, args in ops:
            getattr(out_pen, op)(*args)
        return

    splines = []
    for cv in curves:
        try:
            # ★ 必须**逐条**调用：fontTools 4.65 的 curves_to_quadratic 批量接口
            #   在只给 1 条曲线时会 IndexError（内部按 spline 索引取子曲线）。
            #   逐条调用是最稳的兼容写法。
            splines.append(cu2qu.curves_to_quadratic([cv], [max_err])[0])
        except Exception:
            splines.append(None)  # 标记失败

    # 回放：非曲线操作原样输出；曲线位置换成 qCurveTo（失败的退回三次）
    # ★ endPath 必须跳过：TTGlyphPen 的 endPath() 内部会再调一次 closePath()，
    #   与我们显式回放的 closePath 叠加就会在空轮廓上崩。
    # ★ 遇到「新的 moveTo」时必须先把上一条 contour 收尾（closePath）——
    #   SVG 里一条 path 可以有多条子路径，而 TTGlyphPen 要求每条 contour 显式闭合。
    si = 0
    open_contour = False
    for kind, payload in idx:
        if kind == "op":
            op, args = payload
            if op == "endPath":
                continue
            if op == "moveTo":
                if open_contour:
                    out_pen.closePath()
                open_contour = True
            elif op == "closePath":
                out_pen.closePath()
                open_contour = False
            else:
                getattr(out_pen, op)(*args)
        else:
            spline = splines[si]
            ci = si
            si += 1
            if spline is None:
                # 兜底：降阶失败 → 原样输出三次曲线（注意要用 3 点形式，起点由当前点承担）
                out_pen.curveTo(*curves[ci][1:])
                continue
            pts = [tuple(p) for p in spline]
            # TrueType qCurveTo(*points)：首点是**当前点**（已由 moveTo/lineTo 设好），
            # 不能重复传入 —— 传了会多出一个控制点、把形状画错。
            # 传「控制点… 终点」即可。
            out_pen.qCurveTo(*pts[1:])

    # 收尾：最后一条 contour 若还没闭合，补上（TTGlyphPen 要求每条显式闭合）
    # ★ 不要调 endPath()：它内部会再调一次 closePath()，重复收尾会崩。
    if open_contour:
        out_pen.closePath()


def build(svg_dir, out_ttf, font_name="TianshuGo2"):
    files = sorted(f for f in os.listdir(svg_dir) if f.lower().endswith(".svg"))
    if not files:
        raise SystemExit("%s 里没有 svg" % svg_dir)

    glyph_order = [".notdef"]
    cmap, glyf, hmtx, names = {}, {}, {}, {}

    for i, fn in enumerate(files):
        cp = FIRST_CP + i
        gname = "uni%04X" % cp
        try:
            (vx, vy, vw, vh), shapes = collect(os.path.join(svg_dir, fn))
        except SystemExit:
            continue
        if vw <= 0 or vh <= 0:
            continue

        scale = (UPM - 2 * PAD) / max(vw, vh)
        dx = (UPM - vw * scale) / 2.0
        dy = (UPM - vh * scale) / 2.0
        base_tf = [scale, 0.0, 0.0, -scale, dx - vx * scale, dy + vh * scale]

        # 1) 官方 SVG 解析 → 录制成 pen 调用序列（此时是三次贝塞尔）
        rec = RecordingPen()
        for d, tf in shapes:
            # ★ 用 fontTools **官方** SVG 路径解析器（支持 M/L/H/V/C/S/Q/T/A/Z 全套），
            #   外面套 TransformPen 做 viewBox→em 映射。
            #   别自己手写 path 解析：弧线 A 与平滑曲线 S/T 的隐式控制点极易出错。
            parse_path(d, TransformPen(rec, compose(base_tf, tf)))

        # 2) 三次 → 二次降阶：传统 TrueType glyf 只存二次贝塞尔(Q)
        pen = TTGlyphPen(None)
        cu2qu_convert(rec, pen, max_err=MAX_CU2QU_ERR)

        glyph_order.append(gname)
        cmap[cp] = gname
        glyf[gname] = pen.glyph()
        hmtx[gname] = (int(round(vw * scale)), max(0, int(base_tf[4])))
        names[cp] = fn[:-4]

    # .notdef 必须有实际 glyph 对象（哪怕是空的），否则 glyf 编译时 KeyError
    nd = TTGlyphPen(None)
    nd.moveTo((0, 0))
    nd.lineTo((0, 0))
    nd.closePath()
    glyf[".notdef"] = nd.glyph()
    hmtx[".notdef"] = (int(UPM * 0.5), 0)

    fb = FontBuilder(UPM, isTTF=True)
    fb.setupGlyphOrder(glyph_order)
    fb.setupCharacterMap(cmap)
    fb.setupGlyf(glyf)
    fb.setupHorizontalMetrics(hmtx)
    fb.setupHorizontalHeader(ascent=800, descent=-224)
    fb.setupNameTable({"familyName": font_name, "styleName": "Regular",
                       "fullName": font_name, "psName": font_name})
    fb.setupOS2(sTypoAscender=800, sTypoDescender=-224, usWinAscent=800, usWinDescent=224)
    fb.setupPost()
    os.makedirs(os.path.dirname(os.path.abspath(out_ttf)), exist_ok=True)
    fb.save(out_ttf)

    tsv = os.path.splitext(out_ttf)[0] + ".codepoints.tsv"
    with open(tsv, "w", encoding="utf-8", newline="\n") as f:
        f.write("# 由 tools/icons/svg2font.py 生成：SVG 文件名 → 字体私有码位\n")
        for cp in sorted(names):
            f.write("U+%04X\t%s\n" % (cp, names[cp]))

    print("已生成 %s（%d 个图标，码位 U+%04X 起）" % (out_ttf, len(cmap), FIRST_CP))
    print("对照表：%s" % tsv)


if __name__ == "__main__":
    d = sys.argv[1] if len(sys.argv) > 1 else "reports/_tianshu_icons"
    o = sys.argv[2] if len(sys.argv) > 2 else "client/assets/fonts/TianshuGo2.ttf"
    n = sys.argv[3] if len(sys.argv) > 3 else "TianshuGo2"
    build(d, o, n)
