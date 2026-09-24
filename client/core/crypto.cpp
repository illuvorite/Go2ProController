#include "crypto.hpp"

#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

#include <algorithm>
#include <iterator>
#include <stdexcept>

namespace go2::crypto {

namespace {

const char kB64Chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string opensslError() {
    char buf[256] = {0};
    ERR_error_string_n(ERR_get_error(), buf, sizeof(buf));
    return buf;
}

}  // namespace

std::string base64Encode(const std::vector<uint8_t>& data) {
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    size_t i = 0;
    while (i + 2 < data.size()) {
        uint32_t n = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
        out.push_back(kB64Chars[(n >> 18) & 63]);
        out.push_back(kB64Chars[(n >> 12) & 63]);
        out.push_back(kB64Chars[(n >> 6) & 63]);
        out.push_back(kB64Chars[n & 63]);
        i += 3;
    }
    const size_t rem = data.size() - i;
    if (rem == 1) {
        uint32_t n = data[i] << 16;
        out.push_back(kB64Chars[(n >> 18) & 63]);
        out.push_back(kB64Chars[(n >> 12) & 63]);
        out.push_back('=');
        out.push_back('=');
    } else if (rem == 2) {
        uint32_t n = (data[i] << 16) | (data[i + 1] << 8);
        out.push_back(kB64Chars[(n >> 18) & 63]);
        out.push_back(kB64Chars[(n >> 12) & 63]);
        out.push_back(kB64Chars[(n >> 6) & 63]);
        out.push_back('=');
    }
    return out;
}

std::vector<uint8_t> base64Decode(const std::string& text) {
    static int8_t table[256];
    static bool init = false;
    if (!init) {
        std::fill(std::begin(table), std::end(table), int8_t(-1));
        for (int i = 0; i < 64; ++i) table[static_cast<uint8_t>(kB64Chars[i])] = int8_t(i);
        init = true;
    }

    std::vector<uint8_t> out;
    out.reserve(text.size() * 3 / 4 + 3);
    uint32_t buf = 0;
    int bits = 0;
    for (unsigned char c : text) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
        const int8_t v = table[c];
        if (v < 0) throw std::runtime_error("base64Decode: 非法字符");
        buf = (buf << 6) | uint32_t(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(uint8_t((buf >> bits) & 0xFF));
        }
    }
    return out;
}

std::string md5Hex(const std::string& input) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    if (EVP_Digest(input.data(), input.size(), digest, &len, EVP_md5(), nullptr) != 1)
        throw std::runtime_error("EVP_Digest(MD5) 失败: " + opensslError());

    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (unsigned int i = 0; i < len; ++i) {
        out.push_back(hex[digest[i] >> 4]);
        out.push_back(hex[digest[i] & 0x0F]);
    }
    return out;
}

namespace {

std::vector<uint8_t> pkcs7Pad(const std::string& s) {
    std::vector<uint8_t> data(s.begin(), s.end());
    const size_t pad = 16 - (data.size() % 16);
    data.insert(data.end(), pad, uint8_t(pad));
    return data;
}

std::string pkcs7Unpad(const std::vector<uint8_t>& data) {
    if (data.empty()) return {};
    const uint8_t pad = data.back();
    if (pad == 0 || pad > 16 || pad > data.size())
        throw std::runtime_error("PKCS7 去填充失败");
    return std::string(data.begin(), data.end() - pad);
}

}  // namespace

std::string aes256EcbEncryptBase64(const std::string& plain, const std::string& key32) {
    if (key32.size() != 32) throw std::runtime_error("AES 密钥必须为 32 字节");

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) throw std::runtime_error("EVP_CIPHER_CTX_new 失败");

    const auto padded = pkcs7Pad(plain);
    std::vector<uint8_t> out(padded.size() + 32);
    int outLen = 0, total = 0;

    bool ok = EVP_EncryptInit_ex(ctx, EVP_aes_256_ecb(), nullptr,
                                 reinterpret_cast<const unsigned char*>(key32.data()),
                                 nullptr) == 1;
    if (ok) ok = EVP_CIPHER_CTX_set_padding(ctx, 0) == 1;  // 自己做了 PKCS7
    if (ok) ok = EVP_EncryptUpdate(ctx, out.data(), &outLen, padded.data(),
                                   int(padded.size())) == 1;
    if (ok) {
        total = outLen;
        ok = EVP_EncryptFinal_ex(ctx, out.data() + total, &outLen) == 1;
        total += outLen;
    }
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) throw std::runtime_error("AES-256-ECB 加密失败: " + opensslError());

    out.resize(size_t(total));
    return base64Encode(out);
}

std::string aes256EcbDecryptBase64(const std::string& b64, const std::string& key32) {
    if (key32.size() != 32) throw std::runtime_error("AES 密钥必须为 32 字节");
    const auto raw = base64Decode(b64);
    if (raw.size() % 16 != 0) throw std::runtime_error("AES 密文长度不是 16 的倍数");

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) throw std::runtime_error("EVP_CIPHER_CTX_new 失败");

    std::vector<uint8_t> out(raw.size() + 32);
    int outLen = 0, total = 0;

    bool ok = EVP_DecryptInit_ex(ctx, EVP_aes_256_ecb(), nullptr,
                                 reinterpret_cast<const unsigned char*>(key32.data()),
                                 nullptr) == 1;
    if (ok) ok = EVP_CIPHER_CTX_set_padding(ctx, 0) == 1;
    if (ok) ok = EVP_DecryptUpdate(ctx, out.data(), &outLen, raw.data(), int(raw.size())) == 1;
    if (ok) {
        total = outLen;
        ok = EVP_DecryptFinal_ex(ctx, out.data() + total, &outLen) == 1;
        total += outLen;
    }
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) throw std::runtime_error("AES-256-ECB 解密失败: " + opensslError());

    out.resize(size_t(total));
    return pkcs7Unpad(out);
}

std::string aesGcmDecrypt(const std::vector<uint8_t>& raw, const std::vector<uint8_t>& key) {
    if (raw.size() < 28) throw std::runtime_error("GCM 密文过短（至少 28 字节）");

    const size_t tagLen = 16;
    const size_t nonceLen = 12;
    const uint8_t* tag = raw.data() + raw.size() - tagLen;
    const uint8_t* nonce = raw.data() + raw.size() - tagLen - nonceLen;
    const size_t ctLen = raw.size() - tagLen - nonceLen;

    // 按密钥长度选择算法：16 -> AES-128，24 -> AES-192，32 -> AES-256
    // 注意：Go2 legacy GCM key 是 16 字节，必须用 AES-128-GCM
    const EVP_CIPHER* cipher = nullptr;
    switch (key.size()) {
        case 16: cipher = EVP_aes_128_gcm(); break;
        case 24: cipher = EVP_aes_192_gcm(); break;
        case 32: cipher = EVP_aes_256_gcm(); break;
        default: throw std::runtime_error("GCM 密钥长度非法（需 16/24/32 字节）");
    }

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) throw std::runtime_error("EVP_CIPHER_CTX_new 失败");

    std::vector<uint8_t> out(ctLen + 16);
    int outLen = 0, total = 0;

    bool ok = EVP_DecryptInit_ex(ctx, cipher, nullptr, nullptr, nullptr) == 1;
    if (ok) ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, int(nonceLen), nullptr) == 1;
    if (ok) ok = EVP_DecryptInit_ex(ctx, nullptr, nullptr, key.data(), nonce) == 1;
    if (ok && ctLen > 0)
        ok = EVP_DecryptUpdate(ctx, out.data(), &outLen, raw.data(), int(ctLen)) == 1;
    if (ok) {
        total = outLen;
        ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, int(tagLen),
                                 const_cast<uint8_t*>(tag)) == 1;
    }
    if (ok) ok = EVP_DecryptFinal_ex(ctx, out.data() + total, &outLen) == 1;
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) throw std::runtime_error("AES-256-GCM 解密/校验失败（密钥不对？）");

    total += outLen;
    return std::string(out.begin(), out.begin() + total);
}

std::string rsaEncryptBase64(const std::string& data, const std::string& pubKeyDerB64) {
    const auto der = base64Decode(pubKeyDerB64);

    // 兼容 SubjectPublicKeyInfo 与 PKCS#1 两种 DER 编码
    const unsigned char* p = der.data();
    EVP_PKEY* pkey = d2i_PUBKEY(nullptr, &p, long(der.size()));
    if (!pkey) {
        ERR_clear_error();
        p = der.data();
        RSA* rsa = d2i_RSAPublicKey(nullptr, &p, long(der.size()));
        if (rsa) {
            pkey = EVP_PKEY_new();
            if (!pkey || EVP_PKEY_assign_RSA(pkey, rsa) != 1) {
                if (pkey) EVP_PKEY_free(pkey);
                RSA_free(rsa);
                pkey = nullptr;
            }
        }
    }
    if (!pkey) throw std::runtime_error("无法解析 RSA 公钥 DER");

    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(pkey, nullptr);
    if (!ctx || EVP_PKEY_encrypt_init(ctx) <= 0 ||
        EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_PADDING) <= 0) {
        if (ctx) EVP_PKEY_CTX_free(ctx);
        EVP_PKEY_free(pkey);
        throw std::runtime_error("RSA 上下文初始化失败: " + opensslError());
    }

    const size_t keySize = size_t(EVP_PKEY_size(pkey));
    const size_t maxChunk = keySize - 11;  // PKCS#1 v1.5 开销
    std::vector<uint8_t> out;
    out.reserve(((data.size() / maxChunk) + 1) * keySize);

    for (size_t off = 0; off < data.size(); off += maxChunk) {
        const size_t n = std::min(maxChunk, data.size() - off);
        size_t outLen = keySize;
        std::vector<uint8_t> buf(keySize);
        if (EVP_PKEY_encrypt(ctx, buf.data(), &outLen,
                             reinterpret_cast<const unsigned char*>(data.data() + off),
                             n) <= 0) {
            EVP_PKEY_CTX_free(ctx);
            EVP_PKEY_free(pkey);
            throw std::runtime_error("RSA 加密失败: " + opensslError());
        }
        out.insert(out.end(), buf.begin(), buf.begin() + long(outLen));
    }

    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return base64Encode(out);
}

std::string randomHex32() {
    unsigned char buf[16];
    if (RAND_bytes(buf, sizeof(buf)) != 1)
        throw std::runtime_error("RAND_bytes 失败");
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(32);
    for (unsigned char b : buf) {
        out.push_back(hex[b >> 4]);
        out.push_back(hex[b & 0x0F]);
    }
    return out;
}

}  // namespace go2::crypto
