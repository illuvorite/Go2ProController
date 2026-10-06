#include "key_probe.hpp"

#include "local_keys.hpp"
#include "robot_manager.hpp"
#include "ui.hpp"

#include "../platform/net.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <regex>
#include <set>
#include <thread>
#include <vector>

namespace go2 {

// ---------------------------------------------------------------- 网络探针（跨平台）

/// 非阻塞 TCP 端口探测
static bool tcpOpen(const std::string& ip, unsigned short port, int timeoutMs) {
    platform::initSockets();
    go2_socket_t s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s == GO2_INVALID_SOCKET) return false;
#ifdef _WIN32
    u_long nb = 1;
    ::ioctlsocket(s, FIONBIO, &nb);
#else
    int flags = ::fcntl(s, F_GETFL, 0);
    ::fcntl(s, F_SETFL, flags | O_NONBLOCK);
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = ::htons(port);
    ::inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
    const int rc = ::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    bool ok = (rc == 0);
    if (!ok) {
        const int err = platform::lastSocketError();
#ifdef _WIN32
        if (platform::wouldBlock(err)) {
#else
        if (platform::wouldBlock(err) || err == EINPROGRESS) {
#endif
            ok = platform::waitSocket(s, true, timeoutMs) > 0;
            if (ok) {
                int soErr = 0;
                socklen_t len = sizeof(soErr);
                ::getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soErr), &len);
                ok = (soErr == 0);
            }
        }
    }
    platform::closeSocket(s);
    return ok;
}

/// 阻塞式 HTTP GET（短超时；只拿小响应）
static std::string httpGet(const std::string& ip, unsigned short port,
                           const std::string& path, int timeoutMs) {
    platform::initSockets();
    go2_socket_t s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s == GO2_INVALID_SOCKET) return {};
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = ::htons(port);
    ::inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
    timeval tv{};
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&tv), sizeof(tv));
    ::setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<char*>(&tv), sizeof(tv));
    if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        platform::closeSocket(s);
        return {};
    }
    const std::string req = "GET " + path +
                            " HTTP/1.0\r\nHost: " + ip +
                            "\r\nUser-Agent: go2-remote-keyprobe\r\nConnection: close\r\n\r\n";
    std::string out;
    if (::send(s, req.c_str(), static_cast<int>(req.size()), 0) > 0) {
        char buf[4096];
        for (;;) {
            const int n = static_cast<int>(::recv(s, buf, sizeof(buf), 0));
            if (n <= 0) break;
            out.append(buf, static_cast<size_t>(n));
            if (out.size() > 256 * 1024) break;
        }
    }
    platform::closeSocket(s);
    return out;
}

/// 从文本里捞 32 位 hex 候选（去重 + 字符种类 > 4，滤掉 aaaa… 这种假货）
static std::vector<std::string> hexCandidates(const std::string& text) {
    static const std::regex kHex("\\b[0-9a-fA-F]{32}\\b");
    std::vector<std::string> out;
    std::set<std::string> seen;
    auto it = std::sregex_iterator(text.begin(), text.end(), kHex);
    for (auto end = std::sregex_iterator(); it != end; ++it) {
        std::string k = it->str();
        std::transform(k.begin(), k.end(), k.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const int distinct = static_cast<int>(std::set<char>(k.begin(), k.end()).size());
        if (distinct > 4 && seen.insert(k).second) out.push_back(k);
    }
    return out;
}

// ---------------------------------------------------------------- 对外入口

void runKeyProbe(RobotManager& mgr, UiState& ui, const std::string& ipIn) {
    (void)mgr;  // 本函数只扫狗的端口/Web 服务，用不到连接句柄；
                // 保留形参是为了与 applyKeyCandidate(mgr, ui, key, ip) 的调用形状一致
    std::string ip = ipIn;
    if (ip.empty()) {
        const auto sel = ui.selectedIps();
        if (!sel.empty()) ip = sel.front();
        else {
            std::lock_guard<std::mutex> lock(ui.robotsMutex);
            if (!ui.robots.empty()) ip = ui.robots.front().ip;
        }
    }
    if (ip.empty()) {
        std::lock_guard<std::mutex> lock(ui.keyScanMutex);
        ui.keyScanNote = "没有可探测的 IP —— 先在设备里添加一台狗";
        return;
    }

    ui.keyScanning = true;
    {
        std::lock_guard<std::mutex> lock(ui.keyScanMutex);
        ui.keyCandidates.clear();
        ui.keyScanNote = "探测中：" + ip;
    }
    ui.addLog("[钥匙] 开始探测 " + ip + " 的内网服务 ...");

    static const int kPorts[] = {22, 23, 80, 443, 5555, 2049, 8080,
                                 8081, 8888, 8000, 9090, 9991};
    std::vector<int> open;
    for (int p : kPorts)
        if (tcpOpen(ip, static_cast<unsigned short>(p), 400)) open.push_back(p);

    std::string note;
    if (open.empty()) {
        note = "没有开放端口 —— 狗不在线 / IP 不对 / 不在同一网段";
    } else {
        note = "开放端口:";
        for (size_t i = 0; i < open.size(); ++i)
            note += (i ? "," : " ") + std::to_string(open[i]);
    }
    ui.addLog("[钥匙] " + note);

    std::vector<std::string> found;
    const auto harvest = [&](const std::string& text) {
        for (const auto& k : hexCandidates(text))
            if (std::find(found.begin(), found.end(), k) == found.end()) found.push_back(k);
    };
    for (int p : open) {
        if (p == 80 || p == 8080 || p == 8081 || p == 8888 || p == 8000 || p == 9090) {
            for (const char* path : {"/", "/config", "/debug/vars"})
                harvest(httpGet(ip, static_cast<unsigned short>(p), path, 1500));
        }
    }

    std::string hint;
    for (int p : open) {
        if (p == 5555) hint += " 5555(ADB)";
        if (p == 22) hint += " 22(SSH)";
        if (p == 2049) hint += " 2049(NFS)";
    }
    if (!hint.empty())
        hint = "这些入口需要外部工具（adb/sshpass/mount），目前只能用电脑：" + hint + "；";

    {
        std::lock_guard<std::mutex> lock(ui.keyScanMutex);
        ui.keyCandidates = found;
        ui.keyScanNote = note + (found.empty() ? "" : ("；候选钥匙 " + std::to_string(found.size()) + " 把")) +
                         "。" + hint;
    }
    if (found.empty()) {
        ui.addLog(std::string("[钥匙] 没有直接捞到 32 位 hex。") + hint);
    } else {
        for (const auto& k : found) ui.addLog("[钥匙] 候选: " + k + "（点「采用」后握手验证）");
    }
    ui.keyScanning = false;
}

bool applyKeyCandidate(RobotManager& mgr, UiState& ui, const std::string& key,
                       const std::string& ip) {
    const bool hexOk = key.size() == 32 &&
                       std::all_of(key.begin(), key.end(),
                                   [](unsigned char c) { return std::isxdigit(c) != 0; });
    if (!hexOk) return false;
    {
        // 与设置页手工粘贴走同一个位置：应用数据目录（不落工程目录）
        const std::string keysPath = defaultKeysTxtPath();
        ensureParentDir(keysPath);
        std::ofstream f(keysPath, std::ios::app);
        f << key << "\n";
    }
    mgr.addAesKeys({key});
    if (!ip.empty()) mgr.bindKey(ip, key);
    ui.addLog("[钥匙] 已采用 " + key + (ip.empty() ? "" : (" → 绑定 " + ip)) +
              "（下次连接生效；握手成功即为正确钥匙）");
    return true;
}

}  // namespace go2
