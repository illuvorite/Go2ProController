#pragma once

// ============================================================================
// ITransport 的 WebRTC 实现：把 libdatachannel 的 DataChannel 包成"一条通道"。
//
// ★ 生命周期：本对象**必须由 shared_ptr 持有**（内部用 weak_from_this 保护回调）。
//   原因：`rtc::DataChannel` 的 onOpen/onClosed/onMessage 回调是 libdatachannel
//   **内部线程**异步调用的，可能在通道关闭之后才到；如果回调直接捕获裸 `this`，
//   而本对象已经析构 → use-after-free（本项目历史上真出过同类崩溃）。
//   用 weak_ptr 之后，对象没了回调就静默丢弃，不会踩到已释放内存。
// ============================================================================

#include "transport.hpp"

#include <memory>

namespace rtc {
class DataChannel;
}  // namespace rtc

namespace go2 {

class WebRtcTransport final : public ITransport,
                              public std::enable_shared_from_this<WebRtcTransport> {
public:
    explicit WebRtcTransport(std::shared_ptr<rtc::DataChannel> dc);

    const char* name() const override { return "webrtc"; }
    std::string label() const override;
    bool isOpen() const override;
    bool send(const std::string& text) override;
    void close() override;

    void setOnOpen(std::function<void()> cb) override;
    void setOnClosed(std::function<void()> cb) override;
    void setOnMessage(std::function<void(const std::string&)> cb) override;
    void setOnBinary(std::function<void(std::size_t, const std::string&)> cb) override;

private:
    void fireOpen();
    void fireClosed();
    void fireMessage(const std::string& s);
    void fireBinary(std::size_t n, const std::string& hex);

    std::shared_ptr<rtc::DataChannel> dc_;
    std::function<void()> onOpen_;
    std::function<void()> onClosed_;
    std::function<void(const std::string&)> onMessage_;
    std::function<void(std::size_t, const std::string&)> onBinary_;
};

/// 建一条 WebRTC 传输（dc 为空时返回 nullptr —— 调用方要判空）
std::shared_ptr<WebRtcTransport> makeWebRtcTransport(std::shared_ptr<rtc::DataChannel> dc);

}  // namespace go2
