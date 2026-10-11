#include "discovery.hpp"

#include "crypto.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#else
#include "net.hpp"  // 平台网络层（POSIX / Winsock 由它选择）
#include <fcntl.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <set>
#include <thread>

#ifdef _WIN32
namespace {
/// Windows 下所有 socket 调用前必须初始化 Winsock
struct WinsockGuard {
    WinsockGuard() { WSADATA d; WSAStartup(MAKEWORD(2, 2), &d); }
    ~WinsockGuard() { WSACleanup(); }
};
const WinsockGuard g_winsockGuard;
}  // namespace
#endif

namespace go2 {
namespace {

/// 取网段前三段："192.168.2.108/24" -> "192.168.2"
std::string prefixOf(const std::string& cidr) {
    const size_t pos = cidr.rfind('.');
    return pos == std::string::npos ? cidr : cidr.substr(0, pos);
}

/// 多播发现的协议常量（与参考实现一致）
constexpr uint16_t kSnRecvPort = 10134;
constexpr uint16_t kSnQueryPort = 10131;
constexpr const char* kSnQueryGroup = "231.1.1.1";
constexpr const char* kSnQueryName = "unitree_dapengche";

}  // namespace

std::vector<std::string> Discovery::localIPv4Addresses() {
#ifdef _WIN32
    std::set<std::string> winOut;
    ULONG size = 16 * 1024;
    std::vector<char> buf(size);
    auto* addrs = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
    const ULONG flags =
        GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG rc = ::GetAdaptersAddresses(AF_INET, flags, nullptr, addrs, &size);
    if (rc == ERROR_BUFFER_OVERFLOW) {
        buf.resize(size);
        addrs = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
        rc = ::GetAdaptersAddresses(AF_INET, flags, nullptr, addrs, &size);
    }
    if (rc != NO_ERROR) return {};
    for (auto* a = addrs; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp) continue;
        for (auto* ua = a->FirstUnicastAddress; ua; ua = ua->Next) {
            if (!ua->Address.lpSockaddr || ua->Address.lpSockaddr->sa_family != AF_INET) continue;
            const auto* sin = reinterpret_cast<const sockaddr_in*>(ua->Address.lpSockaddr);
            char ip[INET_ADDRSTRLEN] = {0};
            if (!::inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip))) continue;
            const std::string ws(ip);
            if (ws.rfind("127.", 0) == 0 || ws.rfind("169.254.", 0) == 0) continue;
            winOut.insert(ws);
        }
    }
    return {winOut.begin(), winOut.end()};
#else
    std::set<std::string> out;
    struct ifaddrs* ifs = nullptr;
    if (getifaddrs(&ifs) != 0) return {};
    for (struct ifaddrs* ifa = ifs; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
        const auto* sa = reinterpret_cast<const sockaddr_in*>(ifa->ifa_addr);
        char buf[INET_ADDRSTRLEN] = {0};
        if (!inet_ntop(AF_INET, &sa->sin_addr, buf, sizeof(buf))) continue;
        const std::string ip(buf);
        if (ip.rfind("127.", 0) == 0) continue;
        if (ip.rfind("169.254.", 0) == 0) continue;
        out.insert(ip);
    }
    freeifaddrs(ifs);
    return {out.begin(), out.end()};
#endif
}

std::vector<std::string> Discovery::localSubnets() {
    // 由地址列表推出 /24 —— 两处口径必须一致（过滤规则只有一份）
    std::set<std::string> out;
    for (const auto& ip : localIPv4Addresses()) {
        const size_t pos = ip.rfind('.');
        if (pos == std::string::npos) continue;
        out.insert(ip.substr(0, pos) + ".0/24");
    }
    return {out.begin(), out.end()};
}
// （原 localSubnets 的按平台实现已上移：现在统一由 localIPv4Addresses() 推出 /24，
//   过滤规则只留一份，避免"地址列表"和"网段列表"两处口径漂移。）

bool Discovery::tcpPortOpen(const std::string& ip, uint16_t port, int timeoutMs) {
#ifdef _WIN32
    SOCKET fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCKET) return false;
    u_long nb = 1;
    ::ioctlsocket(fd, FIONBIO, &nb);

    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    if (::inet_pton(AF_INET, ip.c_str(), &sa.sin_addr) != 1) {
        ::closesocket(fd);
        return false;
    }

    bool open = false;
    if (::connect(fd, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) == 0) {
        open = true;
    } else if (::WSAGetLastError() == WSAEWOULDBLOCK) {
        WSAPOLLFD p{fd, POLLOUT, 0};
        if (::WSAPoll(&p, 1, timeoutMs) > 0) {
            int err = 0;
            int len = sizeof(err);
            ::getsockopt(fd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len);
            open = (err == 0);
        }
    }
    if (open) ::shutdown(fd, SD_SEND);  // 优雅收尾，避免堵住狗的单线程信令服务
    ::closesocket(fd);
    return open;
#else
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;

    const int flags = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    if (::inet_pton(AF_INET, ip.c_str(), &sa.sin_addr) != 1) {
        ::close(fd);
        return false;
    }

    bool open = false;
    if (::connect(fd, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) == 0) {
        open = true;
    } else if (errno == EINPROGRESS) {
        pollfd p{fd, POLLOUT, 0};
        if (::poll(&p, 1, timeoutMs) > 0) {
            int err = 0;
            socklen_t len = sizeof(err);
            ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
            open = (err == 0);
        }
    }

    if (open) {
        // 优雅关闭写端，让对端立刻收到 EOF 并释放连接。
        // 否则机器狗的单线程信令服务器要等 Keep-Alive 超时（约 4 秒），
        // 其间紧随其后的 HTTP 确认请求会被全部堵死。
        ::shutdown(fd, SHUT_WR);
    }
    ::close(fd);
    return open;
#endif
}

DiscoveredRobot Discovery::inspect(const std::string& ip) {
    DiscoveredRobot out;
    out.ip = ip;

    // ---- 1. 端口探测：9991（新固件）优先，回退 8081（旧固件）----
    int port = 0;
    if (tcpPortOpen(ip, 9991, 700)) {
        port = 9991;
    } else if (tcpPortOpen(ip, 8081, 700)) {
        port = 8081;
    } else {
        out.note = "9991/8081 均不可达（不在同一网段 / 未开机 / 防火墙）";
        return out;
    }
    out.port = port;

    // ---- 2. 旧固件：8081 只有明文 /offer，能连上但拿不到 data2 ----
    if (port == 8081) {
        out.verified = true;
        out.note = "旧固件（Go2 < 1.1.11）：8081 明文 /offer 流程，不需要钥匙";
        return out;
    }

    // ---- 3. 新固件：GET /con_notify 确认身份并读出 data2 ----
    for (int attempt = 0; attempt < 3; ++attempt) {
        httplib::Client cli(ip, port);
        cli.set_connection_timeout(2, 0);
        cli.set_read_timeout(2, 0);
        if (auto res = cli.Get("/con_notify")) {
            if (res->status == 200 && res->body.size() > 32) {
                std::string text;
                const auto decoded = crypto::base64Decode(res->body);
                text.assign(decoded.begin(), decoded.end());
                if (text.find("data1") == std::string::npos)
                    text = res->body;  // 少数固件直接返回明文
                if (text.find("data1") != std::string::npos) {
                    out.verified = true;
                    int data2 = 0;
                    try {
                        data2 = nlohmann::json::parse(text).value("data2", 1);
                    } catch (...) {
                        data2 = 0;
                    }
                    out.data2 = data2;
                    if (data2 == 3) {
                        out.needsKey = true;
                        out.note = "新固件（Go2 ≥ 1.1.15）：data2=3，需要云账号里的每设备 AES-128 钥匙";
                    } else if (data2 == 2) {
                        out.note = "固件 < 1.1.15：data2=2，内置静态钥匙即可";
                    } else if (data2 == 1) {
                        out.note = "data2=1：明文握手，不需要钥匙";
                    } else {
                        out.note = "已响应 con_notify，但 data2 未知，连接时再确认";
                    }
                    return out;
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    }

    out.note = "9991 端口开放，但 /con_notify 无有效响应（信令服务可能被占用）";
    return out;
}

bool Discovery::isGo2(const std::string& ip) { return inspect(ip).verified; }

std::vector<DiscoveredRobot> Discovery::scanSubnet(
    const std::string& subnetCidr, int timeoutMs,
    const std::function<void(const std::string&)>& progress) {

    const std::string prefix = prefixOf(subnetCidr);
    if (progress) progress("探测网段 " + prefix + ".1 ~ " + prefix + ".254 (TCP 9991/8081) ...");

    // ---- 并行 TCP 探测 1~254 ----
    std::atomic<int> next{1};
    std::vector<std::string> hits;
    std::mutex hitsMutex;
    auto probe = [&] {
        for (;;) {
            const int n = next.fetch_add(1);
            if (n > 254) return;
            const std::string ip = prefix + "." + std::to_string(n);
            if (tcpPortOpen(ip, 9991, timeoutMs) || tcpPortOpen(ip, 8081, timeoutMs)) {
                std::lock_guard<std::mutex> lock(hitsMutex);
                hits.push_back(ip);
            }
        }
    };

    // 探测代价 ≈ 254 台 × 2 个端口 × timeoutMs / workers：
    // 空闲主机要等 ARP 失败（吃满 timeout），所以并发数是决定性的。
    // 实测：workers 只有 4~8 时整轮要 20~30 秒，用户直接感知成"扫描很慢"；
    // 这类探测只是 connect + close，几乎不吃 CPU，适度放大并发把耗时压到几秒。
    const unsigned hw = std::thread::hardware_concurrency();
    const int workers = std::max(16, std::min(128, int(hw ? hw : 8) * 8));
    {
        std::vector<std::thread> pool;
        for (int i = 0; i < workers; ++i) pool.emplace_back(probe);
        for (auto& t : pool) t.join();
    }

    std::sort(hits.begin(), hits.end());
    if (progress) progress("端口探测完成，命中 " + std::to_string(hits.size()) + " 台，逐一确认 ...");

    // ---- 逐台确认是 Go2（命中数通常很少，串行即可）----
    std::vector<DiscoveredRobot> found;
    for (const std::string& ip : hits) {
        DiscoveredRobot info = inspect(ip);
        if (progress) {
            progress("  " + ip + (info.verified ? "  ✓ " : "  ✗ ") + info.note);
        }
        if (info.verified) found.push_back(std::move(info));
    }

    if (progress) progress("扫描结束：发现 " + std::to_string(found.size()) + " 台 Go2");
    return found;
}

std::vector<DiscoveredRobot> Discovery::scanSubnets(
    const std::vector<std::string>& subnetCidrs, int timeoutMs,
    const std::function<void(const std::string&)>& progress) {

    std::vector<DiscoveredRobot> all;
    std::set<std::string> seen;
    for (const auto& cidr : subnetCidrs) {
        for (auto& r : scanSubnet(cidr, timeoutMs, progress)) {
            if (seen.insert(r.ip).second) all.push_back(std::move(r));
        }
    }
    return all;
}

std::vector<std::pair<std::string, std::string>> Discovery::multicastSnScan(
    int timeoutMs, const std::function<void(const std::string&)>& progress) {

    std::vector<std::pair<std::string, std::string>> out;
    std::set<std::string> seenSn;  // 同一台狗在多张网卡上都可能回一次 → 按 SN 去重

    const std::vector<std::string> ifaces = localIPv4Addresses();
    if (ifaces.empty()) {
        if (progress) progress("多播发现跳过：本机没有可用的 IPv4 网卡");
        return out;
    }
    // 打出实际使用的网卡：多播"0 回包"时，第一眼要看的就是它到底从哪张网卡发的
    if (progress) {
        std::string list;
        for (const auto& a : ifaces) list += (list.empty() ? "" : ", ") + a;
        progress("多播发现：经 " + std::to_string(ifaces.size()) + " 张网卡查询 SN -> " + list);
    }

    sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_addr.s_addr = ::inet_addr(kSnQueryGroup);
    target.sin_port = htons(kSnQueryPort);

    const std::string query = std::string("{\"name\":\"") + kSnQueryName + "\"}";

    // ★ 逐张网卡各发一遍 —— 见 hpp 里 localIPv4Addresses() 的说明。
    //   只用 INADDR_ANY 时，Android 会把多播出口落到蜂窝、多网卡 PC 会落到
    //   虚拟网卡，机器狗收不到查询；现象是"绑定/入组/发送全成功但 0 回包"。
    for (size_t i = 0; i < ifaces.size(); ++i) {
        const std::string& bindIp = ifaces[i];
        const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0) continue;

        int reuse = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

        sockaddr_in local{};
        local.sin_family = AF_INET;
        local.sin_addr.s_addr = htonl(INADDR_ANY);
        local.sin_port = htons(kSnRecvPort);
        if (::bind(fd, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0) {
            if (progress && i == 0)
                progress("多播发现跳过：本机 " + std::to_string(kSnRecvPort) +
                         " 端口被占用（另一个客户端正在运行？）");
            ::close(fd);
            continue;
        }

        in_addr ifAddr{};
        if (::inet_pton(AF_INET, bindIp.c_str(), &ifAddr) != 1) {
            ::close(fd);
            continue;
        }
        // 出口网卡 + 入组网卡都钉死在这张网卡上（两个都要设：只设一个不够）
        ::setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &ifAddr, sizeof(ifAddr));

        ip_mreq mreq{};
        mreq.imr_multiaddr.s_addr = ::inet_addr(kSnQueryGroup);
        mreq.imr_interface = ifAddr;
        if (::setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) != 0) {
            if (progress) progress("多播发现跳过：在 " + bindIp + " 上加入多播组失败");
            ::close(fd);
            continue;
        }

        auto setRecvTimeout = [&](int ms) {
            timeval tv{};
            tv.tv_sec = ms / 1000;
            tv.tv_usec = (ms % 1000) * 1000;
            ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        };

        // 收一轮（反复 recvfrom 直到超时）
        auto collect = [&]() {
            char buf[1024];
            for (;;) {
                sockaddr_in from{};
                socklen_t fromLen = sizeof(from);
                const ssize_t n = ::recvfrom(fd, buf, sizeof(buf) - 1, 0,
                                             reinterpret_cast<sockaddr*>(&from), &fromLen);
                if (n <= 0) return;
                buf[n] = '\0';
                nlohmann::json j;
                try {
                    j = nlohmann::json::parse(buf);
                } catch (...) {
                    continue;  // 不是我们能认的包（别人也在用这个多播组？）就丢掉
                }
                const std::string sn = j.value("sn", "");
                if (sn.empty() || !seenSn.insert(sn).second) continue;
                char ipBuf[INET_ADDRSTRLEN] = {0};
                ::inet_ntop(AF_INET, &from.sin_addr, ipBuf, sizeof(ipBuf));
                std::string ip = j.value("ip", std::string(ipBuf));
                if (progress) progress("多播发现: SN=" + sn + " IP=" + ip + " (经 " + bindIp + ")");
                out.emplace_back(sn, ip);
            }
        };

        bool sendOk = true;
        for (int attempt = 0; attempt < 3; ++attempt) {
            if (::sendto(fd, query.data(), query.size(), 0,
                         reinterpret_cast<sockaddr*>(&target), sizeof(target)) < 0) {
                sendOk = false;
                break;
            }
            setRecvTimeout(220);  // 发送间隔期用短超时抢收回复
            collect();
        }
        if (!sendOk) {
            if (progress && i == 0)
                progress("多播发现跳过：在 " + bindIp + " 上发送查询失败（多播路由不可用）");
        } else {
            // 第一张网卡给足等待时间；后续网卡短等即可（同一台狗不会只回某一张网卡）
            setRecvTimeout(i == 0 ? timeoutMs : (timeoutMs < 700 ? timeoutMs : 700));
            collect();
        }

        ::setsockopt(fd, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq));
        ::close(fd);
    }
    return out;
}

}  // namespace go2
