#pragma once

// ============================================================================
// 传输通道抽象（M5 接缝）。
//
// 为什么要有这一层：
//   现在只有 WebRTC 一种实现 —— Go2 走的正是 WebRTC DataChannel（谁连上谁就拿到了
//   一条可靠文本通道）。换机型时最先要变的恰恰是"这条通道"，所以先把它抽出来。
//
// ★ 2026-10-06 更正：原文这里写的是"G1 走 CycloneDDS（rt/api/sport/request）"，
//   **这条结论是错的**，会误导人给 G1 白写一套 DDS 传输。查证结论见 docs/todo.md §T3：
//   G1（AIR / EDU）与 Go2 走**同一套 WebRTC** —— 同一个 con_notify 信令（GET /con_notify，
//   不带机型参数）、同一套 data2=1/2/3 加密（含 data2=3 的每设备 AES-128 key）、
//   同一类 DataChannel。本项目现有的 signaling / crypto / webrtc_transport 基本可原样复用。
//   真正要按机型改的是**上面那层**：指令表与群控语义（G1 是 FSM + 数组参数，
//   Go2 是平铺 api_id + 标量参数），不是传输层。
//   （该结论来自公开实现 legion1581/unitree_webrtc_connect 与 unitree_ui 的文档，
//     **尚未真机验证** —— 立项第一步应当先用真机确认握手能通，再决定要不要写新传输。）
//
//   把"收发一条文本消息"抽干净之后，新增一种传输 = 实现这个接口 + 换个工厂，
//   `RobotManager` / `command_service` / UI 一行都不用动。
//
// ★ 故意**只抽数据面**（打开与否 / 发送 / 关闭 / 回调）。
//   信令握手、RSA/AES 密钥、心跳、订阅、状态机都留在 `RobotClient` —— 那些是
//   "某机型的协议栈"，不属于通道；过早把它们写进接口，等于把 Go2 的私有信令细节
//   固化进抽象层，换机型时反而更难拆。
//
// ★ 当前状态：**接缝已落地并被真正使用**（不是只放个空接口）——
//   `RobotClient` 的发送出口 `sendRaw()` 与断开路径都走 `ITransport`，
//   WebRTC 实现见 core/webrtc_transport.{hpp,cpp}。
// ============================================================================

#include <cstddef>
#include <functional>
#include <string>

namespace go2 {

class ITransport {
public:
    virtual ~ITransport() = default;

    /// 通道类型名（日志/诊断用）："webrtc"、将来的 "dds"
    virtual const char* name() const = 0;

    /// 通道标签（WebRTC 的 DataChannel label；其它实现可返回空）
    virtual std::string label() const = 0;

    /// 通道是否已打开（可以 send）
    virtual bool isOpen() const = 0;

    /// 发送一条文本消息。返回 false = 通道未就绪或发送失败（不抛异常）
    virtual bool send(const std::string& text) = 0;

    /// 主动关闭并释放底层资源（幂等）
    virtual void close() = 0;

    // ---- 回调（在通道自身的线程上被调用，实现方负责不要长时间阻塞）----
    virtual void setOnOpen(std::function<void()> cb) = 0;
    virtual void setOnClosed(std::function<void()> cb) = 0;
    /// 收到一条文本消息
    virtual void setOnMessage(std::function<void(const std::string&)> cb) = 0;
    /// 收到一条二进制消息：字节数 + 前若干字节的 hex 预览（本项目用不到二进制，仅日志）
    virtual void setOnBinary(std::function<void(std::size_t, const std::string&)> cb) = 0;
};

}  // namespace go2
