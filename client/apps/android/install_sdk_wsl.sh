#!/usr/bin/env bash
# ============================================================================
# 在 WSL / Ubuntu 上安装 Android 命令行构建环境（不需要 Android Studio 图形界面）
#
# 安装内容：
#   JDK 17（AGP 8.x 要求）、Android cmdline-tools、platform-tools、platforms;android-34、
#   build-tools;34.0.0、ndk;26.1.10909125、cmake;3.22.1
#
# 幂等：已安装的项目会跳过。安装完打印需要 export 的环境变量。
# 用法： ./install_sdk_wsl.sh            # 全量
#        ./install_sdk_wsl.sh --min      # 不装 NDK（先验证 Gradle/Java 链路）
# ============================================================================
set -euo pipefail

SDK_ROOT="${ANDROID_SDK_ROOT:-$HOME/android-sdk}"
CMDLINE_ZIP="commandlinetools-linux-11076708_latest.zip"
CMDLINE_URL="https://dl.google.com/android/repository/${CMDLINE_ZIP}"
NDK_VERSION="26.1.10909125"

MIN=0
[[ "${1:-}" == "--min" ]] && MIN=1

log()  { printf '\033[1;36m[sdk]\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m[warn]\033[0m %s\n' "$*"; }
die()  { printf '\033[1;31m[err ]\033[0m %s\n' "$*" >&2; exit 1; }

command -v apt-get >/dev/null || die "本脚本用于 Debian/Ubuntu（WSL）"

# ---------------------------------------------------------------- 1) JDK 17（走压缩包，不用 apt）
log "1/4 准备 JDK 17"
JDK_DIR="/opt/jdk17"
if [[ -x "${JDK_DIR}/bin/java" ]]; then
    log "   已下载过 ${JDK_DIR}"
else
    JDK_URL="https://github.com/adoptium/temurin17-binaries/releases/download/jdk-17.0.13%2B11/OpenJDK17U-jdk_x64_linux_hotspot_17.0.13_11.tar.gz"
    log "   下载 Temurin JDK 17（约 180MB，走 http_proxy 如果已设置）"
    mkdir -p "${JDK_DIR}"
    curl -L --fail --silent --show-error --connect-timeout 20 -o /tmp/jdk17.tgz "${JDK_URL}" \
        || die "JDK 下载失败（检查代理/网络后可重跑本脚本）"
    tar -xzf /tmp/jdk17.tgz -C "${JDK_DIR}" --strip-components=1
    rm -f /tmp/jdk17.tgz
fi
export JAVA_HOME="${JDK_DIR}"
export PATH="${JAVA_HOME}/bin:${PATH}"
java -version 2>&1 | head -1

# ---------------------------------------------------------------- 2) cmdline-tools
log "2/4 安装 Android cmdline-tools 到 ${SDK_ROOT}"
mkdir -p "${SDK_ROOT}/cmdline-tools"
if [[ -x "${SDK_ROOT}/cmdline-tools/latest/bin/sdkmanager" ]]; then
    log "   已存在，跳过下载"
else
    TMP="$(mktemp -d)"
    log "   下载 ${CMDLINE_ZIP}（约 150MB）"
    curl -L --fail --silent --show-error --connect-timeout 20 -o "${TMP}/cmdline.zip" "${CMDLINE_URL}" \
        || die "下载失败（网络问题可重跑本脚本）"
    python3 -m zipfile -e "${TMP}/cmdline.zip" "${TMP}"
    rm -rf "${SDK_ROOT}/cmdline-tools/latest"
    mv "${TMP}/cmdline-tools" "${SDK_ROOT}/cmdline-tools/latest"
    rm -rf "${TMP}"
fi

export ANDROID_SDK_ROOT="${SDK_ROOT}"
export ANDROID_HOME="${SDK_ROOT}"
SDKMANAGER="${SDK_ROOT}/cmdline-tools/latest/bin/sdkmanager"

# ---------------------------------------------------------------- 代理（sdkmanager 不读环境变量，要显式传参）
PROXY_FLAGS=()
if [[ -n "${http_proxy:-}" || -n "${https_proxy:-}" ]]; then
    P="${https_proxy:-$http_proxy}"
    PHOST="$(printf '%s' "$P" | sed -E 's#^[a-z]+://##; s#:.*$##')"
    PPORT="$(printf '%s' "$P" | sed -E 's#^.*:##; s#/.*$##')"
    if [[ -n "$PHOST" && -n "$PPORT" ]]; then
        PROXY_FLAGS=(--proxy=http --proxy_host="${PHOST}" --proxy_port="${PPORT}")
        log "sdkmanager 代理：${PHOST}:${PPORT}"
    fi
fi

# ---------------------------------------------------------------- 3) 许可
log "3/4 接受 SDK 许可"
yes | "${SDKMANAGER}" --sdk_root="${SDK_ROOT}" "${PROXY_FLAGS[@]+"${PROXY_FLAGS[@]}"}" --licenses >/dev/null 2>&1 || true

# ---------------------------------------------------------------- 4) 组件
if [[ $MIN -eq 1 ]]; then
    PKGS=(platform-tools "platforms;android-34" "build-tools;34.0.0")
else
    PKGS=(platform-tools "platforms;android-34" "build-tools;34.0.0"
          "ndk;${NDK_VERSION}" "cmake;3.22.1")
fi
log "4/4 安装组件：${PKGS[*]}"
log "   （NDK 约 700MB，网络慢时请耐心等，别中断）"
"${SDKMANAGER}" --sdk_root="${SDK_ROOT}" "${PROXY_FLAGS[@]+"${PROXY_FLAGS[@]}"}" --install "${PKGS[@]}" 2>&1 | tail -5

log "安装完成。请在当前 shell 里生效（或写进 ~/.bashrc）："
cat <<EOF

export ANDROID_SDK_ROOT=${SDK_ROOT}
export ANDROID_HOME=${SDK_ROOT}
export ANDROID_NDK_ROOT=${SDK_ROOT}/ndk/${NDK_VERSION}
export PATH=\$PATH:${SDK_ROOT}/platform-tools:${SDK_ROOT}/cmdline-tools/latest/bin

已安装：
$(ls "${SDK_ROOT}" 2>/dev/null | sed 's/^/   /')
EOF
