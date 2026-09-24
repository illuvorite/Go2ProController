#pragma once

#include "robot_client.hpp"

#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace go2 {

/// 一台机器狗的运行快照（验证模式与界面共用）
struct RobotSnapshot {
    std::string ip;
    ConnState state = ConnState::Disconnected;
    bool desired = false;      ///< 是否处于「应当在线」状态（掉线会自动重连）
    std::string lastError;
    LinkStats stats;
};

/// 多台机器狗的连接管理器：每台机器狗一个 RobotClient 实例。
///
/// 多机场景下它额外负责三件事：
///   1. **错峰连接**：机器狗的信令服务是单线程 accept，同时握手会互相堵死，
///      批量连接按 stagger 间隔排队发起；
///   2. **掉线自动重连**：心跳静默 / 通道断开 / 连接超时后按指数退避重连，
///      退避上限 30 秒；用户手动断开则不再重连；
///   3. **每设备钥匙绑定**：data2=3（Go2 ≥ 1.1.15）的 AES-128 钥匙按 IP 记住并
///      落盘缓存，避免每次都把账号下的所有钥匙试一遍。
///
/// 线程安全：公开方法内部加锁；RobotClient 的连接是异步的。
/// 内部约定：**绝不持锁调用 client->connect()/disconnect()**（可能阻塞等待
/// 客户端线程退出，而客户端线程可能反向回调本管理器 → 死锁）。
class RobotManager {
public:
    RobotManager();
    ~RobotManager();

    /// 新客户端创建时回调（用于接线日志/状态/数据回调）。
    /// 注意：在持有内部锁的状态下被调用，回调里不要反过来调用本管理器的方法。
    std::function<void(RobotClient&)> onCreated;

    /// 获取指定 IP 的客户端；不存在则创建（不自动连接）
    RobotClient& ensure(const std::string& ip);

    /// 查找指定 IP 的客户端；不存在返回 nullptr
    RobotClient* find(const std::string& ip);

    /// 连接指定 IP（不存在则先创建）。使用该 IP 已保存的档案。
    void connect(const std::string& ip);

    /// 连接指定 IP，并记住该台的档案（端口 / AP 模式 / 绑定网卡 / 钥匙等）
    void connect(const std::string& ip, const RobotProfile& profile);

    /// 批量错峰连接：Air + 两台 Pro 一键上线
    void connectAll(const std::vector<std::string>& ips, int staggerMs = 500);

    /// 断开指定 IP（标记为用户主动断开，不再自动重连；客户端保留在列表中）
    void disconnect(const std::string& ip);

    /// 断开全部连接（全部标记为用户主动断开）
    void disconnectAll();

    /// 立即重连所有「应当在线但当前未就绪」的机器狗
    void reconnectFailed();

    /// 设置云账号拉取的每设备 AES-128 key 候选（data2=3 新固件用）。
    /// 注意：这是**替换**语义（切换账号时用 addAesKeys 累加）。
    void setAesKeys(const std::vector<std::string>& keys);

    /// 追加钥匙（多次登录不同账号时使用：两台 Pro 可能绑在不同账号下）
    void addAesKeys(const std::vector<std::string>& keys);

    std::vector<std::string> aesKeys() const;

    /// 把某台机器狗与具体钥匙绑定（握手成功后自动调用，并写入缓存文件）
    void bindKey(const std::string& ip, const std::string& keyHex);
    std::string pinnedKey(const std::string& ip) const;

    /// key 缓存（IP -> 钥匙），默认 go2_keys_cache.json
    void loadKeyCache(const std::string& path = "go2_keys_cache.json");
    void saveKeyCache(const std::string& path = "go2_keys_cache.json") const;

    /// 期望在线的设备列表
    std::vector<std::string> desiredIps() const;

    /// 全部设备快照（含统计），供界面与验证模式使用
    std::vector<RobotSnapshot> snapshot() const;

    /// 停止全部自动重连（析构前调用，保证线程收敛）
    void shutdown();

private:
    struct Pending {
        std::string ip;
        long long dueMs = 0;
        int attempt = 0;
    };

    RobotClient* findLocked(const std::string& ip) const;
    void ensureCallbacks(RobotClient& c);
    void supervisorLoop();
    void scheduleConnect(const std::string& ip, long long delayMs, int attempt);
    void requestReconnect(const std::string& ip, const std::string& reason);
    RobotProfile profileFor(const std::string& ip) const;
    /// 该 IP 的候选钥匙：已绑定的那把排在最前。**调用方必须持有 mutex_**
    std::vector<std::string> keysForLocked(const std::string& ip) const;

    mutable std::mutex mutex_;
    /// 串行化「对 RobotClient 的 connect/disconnect 调用」：
    /// 界面点击与自动重连监督线程可能同时操作同一台，必须避免并发进入。
    mutable std::mutex actionMutex_;
    std::vector<std::unique_ptr<RobotClient>> clients_;
    std::map<std::string, RobotProfile> profiles_;
    std::map<std::string, bool> desired_;
    std::map<std::string, std::string> pinnedKeys_;
    std::map<std::string, std::string> lastError_;
    std::map<std::string, int> reconnectAttempts_;
    std::vector<std::string> aesKeys_;   // 云账号拉取的 key 候选，创建 client 时下发
    std::vector<Pending> pending_;
    std::string keyCachePath_ = "go2_keys_cache.json";

    std::condition_variable cv_;
    std::thread supervisor_;
    bool stop_ = false;
};

}  // namespace go2
