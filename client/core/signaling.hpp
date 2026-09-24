#pragma once

#include <stdexcept>
#include <string>
#include <vector>

namespace go2 {

/// 机器狗局域网信令流程（两种固件走两条完全不同的路）
enum class SignalingKind {
    ConNotify,    ///< :9991 —— Go2 > 1.1.11 / 全部 G1、R1：con_notify + RSA/AES 加密 SDP
    LegacyOffer,  ///< :8081 —— 旧固件 Go2（< 1.1.11）：明文 JSON POST /offer
};

const char* toString(SignalingKind kind);

/// 机器人信令限流：短时间内重连过于频繁，机器狗返回 HTTP 429。
/// 实测表现：连续两次连接同一台狗（间隔几秒）就会命中；此时即使握手成功，
/// 最初几秒下发的运动指令也会被拒绝（回执 code=-1）。应等待 10~15 秒再连。
class RobotThrottledError : public std::runtime_error {
public:
    explicit RobotThrottledError(const std::string& robotIp)
        : std::runtime_error(
              robotIp +
              " 返回 HTTP 429：机器人信令被限流（短时间内重连过于频繁）。"
              "请等 10~15 秒再连；重连间隔建议 ≥15 秒。"
              "此时即使连上，最初几秒下发的指令也会被拒（code=-1）") {}
};

/// 探测到的信令端
struct SignalingEndpoint {
    int port = 0;                              ///< 9991 或 8081；0 = 未探测到
    SignalingKind kind = SignalingKind::ConNotify;
    bool valid() const { return port > 0; }
    /// "9991 (con_notify)" / "8081 (legacy /offer)"
    std::string describe() const;
};

/// con_notify 握手结果
struct ConNotifyResult {
    int data2 = 0;                 ///< 1 = 明文；2 = legacy 静态 GCM key；3 = 每设备 AES-128 key
    std::string publicKeyDerB64;   ///< base64(DER) 的 RSA 公钥
    std::string pathEnding;        ///< con_ing_<pathEnding>
    std::string rawPlaintext;      ///< 解密后的完整 data1（调试用）
    std::string usedKeyHex;        ///< data2==3 时命中的那把每设备钥匙（32 hex）；否则为空
    bool usedLegacyStaticKey = false;  ///< data2==2，用的是内置静态 GCM key
};

/// 本机信令（局域网直连机器人 9991 / 8081 端口）
class LocalSignaling {
public:
    /// @param port 0 = 自动探测（9991 优先，回退 8081）；指定则强制使用该端口
    explicit LocalSignaling(std::string robotIp, int port = 0);

    /// 探测信令端。两个端口都不通时抛异常，异常信息里给出可操作的排查建议。
    SignalingEndpoint endpoint() const;

    /// 第一步：GET /con_notify，取回并解密公钥与路径标识（仅 ConNotify 流程可用）
    /// @param aesKeyCandidates data2==3（新固件）时的每设备 AES-128 key 候选列表
    ///         （32 位 hex；逐个尝试，GCM 校验通过的那个就是本机钥匙）
    ///         data2==3 但候选为空 / 全部不匹配时抛出可区分的错误信息。
    ConNotifyResult fetchConNotify(
        const std::vector<std::string>& aesKeyCandidates = {}) const;

    /// 第二步（ConNotify 流程）：加密 SDP offer 并 POST /con_ing_<path>，
    /// 返回解密后的 SDP answer(JSON 文本)
    std::string exchangeSdp(const ConNotifyResult& notify,
                            const std::string& sdpOfferJson) const;

    /// 第二步（LegacyOffer 流程，Go2 < 1.1.11）：明文 JSON POST /offer，
    /// 请求体与新版一致（{"id","sdp","type","token"}），返回 answer(JSON 文本)
    std::string exchangeSdpLegacy(const std::string& sdpOfferJson) const;

    const std::string& robotIp() const { return robotIp_; }

    /// TCP 端口是否可连（带优雅 shutdown，避免占住机器人单线程信令服务）
    static bool probePort(const std::string& ip, int port, int timeoutMs = 700);

    /// 按 9991 -> 8081 顺序探测信令端；都不通返回 valid()==false
    static SignalingEndpoint probeEndpoint(const std::string& ip, int timeoutMs = 700);

private:
    std::string robotIp_;
    int port_;  // 0 = 自动
};

/// 去掉 SDP 里除 sha-256 之外的指纹行。
/// 实测：机器狗无法处理多条 a=fingerprint，会导致 DTLS 静默失败。
std::string stripExtraFingerprints(const std::string& sdp);

/// SDP 调整开关：把 libdatachannel 生成的 offer 对齐成参考实现（aiortc）的结构。
/// 参考实现能正常连通，而 libdatachannel 默认输出与其有若干差异。
struct SdpTweaks {
    bool stripIceOptions = true;    ///< 去掉 a=ice-options:ice2,trickle
    bool stripGroupLs = true;       ///< 去掉 a=group:LS
    bool stripMaxMessageSize = true;///< 去掉 a=max-message-size
    bool ipv4Only = false;          ///< 去掉 IPv6 候选地址
    /// 只保留这些地址的 ICE 候选（空 = 不过滤）。用于多网卡机器上
    /// 排除 WSL/Hyper-V/VMware 等虚拟网卡的候选，避免机器人选错路径。
    std::vector<std::string> keepCandidateAddresses;
};

/// 读取环境变量生成默认开关（GO2_NO_STRIP_ICEOPT / GO2_IPV4_ONLY 等），便于快速试错
SdpTweaks sdpTweaksFromEnv();

/// 按开关调整 SDP
std::string adjustSdpForGo2(const std::string& sdp, const SdpTweaks& tweaks);

/// 列出 SDP 里所有 a=candidate 的地址（诊断用）
std::vector<std::string> candidateAddresses(const std::string& sdp);

}  // namespace go2
