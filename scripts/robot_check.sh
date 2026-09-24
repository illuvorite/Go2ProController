#!/bin/bash
# 机器狗端诊断脚本
# 将此脚本传到机器狗上运行，收集诊断信息
# 用法: ssh root@192.168.1.211 'bash -s' < robot_check.sh

echo "========================================"
echo "Unitree Go2 机器狗端诊断"
echo "========================================"
echo "时间: $(date)"
echo ""

echo "--- 1. 网络接口 ---"
ip addr show
echo ""

echo "--- 2. 路由表 ---"
ip route show
echo ""

echo "--- 3. IP 转发状态 ---"
cat /proc/sys/net/ipv4/ip_forward
echo ""

echo "--- 4. 监听的 UDP 端口 ---"
ss -ulnp | grep -E '740[0-9]|741[0-9]'
echo ""

echo "--- 5. DDS 相关进程 ---"
ps aux | grep -i dds | grep -v grep
echo ""

echo "--- 6. 防火墙状态 ---"
if command -v iptables &> /dev/null; then
    iptables -L -n 2>/dev/null || echo "需要 root 权限"
fi
echo ""

echo "--- 7. 检查 CycloneDDS 配置 ---"
echo "CYCLONEDDS_URI: $CYCLONEDDS_URI"
if [ -n "$CYCLONEDDS_URI" ]; then
    config_file="${CYCLONEDDS_URI#file://}"
    if [ -f "$config_file" ]; then
        echo "配置文件内容:"
        cat "$config_file"
    fi
fi
echo ""

echo "--- 8. 网络接口上的 DDS 流量 ---"
echo "在 wlan0 上抓包 5 秒..."
timeout 5 tcpdump -i wlan0 -c 100 udp portrange 7400-7420 2>/dev/null || echo "需要 root 权限或 tcpdump"
echo ""

echo "--- 9. 到 PC 的连通性 ---"
# 替换为你的 PC IP
PC_IP="192.168.1.106"
ping -c 3 $PC_IP
echo ""

echo "========================================"
echo "诊断完成"
echo "========================================"
