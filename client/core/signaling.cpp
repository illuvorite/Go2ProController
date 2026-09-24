#include "signaling.hpp"

#include "crypto.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "net.hpp"  // 平台网络层（POSIX / Winsock 由它选择）

#include <cstdlib>
#include <stdexcept>
#include <vector>

namespace go2 {

namespace {

// Go2 < 1.1.15 的静态 AES-GCM 密钥（data2 == 2 分支）
const std::vector<uint8_t> kLegacyGcmKey = {
    232, 86, 130, 189, 22, 84, 155, 0, 142, 4, 166, 104, 43, 179, 235, 227};

/// 两台机器人可能分别只开 9991 或只开 8081，顺序固定为「新 -> 旧」
const int kPortsToProbe[] = {9991, 8081};

}  // namespace

const char* toString(SignalingKind kind) {
    switch (kind) {
        case SignalingKind::ConNotify:   return "con_notify (:9991)";
        case SignalingKind::LegacyOffer: return "legacy /offer (:8081)";
    }
    return "?";
}

std::string SignalingEndpoint::describe() const {
    if (!valid()) return "未探测到信令端口";
    return std::to_string(port) + " (" +
           (kind == SignalingKind::ConNotify ? "con_notify" : "legacy /offer") + ")";
}

/// "a1b2c3..." (32 hex -> 16 字节)。非法输入返回空向量
static std::vector<uint8_t> hexToBytes(const std::string& hex) {
    std::vector<uint8_t> out;
    if (hex.size() < 2 || hex.size() % 2 != 0) return out;
    auto val = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        const int hi = val(hex[i]), lo = val(hex[i + 1]);
        if (hi < 0 || lo < 0) return {};
        out.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return out;
}

LocalSignaling::LocalSignaling(std::string robotIp, int port)
    : robotIp_(std::move(robotIp)), port_(port) {}

bool LocalSignaling::probePort(const std::string& ip, int port, int timeoutMs) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;

    const int flags = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(static_cast<uint16_t>(port));
    if (::inet_pton(AF_INET, ip.c_str(), &sa.sin_addr) != 1) {
        ::close(fd);
        return false;
    }

    bool open = false;
    if (::connect(fd, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) == 0) {
        open = true;
    } else if (errno == EINPROGRESS) {
        pollfd p{fd, POLLOUT, 0};
        if (::poll(&p, 1, timeoutMs) > 0) {
            int err = 0;
            socklen_t len = sizeof(err);
            ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
            open = (err == 0);
        }
    }

    if (open) {
        // 优雅关闭写端：机器狗信令服务器单线程 accept，
        // 裸 close 会让它等 Keep-Alive 超时（约 4 秒）才释放连接。
        ::shutdown(fd, SHUT_WR);
    }
    ::close(fd);
    return open;
}

SignalingEndpoint LocalSignaling::probeEndpoint(const std::string& ip, int timeoutMs) {
    for (int port : kPortsToProbe) {
        if (probePort(ip, port, timeoutMs)) {
            SignalingEndpoint ep;
            ep.port = port;
            ep.kind = (port == 9991) ? SignalingKind::ConNotify
                                     : SignalingKind::LegacyOffer;
            return ep;
        }
    }
    return {};
}

SignalingEndpoint LocalSignaling::endpoint() const {
    if (port_ > 0) {
        SignalingEndpoint ep;
        ep.port = port_;
        ep.kind = (port_ == 8081) ? SignalingKind::LegacyOffer : SignalingKind::ConNotify;
        return ep;
    }
    const SignalingEndpoint ep = probeEndpoint(robotIp_);
    if (!ep.valid())
        throw std::runtime_error(
            robotIp_ + " 的 9991 与 8081 端口都连不上。请依次确认：\n"
            "        1) 机器人已开机且 WiFi 已连到与电脑同一局域网（或电脑已连机器狗热点）；\n"
            "        2) `ping " + robotIp_ + "` 通；\n"
            "        3) 该 IP 是机器狗的地址（不是路由器/其它设备占用）；\n"
            "        4) 电脑防火墙未拦截出站 TCP；\n"
            "        5) 若机器人是 AP 模式（自己开热点），地址通常是 192.168.12.1。");
    return ep;
}

ConNotifyResult LocalSignaling::fetchConNotify(
    const std::vector<std::string>& aesKeyCandidates) const {
    const SignalingEndpoint ep = endpoint();
    if (ep.kind != SignalingKind::ConNotify)
        throw std::runtime_error(
            robotIp_ + " 只提供 " + ep.describe() +
            "（Go2 < 1.1.11 旧固件），没有 con_notify 接口；"
            "客户端会自动改走明文 /offer 流程，无需人工处理");

    httplib::Client cli(robotIp_, ep.port);
    cli.set_connection_timeout(5);
    cli.set_read_timeout(8);

    auto res = cli.Get("/con_notify");
    if (!res)
        throw std::runtime_error("con_notify 请求失败: " + httplib::to_string(res.error()) +
                                 "（机器人信令服务未响应，稍后重试或先断开手机 App）");
    if (res->status == 429) throw RobotThrottledError(robotIp_);
    if (res->status != 200)
        throw std::runtime_error("con_notify 返回 HTTP " + std::to_string(res->status));

    const auto decodedBytes = crypto::base64Decode(res->body);
    const std::string decodedStr(decodedBytes.begin(), decodedBytes.end());

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(decodedStr);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("con_notify JSON 解析失败: ") + e.what());
    }

    ConNotifyResult out;
    out.data2 = j.value("data2", 1);
    const std::string data1 = j.at("data1").get<std::string>();

    if (out.data2 == 2) {
        out.rawPlaintext = crypto::aesGcmDecrypt(crypto::base64Decode(data1), kLegacyGcmKey);
        out.usedLegacyStaticKey = true;
    } else if (out.data2 == 1) {
        out.rawPlaintext = data1;
    } else if (out.data2 == 3) {
        // 新固件（Go2 ≥ 1.1.15 / G1 ≥ 1.5.1 / R1）：data1 用每设备 AES-128-GCM 加密。
        // 候选 key 逐个尝试——GCM 的认证标签校验通过即为这台机器的钥匙。
        std::string lastErr;
        for (const auto& hexKey : aesKeyCandidates) {
            const auto key = hexToBytes(hexKey);
            if (key.size() != 16) continue;
            try {
                out.rawPlaintext =
                    crypto::aesGcmDecrypt(crypto::base64Decode(data1), key);
                out.usedKeyHex = hexKey;
                break;  // GCM 标签校验通过
            } catch (const std::exception& e) {
                lastErr = e.what();
            }
        }
        // 兜底：个别固件把 data2 报成 3，但 data1 仍能用内置静态 key 解开。
        // 只按 data2 判断会给出"缺钥匙"的假故障，所以这里也实测一次。
        if (out.rawPlaintext.empty()) {
            try {
                out.rawPlaintext =
                    crypto::aesGcmDecrypt(crypto::base64Decode(data1), kLegacyGcmKey);
                out.usedLegacyStaticKey = true;
            } catch (...) {
                // 静态 key 也解不开，保持空
            }
        }
        if (out.rawPlaintext.empty()) {
            const std::string hint =
                aesKeyCandidates.empty()
                    ? "本机当前没有任何候选钥匙：请在界面顶栏「云账号」用官方 App "
                      "绑定该机器狗的宇树账号登录，拉取每设备 aes key 后重连"
                      "（两台 Pro 若绑在不同账号，分别登录即可，钥匙会累加）。"
                    : ("请确认该机器狗已在官方 App 绑定当前登录的宇树账号。"
                       "原始错误: " + lastErr);
            throw std::runtime_error(
                "机器人返回 data2 = 3：候选每设备钥匙与内置静态 key 均未通过 GCM 校验，"
                "说明这台机器狗确实需要它自己的每设备 AES-128 钥匙。" + hint);
        }
    } else {
        throw std::runtime_error(
            "机器人返回 data2 = " + std::to_string(out.data2) +
            "，未知固件协议（详见 docs/go2_webrtc_protocol.md）");
    }

    if (out.rawPlaintext.size() < 21)
        throw std::runtime_error("con_notify 解密结果过短");

    // 前后各去掉 10 个字符后是 base64(DER) 的 RSA 公钥
    out.publicKeyDerB64 = out.rawPlaintext.substr(10, out.rawPlaintext.size() - 20);

    // 路径后缀：取末 10 字符，两两分组，每组第二个字符在 "A".."J" 中的下标
    static const std::string kTable = "ABCDEFGHIJ";
    const std::string last10 = out.rawPlaintext.substr(out.rawPlaintext.size() - 10);
    std::string ending;
    for (size_t i = 0; i + 1 < last10.size(); i += 2) {
        const auto pos = kTable.find(last10[i + 1]);
        if (pos != std::string::npos) ending += std::to_string(pos);
    }
    out.pathEnding = ending;
    return out;
}

std::string LocalSignaling::exchangeSdp(const ConNotifyResult& notify,
                                        const std::string& sdpOfferJson) const {
    const SignalingEndpoint ep = endpoint();
    const std::string aesKey = crypto::randomHex32();

    nlohmann::json body;
    body["data1"] = crypto::aes256EcbEncryptBase64(sdpOfferJson, aesKey);
    body["data2"] = crypto::rsaEncryptBase64(aesKey, notify.publicKeyDerB64);

    httplib::Client cli(robotIp_, ep.port);
    cli.set_connection_timeout(5);
    cli.set_read_timeout(20);

    const std::string path = "/con_ing_" + notify.pathEnding;
    auto res = cli.Post(path, body.dump(), "application/x-www-form-urlencoded");
    if (!res) throw std::runtime_error("SDP 提交失败: " + httplib::to_string(res.error()));
    if (res->status != 200)
        throw std::runtime_error("SDP 提交返回 HTTP " + std::to_string(res->status) +
                                 "（机器人可能被手机 App 占用）");

    return crypto::aes256EcbDecryptBase64(res->body, aesKey);
}

std::string LocalSignaling::exchangeSdpLegacy(const std::string& sdpOfferJson) const {
    const SignalingEndpoint ep = endpoint();
    if (ep.port != 8081)
        throw std::runtime_error("内部错误：legacy /offer 流程需要在 8081 端口调用");

    httplib::Client cli(robotIp_, ep.port);
    cli.set_connection_timeout(5);
    cli.set_read_timeout(20);

    auto res = cli.Post("/offer", sdpOfferJson, "application/json");
    if (!res) throw std::runtime_error("legacy /offer 请求失败: " +
                                       httplib::to_string(res.error()));
    if (res->status != 200)
        throw std::runtime_error("legacy /offer 返回 HTTP " + std::to_string(res->status) +
                                 "（机器人可能被手机 App 占用，或固件不支持该接口）");
    return res->body;
}

namespace {

/// 把 SDP 按 \r\n 切分成行（保留原始分隔符风格）
std::vector<std::string> splitLines(const std::string& sdp) {
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos <= sdp.size()) {
        size_t nl = sdp.find("\r\n", pos);
        if (nl == std::string::npos) {
            if (pos < sdp.size()) out.push_back(sdp.substr(pos));
            break;
        }
        out.push_back(sdp.substr(pos, nl - pos));
        pos = nl + 2;
    }
    return out;
}

std::string joinLines(const std::vector<std::string>& lines) {
    std::string out;
    for (size_t i = 0; i < lines.size(); ++i) {
        out += lines[i];
        if (i + 1 < lines.size()) out += "\r\n";
    }
    return out;
}

/// 取 a=candidate 行里的地址字段（第 5 个 token）
std::string candidateAddress(const std::string& line) {
    if (line.rfind("a=candidate:", 0) != 0) return {};
    std::vector<std::string> tok;
    size_t p = 0;
    while (p < line.size()) {
        size_t sp = line.find(' ', p);
        if (sp == std::string::npos) {
            tok.push_back(line.substr(p));
            break;
        }
        tok.push_back(line.substr(p, sp - p));
        p = sp + 1;
    }
    return tok.size() >= 5 ? tok[4] : std::string();
}

bool isIpv6Candidate(const std::string& line) {
    const std::string addr = candidateAddress(line);
    return !addr.empty() && addr.find(':') != std::string::npos;
}

}  // namespace

std::vector<std::string> candidateAddresses(const std::string& sdp) {
    std::vector<std::string> out;
    for (const auto& line : splitLines(sdp)) {
        const std::string addr = candidateAddress(line);
        if (!addr.empty()) out.push_back(addr);
    }
    return out;
}

SdpTweaks sdpTweaksFromEnv() {
    SdpTweaks t;
    auto off = [](const char* name) {
        const char* v = std::getenv(name);
        return v && *v && std::string(v) != "0";
    };
    if (off("GO2_NO_STRIP_ICEOPT")) t.stripIceOptions = false;
    if (off("GO2_NO_STRIP_GROUPLS")) t.stripGroupLs = false;
    if (off("GO2_NO_STRIP_MAXMSG")) t.stripMaxMessageSize = false;
    if (off("GO2_IPV4_ONLY")) t.ipv4Only = true;

    // GO2_KEEP_CANDIDATES=192.168.2.106,10.0.0.5 —— 只保留这些本地地址的候选。
    // 多网卡（含 WSL / Hyper-V / VMware 虚拟网卡）时，机器人可能挑到不可达的候选。
    if (const char* v = std::getenv("GO2_KEEP_CANDIDATES")) {
        std::string s(v);
        size_t p = 0;
        while (p < s.size()) {
            size_t c = s.find(',', p);
            std::string item = s.substr(p, c == std::string::npos ? std::string::npos : c - p);
            while (!item.empty() && item.front() == ' ') item.erase(item.begin());
            while (!item.empty() && item.back() == ' ') item.pop_back();
            if (!item.empty()) t.keepCandidateAddresses.push_back(item);
            if (c == std::string::npos) break;
            p = c + 1;
        }
    }
    return t;
}

std::string adjustSdpForGo2(const std::string& sdp, const SdpTweaks& tweaks) {
    const auto lines = splitLines(sdp);
    std::vector<std::string> out;
    out.reserve(lines.size());

    for (const auto& line : lines) {
        if (tweaks.stripIceOptions && line.rfind("a=ice-options:", 0) == 0) continue;
        if (tweaks.stripGroupLs && line.rfind("a=group:LS", 0) == 0) continue;
        if (tweaks.stripMaxMessageSize && line.rfind("a=max-message-size:", 0) == 0) continue;
        if (tweaks.ipv4Only && isIpv6Candidate(line)) continue;
        if (!tweaks.keepCandidateAddresses.empty()) {
            const std::string addr = candidateAddress(line);
            if (!addr.empty()) {
                bool keep = false;
                for (const auto& allowed : tweaks.keepCandidateAddresses)
                    if (addr == allowed) { keep = true; break; }
                if (!keep) continue;  // 丢掉虚拟网卡等无关候选
            }
        }
        out.push_back(line);
    }
    return joinLines(out);
}

std::string stripExtraFingerprints(const std::string& sdp) {
    const std::string& kPrefixSha256 = "a=fingerprint:sha-256";
    const std::string& kPrefixFp = "a=fingerprint:";

    std::string out;
    out.reserve(sdp.size());

    size_t pos = 0;
    while (pos <= sdp.size()) {
        size_t nl = sdp.find("\r\n", pos);
        const bool last = (nl == std::string::npos);
        const std::string line = last ? sdp.substr(pos) : sdp.substr(pos, nl - pos);

        const bool isFingerprint = line.rfind(kPrefixFp, 0) == 0;
        const bool keep = !isFingerprint || line.rfind(kPrefixSha256, 0) == 0;
        if (keep) {
            out += line;
            if (!last) out += "\r\n";
        } else if (!last) {
            // 删掉这一行时，连带把它的行尾也去掉
        }

        if (last) break;
        pos = nl + 2;
    }
    return out;
}

}  // namespace go2
