关于 Unitree Go2 无线通讯异常的排查需求说明
背景： 目前正在进行 Unitree Go2 机器狗的二次开发，在有线连接（192.168.123.x）下，所有控制（Low Level / High Level）均正常。 但在切换到无线连接（WiFi, 192.168.1.x）时，遭遇了“网络层通畅，但应用层（DDS）无数据回传”的问题，导致无法控制机器狗。需要协助进行双端抓包分析，定位数据丢失的具体节点。

1. 网络拓扑与环境
控制端 (PC):

系统: Ubuntu 22.04

无线网卡: wlo1

IP: 192.168.1.106

防火墙: ufw disable, iptables -F (已全关)

被控端 (Robot Go2):

无线接口 (wlan0): 192.168.1.211

内部有线接口 (eth0): 192.168.123.161 (机器狗内部运控 PC)

注：Robot 连接在同一路由器的 1 网段下。

2. 目前已验证的现象
我们进行了一系列测试，现象非常矛盾：

ICMP (Ping) 完全通畅：

PC ping 192.168.1.211 (Robot WiFi) -> 通 (延迟 ~13ms)。

PC 配置静态路由 (ip route add 192.168.123.0/24 via 192.168.1.211) 后，ping 192.168.123.161 (Robot 内部 IP) -> 也通 (延迟 ~45ms)。

结论：物理链路、路由转发、IP 转发功能均正常。

UDP (DDS SDK) 通讯失败：

运行官方示例 go2_sport_client（已配置 cyclonedds.xml 强制指定 Peer 为 192.168.1.211 或 123.161）。

现象：程序能启动，但读取到的状态数据全为 0 (x:0, y:0)，机器狗不执行动作。

有线对比：插上网线运行相同程序，数据正常更新 (x:0.03...)，机器狗动作正常。

PC 端抓包结果 (tcpdump)：

在 PC 端执行 sudo tcpdump -i wlo1 src 192.168.1.211 -n。

结果：0 packets captured。

结论：机器狗收到了 PC 的指令（因为 Ping 能回），但在 DDS 协议层面，机器狗没有向 PC 的无线 IP 发送任何 UDP 回包。

3. 怀疑原因
目前推测问题出在机器狗内部的 DDS 中间件配置或路由策略上：

可能性 A (DDS 绑定限制): 机器狗内部的 DDS 服务可能被配置为仅监听/发送到 eth0 (123.x) 接口，忽略了 wlan0 接口的请求。

可能性 B (回程路由缺失): 机器狗内部 Linux 可能没有指向 192.168.1.x 网段的路由，导致 UDP 回包被丢弃（虽然 Ping 能回很奇怪，可能是 ICMP 处理机制不同）。

可能性 C (防火墙拦截): 机器狗内部的 iptables 可能拦截了来自非 123 网段的 UDP 通讯。

4. 协助需求 (抓包任务)
麻烦做一个针对 DDS/UDP 的抓包或诊断工具，或者协助进行以下深层分析：

双向确认：需要确认 PC 发出的 DDS "Discovery"（发现）包，内容是否正确？（是否包含正确的回复 IP 和端口）。

机器狗内部视角：

（如果能 SSH 进狗）在狗的内部运行 tcpdump：tcpdump -i any port 7417 (或其他 DDS 常用端口)。

观察：狗到底有没有收到 PC 的包？收到后有没有尝试从哪个网卡回包？

验证 DDS XML 配置：帮我检查目前的 cyclonedds.xml 单播配置是否符合标准 CycloneDDS 跨网段通讯协议。

附：目前使用的 PC 端 DDS 配置文件 (cyclonedds.xml)

```XML
<CycloneDDS>
    <Domain id="any">
        <General>
            <NetworkInterfaceAddress>wlo1</NetworkInterfaceAddress>
            <AllowMulticast>false</AllowMulticast> 
        </General>
        <Discovery>
            <Peers>
                <Peer address="192.168.1.211"/> </Peers>
            <ParticipantIndex>auto</ParticipantIndex>
        </Discovery>
    </Domain>
</CycloneDDS>
```
