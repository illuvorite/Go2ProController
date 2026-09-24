#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace go2 {

struct DiscoveredRobot {
    std::string ip;
    bool verified = false;  ///< 已确认是 Go2 信令服务
    int port = 0;           ///< 命中的信令端口：9991（新固件）或 8081（旧固件）
    int data2 = 0;          ///< 0=未知/旧固件；1=明文；2=静态 key；3=需要每设备 key
    bool needsKey = false;  ///< data2==3，必须提供云账号里的每设备 AES-128 key
    std::string sn;         ///< 序列号（仅多播发现能拿到）
    std::string note;       ///< 人类可读的状态说明（可直接显示在界面上）
};

/// 局域网设备发现
///
/// Go2 的 WiFi 侧特征（实测 + 参考实现，见 docs/go2_webrtc_protocol.md）：
///   1. TCP 9991 开放 —— 新固件（Go2 > 1.1.11）的 WebRTC 信令服务器（Boost.Beast），
///      GET /con_notify 返回 base64(JSON) 且含 data1/data2
///   2. TCP 8081 开放 —— 旧固件（Go2 < 1.1.11）的明文 /offer 信令；
///      只开 8081 的机器狗同样可以被控制，但只探测 9991 会漏掉它
///   3. UDP 多播组 231.1.1.1:10131 可查询 SN，回复到本机 10134 端口
class Discovery {
public:
    /// 本机所在网段（/24 列表），已排除回环与链路本地地址
    static std::vector<std::string> localSubnets();

    /// TCP 端口探测（非阻塞 connect + poll，不依赖 ICMP）
    static bool tcpPortOpen(const std::string& ip, uint16_t port, int timeoutMs);

    /// 对单个 IP 做完整体检：端口 -> con_notify -> data2 / 是否需要钥匙
    static DiscoveredRobot inspect(const std::string& ip);

    /// 判断某 IP 是否为 Go2（含 8081 旧固件）
    static bool isGo2(const std::string& ip);

    /// 扫描一个 /24 网段（并行 TCP 探测 + 逐台确认）。
    /// 阻塞式，耗时数秒，请放到后台线程调用。
    /// progress: 进度回调（每条进度日志触发一次），可为空。
    static std::vector<DiscoveredRobot> scanSubnet(
        const std::string& subnetCidr, int timeoutMs = 400,
        const std::function<void(const std::string&)>& progress = {});

    /// 依次扫描多个网段（去重）。用于「路由器网段 + 机器狗 AP 网段」并存的情况。
    static std::vector<DiscoveredRobot> scanSubnets(
        const std::vector<std::string>& subnetCidrs, int timeoutMs = 400,
        const std::function<void(const std::string&)>& progress = {});

    /// 按 Go2 多播协议查询 SN -> IP（跨网段若路由器不转发多播则可能为空）。
    /// 返回 (sn, ip) 列表，便于把云端的「SN -> 钥匙」绑定到具体 IP。
    static std::vector<std::pair<std::string, std::string>> multicastSnScan(
        int timeoutMs = 2000,
        const std::function<void(const std::string&)>& progress = {});
};

}  // namespace go2
