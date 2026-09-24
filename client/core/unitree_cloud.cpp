#include "unitree_cloud.hpp"

#include "crypto.hpp"

#include <httplib.h>

#include <chrono>
#include <cstdio>
#include <random>
#include <stdexcept>

namespace go2 {
namespace {

// 官方 App 的签名密钥（公开逆向成果，见参考实现 unitree_cloud.py）
const char* kAppSignSecret = "XyvkwK45hp5PHfA8";

std::string randomHex(size_t bytes) {
    static std::mt19937_64 gen(std::random_device{}() ^
                               std::chrono::steady_clock::now().time_since_epoch().count());
    char buf[3];
    std::string out;
    for (size_t i = 0; i < bytes; ++i) {
        std::snprintf(buf, sizeof(buf), "%02x", int(gen() & 0xff));
        out += buf;
    }
    return out;
}

std::string nowMs() {
    return std::to_string(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

std::string hostOf(const std::string& region) {
    return region == "cn" ? "robot-api.unitree.com" : "global-robot-api.unitree.com";
}

// 与官方 apk 一致的请求头；细微差异可能导致 code:100 变 1003，勿随意删改
httplib::Headers makeHeaders(const std::string& token) {
    const std::string ts = nowMs();
    const std::string nonce = randomHex(16);
    httplib::Headers h{
        {"DeviceId", "Samsung/Samsung/SM-S931B/s24/14/34"},
        {"DevicePlatform", "Android"},
        {"DeviceModel", "SM-S931B"},
        {"SystemVersion", "34"},
        {"AppVersion", "1.11.4"},
        {"AppLocale", "en_US"},
        {"Channel", "UMENG_CHANNEL"},
        {"User-Agent",
         "Mozilla/5.0 (Linux; Android 14; SM-S931B Build/AP3A.240905.015.A2; wv) "
         "AppleWebKit/537.36 (KHTML, like Gecko) Version/4.0 Chrome/127.0.6533.103 "
         "Mobile Safari/537.36"},
        {"AppTimezone", "UTC"},
        {"AppTimestamp", ts},
        {"AppNonce", nonce},
        {"AppSign", crypto::md5Hex(std::string(kAppSignSecret) + ts + nonce)},
        {"AppName", "Go2"},
        {"Token", token},
    };
    return h;
}

}  // namespace

UnitreeCloud::UnitreeCloud(std::string region) : baseUrl_(hostOf(region)) {}

nlohmann::json UnitreeCloud::request(const std::string& method,
                                     const std::string& path,
                                     const nlohmann::json* formBody) {
    httplib::Client cli("https://" + baseUrl_);
    cli.set_connection_timeout(8, 0);
    cli.set_read_timeout(15, 0);

    const httplib::Headers headers = makeHeaders(accessToken_);

    httplib::Result res = [&] {
        if (method == "GET") return cli.Get(path, headers);
        httplib::Params params;
        if (formBody)
            for (auto it = formBody->begin(); it != formBody->end(); ++it)
                params.emplace(it.key(), it.value().get<std::string>());
        return cli.Post(path, headers, params);
    }();

    if (!res)
        throw std::runtime_error("云 API 请求失败(" + path + "): " +
                                 httplib::to_string(res.error()));
    if (res->status != 200)
        throw std::runtime_error("云 API HTTP " + std::to_string(res->status) + " (" +
                                 path + ")");
    try {
        return nlohmann::json::parse(res->body);
    } catch (...) {
        throw std::runtime_error("云 API 响应不是 JSON (" + path + ")");
    }
}

nlohmann::json UnitreeCloud::check(const nlohmann::json& resp, const char* action) {
    const int code = resp.value("code", -1);
    if (code != 100)
        throw std::runtime_error("云 API " + std::string(action) + " 失败 code=" +
                                 std::to_string(code) + " " +
                                 resp.value("errorMsg", std::string("")));
    return resp.value("data", nlohmann::json());
}

void UnitreeCloud::loginEmail(const std::string& email, const std::string& password) {
    nlohmann::json form;
    form["email"] = email;
    form["password"] = crypto::md5Hex(password);  // 官方 App 同样只传 MD5
    const auto data = check(request("POST", "/login/email", &form), "login/email");
    accessToken_ = data.value("accessToken", "");
    refreshToken_ = data.value("refreshToken", "");
    if (accessToken_.empty())
        throw std::runtime_error("登录成功但未返回 accessToken");
}

std::vector<CloudDevice> UnitreeCloud::listDevices() {
    const auto data = check(request("GET", "/device/bind/list", nullptr),
                            "device/bind/list");
    std::vector<CloudDevice> out;
    if (!data.is_array()) return out;
    for (const auto& d : data) {
        CloudDevice dev;
        dev.sn = d.value("sn", "");
        dev.alias = d.value("alias", "");
        dev.series = d.value("series", "");
        dev.mac = d.value("mac", "");
        dev.online = d.value("online", false);
        dev.aesKey = d.value("key", "");
        if (dev.aesKey.empty()) dev.aesKey = d.value("gcm_key", "");
        out.push_back(std::move(dev));
    }
    return out;
}

void UnitreeCloud::ping() const {
    httplib::Client cli("https://" + baseUrl_);
    cli.set_connection_timeout(8, 0);
    cli.set_read_timeout(15, 0);
    auto res = cli.Get("/system/pubKey");
    if (!res)
        throw std::runtime_error("云 API 不可达: " + httplib::to_string(res.error()));
    const auto j = nlohmann::json::parse(res->body);
    if (j.value("code", -1) != 100)
        throw std::runtime_error("云 API 异常 code=" + std::to_string(j.value("code", -1)));
}

}  // namespace go2
