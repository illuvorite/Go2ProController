#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Windows 端 Android 构建环境安装器（命令行，无需 Android Studio）

装到项目内 client/apps/android/_sdk/（不碰 Program Files、不改注册表）：
  1. Temurin JDK 17（zip 解压）
  2. Android cmdline-tools（Windows）
  3. sdkmanager 装 platform-tools / platforms;android-34 / build-tools;34.0.0 / ndk;26.1.10909125 / cmake;3.22.1

全程写日志到 _sdk/install.log（每 2MB 一行进度），便于外部轮询查看。
默认走本机代理 http://127.0.0.1:7897（可用 --no-proxy 关闭）。
"""
import io
import os
import subprocess
import sys
import time
import urllib.request
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
SDK = os.path.join(HERE, "_sdk")
LOG = os.path.join(SDK, "install.log")
PROXY = "http://127.0.0.1:7897"
JDK_DIR = os.path.join(SDK, "jdk17")

JDK_URL = ("https://github.com/adoptium/temurin17-binaries/releases/download/"
           "jdk-17.0.13%2B11/OpenJDK17U-jdk_x64_windows_hotspot_17.0.13_11.zip")
CMDLINE_URL = ("https://dl.google.com/android/repository/"
               "commandlinetools-win-11076708_latest.zip")
PKGS = ["platform-tools", "platforms;android-34", "build-tools;34.0.0",
        "ndk;26.1.10909125", "cmake;3.22.1"]

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


def opener(use_proxy=True):
    handlers = []
    if use_proxy:
        handlers.append(urllib.request.ProxyHandler({"http": PROXY, "https": PROXY}))
    return urllib.request.build_opener(*handlers)


def download(url, dst, use_proxy=True):
    """用系统自带的 curl.exe 下载（Python urllib 走本地代理会报 URLError，curl 稳定）；
    每 3 秒把已下载大小写进日志，外部轮询即可看到实时进度。"""
    if os.path.isfile(dst) and os.path.getsize(dst) > 1024 * 1024:
        log("已存在，跳过下载: %s" % os.path.basename(dst))
        return dst
    if os.path.isfile(dst):
        os.remove(dst)
    name = url.split("/")[-1]
    log("下载 %s" % name)
    cmd = ["curl.exe", "-L", "--fail", "--silent", "--show-error", "--retry", "3",
           "--connect-timeout", "30", "-o", dst]
    if use_proxy:
        cmd += ["-x", PROXY]
    cmd.append(url)
    t0 = time.time()
    p = subprocess.Popen(cmd)
    while p.poll() is None:
        time.sleep(3)
        if os.path.isfile(dst):
            mb = os.path.getsize(dst) / 1048576.0
            log("   ... %.1f MB  (%.2f MB/s)" % (mb, mb / max(0.1, time.time() - t0)))
    if p.returncode != 0 or not os.path.isfile(dst):
        raise RuntimeError("下载失败 %s（curl 退出码 %s）" % (name, p.returncode))
    log("完成 %s (%.1f MB, %.0fs)" % (name, os.path.getsize(dst) / 1048576.0, time.time() - t0))
    return dst


def unzip(src, dst):
    log("解压 -> %s" % dst)
    with zipfile.ZipFile(src) as z:
        z.extractall(dst)


def run(cmd, **kw):
    log("$ " + " ".join(str(c) for c in cmd[:6]) + (" ..." if len(cmd) > 6 else ""))
    p = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace",
                       **kw)
    tail = (p.stdout or "")[-600:] + (p.stderr or "")[-400:]
    for line in tail.splitlines()[-8:]:
        log("   | " + line.strip()[:150])
    if p.returncode != 0:
        log("!! 退出码 %d" % p.returncode)
    return p.returncode


def main():
    use_proxy = "--no-proxy" not in sys.argv
    os.makedirs(SDK, exist_ok=True)
    log("=== Android 构建环境安装开始（SDK 根目录 %s）===" % SDK)
    log("代理: %s" % (PROXY if use_proxy else "不使用"))

    # ---- 1) JDK 17 ----
    if os.path.isfile(os.path.join(JDK_DIR, "bin", "java.exe")):
        log("JDK 17 已就绪")
    else:
        z = download(JDK_URL, os.path.join(SDK, "jdk17.zip"), use_proxy)
        unzip(z, JDK_DIR)
        inner = [d for d in os.listdir(JDK_DIR) if d.startswith("jdk-")]
        if inner:  # zip 里还有一层目录，展平
            src = os.path.join(JDK_DIR, inner[0])
            for name in os.listdir(src):
                os.replace(os.path.join(src, name), os.path.join(JDK_DIR, name))
            os.rmdir(src)
        log("JDK 17 安装完成: %s" % JDK_DIR)

    java_exe = os.path.join(JDK_DIR, "bin", "java.exe")
    run([java_exe, "-version"])

    # ---- 2) cmdline-tools ----
    cmdtools = os.path.join(SDK, "cmdline-tools", "latest", "bin", "sdkmanager.bat")
    if not os.path.isfile(cmdtools):
        z = download(CMDLINE_URL, os.path.join(SDK, "cmdline-tools.zip"), use_proxy)
        tmp = os.path.join(SDK, "_cmdtools_tmp")
        unzip(z, tmp)
        dst = os.path.join(SDK, "cmdline-tools", "latest")
        if os.path.isdir(dst):
            import shutil
            shutil.rmtree(dst, ignore_errors=True)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        os.replace(os.path.join(tmp, "cmdline-tools"), dst)
        import shutil
        shutil.rmtree(tmp, ignore_errors=True)
        log("cmdline-tools 就绪: %s" % cmdtools)

    env = dict(os.environ)
    env["JAVA_HOME"] = JDK_DIR
    env["ANDROID_SDK_ROOT"] = SDK
    env["ANDROID_HOME"] = SDK
    env["PATH"] = os.path.join(JDK_DIR, "bin") + os.pathsep + env.get("PATH", "")

    proxy_args = ["--proxy=http", "--proxy_host=127.0.0.1", "--proxy_port=7897"] if use_proxy else []

    # ---- 3) 许可 ----
    log("接受 SDK 许可")
    lic = subprocess.run(["cmd", "/c", cmdtools, "--sdk_root=" + SDK] + proxy_args + ["--licenses"],
                         input="y\n" * 200, capture_output=True, text=True, env=env)
    log("   licenses 退出码 %d" % lic.returncode)

    # ---- 4) 组件 ----
    log("安装组件: %s" % ", ".join(PKGS))
    log("（NDK 约 700MB，代理下通常几分钟）")
    rc = run(["cmd", "/c", cmdtools, "--sdk_root=" + SDK] + proxy_args + ["--install"] + PKGS,
             env=env)
    log("组件安装退出码 %d" % rc)

    # ---- 5) 打印环境变量 ----
    ndk = os.path.join(SDK, "ndk", "26.1.10909125")
    log("=== 安装结束 ===")
    log("JAVA_HOME=%s" % JDK_DIR)
    log("ANDROID_SDK_ROOT=%s" % SDK)
    log("ANDROID_NDK_ROOT=%s" % ndk)
    log("NDK 存在: %s" % os.path.isdir(ndk))


if __name__ == "__main__":
    try:
        main()
    except Exception as e:  # noqa: BLE001
        log("!! 异常: %r" % e)
        raise
