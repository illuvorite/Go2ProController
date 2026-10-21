#pragma once

#include <nlohmann/json.hpp>

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace go2 {

/// 云端绑定的一台设备
struct CloudDevice {
    std::string sn;
    std::string alias;
    std::string series;
    std::string mac;
    bool online = false;
    std::string aesKey;  // 32 位 hex，data2=3 新固件的每设备 AES-128 key
};

/// 宇树云 API 客户端（与官方 App 同一接口）。
/// 用途：登录账号 -> device/bind/list 拿每设备 AES-128 key，
/// 供 data2=3 新固件（Go2 ≥ 1.1.15 / G1 ≥ 1.5.1 / R1）的局域网握手解密。
///
/// 协议要点（与官方 apk 对齐，头字段不可少）：
///   - AppSign = md5("XyvkwK45hp5PHfA8" + AppTimestamp + AppNonce)
///   - 业务应答 code==100 为成功；1001 表示 token 过期（可用 refreshToken 刷新一次）
/// 平台注入的 HTTP 传输（可选）。
///
/// 为什么需要：宇树云在腾讯 EdgeOne 后面，它对 **Android 静态 OpenSSL** 的 TLS
/// 指纹会回 HTTP 567 人机验证页（桌面 OpenSSL 不会）。安卓侧把它换成系统网络栈
/// （HttpURLConnection/Conscrypt，与官方 App 同源）即可通过。
///
/// @param method "GET" / "POST"
/// @param url    完整 URL
/// @param headers 请求头（保持顺序与大小写）
/// @param body   POST 表单体（已 urlencode）；GET 为空
/// @param outBody 输出：响应体
/// @return HTTP 状态码；0 表示网络层失败（outBody 里是错误描述）
using CloudHttpFn = std::function<int(
    const std::string& method, const std::string& url,
    const std::vector<std::pair<std::string, std::string>>& headers,
    const std::string& body, std::string& outBody)>;

/// 注入平台传输；传 nullptr 恢复内置 httplib（桌面默认）。
void setCloudHttpTransport(CloudHttpFn fn);

class UnitreeCloud {
public:
    /// @param region "global"（海外账号）或 "cn"（国内账号）
    explicit UnitreeCloud(std::string region = "global");

    /// 邮箱 + 明文密码登录（密码仅以 MD5 形式经 TLS 发给宇树官方 API）
    void loginEmail(const std::string& email, const std::string& password);

    /// 拉取账号绑定的全部设备（含每设备 AES-128 key）
    std::vector<CloudDevice> listDevices();

    /// 无需登录的连通性自检：GET system/pubKey
    void ping() const;

    const std::string& accessToken() const { return accessToken_; }

private:
    nlohmann::json request(const std::string& method, const std::string& path,
                           const nlohmann::json* formBody);
    nlohmann::json check(const nlohmann::json& resp, const char* action);

    std::string baseUrl_;
    std::string accessToken_;
    std::string refreshToken_;
};

}  // namespace go2
