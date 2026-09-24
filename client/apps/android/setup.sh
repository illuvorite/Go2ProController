#!/usr/bin/env bash
# ============================================================================
# Android 工程装配脚本（在 client/apps/android/ 下执行）
#
# 做四件事：
#   1. 下载依赖源码：SDL2、Dear ImGui（放 client/third_party/）
#   2. 用 SDL2 自带的 android-project 模板生成 app/（含 SDLActivity 等 Java 类）
#   3. 注入我们的原生入口：app/src/main/jni/CMakeLists.txt → native/CMakeLists.txt
#   4. 注入 Android 侧配置：权限（网络/组播/WiFi）、minSdk 26、包名、MulticastLock 的 MainActivity
#
# 可重复执行：已存在的目录不重复下载；注入部分会覆盖为最新模板值。
#
# 用法：
#   ./setup.sh            # 完整装配
#   ./setup.sh --check    # 只检查环境与前置条件，不做修改
# ============================================================================
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CLIENT_DIR="$(cd "${HERE}/../.." && pwd)"
THIRD_PARTY="${CLIENT_DIR}/third_party"
APP_DIR="${HERE}/app"

SDL_REPO="https://github.com/libsdl-org/SDL.git"
SDL_TAG="release-2.30.9"          # SDL2 稳定版（ImGui 用 imgui_impl_sdl2）
IMGUI_REPO="https://github.com/ocornut/imgui.git"
IMGUI_TAG="v1.92.9b"

PKG_NAME="com.go2.remote"
MIN_SDK=26

log()  { printf '\033[1;36m[setup]\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m[warn ]\033[0m %s\n' "$*"; }
die()  { printf '\033[1;31m[error]\033[0m %s\n' "$*" >&2; exit 1; }

CHECK_ONLY=0
[[ "${1:-}" == "--check" ]] && CHECK_ONLY=1

# ---------------------------------------------------------------- 环境检查
log "环境检查"
missing=0
for tool in git cmake ninja; do
    if command -v "$tool" >/dev/null 2>&1; then
        printf '   %-8s %s\n' "$tool" "$(command -v $tool)"
    else
        printf '   %-8s \033[1;31m缺失\033[0m\n' "$tool"; missing=1
    fi
done

if [[ -n "${ANDROID_NDK_ROOT:-}" && -d "${ANDROID_NDK_ROOT}" ]]; then
    printf '   %-8s %s\n' "NDK" "${ANDROID_NDK_ROOT}"
else
    printf '   %-8s \033[1;33m未设置 ANDROID_NDK_ROOT（Gradle 构建时会用 SDK 内自带的 NDK）\033[0m\n' "NDK"
fi
if [[ -n "${ANDROID_SDK_ROOT:-}" && -d "${ANDROID_SDK_ROOT}" ]]; then
    printf '   %-8s %s\n' "SDK" "${ANDROID_SDK_ROOT}"
else
    printf '   %-8s \033[1;33m未设置 ANDROID_SDK_ROOT（用 Android Studio 打开时无需设置）\033[0m\n' "SDK"
fi

if command -v gradle >/dev/null 2>&1; then
    printf '   %-8s %s\n' "gradle" "$(gradle --version 2>/dev/null | grep -m1 Gradle)"
else
    printf '   %-8s \033[1;33m未安装命令行 gradle（可用 Android Studio 的 wrapper）\033[0m\n' "gradle"
fi
[[ $missing -eq 1 ]] && die "缺少必需工具（见上），请先安装"

if [[ $CHECK_ONLY -eq 1 ]]; then
    log "--check 完成：以上为环境现状，未做任何修改"
    exit 0
fi

# ---------------------------------------------------------------- 1) 依赖源码
mkdir -p "${THIRD_PARTY}"

if [[ -d "${THIRD_PARTY}/SDL/.git" ]]; then
    log "SDL2 已存在，跳过下载"
else
    log "下载 SDL2 ${SDL_TAG}（约 15MB，浅克隆）"
    git clone --depth 1 --branch "${SDL_TAG}" "${SDL_REPO}" "${THIRD_PARTY}/SDL"
fi

if [[ -f "${THIRD_PARTY}/imgui/imgui.cpp" ]]; then
    log "Dear ImGui 已存在，跳过下载"
else
    log "下载 Dear ImGui ${IMGUI_TAG}"
    git clone --depth 1 --branch "${IMGUI_TAG}" "${IMGUI_REPO}" "${THIRD_PARTY}/imgui"
fi

# ---------------------------------------------------------------- 2) 展开 Gradle 工程
# SDL 模板是"顶层 Gradle 工程（build.gradle/settings.gradle/gradlew）+ app/ 模块"两层结构，
# 整体展开到 apps/android/ 下；app/ 才是模块目录（applicationId / minSdk / externalNativeBuild 在里面）
TEMPLATE="${THIRD_PARTY}/SDL/android-project"
if [[ -f "${APP_DIR}/src/main/AndroidManifest.xml" ]]; then
    log "Gradle 工程已存在：只更新注入部分（模板不覆盖）"
else
    log "从 SDL2 模板展开 Gradle 工程（顶层 + app/ 模块）"
    [[ -d "${TEMPLATE}" ]] || die "SDL 里没有 android-project 模板"
    rm -rf "${APP_DIR}"          # 清掉可能存在的半成品
    cp -r "${TEMPLATE}/." "${HERE}/"
fi
[[ -f "${APP_DIR}/src/main/AndroidManifest.xml" ]] || die "模板展开失败：找不到 app/src/main/AndroidManifest.xml"

# ---------------------------------------------------------------- 3) 原生构建入口
# 注意：SDL 模板的原生目录是**模块级** app/jni/（不是 app/src/main/jni/），
# 且默认走 ndkBuild(Android.mk)；下面把它改成 CMake 并接到我们的 native/ 工程
JNI_DIR="${APP_DIR}/jni"
mkdir -p "${JNI_DIR}"
cat > "${JNI_DIR}/CMakeLists.txt" <<'EOF'
# 由 apps/android/setup.sh 生成：把 SDL 模板的 jni 目录接到我们的原生工程
# 踩坑记录：① 绝对路径 /mnt/d/... 在 Windows 下找不到 → 用相对路径；
#           ② 用 include() 拼接过 → CMake 报 "No project() command"，必须 project() + add_subdirectory
cmake_minimum_required(VERSION 3.18)
project(go2_android_top LANGUAGES C CXX)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../native" "${CMAKE_BINARY_DIR}/go2native")
EOF
log "已写入 app/jni/CMakeLists.txt → native/CMakeLists.txt"

# ---------------------------------------------------------------- 4) Android 侧配置
python3 - "${APP_DIR}" "${PKG_NAME}" "${MIN_SDK}" <<'PYEOF'
import io, os, re, sys

app_dir, pkg, min_sdk = sys.argv[1], sys.argv[2], int(sys.argv[3])
warn = lambda m: print("   [warn ] " + m)

# ---- 4.1 Manifest：权限 ----
mani = os.path.join(app_dir, "src/main/AndroidManifest.xml")
perms = """
    <!-- 局域网控制：网络与 WiFi 状态 -->
    <uses-permission android:name="android.permission.INTERNET"/>
    <uses-permission android:name="android.permission.ACCESS_NETWORK_STATE"/>
    <uses-permission android:name="android.permission.ACCESS_WIFI_STATE"/>
    <uses-permission android:name="android.permission.CHANGE_WIFI_STATE"/>
    <!-- 组播发现（SN 多播 231.1.1.1:10131）必需，否则收不到任何回包 -->
    <uses-permission android:name="android.permission.CHANGE_WIFI_MULTICAST_STATE"/>
    <!-- 部分机型扫描 WiFi 需要定位权限（仅用于读取网络信息，不采集位置） -->
    <uses-permission android:name="android.permission.ACCESS_FINE_LOCATION"/>
"""
if os.path.isfile(mani):
    t = io.open(mani, encoding="utf-8").read()
    if "CHANGE_WIFI_MULTICAST_STATE" not in t:
        # 插到 <manifest ...> 之后（<application> 之前）
        m = re.search(r"(<manifest[^>]*>\s*\n)", t)
        t = (t[:m.end()] + perms + t[m.end():]) if m else t
        io.open(mani, "w", encoding="utf-8", newline="\n").write(t)
        print("   [OK] Manifest 已加权限")
    else:
        print("   [已改] Manifest 权限已存在")
    # 横屏更适合双摇杆
    t = io.open(mani, encoding="utf-8").read()
    if "screenOrientation" not in t:
        t = t.replace("<activity ", '<activity android:screenOrientation="sensorLandscape" ', 1)
        io.open(mani, "w", encoding="utf-8", newline="\n").write(t)
        print("   [OK] Manifest 已设为横屏")
else:
    warn("找不到 AndroidManifest.xml，请手工加网络/组播权限")

# ---- 4.2 build.gradle：包名 + minSdk + 切到 CMake ----
p = os.path.join(app_dir, "build.gradle")
t = io.open(p, encoding="utf-8").read()
orig = t

# (a) applicationId（SDL 模板只有 namespace，没有 applicationId）
if "applicationId" not in t:
    t = t.replace("    defaultConfig {\n",
                  '    defaultConfig {\n        applicationId "%s"\n' % pkg, 1)

# (b) minSdk
t = re.sub(r"minSdkVersion\s+\d+", "minSdkVersion %d" % min_sdk, t)

# (c) ndkBuild → cmake（defaultConfig 里的参数块）
t = t.replace("""            ndkBuild {
                arguments "APP_PLATFORM=android-19"
                abiFilters 'armeabi-v7a', 'arm64-v8a', 'x86', 'x86_64'
            }""",
              """            cmake {
                arguments "-DANDROID_STL=c++_shared"
                abiFilters 'arm64-v8a'      // 真机；模拟器可加 'x86_64'
            }""")

# (d) ndkBuild 的构建脚本路径 → CMake
t = t.replace("""            ndkBuild {
                path 'jni/Android.mk'
            }""",
              """            cmake {
                path 'jni/CMakeLists.txt'
            }""")

if t != orig:
    io.open(p, "w", encoding="utf-8", newline="\n").write(t)
    print("   [OK] build.gradle：applicationId=%s, minSdk=%d, 已切到 CMake" % (pkg, min_sdk))
    if "cmake {" not in t or "path 'jni/CMakeLists.txt'" not in t:
        warn("build.gradle 里 CMake 配置可能没替换干净，请核对 externalNativeBuild")
else:
    warn("build.gradle 未改动，请手工确认 applicationId=%s / minSdk=%d / 使用 CMake" % (pkg, min_sdk))

# ---- 4.3 MainActivity：加 MulticastLock（组播发现必需） ----
act = os.path.join(app_dir, "src/main/java/org/libsdl/app/MainActivity.java")
os.makedirs(os.path.dirname(act), exist_ok=True)
io.open(act, "w", encoding="utf-8", newline="\n").write('''package org.libsdl.app;

import android.content.Context;
import android.net.wifi.WifiManager;

/**
 * 应用主 Activity（SDL2 模板基类）。
 * 额外职责：持有 WifiManager.MulticastLock —— Android 默认丢弃组播包，
 * 不持锁时 SN 多播发现（231.1.1.1:10131）收不到任何回包。
 * 原生侧通过 SDL_AndroidGetActivity() 调用下面两个方法。
 */
public class MainActivity extends SDLActivity {
    private WifiManager.MulticastLock multicastLock = null;

    @Override
    protected String[] getLibraries() {
        return new String[]{"SDL2", "main"};
    }

    /** 原生侧调用：申请组播锁 */
    public void acquireMulticastLock() {
        if (multicastLock != null) return;
        WifiManager wifi = (WifiManager) getApplicationContext()
                .getSystemService(Context.WIFI_SERVICE);
        if (wifi == null) return;
        multicastLock = wifi.createMulticastLock("go2-multicast");
        multicastLock.setReferenceCounted(false);
        multicastLock.acquire();
    }

    /** 原生侧调用：释放组播锁 */
    public void releaseMulticastLock() {
        if (multicastLock == null) return;
        if (multicastLock.isHeld()) multicastLock.release();
        multicastLock = null;
    }
}
''')
print("   [OK] MainActivity.java 已写入（含 MulticastLock）")

# ---- 4.4 内嵌字体目录（没有字体时中文会显示为方块，但不影响运行） ----
fd = os.path.join(app_dir, "src/main/assets/fonts")
os.makedirs(fd, exist_ok=True)
io.open(os.path.join(fd, "PUT_FONT_HERE.txt"), "w", encoding="utf-8").write(
    "把中文字体放到本目录，文件名用下列之一（应用会自动加载第一个找到的）：\n"
    "  NotoSansSC-Regular.otf / NotoSansSC-Regular.ttf\n"
    "  SourceHanSansSC-Regular.otf / wqy-microhei.ttc\n\n"
    "推荐 Noto Sans SC 或思源黑体（OFL 授权，允许随应用分发）。\n"
    "下载示例（自行执行）：\n"
    "  curl -L -o NotoSansSC-Regular.otf \\\n"
    "    https://github.com/notofonts/noto-cjk/raw/main/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf\n")
print("   [OK] assets/fonts/ 已就绪（放入字体后重新构建生效）")
PYEOF

# ---------------------------------------------------------------- 完成提示
log "装配完成"
cat <<EOF

下一步（二选一）：

A) Android Studio（推荐）
   1. 用 Android Studio 打开目录：${HERE}/app
   2. 首次会自动下载 SDK/NDK 与 Gradle 依赖
   3. 点 Run ▶ 安装到手机（手机需开 USB 调试）

B) 命令行
   cd ${HERE}/app
   ./gradlew assembleDebug        # 产物：app/build/outputs/apk/debug/*.apk
   adb install -r app/build/outputs/apk/debug/*.apk

原生依赖（libdatachannel / OpenSSL）需要 Android 版本，二选一：
   · vcpkg：vcpkg install openssl libdatachannel nlohmann-json --triplet arm64-android
            并在 app/build.gradle 的 cmake arguments 里加
            -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
            -DVCPKG_TARGET_TRIPLET=arm64-android
   · 或预编译前缀：-DGO2_ANDROID_DEPS=<prefix>（含 include/ 与 lib/）

首次构建报错属正常（NDK 版本、依赖路径差异）——把日志贴出来即可定位。
EOF
