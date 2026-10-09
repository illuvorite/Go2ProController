#include "webrtc_transport.hpp"

#include <rtc/rtc.hpp>

#include <cstdio>
#include <stdexcept>
#include <utility>
#include <variant>

namespace go2 {

WebRtcTransport::WebRtcTransport(std::shared_ptr<rtc::DataChannel> dc) : dc_(std::move(dc)) {}

void WebRtcTransport::wireCallbacks() {
    if (!dc_) return;

    // 回调在"本对象已被 shared_ptr 接管之后"才挂到通道上（见 hpp 的说明）：
    // weak_from_this() 在构造函数里拿到的是空指针，在那里注册会让全部回调静默失效。
    // setOnXxx 只是替换"转发目标"，所以这里捕获 weak_ptr、不捕获 this。
    dc_->onOpen([w = weak_from_this()] {
        if (auto self = w.lock()) self->fireOpen();
    });
    dc_->onClosed([w = weak_from_this()] {
        if (auto self = w.lock()) self->fireClosed();
    });
    dc_->onMessage([w = weak_from_this()](rtc::message_variant data) {
        auto self = w.lock();
        if (!self) return;
        if (auto* s = std::get_if<std::string>(&data)) {
            self->fireMessage(*s);
        } else if (auto* b = std::get_if<rtc::binary>(&data)) {
            // 本项目不用二进制通道，只留一小段 hex 预览用于排查
            std::string hex;
            char buf[8];
            for (std::size_t i = 0; i < b->size() && i < 24; ++i) {
                std::snprintf(buf, sizeof(buf), "%02x ", int((*b)[i]));
                hex += buf;
            }
            self->fireBinary(b->size(), hex);
        }
    });
}

std::string WebRtcTransport::label() const { return dc_ ? dc_->label() : std::string(); }

bool WebRtcTransport::isOpen() const {
    // libdatachannel 的 isOpen() 不是线程安全的读，但这里只在"发送前判断"这一处用，
    // 且失败会被下面的 send 兜住 —— 与改动前的行为一致（原来也是 dc_->isOpen()）。
    return dc_ && dc_->isOpen();
}

bool WebRtcTransport::send(const std::string& text) {
    if (!isOpen()) return false;
    try {
        dc_->send(std::string(text));  // 拷贝一份：send 内部可能异步持有
        return true;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[webrtc] 发送失败: %s\n", e.what());
        return false;
    }
}

void WebRtcTransport::close() {
    if (!dc_) return;
    try {
        dc_->close();
    } catch (...) {
        // 通道可能已经自己关了 —— 关闭失败无所谓
    }
    dc_.reset();
}

void WebRtcTransport::setOnOpen(std::function<void()> cb) { onOpen_ = std::move(cb); }
void WebRtcTransport::setOnClosed(std::function<void()> cb) { onClosed_ = std::move(cb); }
void WebRtcTransport::setOnMessage(std::function<void(const std::string&)> cb) {
    onMessage_ = std::move(cb);
}
void WebRtcTransport::setOnBinary(std::function<void(std::size_t, const std::string&)> cb) {
    onBinary_ = std::move(cb);
}

void WebRtcTransport::fireOpen() {
    if (onOpen_) onOpen_();
}
void WebRtcTransport::fireClosed() {
    if (onClosed_) onClosed_();
}
void WebRtcTransport::fireMessage(const std::string& s) {
    if (onMessage_) onMessage_(s);
}
void WebRtcTransport::fireBinary(std::size_t n, const std::string& hex) {
    if (onBinary_) onBinary_(n, hex);
}

std::shared_ptr<WebRtcTransport> makeWebRtcTransport(std::shared_ptr<rtc::DataChannel> dc) {
    if (!dc) return nullptr;
    // ★ 必须先建出 shared_ptr，再挂回调 —— 见 WebRtcTransport::wireCallbacks() 的说明。
    auto transport = std::make_shared<WebRtcTransport>(std::move(dc));
    transport->wireCallbacks();
    return transport;
}

}  // namespace go2
