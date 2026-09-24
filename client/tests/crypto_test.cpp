// 加密模块自测：用真实抓取的 con_notify 响应验证 解密/公钥提取/路径推导 是否正确。
// AES-GCM 带认证，解密成功即证明密钥与数据都正确。
#include "crypto.hpp"
#include "signaling.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <string>

namespace {

// 2026-09-22 从 192.168.2.111 真实抓取的 /con_notify 响应（base64）
const char* kRealConNotify =
    "eyJkYXRhMSI6ImRXNXI4SVl6OGMzRTFpWE1QcS9uVlBmR3F0d2w5WGZaU2VaM2xmd2hKY3JvOE1BMGlHMzVTRDFJenVaQXRrMDY2ZjlNSzVobC8yQlpSQ2d4eU55dTNleTFmUHJWQ3QyZFhMcFovV0NmMzFoUG0raTY2bFcvSnJZZ1N5bGttNkU3b2RxYUlNTWI5UWUwbkc1RDJKZzhielJWMnlIY2VZTmFVZllJVTArY29zNUpudzNTVzlHZ3o1MHJFWE5JQVU4eUdKOG1TTmw3YUtoSi9PU0FuQjR1NXhzK3pWRHErL2hxZUYrR2tPQ29kMXlhUVh4VlU2RkVEU3JHS3Y0RmxYVVoyN3o0R2dnajJIbitQaHJxSXo1SE9UcXFXRWVTQzhoemZDYVYyb1RJSGMxZ09oS2FGTGFIeEpURm8yRTdieWkwSkxXbGZaWFlOWml4a0hGbWdySk5jaU1jT2NKbEppVW5XVWxwNkZrM2hUaVhzVVJlNjZJeFR4OHYxK1d6UXNaT0xCdWpxV2lzTnZTYWlJa2l6R2dqN1R3R1dOMTB0ZU1DbDNURUxKdzZPR1ZRcmtWeTZCbGxCZ2twT3k3N3JVUS9TcUhVdlJmQTBzcHRnUFFXeEdWNllyRVVJVkFRZlpoa1c5MXR6MlJMZmJyckxSRVJ4cUxIWkZjQ3BmaVBVY2d4NEpUV3d3a2ZoMGNBYVUxSWNWcW5vSDhyTUhPUjNocFZlQ1FKMFQ2TzFsalVxYU5qSVpBNHZ0NTJwVUFKdVk0bU1kY3FxTjAvV0xVPSIsImRhdGEyIjoyfQ==";

int failures = 0;

void check(bool cond, const std::string& what) {
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (!cond) ++failures;
}

}  // namespace

int main() {
    std::printf("=== 加密模块自测 ===\n");

    // 1. base64 往返
    {
        const std::string s = "Hello, Go2! 测试中文";
        std::vector<uint8_t> bytes(s.begin(), s.end());
        const auto round = go2::crypto::base64Decode(go2::crypto::base64Encode(bytes));
        check(std::string(round.begin(), round.end()) == s, "base64 编解码往返一致");
    }

    // 2. MD5（校验算法核心）
    check(go2::crypto::md5Hex("abc") == "900150983cd24fb0d6963f7d28e17f72",
          "MD5(\"abc\") 结果正确");

    // 3. AES-256-ECB 往返（PKCS7）
    {
        const std::string key = "0123456789abcdef0123456789abcdef";
        const std::string plain = "test sdp payload with padding \r\n";
        const auto enc = go2::crypto::aes256EcbEncryptBase64(plain, key);
        const auto dec = go2::crypto::aes256EcbDecryptBase64(enc, key);
        check(dec == plain, "AES-256-ECB + PKCS7 往返一致");
    }

    // 4. 用真实抓包数据走完整链路：base64 -> JSON -> AES-GCM -> 公钥提取 -> 路径推导
    {
        const auto outer = go2::crypto::base64Decode(kRealConNotify);
        const std::string outerStr(outer.begin(), outer.end());
        auto j = nlohmann::json::parse(outerStr);
        check(j.value("data2", -1) == 2, "真实数据 data2 == 2");

        const std::string data1 = j.at("data1").get<std::string>();
        const auto raw = go2::crypto::base64Decode(data1);

        const std::vector<uint8_t> gcmKey = {232, 86, 130, 189, 22, 84, 155, 0,
                                             142, 4, 166, 104, 43, 179, 235, 227};
        std::string plain;
        bool ok = true;
        try {
            plain = go2::crypto::aesGcmDecrypt(raw, gcmKey);
        } catch (const std::exception& e) {
            ok = false;
            std::printf("      GCM 异常: %s\n", e.what());
        }
        check(ok && plain.size() > 20, "AES-GCM 解密真实数据成功（验证通过）");

        if (ok && plain.size() > 20) {
            const std::string pubB64 = plain.substr(10, plain.size() - 20);
            std::printf("      公钥 base64 长度 = %zu\n", pubB64.size());

            // 路径后缀推导
            static const std::string table = "ABCDEFGHIJ";
            const std::string last10 = plain.substr(plain.size() - 10);
            std::string ending;
            for (size_t i = 0; i + 1 < last10.size(); i += 2) {
                const auto pos = table.find(last10[i + 1]);
                if (pos != std::string::npos) ending += std::to_string(pos);
            }
            std::printf("      末 10 字符 = \"%s\" -> 路径后缀 = \"%s\"\n",
                        last10.c_str(), ending.c_str());
            check(!ending.empty(), "路径后缀推导成功");

            // RSA 公钥必须能被解析（走一次加密来验证）
            bool rsaOk = true;
            try {
                const auto ct = go2::crypto::rsaEncryptBase64("0123456789abcdef0123456789abcdef",
                                                              pubB64);
                const auto ctBytes = go2::crypto::base64Decode(ct);
                rsaOk = (ctBytes.size() == 256);  // RSA-2048
                std::printf("      RSA 密文长度 = %zu (期望 256)\n", ctBytes.size());
            } catch (const std::exception& e) {
                rsaOk = false;
                std::printf("      RSA 异常: %s\n", e.what());
            }
            check(rsaOk, "真实公钥可解析并完成 RSA-2048 加密");
        }
    }

    // 5. 指纹裁剪
    {
        const std::string sdp =
            "v=0\r\n"
            "a=fingerprint:sha-256 AA:BB\r\n"
            "a=fingerprint:sha-384 CC:DD\r\n"
            "a=fingerprint:sha-512 EE:FF\r\n"
            "m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n";
        const auto out = go2::stripExtraFingerprints(sdp);
        const bool onlySha256 = out.find("sha-384") == std::string::npos &&
                                out.find("sha-512") == std::string::npos &&
                                out.find("sha-256") != std::string::npos;
        check(onlySha256, "stripExtraFingerprints 只保留 sha-256");
        check(out.find("m=application") != std::string::npos, "裁剪后不影响其他 SDP 行");
    }

    std::printf("\n结果: %s（失败 %d 项）\n", failures == 0 ? "全部通过 ✔" : "存在失败 ✘",
                failures);
    return failures == 0 ? 0 : 1;
}
