#include "robot_client.hpp"

#include "crypto.hpp"
#include "signaling.hpp"
#include "sport_library.hpp"

#include <rtc/rtc.hpp>

#include "net.hpp"  // 平台网络层（POSIX / Winsock 由它选择）

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <stdexcept>

namespace go2 {

using namespace std::chrono_literals;

const char* toString(ConnState s) {
    switch (s) {
        case ConnState::Disconnected: return "未连接";
        case ConnState::Signaling:    return "信令交换中";
        case ConnState::Connecting:   return "通道建立中";
        case ConnState::Validating:   return "校验中";
        case ConnState::Ready:        return "就绪";
        case ConnState::Failed:       return "失败";
    }
    return "?";
}

namespace {

std::vector<uint8_t> hexToBytes(const std::string& hex) {
    std::vector<uint8_t> out;
    out.reserve(hex.size() / 2);
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        const int hi = nib(hex[i]), lo = nib(hex[i + 1]);
        if (hi < 0 || lo < 0) break;
        out.push_back(uint8_t((hi << 4) | lo));
    }
    return out;
}

long long nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::string nowTimeStr() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32] = {0};
    std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
    return buf;
}

std::string fullTimeStr() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32] = {0};
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

bool envOn(const char* name) {
    const char* v = std::getenv(name);
    return v && *v && std::string(v) != "0";
}

/// 把 SDP 里所有 m= 行汇总成一行（日志用）
std::string mlineSummary(const std::string& sdp) {
    std::string ml;
    size_t p = 0;
    while ((p = sdp.find("m=", p)) != std::string::npos) {
        size_t e = sdp.find("\r\n", p);
        if (e == std::string::npos) e = sdp.size();
        if (!ml.empty()) ml += " | ";
        ml += sdp.substr(p, e - p);
        p = e;
    }
    return ml;
}

}  // namespace

std::string routeLocalAddress(const std::string& robotIp) {
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return {};
    sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_port = htons(9991);
    if (::inet_pton(AF_INET, robotIp.c_str(), &target.sin_addr) != 1) {
        ::close(fd);
        return {};
    }
    // UDP connect 不发包，只让内核选出到该地址会用的源地址
    if (::connect(fd, reinterpret_cast<sockaddr*>(&target), sizeof(target)) != 0) {
        ::close(fd);
        return {};
    }
    sockaddr_in local{};
    socklen_t len = sizeof(local);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&local), &len) != 0) {
        ::close(fd);
        return {};
    }
    char buf[INET_ADDRSTRLEN] = {0};
    const char* ok = ::inet_ntop(AF_INET, &local.sin_addr, buf, sizeof(buf));
    ::close(fd);
    return ok ? std::string(buf) : std::string();
}

RobotClient::RobotClient() {
    rtc::InitLogger(rtc::LogLevel::Warning);
}

RobotClient::~RobotClient() { disconnect(); }

void RobotClient::setState(ConnState s, const std::string& note) {
    state_.store(s);
    log(std::string("[状态] ") + toString(s) + (note.empty() ? "" : " - " + note));
    if (onStateChanged) onStateChanged(s);
}

void RobotClient::log(const std::string& msg) {
    if (onLog) onLog(msg);
}

std::string RobotClient::lastError() const {
    std::lock_guard<std::mutex> lock(errMutex_);
    return lastError_;
}

LinkStats RobotClient::stats() const {
    LinkStats s;
    s.ready = isReady();
    s.data2 = data2_.load();
    s.signalingPort = usedPort_.load();
    s.txMessages = txMessages_.load();
    s.rxMessages = rxMessages_.load();
    s.txBytes = txBytes_.load();
    s.rxBytes = rxBytes_.load();
    s.heartbeatsSent = heartbeatsSent_.load();
    s.heartbeatsAcked = heartbeatsAcked_.load();
    s.rttMs = rttMs_.load();
    s.reconnects = reconnects_.load();
    const long long last = lastRxMs_.load();
    s.lastRxAgeMs = last > 0 ? (nowMs() - last) : -1;
    const long long readyAt = readyAtMs_.load();
    s.readySeconds = readyAt > 0 ? (nowMs() - readyAt) / 1000 : 0;
    {
        std::lock_guard<std::mutex> lock(peerMutex_);
        s.localAddress = localAddress_;
        s.remoteAddress = remoteAddress_;
        s.connectedSince = connectedSince_;
        s.keySource = keySource_;
    }
    return s;
}

void RobotClient::noteRx(size_t bytes) {
    lastRxMs_.store(nowMs());
    rxMessages_.fetch_add(1);
    rxBytes_.fetch_add(bytes);
}

// ------------------------------------------------------------------ 连接

void RobotClient::setAesKeys(std::vector<std::string> keys) {
    std::lock_guard<std::mutex> lock(aesKeysMutex_);
    aesKeys_ = std::move(keys);
}

void RobotClient::setPinnedKey(std::string keyHex) {
    std::lock_guard<std::mutex> lock(aesKeysMutex_);
    pinnedKey_ = std::move(keyHex);
}

void RobotClient::connect(const std::string& ip) {
    RobotProfile p;
    p.ip = ip;
    connect(ip, p);
}

void RobotClient::connect(const std::string& ip, const RobotProfile& profile) {
    disconnect();

    profile_ = profile;
    profile_.ip = ip;
    targetIp_ = ip;
    usedPort_.store(0);
    data2_.store(0);
    txMessages_ = rxMessages_ = txBytes_ = rxBytes_ = 0;
    heartbeatsSent_ = heartbeatsAcked_ = 0;
    lastRxMs_.store(0);
    rttMs_.store(-1);
    {
        std::lock_guard<std::mutex> lock(peerMutex_);
        localAddress_.clear();
        remoteAddress_.clear();
        keySource_.clear();
    }
    // 有过一次成功会话后再次连接，计为重连
    if (readyAtMs_.load() > 0) reconnects_.fetch_add(1);
    readyAtMs_.store(0);

    std::vector<std::string> keys;
    {
        std::lock_guard<std::mutex> lock(aesKeysMutex_);
        keys = aesKeys_;
    }
    const std::string pinned =
        !profile_.pinnedAesKey.empty() ? profile_.pinnedAesKey : std::string();
    if (!pinned.empty()) keys.insert(keys.begin(), pinned);
    profile_.aesKeyCandidates = keys;

    running_.store(true);  // 作为「本会话仍有效」的取消标志，disconnect() 置 false
    setState(ConnState::Signaling, ip + (profile_.apMode ? " (AP 模式)" : ""));

    workerThread_ = std::thread([this] {
        // ---- 1. 信令阶段（同步，可重试）----
        int attempt = 0;
        for (;;) {
            bool throttled = false;
            try {
                if (runSignaling(profile_)) break;
            } catch (const RobotThrottledError& e) {  // 429：需要更长退避
                throttled = true;
                {
                    std::lock_guard<std::mutex> lock(errMutex_);
                    lastError_ = e.what();
                }
                log(std::string("[限流] ") + e.what());
            } catch (const std::exception& e) {
                {
                    std::lock_guard<std::mutex> lock(errMutex_);
                    lastError_ = e.what();
                }
                log(std::string("[失败] ") + e.what());
            }
            if (!running_.load()) return;  // 已被手动断开
            if (attempt >= profile_.maxRetries) {
                setState(ConnState::Failed, lastError());
                return;
            }
            ++attempt;
            // 限流时至少等 8 秒，避免越试越糟
            const int waitMs = throttled ? std::max(profile_.retryDelayMs * attempt, 8000)
                                         : profile_.retryDelayMs * attempt;
            log("[重试] 第 " + std::to_string(attempt) + "/" +
                std::to_string(profile_.maxRetries) + " 次，等待 " +
                std::to_string(waitMs) + " ms ...");
            for (int i = 0; i < waitMs / 100 && running_.load(); ++i)
                std::this_thread::sleep_for(100ms);
            if (!running_.load()) return;
        }

        // ---- 2. 通道建立 + 校验阶段（由 monitor 线程监管超时与心跳）----
        setState(ConnState::Connecting);
        startMonitor();
    });
}

/// 信令交换：探测端口 -> 取公钥（或走旧流程）-> 生成 offer -> 提交 -> 设远端描述
bool RobotClient::runSignaling(const RobotProfile& profile) {
    LocalSignaling signaling(profile.ip, profile.signalingPort);
    const SignalingEndpoint ep = signaling.endpoint();
    usedPort_.store(ep.port);
    log("[信令] " + profile.ip + " 命中 " + ep.describe());

    const std::string offerId =
        !profile.signalingId.empty() ? profile.signalingId
                                     : (profile.apMode ? std::string() : std::string("STA_localNetwork"));

    // ---- 建 PeerConnection ----
    // 关键：不配置任何 STUN（否则 offer 会多出 srflx 公网候选）
    rtc::Configuration config;
    config.iceServers.clear();
    if (!profile.bindAddress.empty()) {
        config.bindAddress = profile.bindAddress;
        log("[ICE] 绑定本机地址 " + profile.bindAddress);
    } else if (envOn("GO2_BIND_ROUTE")) {
        const std::string routeAddr = routeLocalAddress(profile.ip);
        if (!routeAddr.empty()) {
            config.bindAddress = routeAddr;
            log("[ICE] 按路由自动绑定本机地址 " + routeAddr);
        }
    }
    pc_ = std::make_shared<rtc::PeerConnection>(config);

    pc_->onStateChange([this](rtc::PeerConnection::State st) {
        using S = rtc::PeerConnection::State;
        switch (st) {
            case S::Connecting: setState(ConnState::Connecting); break;
            case S::Connected:  log("[ICE] 已连接"); break;
            case S::Failed: {
                setState(ConnState::Failed, "ICE/DTLS 失败");
                if (running_.load() && onNeedsReconnect)
                    onNeedsReconnect(targetIp_, "ICE/DTLS 连接失败");
                break;
            }
            case S::Disconnected:
            case S::Closed:
                if (running_.load() && onNeedsReconnect)
                    onNeedsReconnect(targetIp_, "通道被断开");
                break;
            default: break;
        }
    });

    // 关键：offer 里必须有 audio / video 两条 m-line，否则机器狗 SCTP 协商失败
    {
        auto audio = rtc::Description::Audio("audio", rtc::Description::Direction::SendRecv);
        audio.addOpusCodec(111);
        audioTrack_ = pc_->addTrack(audio);

        auto video = rtc::Description::Video("video", rtc::Description::Direction::RecvOnly);
        video.addH264Codec(96);
        video.addH264Codec(97);
        videoTrack_ = pc_->addTrack(video);
    }

    auto dc = pc_->createDataChannel("data");
    dc_ = dc;

    dc->onOpen([this] {
        log("[通道] DataChannel 已打开");
        if (state_.load() == ConnState::Connecting)
            setState(ConnState::Validating);
    });
    dc->onClosed([this] {
        log("[通道] 已关闭");
        if (running_.load() && onNeedsReconnect)
            onNeedsReconnect(targetIp_, "DataChannel 已关闭");
    });
    dc->onMessage([this](rtc::message_variant data) {
        if (auto* s = std::get_if<std::string>(&data)) {
            noteRx(s->size());
            handleMessage(*s);
        } else if (auto* b = std::get_if<rtc::binary>(&data)) {
            noteRx(b->size());
            std::string hex;
            char buf[8];
            for (size_t i = 0; i < b->size() && i < 24; ++i) {
                std::snprintf(buf, sizeof(buf), "%02x ", int((*b)[i]));
                hex += buf;
            }
            log("[收] 二进制 " + std::to_string(b->size()) + " 字节: " + hex);
        }
    });

    // 机器狗可能主动建立自己的数据通道，必须捕获
    pc_->onDataChannel([this](std::shared_ptr<rtc::DataChannel> ch) {
        log("[通道] 机器狗自建通道: label=\"" + ch->label() + "\"");
        ch->onMessage([this, ch](rtc::message_variant data) {
            if (auto* s = std::get_if<std::string>(&data)) {
                noteRx(s->size());
                log("[收/" + ch->label() + "] " + s->substr(0, 200));
                handleMessage(*s);
            } else if (auto* b = std::get_if<rtc::binary>(&data)) {
                noteRx(b->size());
                log("[收/" + ch->label() + "] 二进制 " + std::to_string(b->size()) + " 字节");
            }
        });
        ch->onOpen([this, ch] { log("[通道] 机器狗通道已打开: \"" + ch->label() + "\""); });
    });

    // ---- 生成本地 offer ----
    // 注意：这个回调在 libdatachannel 内部线程被异步调用，且**可能在
    // runSignaling 返回之后**再次触发（ICE 收集完成、SDP 重新生成等）。
    // 因此捕获状态必须放在堆上（shared_ptr），绝不能捕获本函数的局部变量，
    // 否则就是悬垂引用 → 随机段错误（实测踩过）。
    struct LocalSdpState {
        std::mutex mutex;
        std::string sdp;
    };
    auto sdpState = std::make_shared<LocalSdpState>();
    pc_->onLocalDescription([sdpState](rtc::Description desc) {
        std::lock_guard<std::mutex> lock(sdpState->mutex);
        sdpState->sdp = std::string(desc);
    });

    log("[信令] 生成本地 offer ...");
    try {
        pc_->setLocalDescription(rtc::Description::Type::Offer);
    } catch (const std::exception& e) {
        log(std::string("[信令] setLocalDescription 提示: ") + e.what());
    }

    std::string offerSdp;
    for (int i = 0; i < 60; ++i) {
        {
            std::lock_guard<std::mutex> lock(sdpState->mutex);
            if (!sdpState->sdp.empty()) offerSdp = sdpState->sdp;
        }
        if (offerSdp.empty()) {
            if (auto local = pc_->localDescription(); local.has_value())
                offerSdp = std::string(*local);
        }
        const bool gatheringDone =
            pc_->gatheringState() == rtc::PeerConnection::GatheringState::Complete;
        if (!offerSdp.empty() && gatheringDone) break;
        std::this_thread::sleep_for(100ms);
    }
    if (offerSdp.empty()) throw std::runtime_error("本地 SDP 生成超时");

    log("[SDP] m-line: " + mlineSummary(offerSdp));

    {
        int fpCount = 0;
        for (size_t p = 0; (p = offerSdp.find("a=fingerprint:", p)) != std::string::npos; p += 15)
            ++fpCount;
        const auto addrs = candidateAddresses(offerSdp);
        std::string addrList;
        for (size_t i = 0; i < addrs.size(); ++i) {
            if (i) addrList += ", ";
            addrList += addrs[i];
        }
        log("[SDP] 指纹 " + std::to_string(fpCount) + " 条，候选地址 " +
            std::to_string(addrs.size()) + " 个: " + addrList);
    }

    // 关键：只保留 sha-256 指纹，否则机器狗 DTLS 静默失败
    std::string sdp = stripExtraFingerprints(offerSdp);

    SdpTweaks tweaks = sdpTweaksFromEnv();
    if (profile.ipv4Only) tweaks.ipv4Only = true;
    if (tweaks.keepCandidateAddresses.empty() && !profile.keepCandidateAddresses.empty())
        tweaks.keepCandidateAddresses = profile.keepCandidateAddresses;
    const std::string adjusted = adjustSdpForGo2(sdp, tweaks);
    if (adjusted.size() != sdp.size())
        log("[SDP] 已裁剪: " + std::to_string(sdp.size()) + " -> " +
            std::to_string(adjusted.size()) + " 字节（指纹/IPv6/候选过滤）");

    nlohmann::json offerJson;
    offerJson["id"] = offerId;
    offerJson["sdp"] = adjusted;
    offerJson["type"] = "offer";
    offerJson["token"] = "";

    // ---- 交换 SDP ----
    std::string answerText;
    if (ep.kind == SignalingKind::ConNotify) {
        log("[信令] GET /con_notify ...");
        const auto& candidates = profile.aesKeyCandidates;
        const auto notify = signaling.fetchConNotify(candidates);
        data2_.store(notify.data2);
        usedPort_.store(ep.port);
        if (notify.data2 == 3) {
            int idx = -1;
            for (size_t i = 0; i < candidates.size(); ++i)
                if (candidates[i] == notify.usedKeyHex) { idx = int(i); break; }
            {
                std::lock_guard<std::mutex> lock(peerMutex_);
                keySource_ = idx >= 0 ? ("云账号 key(#" + std::to_string(idx + 1) + ")")
                                      : "云账号 key";
            }
            if (notify.usedLegacyStaticKey) {
                {
                    std::lock_guard<std::mutex> lock(peerMutex_);
                    keySource_ = "内置静态key（固件上报 data2=3）";
                }
                log("[信令] data2=3，但内置静态 key 实测可解 —— 用静态 key 继续");
            } else {
                log("[信令] data2=3，钥匙命中（候选 #" + std::to_string(idx + 1) + "）");
            }
        } else if (notify.data2 == 2) {
            std::lock_guard<std::mutex> lock(peerMutex_);
            keySource_ = "内置静态key";
            log("[信令] data2=2，使用内置静态 GCM key");
        } else {
            {
                std::lock_guard<std::mutex> lock(peerMutex_);
                keySource_ = "明文握手";
            }
            log("[信令] data2=" + std::to_string(notify.data2) + "，明文握手");
        }
        if (onHandshake) onHandshake(targetIp_, notify.data2, notify.usedKeyHex);

        log("[信令] POST /con_ing_" + notify.pathEnding + " ...");
        answerText = signaling.exchangeSdp(notify, offerJson.dump());
    } else {
        data2_.store(0);
        {
            std::lock_guard<std::mutex> lock(peerMutex_);
            keySource_ = "旧固件无需钥匙";
        }
        log("[信令] 旧固件流程：明文 POST /offer ...");
        answerText = signaling.exchangeSdpLegacy(offerJson.dump());
    }

    nlohmann::json answer;
    try {
        answer = nlohmann::json::parse(answerText);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("answer 解析失败: ") + e.what() +
                                 " 原始响应: " + answerText.substr(0, 200));
    }

    const std::string answerSdp = answer.value("sdp", "");
    if (answerSdp == "reject")
        throw std::runtime_error(
            "机器人拒绝连接（sdp=reject）：同一时刻只允许一条 WebRTC 连接。"
            "请先断开手机 App / 上一个客户端，等待 10~20 秒让旧会话释放后重试");
    if (answerSdp.empty()) throw std::runtime_error("answer 里没有 sdp");
    // 防御：机器人忙（限流/旧会话未释放）时会回过残缺的 answer，
    // 直接塞给 libdatachannel 可能触发底层崩溃，这里先做结构校验再交给它。
    {
        int mlineCount = 0;
        for (size_t p = 0; (p = answerSdp.find("m=", p)) != std::string::npos; p += 2) ++mlineCount;
        const bool sane = answerSdp.find("v=0") != std::string::npos &&
                          mlineCount >= 2 &&
                          answerSdp.find("a=fingerprint:") != std::string::npos &&
                          answerSdp.find("a=ice-ufrag:") != std::string::npos;
        if (!sane)
            throw std::runtime_error(
                "机器人返回的 answer SDP 不完整（m-line " + std::to_string(mlineCount) +
                " 条）：通常是机器人还在限流/旧会话未释放，等待 10~15 秒后会自动重试");
    }

    {
        std::string keys;
        size_t p = 0;
        while ((p = answerSdp.find("a=", p)) != std::string::npos) {
            size_t e = answerSdp.find("\r\n", p);
            if (e == std::string::npos) e = answerSdp.size();
            std::string line = answerSdp.substr(p, e - p);
            if (line.rfind("a=setup:", 0) == 0 || line.rfind("a=mid:", 0) == 0 ||
                line.rfind("a=sctp-port", 0) == 0)
                keys += line + "  ";
            p = e;
        }
        log("[answer] " + keys);
    }

    pc_->setRemoteDescription(rtc::Description(answerSdp, answer.value("type", "answer")));
    return true;
}

void RobotClient::disconnect() {
    // 尽力而为：断开前先停车，避免机器狗保持最后的速度继续走
    if (dc_ && dc_->isOpen()) {
        try { stopMove(); } catch (...) {}
    }

    running_.store(false);
    monitorStop_.store(true);
    if (monitorThread_.joinable()) monitorThread_.join();

    audioTrack_.reset();
    videoTrack_.reset();

    if (dc_) {
        try { dc_->close(); } catch (...) {}
        dc_.reset();
    }
    if (pc_) {
        try { pc_->close(); } catch (...) {}
        pc_.reset();
    }
    if (workerThread_.joinable()) workerThread_.join();

    state_.store(ConnState::Disconnected);
    readyAtMs_.store(0);
}

// ------------------------------------------------------------------ 消息

void RobotClient::handleMessage(const std::string& text) {
    nlohmann::json msg;
    try {
        msg = nlohmann::json::parse(text);
    } catch (...) {
        log("[收] (非 JSON) " + text.substr(0, 120));
        return;
    }

    const std::string type = msg.value("type", "");

    if (type == "validation") {
        handleValidation(msg);
        return;
    }
    if (type == "err") {
        const std::string info = msg.value("info", "");
        if (info == "Validation Needed.") {
            std::lock_guard<std::mutex> lock(validationMutex_);
            if (!validationKey_.empty()) {
                log("[校验] 机器人要求重新校验，重发 ...");
                const std::string reply = crypto::base64Encode(
                    hexToBytes(crypto::md5Hex("UnitreeGo2_" + validationKey_)));
                nlohmann::json m;
                m["type"] = "validation";
                m["topic"] = "";
                m["data"] = reply;
                sendRaw(m.dump());
            }
        } else {
            log("[错误] " + text);
        }
        return;
    }
    if (type == "heartbeat") {
        heartbeatsAcked_.fetch_add(1);
        return;
    }

    if (type == "errors" || type == "add_error" || type == "rm_error") {
        const std::string d = msg.value("data", nlohmann::json()).dump();
        if (d != "[]" && d != "{}" && d != "\"\"") log("[机器人错误] " + d);
        return;
    }

    // 指令回执：{"type":"res","topic":"rt/api/sport/response","data":{...,"status":{"code":0}}}
    if (type == "res") {
        int code = -1;
        int apiId = 0;
        int errCode = -1;
        bool parsed = false;
        try {
            code = msg.at("data").at("header").at("status").at("code").get<int>();
            parsed = true;
        } catch (...) {
        }
        try {
            apiId = msg.at("data").at("header").at("identity").at("api_id").get<int>();
        } catch (...) {
        }
        if (parsed) {
            // error_code 的位置随固件版本不同，两处都试一次
            try {
                errCode = msg.at("data").at("header").at("status").at("error_code").get<int>();
            } catch (...) {
                try {
                    errCode = msg.at("data").at("error_code").get<int>();
                } catch (...) {
                }
            }
        }

        const std::string topic = msg.value("topic", "");
        const bool isSport = topic.find("/sport/") != std::string::npos;
        // 把 api_id 翻成动作名，日志写「哪个动作 + 结果」，而不是一串裸 id
        const std::string name = isSport ? labelForApiId(apiId) : std::string();
        const std::string who =
            !name.empty() ? name + " (api " + std::to_string(apiId) + ")"
                          : (topic.empty() ? std::string("回执") : topic);

        std::string note;
        if (parsed && code != 0 && isSport) {
            if (errCode > 0) note = "error_code=" + std::to_string(errCode) + "；";
            note += rejectReason(code);
        }

        if (!parsed) {
            // 回执结构与预期不符时打印原始 JSON，便于对照固件差异排查
            log("[回执] " + topic + " 结构异常，原始报文: " + text.substr(0, 300));
        } else if (rawAck_.load()) {
            log("[回执原始] " + text.substr(0, 400));
        } else if (code == 0) {
            log("[回执] " + who + " —— 成功 ✔");
        } else {
            log("[回执] " + who + " —— 失败（code=" + std::to_string(code) + "）" +
                (note.empty() ? std::string() : "：" + note));
        }

        if (parsed && isSport) {
            if (onSportAck) onSportAck(apiId, code, note);
            // 被拒：先看是不是"刚就绪预热"（可自动重发），否则换另一套指令集再试
            if (code != 0 && apiId != 0 && !scheduleRetryFor(apiId))
                tryAlternateApiId(apiId);
        }
        if (onTopicData) onTopicData(topic, msg);
        return;
    }

    // 订阅数据
    const std::string topic = msg.value("topic", "");
    recordHealth(topic, msg);
    if (onTopicData) onTopicData(topic, msg);
}

/// 记录机器人健康信息：运动状态里的 error_code、低层状态里的电量。
/// 指令被拒（code=-1）时用它给出真实原因（最常见是低电量保护）。
void RobotClient::recordHealth(const std::string& topic, const nlohmann::json& msg) {
    if (!msg.contains("data")) return;
    const auto& d = msg["data"];
    try {
        if (topic.find("sportmodestate") != std::string::npos && d.contains("error_code"))
            lastErrorCode_.store(d["error_code"].get<int>());
        else if (topic.find("lowstate") != std::string::npos && d.contains("bms_state") &&
                 d["bms_state"].contains("soc"))
            lastSoc_.store(d["bms_state"]["soc"].get<int>());
    } catch (...) {
        // 字段随固件变化，解析失败忽略
    }
}

/// 指令被拒时的原因说明。实测这台 Go2 Pro（MCF 固件）的三类典型：
///   - error_code=1013 + code=3203：刚就绪约 10 秒内，运动服务预热 → 自动重试即可
///   - code=3203 + error_code=100 ：该 api_id 不在当前固件的指令表里（指令集不匹配）
///   - 低电量保护：只有 RecoveryStand / StopMove 之类放行
std::string RobotClient::rejectReason(int ackCode) const {
    const int soc = lastSoc_.load();
    const int ec = lastErrorCode_.load();
    std::string why;
    if (soc >= 0 && soc < 20)
        why = "电量仅 " + std::to_string(soc) +
              "%，低电量保护：运动指令会被拒（RecoveryStand/StopMove 之类放行），请先充电";
    else if (ec == 1013)
        why = "运动服务预热中（连接后约 10 秒内会被拒），稍后自动重试即可";
    else if (ackCode == 3203 || ec == 100)
        why = "该指令不在当前固件的指令表里（指令集不匹配或固件不支持）——"
              "被拒时会自动改用另一套 api_id 重试，仍失败则说明固件确实没有这条动作";
    else if (ec > 0)
        why = "机器人上报 error_code=" + std::to_string(ec) + "（状态限制或保护中）";
    else
        why = "机器人当前状态不接受该指令（姿态异常 / 运动模式限制 / 刚就绪）";
    if (ec > 0 && ec != 1013 && ec != 100 && !(soc >= 0 && soc < 20))
        why += "；error_code=" + std::to_string(ec) + "，电量 " + std::to_string(soc) + "%";
    return why;
}

void RobotClient::handleValidation(const nlohmann::json& msg) {
    const std::string data = msg.value("data", "");

    if (data == "Validation Ok.") {
        log("[校验] OK ✔");
        readyAtMs_.store(nowMs());
        {
            std::lock_guard<std::mutex> lock(peerMutex_);
            connectedSince_ = nowTimeStr();
        }
        setState(ConnState::Ready);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(validationMutex_);
        validationKey_ = data;
    }
    log("[校验] 收到 key，回发校验值 ...");

    const std::string reply =
        crypto::base64Encode(hexToBytes(crypto::md5Hex("UnitreeGo2_" + data)));

    nlohmann::json m;
    m["type"] = "validation";
    m["topic"] = "";
    m["data"] = reply;
    sendRaw(m.dump());
}

// ------------------------------------------------------------------ 发送

bool RobotClient::sendRaw(const std::string& text) {
    if (!dc_ || !dc_->isOpen()) return false;
    try {
        dc_->send(std::string(text));
        txMessages_.fetch_add(1);
        txBytes_.fetch_add(text.size());
        return true;
    } catch (const std::exception& e) {
        log(std::string("[发送失败] ") + e.what());
        return false;
    }
}

std::string RobotClient::buildRequest(const std::string& topic, int apiId,
                                      const nlohmann::json& parameter) const {
    nlohmann::json msg;
    msg["type"] = "req";
    msg["topic"] = topic;
    msg["data"]["header"]["identity"]["id"] = int32_t(requestId_.fetch_add(1) + 1000);
    msg["data"]["header"]["identity"]["api_id"] = apiId;
    // 注意：parameter 在协议里是「字符串」，不是对象
    msg["data"]["parameter"] = parameter.is_null() ? "" : parameter.dump();
    return msg.dump();
}

bool RobotClient::sendSportCommand(int apiId, const nlohmann::json& parameter) {
    if (!isReady()) {
        log("[指令] 通道未就绪，忽略");
        return false;
    }
    const std::string text = buildRequest("rt/api/sport/request", apiId, parameter);
    const bool ok = sendRaw(text);
    if (ok) {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pendingCmds_.push_back({apiId, parameter, nowMs()});
        if (pendingCmds_.size() > 12) pendingCmds_.erase(pendingCmds_.begin());
    }
    const std::string name = labelForApiId(apiId);
    log((ok ? "[指令] 已发送 " : "[指令] 发送失败 ") +
        (name.empty() ? std::string() : name + " ") + "(api " + std::to_string(apiId) + ")");
    return ok;
}

/// 运动指令被拒（code != 0）时判断是否需要重试：
/// 实测 Go2 Pro（1.1.14 固件）在通道就绪后约 10 秒内会拒绝运动指令
/// （error_code=1013，运动服务预热），放慢节奏重发即可成功。
bool RobotClient::scheduleRetryFor(int apiId) {
    const long long now = nowMs();
    const long long readyAt = readyAtMs_.load();
    // 低电量保护下重试无意义，只会刷屏
    const int soc = lastSoc_.load();
    if (soc >= 0 && soc < 20) return false;
    if (readyAt <= 0 || now - readyAt > 15000) return false;  // 预热窗口约 10 秒，放宽到 15 秒
    PendingCmd cmd;
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        for (auto it = pendingCmds_.rbegin(); it != pendingCmds_.rend(); ++it) {
            if (it->apiId == apiId) {
                if (now - it->sentAtMs > 12000) return false;  // 太久了，不是预热问题
                cmd = *it;
                break;
            }
        }
        if (cmd.apiId == 0) return false;
        for (const auto& r : retryQueue_)
            if (r.apiId == apiId) return false;  // 已在重试队列
        // 阶梯退避：把重发安排到"就绪 + 10 秒"预热期之后（1~5 秒的间隔）
        long long delay = (readyAt + 10000) - now;
        if (delay < 900) delay = 900;
        if (delay > 5000) delay = 5000;
        cmd.sentAtMs = now + delay;
        retryQueue_.push_back(cmd);
    }
    log("[指令] api_id=" + std::to_string(apiId) +
        " 被机器人拒绝（运动服务预热中，error_code=1013 属正常现象），稍后自动重试");
    return true;
}

/// 指令被拒 → 改用另一套指令集的 api_id 重试一次。
/// Go2 Pro 等 MCF 固件的运动服务只认自己那套 id（如后空翻 MCF=2043 / 普通=1044），
/// 用错指令集时指令会被逐条拒绝 —— 这是"很多动作用不了"的主要原因。
bool RobotClient::tryAlternateApiId(int apiId) {
    const auto alts = alternateApiIds(apiId);
    if (alts.empty()) return false;

    const int soc = lastSoc_.load();
    if (soc >= 0 && soc < 20) return false;  // 低电量保护：换 id 也没用，别刷屏

    const long long now = nowMs();
    {
        std::lock_guard<std::mutex> lock(fallbackMutex_);
        for (int id : alts) {
            auto it = fallbackAtMs_.find(id);
            if (it != fallbackAtMs_.end() && now - it->second < 4000) return false;  // 防来回翻腾
        }
        fallbackAtMs_[apiId] = now;
        for (int id : alts) fallbackAtMs_[id] = now;
    }

    // 找回这条指令原始参数（被拒的回执里只有 api_id，没有参数）
    nlohmann::json param;
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        for (auto it = pendingCmds_.rbegin(); it != pendingCmds_.rend(); ++it)
            if (it->apiId == apiId) {
                param = it->param;
                break;
            }
    }

    const std::string name = labelForApiId(apiId);
    log("[动作] " + (name.empty() ? "api " + std::to_string(apiId) : name) +
        " 被拒 → 自动改用另一套指令集 api " + std::to_string(alts[0]) +
        " 重试（normal 与 MCF 固件的 api_id 不同）");
    return sendSportCommand(alts[0], param);
}

/// 关闭全部「持续模式」开关（`{"data": false}`）。
/// 实测教训：点了"自由行走/领航跟随"之后，StopMove(1003) 回 code=0 但狗照样自己走 ——
/// 因为这类指令是 on/off 语义、会一直生效；唯一停法是同一个 api_id 带 false 关掉。
int RobotClient::disablePersistentModes(const std::vector<int>& ids) {
    if (!isReady()) return 0;
    const std::vector<int> targets = ids.empty() ? persistentModeIds() : ids;
    nlohmann::json off;
    off["data"] = false;
    int sent = 0;
    for (int id : targets) {
        if (sendSportCommand(id, off)) ++sent;
        std::this_thread::sleep_for(45ms);  // 逐条发，别把运动服务打爆
        if (!isReady()) break;              // 中途掉线就停
    }
    if (sent > 0)
        log("[急停] 已逐个关闭 " + std::to_string(sent) +
            " 个持续模式开关（自由行走 / 领航跟随 / 交叉步 / 经济步态 …）");
    return sent;
}

bool RobotClient::move(float x, float y, float z) {
    nlohmann::json p;
    p["x"] = x;
    p["y"] = y;
    p["z"] = z;
    return sendSportCommand(1008, p);
}

bool RobotClient::setMotionMode(const std::string& name) {
    if (!isReady()) return false;
    nlohmann::json param;
    param["name"] = name;
    return sendRaw(buildRequest("rt/api/motion_switcher/request", 1002, param));
}

bool RobotClient::queryMotionMode() {
    if (!isReady()) return false;
    return sendRaw(buildRequest("rt/api/motion_switcher/request", 1001, nlohmann::json()));
}

bool RobotClient::subscribe(const std::string& topic) {
    nlohmann::json m;
    m["type"] = "subscribe";
    m["topic"] = topic;
    return sendRaw(m.dump());
}

bool RobotClient::unsubscribe(const std::string& topic) {
    nlohmann::json m;
    m["type"] = "unsubscribe";
    m["topic"] = topic;
    return sendRaw(m.dump());
}

// ------------------------------------------------------------------ 心跳 + 看门狗

void RobotClient::startMonitor() {
    monitorStop_.store(false);
    const RobotProfile profile = profile_;
    monitorThread_ = std::thread([this, profile] {
        const long long phaseStart = nowMs();
        long long lastHeartbeat = 0;

        while (running_.load() && !monitorStop_.load()) {
            std::this_thread::sleep_for(200ms);
            if (!running_.load() || monitorStop_.load()) break;

            const ConnState st = state_.load();
            const long long now = nowMs();

            // 1) 通道建立 + 校验总超时
            if (st == ConnState::Connecting || st == ConnState::Validating) {
                if (now - phaseStart > profile.readyTimeoutMs) {
                    const std::string why =
                        st == ConnState::Connecting
                            ? "通道建立超时（ICE/DTLS 未完成；机器人可能仍被上一个会话占用）"
                            : "校验超时（data channel 已开会话但校验未完成；钥匙可能不对）";
                    setState(ConnState::Failed, why);
                    if (onNeedsReconnect) onNeedsReconnect(targetIp_, why);
                    return;
                }
                continue;
            }

            if (st != ConnState::Ready) continue;

            // 1.5) 消费"刚就绪被拒"的重试队列
            {
                std::vector<PendingCmd> due;
                {
                    std::lock_guard<std::mutex> lock(pendingMutex_);
                    for (auto it = retryQueue_.begin(); it != retryQueue_.end();) {
                        if (now >= it->sentAtMs) {
                            due.push_back(*it);
                            it = retryQueue_.erase(it);
                        } else {
                            ++it;
                        }
                    }
                }
                for (const auto& cmd : due) {
                    log("[指令] 重试 api_id=" + std::to_string(cmd.apiId));
                    sendRaw(buildRequest("rt/api/sport/request", cmd.apiId, cmd.param));
                }
            }

            // 2) 心跳（每 2 秒，不发会被机器人断开）
            if (now - lastHeartbeat >= 2000) {
                lastHeartbeat = now;
                nlohmann::json m;
                m["type"] = "heartbeat";
                m["topic"] = "";
                m["data"]["timeInStr"] = fullTimeStr();
                m["data"]["timeInNum"] = (long long)std::time(nullptr);
                if (sendRaw(m.dump())) heartbeatsSent_.fetch_add(1);
                // 空闲时补一个 RTT / 候选对快照，供验证模式展示
                if (pc_) {
                    if (auto rtt = pc_->rtt(); rtt.has_value())
                        rttMs_.store(int(rtt->count()));
                }
            }

            // 3) 静默看门狗：Ready 后长时间收不到任何数据 → 判定掉线
            const long long last = lastRxMs_.load();
            if (last > 0 && now - last > profile.staleTimeoutMs) {
                const std::string why = "心跳静默 " + std::to_string((now - last) / 1000) +
                                        " 秒，判定链路已断，准备重连";
                setState(ConnState::Failed, why);
                if (onNeedsReconnect) onNeedsReconnect(targetIp_, why);
                return;
            }
        }
    });
}

void RobotClient::stopMonitor() {
    monitorStop_.store(true);
    if (monitorThread_.joinable()) monitorThread_.join();
}

}  // namespace go2
