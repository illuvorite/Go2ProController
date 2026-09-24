# ============================================================================
# Android arm64-v8a native deps WITHOUT vcpkg.
#   (vcpkg fails on this machine: "Could not locate a complete Visual Studio instance")
#
#   OpenSSL           : KDAB android_openssl ssl_3/arm64-v8a prebuilt static libs
#   libdatachannel    : cross-compiled with project-local NDK r26 clang (+ libjuice, libusrsctp)
#   httplib/nlohmann  : header-only, reused from client/build/_deps sources
#
# Output prefix: client/apps/android/_deps_arm64/{include,lib}
# Gradle side uses -DGO2_ANDROID_DEPS=<prefix>  (supported by native/CMakeLists.txt)
#
# NOTE: keep this file ASCII-only - Windows PowerShell 5.1 reads .ps1 as ANSI(GBK)
#       when there is no BOM, and Chinese text then eats the following quote char.
#
# Log: _deps_arm64.log  (steps [1/4]..[4/4]; failure writes FAILED)
# ============================================================================
$ErrorActionPreference = "Continue"

$base     = "d:\code\project\network_get\client\apps\android"
$client   = "d:\code\project\network_get\client"
$prefix   = "$base\_deps_arm64"
$ndk      = "$base\_sdk\ndk\26.1.10909125"
$cmakeBin = "$base\_sdk\cmake\3.22.1\bin"
$cmake    = "$cmakeBin\cmake.exe"
$ninja    = "$cmakeBin\ninja.exe"
$log      = "$base\_deps_arm64.log"

function W($m) { $m | Out-File -Append -Encoding utf8 $script:log }

"[start] $(Get-Date)" | Out-File -Encoding utf8 $log

# ---------------------------------------------------------------- [1/4] OpenSSL
$ssl = "$base\_deps_tmp\android_openssl-master\ssl_3\arm64-v8a"
if (-not (Test-Path "$ssl\libcrypto.a")) {
    W "[1/4] FAILED: prebuilt OpenSSL not found at $ssl"
    exit 1
}
New-Item -ItemType Directory -Force -Path "$prefix\include" | Out-Null
New-Item -ItemType Directory -Force -Path "$prefix\lib"     | Out-Null
# KDAB layout: the per-ABI dir holds ONLY the libs; headers are shared one level up
#   ssl_3/arm64-v8a/{libcrypto.a,libssl.a}   and   ssl_3/include/openssl/*.h
# (there is a second, asm-free variant tree at no-asm/ssl_3/... - not used here)
$sslInc = "$base\_deps_tmp\android_openssl-master\ssl_3\include\openssl"
if (-not (Test-Path "$sslInc\ssl.h")) {
    $found = Get-ChildItem "$base\_deps_tmp\android_openssl-master\ssl_3" -Recurse -Filter "ssl.h" `
        -ErrorAction SilentlyContinue | Where-Object { $_.Directory.Name -eq "openssl" } |
        Select-Object -First 1
    if ($found) { $sslInc = $found.Directory.FullName }
}
if (-not $sslInc -or -not (Test-Path "$sslInc\ssl.h")) {
    W "[1/4] FAILED: cannot find openssl/ssl.h under $base\_deps_tmp\android_openssl-master\ssl_3"
    exit 1
}
# copy the CONTENTS (copying the folder into an existing folder would nest it one level deeper)
New-Item -ItemType Directory -Force -Path "$prefix\include\openssl" | Out-Null
Copy-Item "$sslInc\*" "$prefix\include\openssl" -Recurse -Force
W "[1/4] headers <- $sslInc"
Copy-Item "$ssl\libcrypto.a" "$prefix\lib\libcrypto.a" -Force
Copy-Item "$ssl\libssl.a"    "$prefix\lib\libssl.a"    -Force
if (-not (Test-Path "$prefix\include\openssl\ssl.h")) {
    W "[1/4] FAILED: openssl headers missing under $prefix\include (source: $ssl\include)"
    exit 1
}
W "[1/4] OpenSSL ok: libs=$((Get-ChildItem "$prefix\lib" | ForEach-Object Name) -join ', ') headers=$((Get-ChildItem "$prefix\include\openssl" -Filter *.h).Count) files"

# ------------------------------------------------- [2/4] configure libdatachannel
$src = "$client\build\_deps\libdatachannel-src"
$bld = "$base\_build_rtc"
$cfgArgs = @(
    "-S", $src, "-B", $bld, "-G", "Ninja",
    "-DCMAKE_MAKE_PROGRAM=$ninja",
    "-DCMAKE_TOOLCHAIN_FILE=$ndk\build\cmake\android.toolchain.cmake",
    "-DANDROID_ABI=arm64-v8a",
    "-DANDROID_PLATFORM=android-26",
    "-DCMAKE_BUILD_TYPE=Release",
    "-DCMAKE_INSTALL_PREFIX=$prefix",
    "-DBUILD_SHARED_LIBS=OFF",
    "-DNO_EXAMPLES=ON", "-DNO_TESTS=ON", "-DNO_MEDIA=ON", "-DNO_WEBSOCKET=ON",
    "-DUSE_MBEDTLS=OFF", "-DUSE_GNUTLS=OFF", "-DUSE_OPENSSL=ON",
    # NDK toolchain sets CMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY, so find_library()
    # only looks inside the NDK sysroot and never sees our prebuilt prefix.
    # => pass the archives explicitly and relax the root-path modes.
    "-DOPENSSL_ROOT_DIR=$prefix",
    "-DOPENSSL_INCLUDE_DIR=$prefix\include",
    "-DOPENSSL_CRYPTO_LIBRARY=$prefix\lib\libcrypto.a",
    "-DOPENSSL_SSL_LIBRARY=$prefix\lib\libssl.a",
    "-DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=BOTH",
    "-DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=BOTH",
    "-DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=BOTH"
)
$o = & $cmake @cfgArgs 2>&1
$o | Out-File -Append -Encoding utf8 $log
if ($LASTEXITCODE -ne 0) {
    W "[2/4] FAILED configure rc=$LASTEXITCODE"
    exit 2
}
W "[2/4] configure ok"

# ------------------------------------------------- [3/4] build + install
$o = & $cmake --build $bld --target install 2>&1
$o | Out-File -Append -Encoding utf8 $log
if ($LASTEXITCODE -ne 0) {
    W "[3/4] FAILED build rc=$LASTEXITCODE"
    exit 3
}
W "[3/4] build+install ok"

# ------------------------------------------------- [4/4] collect juice / usrsctp
#   libdatachannel's install step only installs its own archive; sub-deps must be copied.
Get-ChildItem $bld -Recurse -Include "libjuice.a", "libusrsctp.a" -ErrorAction SilentlyContinue |
    ForEach-Object {
        Copy-Item $_.FullName "$prefix\lib\$($_.Name)" -Force
        W "[4/4] collected $($_.Name)"
    }

W "---- prefix lib listing ----"
Get-ChildItem "$prefix\lib" -ErrorAction SilentlyContinue |
    ForEach-Object { W ("  " + $_.Name + "  " + [Math]::Round($_.Length / 1MB, 2) + " MB") }
W "DEPS DONE $(Get-Date)"
