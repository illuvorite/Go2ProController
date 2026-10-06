#pragma once

// ============================================================================
// 测试用的假「下发出口」。
//
// `cmd::` 这一层只通过 CommandSink 发包，所以测试不需要真机器人、不需要 WebRTC：
// FakeSink 只记录"发给了谁、发的是什么"，用来把群控/急停语义钉死。
// 被 command_service_test 与 ui_state_thread_test 共用。
// ============================================================================

#include "command_service.hpp"
#include "sport_library.hpp"

#include <algorithm>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace go2test {

constexpr const char* kNullParam = "<null>";

class FakeSink final : public go2::CommandSink {
public:
    std::vector<std::string> ready;     ///< 就绪设备
    std::vector<std::string> selected;  ///< 被勾选（受控）设备

    struct Sent {
        std::string ip;
        int apiId = 0;
        std::string param;
    };
    struct Closed {
        std::string ip;
        std::vector<int> ids;
    };

    mutable std::mutex mtx;  ///< 并发测试里会多线程写
    std::vector<Sent> sent;
    std::vector<std::string> stopped;
    std::vector<Closed> closed;

    std::vector<std::string> readyIps() const override {
        std::lock_guard<std::mutex> lock(mtx);
        return ready;
    }

    std::vector<std::string> selectedReadyIps() const override {
        std::lock_guard<std::mutex> lock(mtx);
        std::vector<std::string> out;
        for (const auto& ip : selected)
            if (std::find(ready.begin(), ready.end(), ip) != ready.end()) out.push_back(ip);
        return out;
    }

    bool sendSport(const std::string& ip, int apiId, const nlohmann::json& p) override {
        std::lock_guard<std::mutex> lock(mtx);
        // 生产实现只会发给就绪设备；假实现也照此办理，否则测不出"目标集合"的差异
        if (std::find(ready.begin(), ready.end(), ip) == ready.end()) return false;
        sent.push_back({ip, apiId, p.is_null() ? kNullParam : p.dump()});
        return true;
    }

    bool stopMove(const std::string& ip) override {
        std::lock_guard<std::mutex> lock(mtx);
        if (std::find(ready.begin(), ready.end(), ip) == ready.end()) return false;
        stopped.push_back(ip);
        return true;
    }

    int disablePersistent(const std::string& ip, const std::vector<int>& ids) override {
        std::lock_guard<std::mutex> lock(mtx);
        closed.push_back({ip, ids});
        return static_cast<int>(ids.size());
    }

    // ---- 供断言使用的读取（都加锁，能在并发场景下安全调用）----
    int countApi(int apiId) const {
        std::lock_guard<std::mutex> lock(mtx);
        int n = 0;
        for (const auto& s : sent)
            if (s.apiId == apiId) ++n;
        return n;
    }

    int countTo(const std::string& ip) const {
        std::lock_guard<std::mutex> lock(mtx);
        int n = 0;
        for (const auto& s : sent)
            if (s.ip == ip) ++n;
        return n;
    }

    size_t sentCount() const {
        std::lock_guard<std::mutex> lock(mtx);
        return sent.size();
    }

    size_t stoppedCount() const {
        std::lock_guard<std::mutex> lock(mtx);
        return stopped.size();
    }

    size_t closedCount() const {
        std::lock_guard<std::mutex> lock(mtx);
        return closed.size();
    }

    void reset() {
        std::lock_guard<std::mutex> lock(mtx);
        sent.clear();
        stopped.clear();
        closed.clear();
    }
};

/// 非持有型 shared_ptr：`cmd::estop` 按 shared_ptr 收 sink（异步时要延长寿命），
/// 同步测试里直接借用同一个对象，才看得到它的记录。
inline std::shared_ptr<go2::CommandSink> borrow(go2::CommandSink& s) {
    return std::shared_ptr<go2::CommandSink>(&s, [](go2::CommandSink*) {});
}

}  // namespace go2test
