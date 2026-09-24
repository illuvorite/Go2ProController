#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""用 curl 直下官方 zip 并手工展开成 SDK 布局（绕开 sdkmanager 的 Java 下载器）。

本机实测：sdkmanager 走代理/直连都卡在 0 字节，而 curl 直连 dl.google.com 有 ~1.6MB/s。
产物布局：
  _sdk/platform-tools/            _sdk/platforms/android-34/
  _sdk/build-tools/34.0.0/        _sdk/ndk/26.1.10909125/
每个包会先探测候选 URL（HEAD 200）再下，展开时自动剥掉压缩包里的顶层目录。
"""
import io
import os
import re
import shutil
import subprocess
import sys
import time
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
SDK = os.path.join(HERE, "_sdk")
LOG = os.path.join(SDK, "install.log")
BASE = "https://dl.google.com/android/repository/"

# (目标相对路径, 候选 zip 名)；AUTO:<package path> 表示从官方 repository XML 里查真实文件名
PACKAGES = [
    ("platform-tools", ["platform-tools-latest-windows.zip"]),
    ("platforms/android-34", ["AUTO:platforms;android-34"]),
    ("build-tools/34.0.0", ["build-tools_r34-windows.zip", "build-tools_r34.0.0-windows.zip"]),
    ("ndk/26.1.10909125", ["android-ndk-r26b-windows.zip"]),
]


def discover_zip(package_path):
    """从 dl.google.com 的 repository2-3.xml 里查某个包的 zip 文件名（最可靠）"""
    xml = os.path.join(SDK, "repo.xml")
    if not os.path.isfile(xml) or os.path.getsize(xml) < 10000:
        log("   取官方仓库索引 repository2-3.xml")
        subprocess.run(["curl.exe", "-sL", "--max-time", "90", "-o", xml,
                        BASE + "repository2-3.xml"])
    try:
        text = io.open(xml, encoding="utf-8", errors="replace").read()
    except Exception:  # noqa: BLE001
        return None
    idx = text.find('path="%s"' % package_path)
    if idx < 0:
        return None
    seg = text[idx:idx + 6000]
    for m in re.finditer(r"<url>([^<]+\.zip)</url>", seg):
        name = m.group(1)
        if "windows" in name or "windows" not in name:
            return name
    return None

_logf = None


def log(msg):
    global _logf
    line = "[%s] %s" % (time.strftime("%H:%M:%S"), msg)
    print(line, flush=True)
    if _logf is None:
        os.makedirs(SDK, exist_ok=True)
        _logf = io.open(LOG, "a", encoding="utf-8")
    _logf.write(line + "\n")
    _logf.flush()


def http_code(url):
    p = subprocess.run(["curl.exe", "-sIL", "--max-time", "25", "-o", "NUL", "-w", "%{http_code}",
                        url], capture_output=True, text=True)
    return (p.stdout or "").strip().split()[-1:] or [""]


def pick_url(names):
    for n in names:
        code = http_code(BASE + n)[0]
        if code == "200":
            log("   命中 %s" % n)
            return BASE + n, os.path.join(SDK, "_zips", n)
    return None, None


def download(url, dst):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    if os.path.isfile(dst) and os.path.getsize(dst) > 1024 * 1024:
        log("   已存在：%s" % os.path.basename(dst))
        return dst
    log("   下载 %s" % os.path.basename(dst))
    t0 = time.time()
    p = subprocess.Popen(["curl.exe", "-L", "--fail", "--silent", "--show-error", "--retry", "3",
                          "--connect-timeout", "30", "-o", dst, url])
    while p.poll() is None:
        time.sleep(3)
        if os.path.isfile(dst):
            mb = os.path.getsize(dst) / 1048576.0
            log("      ... %6.1f MB  (%.2f MB/s)" % (mb, mb / max(0.1, time.time() - t0)))
    if p.returncode != 0:
        raise RuntimeError("下载失败 %s" % url)
    log("   完成 %.1f MB / %.0fs" % (os.path.getsize(dst) / 1048576.0, time.time() - t0))
    return dst


def extract(zip_path, target):
    """解压到 target（自动剥掉压缩包里的顶层目录）。

    注意：Windows 上 os.replace() 重命名目录可能被文件占用/杀软打断，
    这里改用 extractall → shutil.move，并对每一步单独记日志，便于定位卡点。
    """
    if os.path.isfile(os.path.join(target, "source.properties")) or \
            os.path.isfile(os.path.join(target, "android.jar")):
        log("   已解压好，跳过：%s" % target)
        return
    part = target + ".part"
    log("   解压 %s（%.1f MB）→ %s" % (os.path.basename(zip_path),
                                     os.path.getsize(zip_path) / 1048576.0, target))
    for d in (part, target):
        if os.path.isdir(d):
            log("   清理旧目录 %s" % os.path.basename(d))
            shutil.rmtree(d, ignore_errors=True)
    os.makedirs(part, exist_ok=True)
    t0 = time.time()
    with zipfile.ZipFile(zip_path) as z:
        z.extractall(part)
    log("   解压完成 %.0fs，正在整理目录 ..." % (time.time() - t0))
    entries = [e for e in os.listdir(part) if not e.startswith("__MACOSX")]
    src = part
    if len(entries) == 1 and os.path.isdir(os.path.join(part, entries[0])):
        src = os.path.join(part, entries[0])
    os.makedirs(os.path.dirname(target), exist_ok=True)
    shutil.move(src, target)
    shutil.rmtree(part, ignore_errors=True)
    log("   就绪：%s" % target)


def main():
    os.makedirs(os.path.join(SDK, "_zips"), exist_ok=True)
    log("=== 手工安装 SDK 组件（curl 直连 dl.google.com）===")
    for rel, names in PACKAGES:
        target = os.path.join(SDK, rel.replace("/", os.sep))
        log("→ %s" % rel)
        # 只有存在真实标记文件才算装好（sdkmanager 中断后会留空壳目录，不能用"目录非空"判断）
        if os.path.isfile(os.path.join(target, "source.properties")) or \
                os.path.isfile(os.path.join(target, "android.jar")):
            log("   已安装，跳过")
            continue
        if names and names[0].startswith("AUTO:"):
            found = discover_zip(names[0][5:])
            if not found:
                log("   !! 仓库索引里没找到 %s" % names[0][5:])
                continue
            log("   索引给出：%s" % found)
            names = [found]
        url, dst = pick_url(names)
        if not url:
            log("   !! 所有候选 URL 都不可用：%s" % ", ".join(names))
            continue
        zip_path = download(url, dst)
        extract(zip_path, target)
    log("=== 完成；SDK 布局 ===")
    for root, dirs, _ in os.walk(SDK):
        if root.count(os.sep) - SDK.count(os.sep) > 1:
            dirs[:] = []
            continue
        for d in sorted(dirs)[:6]:
            p = os.path.join(root, d)
            size = sum(os.path.getsize(os.path.join(dp, f)) for dp, _, fs in os.walk(p)
                       for f in fs) / 1048576.0
            log("   %-42s %8.1f MB" % (p[len(SDK) + 1:], size))


if __name__ == "__main__":
    try:
        main()
    except Exception as e:  # noqa: BLE001
        log("!! 异常: %r" % e)
        raise
