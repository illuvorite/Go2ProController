#!/usr/bin/env python3
"""界面截图的转换与比对（纯标准库，无第三方依赖）。

为什么不用 Pillow：本仓库的 Python 侧只保证标准库可用（`requirements.txt` 里没有
图像库），而这里要做的事很少 —— 读 PPM/PNG、写 PNG、逐像素比差。用 zlib + struct
就能做完，不必为此往部署环境里加依赖。

坐标约定（容易搞错，写清楚）：
  · `glReadPixels` 读回来的帧缓冲是**自下而上**的（OpenGL 原点在左下角），
    所以 GO2_SHOT 写出的 PPM 在"人类视角"下是**上下颠倒**的；
  · 本模块内部统一用**自上而下**（人类视角）的字节序，读 PPM 时翻转一次；
  · 写 PNG 时也按自上而下写 —— 于是导出的 PNG 方向正确。

子命令：
    ppm2png <in.ppm> <out.png>              转成 PNG（方向校正）
    diff <A> <B> [out_diff.png]             逐像素比差；超阈值时退出码 1
    info <img>                              打印尺寸/大小
"""

import struct
import sys
import zlib


# ---------------------------------------------------------------- 读
def read_ppm(path):
    """读 P6 PPM。返回 (w, h, rgb)，rgb 已翻转为**自上而下**。"""
    with open(path, "rb") as f:
        data = f.read()
    fields = []
    i = 0
    while len(fields) < 4:
        while data[i:i + 1].isspace():
            i += 1
        if data[i:i + 1] == b"#":
            while data[i:i + 1] != b"\n":
                i += 1
            continue
        j = i
        while not data[j:j + 1].isspace():
            j += 1
        fields.append(data[i:j])
        i = j
    i += 1  # 头之后的分隔空白
    w, h = int(fields[1]), int(fields[2])
    raw = data[i:i + w * h * 3]
    # GL 读回是自下而上 → 翻成自上而下
    out = bytearray(len(raw))
    row = w * 3
    for y in range(h):
        out[y * row:(y + 1) * row] = raw[(h - 1 - y) * row:(h - y) * row]
    return w, h, bytes(out)


def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def read_png(path):
    """读 8bit RGB/RGBA、非隔行 PNG。返回 (w, h, rgb)。"""
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("不是 PNG: " + path)
    i = 8
    w = h = depth = ctype = interlace = None
    idat = bytearray()
    while i < len(data):
        (ln,) = struct.unpack(">I", data[i:i + 4])
        tag = data[i + 4:i + 8]
        payload = data[i + 8:i + 8 + ln]
        i += 12 + ln
        if tag == b"IHDR":
            w, h, depth, ctype, _comp, _filt, interlace = struct.unpack(">IIBBBBB", payload)
        elif tag == b"IDAT":
            idat += payload
        elif tag == b"IEND":
            break
    if depth != 8 or interlace != 0 or ctype not in (2, 6):
        raise ValueError("只支持 8bit 非隔行 RGB/RGBA PNG")
    nch = 3 if ctype == 2 else 4
    raw = zlib.decompress(bytes(idat))
    stride = w * nch
    out = bytearray(w * h * 3)
    prev = bytearray(stride)
    pos = 0
    for y in range(h):
        ft = raw[pos]
        pos += 1
        line = bytearray(raw[pos:pos + stride])
        pos += stride
        if ft == 1:
            for x in range(nch, stride):
                line[x] = (line[x] + line[x - nch]) & 0xFF
        elif ft == 2:
            for x in range(stride):
                line[x] = (line[x] + prev[x]) & 0xFF
        elif ft == 3:
            for x in range(stride):
                a = line[x - nch] if x >= nch else 0
                line[x] = (line[x] + ((a + prev[x]) >> 1)) & 0xFF
        elif ft == 4:
            for x in range(stride):
                a = line[x - nch] if x >= nch else 0
                c = prev[x - nch] if x >= nch else 0
                line[x] = (line[x] + _paeth(a, prev[x], c)) & 0xFF
        elif ft != 0:
            raise ValueError("未知 PNG 行过滤类型 %d" % ft)
        row = y * w * 3
        for x in range(w):
            out[row + x * 3:row + x * 3 + 3] = line[x * nch:x * nch + 3]
        prev = line
    return w, h, bytes(out)


def load_any(path):
    """按扩展名自动选读取器，统一返回自上而下的 RGB。"""
    if path.lower().endswith(".ppm"):
        return read_ppm(path)
    if path.lower().endswith(".png"):
        return read_png(path)
    raise ValueError("只支持 .ppm / .png: " + path)


# ---------------------------------------------------------------- 写
def write_png(path, w, h, rgb):
    raw = bytearray()
    for y in range(h):
        raw.append(0)  # filter: none
        raw += rgb[y * w * 3:(y + 1) * w * 3]

    def chunk(tag, payload):
        c = struct.pack(">I", len(payload)) + tag + payload
        return c + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 6))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


# ---------------------------------------------------------------- 命令
def cmd_ppm2png(argv):
    if len(argv) < 2:
        print("用法: imgtool.py ppm2png <in.ppm> <out.png>")
        return 1
    w, h, rgb = read_ppm(argv[0])
    write_png(argv[1], w, h, rgb)
    print("[imgtool] %s → %s (%dx%d)" % (argv[0], argv[1], w, h))
    return 0


def cmd_info(argv):
    if not argv:
        print("用法: imgtool.py info <img>")
        return 1
    w, h, _ = load_any(argv[0])
    import os
    print("%s  %dx%d  %.1f KB" % (argv[0], w, h, os.path.getsize(argv[0]) / 1024.0))
    return 0


def cmd_diff(argv):
    """逐像素比差。

    阈值说明：跨 GPU/驱动渲染同一个界面会有极小的抗锯齿差异，所以不要求逐字节相同；
    默认把"任一通道差 > 8"的像素算作**不同**，并要求不同像素占比 <= 0.5%。
    真正改动布局/颜色时，占比会远超这个数，不会被漏掉。
    """
    if len(argv) < 2:
        print("用法: imgtool.py diff <A> <B> [out_diff.png] [--tol N] [--ratio R]")
        return 2
    a_path, b_path = argv[0], argv[1]
    out_path = None
    tol, ratio_limit = 8, 0.005
    rest = argv[2:]
    i = 0
    while i < len(rest):
        if rest[i] == "--tol":
            tol = int(rest[i + 1]); i += 2
        elif rest[i] == "--ratio":
            ratio_limit = float(rest[i + 1]); i += 2
        else:
            out_path = rest[i]; i += 1

    wa, ha, ra = load_any(a_path)
    wb, hb, rb = load_any(b_path)
    if (wa, ha) != (wb, hb):
        print("[imgtool] 尺寸不同：%s=%dx%d  %s=%dx%d → 视为不一致"
              % (a_path, wa, ha, b_path, wb, hb))
        return 1

    diff = bytearray(wa * ha * 3)
    changed = 0
    total = wa * ha
    maxd = 0
    for p in range(0, len(ra), 3):
        d = max(abs(ra[p] - rb[p]), abs(ra[p + 1] - rb[p + 1]), abs(ra[p + 2] - rb[p + 2]))
        if d > maxd:
            maxd = d
        if d > tol:
            changed += 1
            diff[p] = 255          # 差异点标红
            diff[p + 1] = 40
            diff[p + 2] = 40
        else:
            g = 24                 # 相同点压暗，方便一眼看到红块
            diff[p] = diff[p + 1] = diff[p + 2] = g
    ratio = changed / float(total) if total else 0.0
    print("[imgtool] %dx%d  不同像素 %d / %d = %.3f%%  最大通道差 %d  (tol=%d, 上限 %.2f%%)"
          % (wa, ha, changed, total, ratio * 100.0, maxd, tol, ratio_limit * 100.0))
    if out_path:
        write_png(out_path, wa, ha, bytes(diff))
        print("[imgtool] 差异图 → %s（红色=不同）" % out_path)
    return 0 if ratio <= ratio_limit else 1


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    cmd = sys.argv[1]
    argv = sys.argv[2:]
    if cmd == "ppm2png":
        return cmd_ppm2png(argv)
    if cmd == "diff":
        return cmd_diff(argv)
    if cmd == "info":
        return cmd_info(argv)
    print("未知子命令: " + cmd)
    return 2


if __name__ == "__main__":
    sys.exit(main())
