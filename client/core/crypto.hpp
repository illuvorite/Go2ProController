#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace go2::crypto {

std::string base64Encode(const std::vector<uint8_t>& data);
std::vector<uint8_t> base64Decode(const std::string& text);

std::string md5Hex(const std::string& input);

// AES-256-ECB + PKCS7 —— 用于 SDP 加解密，结果/输入均为 base64
std::string aes256EcbEncryptBase64(const std::string& plain, const std::string& key32);
std::string aes256EcbDecryptBase64(const std::string& b64, const std::string& key32);

// AES-256-GCM 解密 —— con_notify 响应里 data2 == 2 的分支
// 输入格式: [ciphertext ...][nonce(12)][tag(16)]
std::string aesGcmDecrypt(const std::vector<uint8_t>& raw, const std::vector<uint8_t>& key);

// RSA PKCS#1 v1.5 加密（自动分块），返回 base64
// pubKeyDerB64: base64 编码的 DER 公钥（SPKI 或 PKCS#1 均可）
std::string rsaEncryptBase64(const std::string& data, const std::string& pubKeyDerB64);

// 随机 16 字节 -> 32 个 hex 字符，作为 AES-256 密钥
std::string randomHex32();

}  // namespace go2::crypto
