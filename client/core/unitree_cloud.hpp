#pragma once

#include <nlohmann/json.hpp>

#include <string>
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
