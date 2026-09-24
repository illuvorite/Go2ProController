#include "robot_manager.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>

namespace go2 {

namespace {

long long nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

/// 指数退避：5s, 10s, 20s, 30s（封顶）。
/// 起点取 5 秒是因为机器狗信令有限流（连续重连会吃到 HTTP 429），
/// 退避太短只会越试越糟。
long long backoffMs(int attempt) {
    const int capped = std::max(0, std::min(attempt - 1, 4));
    const long long ms = 5000LL << capped;
    return std::min<long long>(ms, 30000);
}

}  // namespace

RobotManager::RobotManager() {
    supervisor_ = std::thread([this] { supervisorLoop(); });
}

RobotManager::~RobotManager() {
    shutdown();
    std::vector<RobotClient*> all;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& c : clients_) all.push_back(c.get());
    }
    for (auto* c : all) {
        if (c) c->disconnect();  // 内部会先停车，避免机器狗保持最后速度
    }
}

void RobotManager::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_) return;
        stop_ = true;
    }
    cv_.notify_all();
    if (supervisor_.joinable()) supervisor_.join();
}

RobotClient* RobotManager::findLocked(const std::string& ip) const {
    for (auto& c : clients_)
        if (c && c->ip() == ip) return c.get();
    return nullptr;
}

std::vector<std::string> RobotManager::keysForLocked(const std::string& ip) const {
    std::vector<std::string> keys;
    auto it = pinnedKeys_.find(ip);
    if (it != pinnedKeys_.end() && !it->second.empty()) keys.push_back(it->second);
    for (const auto& k : aesKeys_)
        if (std::find(keys.begin(), keys.end(), k) == keys.end()) keys.push_back(k);
    return keys;
}

RobotProfile RobotManager::profileFor(const std::string& ip) const {
    RobotProfile profile;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = profiles_.find(ip);
        if (it != profiles_.end()) profile = it->second;
        profile.ip = ip;
        profile.aesKeyCandidates = keysForLocked(ip);
        auto pk = pinnedKeys_.find(ip);
        if (pk != pinnedKeys_.end()) profile.pinnedAesKey = pk->second;
    }
    return profile;
}

RobotClient& RobotManager::ensure(const std::string& ip) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& c : clients_)
        if (c && c->ip() == ip) return *c;

    auto client = std::make_unique<RobotClient>();
    RobotClient& ref = *client;
    const auto keys = keysForLocked(ip);
    if (!keys.empty()) ref.setAesKeys(keys);
    auto pk = pinnedKeys_.find(ip);
    if (pk != pinnedKeys_.end()) ref.setPinnedKey(pk->second);
    clients_.push_back(std::move(client));
    ensureCallbacks(ref);
    if (onCreated) onCreated(ref);  // 持锁状态回调，见头文件说明
    return ref;
}

void RobotManager::ensureCallbacks(RobotClient& c) {
    c.onHandshake = [this](const std::string& ip, int data2, const std::string& usedKeyHex) {
        if (data2 == 3 && !usedKeyHex.empty()) {
            bindKey(ip, usedKeyHex);  // 记住「这台狗用哪把钥匙」
        }
    };
    c.onNeedsReconnect = [this](const std::string& ip, const std::string& reason) {
        requestReconnect(ip, reason);
    };
}

RobotClient* RobotManager::find(const std::string& ip) {
    std::lock_guard<std::mutex> lock(mutex_);
    return findLocked(ip);
}

void RobotManager::connect(const std::string& ip) { connect(ip, profileFor(ip)); }

void RobotManager::connect(const std::string& ip, const RobotProfile& profile) {
    RobotProfile p = profile;
    p.ip = ip;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        p.aesKeyCandidates = keysForLocked(ip);
        auto pk = pinnedKeys_.find(ip);
        if (pk != pinnedKeys_.end()) p.pinnedAesKey = pk->second;
        profiles_[ip] = p;
        desired_[ip] = true;
        reconnectAttempts_[ip] = 0;
        pending_.erase(std::remove_if(pending_.begin(), pending_.end(),
                                      [&](const Pending& x) { return x.ip == ip; }),
                       pending_.end());
    }

    RobotClient& c = ensure(ip);  // 内部会同步钥匙
    c.setAesKeys(p.aesKeyCandidates);
    if (!p.pinnedAesKey.empty()) c.setPinnedKey(p.pinnedAesKey);
    std::lock_guard<std::mutex> act(actionMutex_);
    c.connect(ip, p);
}

void RobotManager::connectAll(const std::vector<std::string>& ips, int staggerMs) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (size_t i = 0; i < ips.size(); ++i) {
            const std::string& ip = ips[i];
            desired_[ip] = true;
            reconnectAttempts_[ip] = 0;
            auto it = profiles_.find(ip);
            if (it == profiles_.end()) {
                RobotProfile p;
                p.ip = ip;
                p.aesKeyCandidates = keysForLocked(ip);
                profiles_[ip] = p;
            }
            pending_.erase(std::remove_if(pending_.begin(), pending_.end(),
                                          [&](const Pending& x) { return x.ip == ip; }),
                           pending_.end());
            pending_.push_back({ip, nowMs() + (long long)i * staggerMs, 0});
        }
    }
    cv_.notify_all();
}

void RobotManager::disconnect(const std::string& ip) {
    RobotClient* c = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        desired_[ip] = false;
        pending_.erase(std::remove_if(pending_.begin(), pending_.end(),
                                      [&](const Pending& x) { return x.ip == ip; }),
                       pending_.end());
        c = findLocked(ip);
    }
    if (c) {
        std::lock_guard<std::mutex> act(actionMutex_);
        c->disconnect();
    }
}

void RobotManager::disconnectAll() {
    std::vector<RobotClient*> all;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& d : desired_) d.second = false;
        pending_.clear();
        for (auto& c : clients_) all.push_back(c.get());
    }
    std::lock_guard<std::mutex> act(actionMutex_);
    for (auto* c : all)
        if (c) c->disconnect();
}

void RobotManager::reconnectFailed() {
    std::vector<std::string> todo;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        int i = 0;
        for (auto& d : desired_) {
            if (!d.second) continue;
            RobotClient* c = findLocked(d.first);
            if (c && c->isReady()) continue;
            const std::string ip = d.first;
            pending_.erase(std::remove_if(pending_.begin(), pending_.end(),
                                          [&](const Pending& x) { return x.ip == ip; }),
                           pending_.end());
            pending_.push_back({ip, nowMs() + (long long)(i++) * 400, reconnectAttempts_[ip]});
            todo.push_back(ip);
        }
    }
    cv_.notify_all();
}

void RobotManager::scheduleConnect(const std::string& ip, long long delayMs, int attempt) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!desired_[ip]) return;
        for (auto& p : pending_) {
            if (p.ip == ip) {
                p.dueMs = std::min(p.dueMs, nowMs() + delayMs);
                p.attempt = attempt;
                cv_.notify_all();
                return;
            }
        }
        pending_.push_back({ip, nowMs() + delayMs, attempt});
    }
    cv_.notify_all();
}

void RobotManager::requestReconnect(const std::string& ip, const std::string& reason) {
    int attempt = 0;
    long long delay = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        lastError_[ip] = reason;
        if (!desired_[ip]) return;  // 用户主动断开的不重连
        attempt = ++reconnectAttempts_[ip];
        delay = backoffMs(attempt);
        bool merged = false;
        for (auto& p : pending_) {
            if (p.ip == ip) {
                p.dueMs = std::min(p.dueMs, nowMs() + delay);
                p.attempt = attempt;
                merged = true;
                break;
            }
        }
        if (!merged) pending_.push_back({ip, nowMs() + delay, attempt});
    }
    cv_.notify_all();
}

void RobotManager::supervisorLoop() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stop_) {
        cv_.wait_for(lock, std::chrono::milliseconds(150));
        if (stop_) break;

        const long long now = nowMs();
        std::vector<std::pair<std::string, RobotProfile>> jobs;
        for (auto it = pending_.begin(); it != pending_.end();) {
            if (it->dueMs > now) {
                ++it;
                continue;
            }
            const std::string ip = it->ip;
            it = pending_.erase(it);
            if (!desired_[ip]) continue;
            RobotProfile prof;
            auto pit = profiles_.find(ip);
            if (pit != profiles_.end()) prof = pit->second;
            prof.ip = ip;
            prof.aesKeyCandidates = keysForLocked(ip);
            auto pk = pinnedKeys_.find(ip);
            if (pk != pinnedKeys_.end()) prof.pinnedAesKey = pk->second;
            jobs.emplace_back(ip, std::move(prof));
        }

        lock.unlock();
        for (auto& j : jobs) {
            // 注意：ensure/connect 都会加锁，必须在解锁后调用；
            // actionMutex_ 避免与界面点击同时操作同一台客户端。
            // 必须用 ensure 而不是 find：自动连接（命令行 IP / 扫描发现）时
            // 客户端可能尚未创建，find 会返回空导致任务被静默丢弃
            //（现象：日志停在"开始连接 ..."后毫无进展）。
            RobotClient& c = ensure(j.first);
            std::lock_guard<std::mutex> act(actionMutex_);
            c.setAesKeys(j.second.aesKeyCandidates);
            if (!j.second.pinnedAesKey.empty()) c.setPinnedKey(j.second.pinnedAesKey);
            c.connect(j.first, j.second);
        }
        lock.lock();
    }
}

void RobotManager::setAesKeys(const std::vector<std::string>& keys) {
    std::vector<RobotClient*> all;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        aesKeys_ = keys;
        for (auto& c : clients_) all.push_back(c.get());
    }
    for (auto* c : all)
        if (c) c->setAesKeys(keys);
}

void RobotManager::addAesKeys(const std::vector<std::string>& keys) {
    std::vector<RobotClient*> all;
    std::vector<std::string> merged;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& k : keys) {
            if (k.empty()) continue;
            if (std::find(aesKeys_.begin(), aesKeys_.end(), k) == aesKeys_.end())
                aesKeys_.push_back(k);
        }
        merged = aesKeys_;
        for (auto& c : clients_) all.push_back(c.get());
    }
    for (auto* c : all)
        if (c) c->setAesKeys(merged);
}

std::vector<std::string> RobotManager::aesKeys() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return aesKeys_;
}

void RobotManager::bindKey(const std::string& ip, const std::string& keyHex) {
    if (ip.empty() || keyHex.empty()) return;
    bool changed = false;
    std::vector<RobotClient*> others;
    std::vector<std::string> merged;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pinnedKeys_[ip] != keyHex) {
            pinnedKeys_[ip] = keyHex;
            changed = true;
        }
        if (std::find(aesKeys_.begin(), aesKeys_.end(), keyHex) == aesKeys_.end())
            aesKeys_.push_back(keyHex);
        merged = keysForLocked(ip);
        for (auto& c : clients_)
            if (c && c->ip() == ip) others.push_back(c.get());
    }
    for (auto* c : others) {
        c->setPinnedKey(keyHex);
        c->setAesKeys(merged);
    }
    if (changed) saveKeyCache();
}

std::string RobotManager::pinnedKey(const std::string& ip) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pinnedKeys_.find(ip);
    return it == pinnedKeys_.end() ? std::string() : it->second;
}

void RobotManager::loadKeyCache(const std::string& path) {
    keyCachePath_ = path;
    std::ifstream in(path);
    if (!in) return;
    nlohmann::json j;
    try {
        in >> j;
    } catch (...) {
        return;
    }
    if (!j.is_object()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = j.begin(); it != j.end(); ++it) {
        const std::string key = it.value().is_string() ? it.value().get<std::string>() : "";
        if (key.size() != 32) continue;
        pinnedKeys_[it.key()] = key;
        if (std::find(aesKeys_.begin(), aesKeys_.end(), key) == aesKeys_.end())
            aesKeys_.push_back(key);
    }
}

void RobotManager::saveKeyCache(const std::string& path) const {
    std::lock_guard<std::mutex> lock(mutex_);
    nlohmann::json j = nlohmann::json::object();
    for (const auto& kv : pinnedKeys_) {
        if (!kv.second.empty()) j[kv.first] = kv.second;
    }
    if (j.empty()) return;
    std::ofstream out(path);
    if (!out) return;
    out << j.dump(2) << "\n";
}

std::vector<std::string> RobotManager::desiredIps() const {
    std::vector<std::string> out;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& kv : desired_)
        if (kv.second) out.push_back(kv.first);
    return out;
}

std::vector<RobotSnapshot> RobotManager::snapshot() const {
    std::vector<RobotSnapshot> out;
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& c : clients_) {
        if (!c) continue;
        RobotSnapshot s;
        s.ip = c->ip();
        s.state = c->state();
        s.stats = c->stats();
        s.lastError = c->lastError();
        s.desired = desired_.count(s.ip) ? desired_.at(s.ip) : false;
        out.push_back(std::move(s));
    }
    return out;
}

}  // namespace go2
