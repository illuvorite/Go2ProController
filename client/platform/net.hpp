#pragma once

// ============================================================================
// platform/net.hpp —— 平台网络层唯一接缝
//
// 规则：core/ 里的代码**只能**通过本文件使用网络能力，
//       不允许再直接 include <sys/socket.h> / <winsock2.h> 等平台头。
// 现状：
//   - POSIX（Linux / macOS / WSL）：直接透传系统头，行为与改造前完全一致
//   - Windows：提供 Winsock 头 + 归一化辅助（initSockets / closeSocket / lastSocketError）
// 后续（P1 Windows 移植）会把调用点从"直接调 POSIX 名"迁到本文件的 helper。
// ============================================================================

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <iphlpapi.h>
using go2_socket_t = SOCKET;
#  define GO2_INVALID_SOCKET INVALID_SOCKET
#else
#  include <arpa/inet.h>
#  include <errno.h>
#  include <fcntl.h>
#  include <net/if.h>
#  include <netinet/in.h>
#  include <poll.h>
#  include <sys/socket.h>
#  include <sys/types.h>
#  include <unistd.h>
#  ifdef __linux__
#    include <sys/ioctl.h>
#  endif
using go2_socket_t = int;
#  define GO2_INVALID_SOCKET (-1)
#endif

namespace go2 {
namespace platform {

/// 进程内只初始化一次（Windows 需要 WSAStartup；POSIX 空实现）
inline bool initSockets() {
#ifdef _WIN32
    static const bool ok = [] {
        WSADATA d;
        return WSAStartup(MAKEWORD(2, 2), &d) == 0;
    }();
    return ok;
#else
    return true;
#endif
}

inline void closeSocket(go2_socket_t s) {
    if (s == GO2_INVALID_SOCKET) return;
#ifdef _WIN32
    ::closesocket(s);
#else
    ::close(s);
#endif
}

inline int lastSocketError() {
#ifdef _WIN32
    return ::WSAGetLastError();
#else
    return errno;
#endif
}

/// 非阻塞调用"暂时不可用"（POSIX: EAGAIN/EWOULDBLOCK；Windows: WSAEWOULDBLOCK）
inline bool wouldBlock(int err) {
#ifdef _WIN32
    return err == WSAEWOULDBLOCK;
#else
    return err == EAGAIN || err == EWOULDBLOCK;
#endif
}

/// 等待套接字可读/可写（Windows 用 WSAPoll，POSIX 用 poll），返回就绪个数
inline int waitSocket(go2_socket_t s, bool wantWrite, int timeoutMs) {
#ifdef _WIN32
    WSAPOLLFD p{};
    p.fd = s;
    p.events = wantWrite ? POLLWRNORM : POLLRDNORM;
    return ::WSAPoll(&p, 1, timeoutMs);
#else
    struct ::pollfd p{};
    p.fd = s;
    p.events = wantWrite ? POLLOUT : POLLIN;
    return ::poll(&p, 1, timeoutMs);
#endif
}

}  // namespace platform
}  // namespace go2
