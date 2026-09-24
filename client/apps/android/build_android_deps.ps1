# ============================================================================
# vcpkg 交叉编译 Android(arm64-v8a) 依赖：openssl / libdatachannel / nlohmann-json
# 日志：_vcpkg.log（结尾 DONE/FAILED）  产物：D:\vcpkg\installed\arm64-android\
#
# ★ 代理必须"不带 scheme"，且实测：**用 PowerShell 的 $env: 设置才生效**
#   （同样内容的 .bat 用 cmd set 设进去，vcpkg 仍报 curl error 5 Could not resolve proxy name）
#   HTTPS_PROXY=http://127.0.0.1:7897  -> 失败
#   HTTPS_PROXY=127.0.0.1:7897         -> 成功（cmake 40MB 已实测）
# ============================================================================
$env:HTTPS_PROXY    = "127.0.0.1:7897"
$env:HTTP_PROXY     = "127.0.0.1:7897"
$env:ALL_PROXY      = "127.0.0.1:7897"
$env:https_proxy    = "127.0.0.1:7897"
$env:http_proxy     = "127.0.0.1:7897"

$base = "d:\code\project\network_get\client\apps\android"
$env:PATH = "$base\_sdk\cmake\3.22.1\bin;$env:PATH"
$env:ANDROID_NDK_HOME = "$base\_sdk\ndk\26.1.10909125"
$env:ANDROID_NDK      = $env:ANDROID_NDK_HOME
$log = "$base\_vcpkg.log"

"==== vcpkg arm64-android deps start $(Get-Date) ====" | Out-File -Encoding utf8 $log
"proxy=$($env:HTTPS_PROXY) (no scheme)  ndk=$($env:ANDROID_NDK_HOME)" | Out-File -Append -Encoding utf8 $log

& D:\vcpkg\vcpkg.exe install openssl libdatachannel nlohmann-json --triplet arm64-android --clean-after-build *>> $log
if ($LASTEXITCODE -ne 0) {
    "FAILED rc=$LASTEXITCODE $(Get-Date)" | Out-File -Append -Encoding utf8 $log
} else {
    "DONE $(Get-Date)" | Out-File -Append -Encoding utf8 $log
}
