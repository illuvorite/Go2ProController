#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

namespace rtc {
class PeerConnection;
class DataChannel;
class Track;
}  // namespace rtc

namespace go2 {

enum class ConnState {
    Disconnected,
    Signaling,     // 正在与 9991 / 8081 交换 SDP
    Connecting,    // ICE / DTLS / SCTP 建立中
    Validating,    // 等待机器狗校验
    Ready,         // 可下发指令
    Failed,
};

const char* toString(ConnState s);

/// 单台机器狗的连接档案。多机场景下每台狗的参数可能不同：
///   - Air / 老固件：signalingPort=0（自动，命中 8081）、signalingId="STA_localNetwork"
///   - Pro / 新固件（≥1.1.15）：data2=3，需要 pinnedAesKey / aesKeyCandidates
///   - AP 模式直连（192.168.12.1）：signalingId=""（与参考实现一致）
struct RobotProfile {
    std::string ip;
    std::string sn;                       ///< 可选，仅用于日志与钥匙绑定
    int signalingPort = 0;                ///< 0 = 自动探测（9991 -> 8081）
    bool apMode = false;                  ///< 直连机器狗热点（192.168.12.1）
    std::string signalingId;              ///< 非空则覆盖默认 id（STA 默认 STA_localNetwork）
    std::string bindAddress;              ///< 绑定的本机网卡地址；空 = 系统自动选择
    std::vector<std::string> keepCandidateAddresses;  ///< 只保留这些本地地址的 ICE 候选
    bool ipv4Only = false;                ///< 丢掉 IPv6 候选
    int maxRetries = 3;                   ///< 信令阶段失败后的重试次数
    int retryDelayMs = 1200;              ///< 首次重试等待（之后线性退避）
    int readyTimeoutMs = 20000;           ///< 通道建立+校验的总超时
    int staleTimeoutMs = 12000;           ///< Ready 后多久收不到任何数据判定掉线
    std::string pinnedAesKey;             ///< 已知属于这台狗的每设备 AES-128 key（32 hex）
    std::vector<std::string> aesKeyCandidates;  ///< 候选取匙（含 pinned 之外的其它钥匙）
};

/// 链路统计（多机稳定性验证用）
struct LinkStats {
    bool ready = false;
    int data2 = 0;                        ///< 0=旧固件 8081 流程；1/2/3 见协议文档
    int signalingPort = 0;
    std::string keySource;                ///< "内置静态key" / "云账号 key(#n)" / "-"
    size_t txMessages = 0;
    size_t rxMessages = 0;
    size_t txBytes = 0;
    size_t rxBytes = 0;
    int heartbeatsSent = 0;
    int heartbeatsAcked = 0;
    long long lastRxAgeMs = -1;           ///< 距最近一次收到机器狗数据的毫秒数，-1=从未收到
    int rttMs = -1;                       ///< libjuice 报告的数据通道 RTT
    std::string localAddress;             ///< ICE 选中的本地地址:端口
    std::string remoteAddress;            ///< ICE 选中的对端地址:端口
    int reconnects = 0;                   ///< 本会话内重连次数
    std::string connectedSince;           ///< 就绪时刻 hh:mm:ss
    long long readySeconds = 0;           ///< 就绪持续秒数
};

/// 与机器狗的 WebRTC 控制通道（协议见 docs/go2_webrtc_protocol.md）
class RobotClient {
public:
    RobotClient();
    ~RobotClient();

    RobotClient(const RobotClient&) = delete;
    RobotClient& operator=(const RobotClient&) = delete;

    void connect(const std::string& ip);
    void connect(const std::string& ip, const RobotProfile& profile);
    void disconnect();

    ConnState state() const { return state_.load(); }
    bool isReady() const { return state_.load() == ConnState::Ready; }
    const std::string& ip() const { return targetIp_; }
    const RobotProfile& profile() const { return profile_; }
    std::string lastError() const;
    LinkStats stats() const;

    /// data2=3（新固件）的每设备 AES-128 key 候选（32 位 hex，可多个）。
    /// 连接时逐个尝试，GCM 校验通过的那个生效。云账号登录后设置。
    void setAesKeys(std::vector<std::string> keys);

    /// 把「已知属于这台狗」的钥匙钉住，连接时优先使用
    void setPinnedKey(std::string keyHex);

    /// 下发运动控制指令。parameter 为空对象时对应协议里的空字符串。
    /// @return 是否成功放入发送队列
    bool sendSportCommand(int apiId, const nlohmann::json& parameter = nlohmann::json());

    /// 请求机器人状态主题（只读）
    bool subscribe(const std::string& topic);
    bool unsubscribe(const std::string& topic);

    // ---- 回调（在多线程环境下被调用，实现方需自行加锁）----
    std::function<void(const std::string&)> onLog;
    std::function<void(ConnState)> onStateChanged;
    /// topic + 原始 JSON 数据
    std::function<void(const std::string& topic, const nlohmann::json& data)> onTopicData;
    /// 握手成功：回传 data2 与本次实际命中的钥匙（用于把钥匙绑定到该 IP）
    std::function<void(const std::string& ip, int data2, const std::string& usedKeyHex)> onHandshake;
    /// 动作回执（api_id / code / 失败说明）。界面用它标注"这条动作能不能用"。
    std::function<void(int apiId, int code, const std::string& note)> onSportAck;
    /// 需要重连（连接超时 / 心跳静默 / 通道断开）。由上层决定是否重连与退避节奏。
    std::function<void(const std::string& ip, const std::string& reason)> onNeedsReconnect;

    /// 急停第二步：把「持续模式」（自由行走 / 领航跟随 / 交叉步 / 经济步态 …）逐个用 `{"data":false}` 关掉。
    /// **StopMove 只停速度，停不掉这些一直生效的 on/off 开关** —— 这是"点了急停还在动"的根因。
    /// @param ids 要关闭的 api_id；**传空表 = 关闭全部已知开关**（界面按"真正开过的"传，避免灌爆通道）
    /// 内部每条间隔 45ms 发送（会阻塞）→ 请在后台线程调用。返回成功下发条数。
    int disablePersistentModes(const std::vector<int>& ids = {});

    // 便捷动作
    bool stopMove()      { return sendSportCommand(1003); }
    bool balanceStand()  { return sendSportCommand(1002); }
    bool standUp()       { return sendSportCommand(1004); }
    bool standDown()     { return sendSportCommand(1005); }
    bool recoveryStand() { return sendSportCommand(1006); }
    bool damp()          { return sendSportCommand(1001); }
    bool hello()         { return sendSportCommand(1016); }
    bool stretch()       { return sendSportCommand(1017); }
    /// x 前后(米/秒) y 左右(米/秒) z 转向(弧度/秒)
    bool move(float x, float y, float z);

    /// 切换运动模式：normal / ai
    bool setMotionMode(const std::string& name);

    /// 查询当前运动模式（rt/api/motion_switcher/request, api_id=1001）
    bool queryMotionMode();

    /// 打印所有指令回执的原始 JSON（诊断动作被拒时打开）
    void setRawAckLogging(bool on) { rawAck_.store(on); }

private:
    void setState(ConnState s, const std::string& note = {});
    void log(const std::string& msg);
    void handleMessage(const std::string& text);
    void handleValidation(const nlohmann::json& msg);
    void startMonitor();
    void stopMonitor();
    /// 刚就绪阶段指令被拒时安排一次重试（返回是否已安排）
    bool scheduleRetryFor(int apiId);
    /// 指令被拒时改用另一套指令集（MCF <-> 普通）的 api_id 重试一次
    bool tryAlternateApiId(int apiId);
    /// 记录机器人健康信息（error_code / 电量）
    void recordHealth(const std::string& topic, const nlohmann::json& msg);
    /// 指令被拒（code != 0）的原因说明；ackCode 为回执里的 code，用于区分"指令集不匹配"
    std::string rejectReason(int ackCode = -1) const;
    /// 在 worker 线程内完成信令交换；成功返回 true（失败信息写入 lastError_）
    bool runSignaling(const RobotProfile& profile);
    bool sendRaw(const std::string& text);
    std::string buildRequest(const std::string& topic, int apiId,
                             const nlohmann::json& parameter) const;

    std::atomic<ConnState> state_{ConnState::Disconnected};
    std::shared_ptr<rtc::PeerConnection> pc_;
    std::shared_ptr<rtc::DataChannel> dc_;
    // 必须持有 track 的 shared_ptr：libdatachannel 的 addTrack 返回 shared_ptr，
    // 一旦析构就会把该 m-line 从 SDP 里移除
    std::shared_ptr<rtc::Track> audioTrack_;
    std::shared_ptr<rtc::Track> videoTrack_;

    std::atomic<bool> running_{false};
    std::atomic<bool> monitorStop_{false};
    std::thread monitorThread_;
    std::thread workerThread_;

    mutable std::mutex errMutex_;
    std::string lastError_;
    std::string targetIp_;   // 本次连接的目标 IP（多机管理按 IP 区分）
    RobotProfile profile_;
    std::mutex aesKeysMutex_;
    std::vector<std::string> aesKeys_;  // data2=3 的每设备 key 候选
    std::string pinnedKey_;

    mutable std::atomic<uint32_t> requestId_{1};
    std::string validationKey_;
    std::mutex validationMutex_;

    // 运动指令"首次被拒"保护：刚就绪时机器人会拒指令（回执 code=-1），
    // 这里记录最近下发的运动指令参数，被拒后延时重发一次。
    struct PendingCmd {
        int apiId = 0;
        nlohmann::json param;
        long long sentAtMs = 0;
    };
    std::mutex pendingMutex_;
    std::vector<PendingCmd> pendingCmds_;          // 最近下发的运动指令（用于重试）
    std::vector<PendingCmd> retryQueue_;           // 待重试队列（由 monitor 线程消费）
    long long lastRejectLogMs_ = 0;
    // 指令集回退（normal <-> MCF）去抖：防止两个 id 之间来回翻腾
    std::mutex fallbackMutex_;
    std::map<int, long long> fallbackAtMs_;

    // ---- 统计 ----
    std::atomic<size_t> txMessages_{0}, rxMessages_{0}, txBytes_{0}, rxBytes_{0};
    std::atomic<int> heartbeatsSent_{0}, heartbeatsAcked_{0};
    std::atomic<long long> lastRxMs_{0};     ///< steady_clock 毫秒；0 = 从未收到
    std::atomic<int> rttMs_{-1};
    std::atomic<int> reconnects_{0};
    std::atomic<long long> readyAtMs_{0};
    std::atomic<bool> rawAck_{false};
    // 机器人健康信息（来自状态话题），用于解释"指令被拒 code=-1"
    std::atomic<int> lastErrorCode_{-1};   ///< sportmodestate.error_code
    std::atomic<int> lastSoc_{-1};         ///< lowstate.bms_state.soc（电量百分比）
    mutable std::mutex peerMutex_;
    std::string localAddress_, remoteAddress_;
    std::string keySource_;
    std::string connectedSince_;
    std::atomic<int> data2_{0};
    std::atomic<int> usedPort_{0};

    /// 记录收到一条数据（刷新静默计时）
    void noteRx(size_t bytes);
};

/// 查路由：返回本机在访问 robotIp 时会使用的源地址（IPv4）。失败返回空串。
/// 多网卡（WiFi + 有线 + WSL/Hyper-V 虚拟网卡）时用于把 ICE 绑定到正确的网卡。
std::string routeLocalAddress(const std::string& robotIp);

}  // namespace go2
