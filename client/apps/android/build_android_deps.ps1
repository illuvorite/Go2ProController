# ============================================================================
# vcpkg cross-compile Android(arm64-v8a) deps: openssl / libdatachannel / nlohmann-json
# Log: _vcpkg.log (ends with DONE/FAILED)  Output: %VCPKG_ROOT%\installed\arm64-android\
#
# ★ Proxy must be set WITHOUT a scheme, and only $env: assignment actually works
#   (same content in a .bat via cmd set still fails: curl error 5 Could not resolve proxy name)
#   HTTPS_PROXY=http://127.0.0.1:7897  -> fails
#   HTTPS_PROXY=127.0.0.1:7897         -> works (verified with a 40MB cmake download)
#
# ★ Both the proxy and the vcpkg root used to be hardcoded to one machine
#   ("127.0.0.1:7897" / "D:\vcpkg\vcpkg.exe"), so this script could not run anywhere
#   else. Now: proxy is only injected when you ask for it, vcpkg comes from
#   $env:VCPKG_ROOT. Example:
#     $env:GO2_VCPKG_PROXY = "127.0.0.1:7897"; $env:VCPKG_ROOT = "D:\vcpkg"; .\build_android_deps.ps1
# ============================================================================
if ($env:GO2_VCPKG_PROXY) {
    $env:HTTPS_PROXY    = $env:GO2_VCPKG_PROXY
    $env:HTTP_PROXY     = $env:GO2_VCPKG_PROXY
    $env:ALL_PROXY      = $env:GO2_VCPKG_PROXY
    $env:https_proxy    = $env:GO2_VCPKG_PROXY
    $env:http_proxy     = $env:GO2_VCPKG_PROXY
}

$base = $PSScriptRoot
$env:PATH = "$base\_sdk\cmake\3.22.1\bin;$env:PATH"
$env:ANDROID_NDK_HOME = "$base\_sdk\ndk\26.1.10909125"
$env:ANDROID_NDK      = $env:ANDROID_NDK_HOME
$log = "$base\_vcpkg.log"

$vcpkg = if ($env:VCPKG_ROOT) { Join-Path $env:VCPKG_ROOT "vcpkg.exe" } else { "vcpkg.exe" }

"==== vcpkg arm64-android deps start $(Get-Date) ====" | Out-File -Encoding utf8 $log
"proxy=$($env:HTTPS_PROXY) (no scheme)  ndk=$($env:ANDROID_NDK_HOME)  vcpkg=$vcpkg" | Out-File -Append -Encoding utf8 $log

& $vcpkg install openssl libdatachannel nlohmann-json --triplet arm64-android --clean-after-build *>> $log
if ($LASTEXITCODE -ne 0) {
    "FAILED rc=$LASTEXITCODE $(Get-Date)" | Out-File -Append -Encoding utf8 $log
} else {
    "DONE $(Get-Date)" | Out-File -Append -Encoding utf8 $log
}
