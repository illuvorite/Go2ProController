#!/bin/bash
# 设置到机器狗内网的路由
# 用法: sudo ./setup_route.sh [机器狗WiFi IP] [内网网段]

ROBOT_WIFI_IP=${1:-192.168.1.211}
INTERNAL_SUBNET=${2:-192.168.123.0/24}

echo "设置路由..."
echo "  目标网段: $INTERNAL_SUBNET"
echo "  网关: $ROBOT_WIFI_IP"

# 检查路由是否已存在
if ip route show | grep -q "$INTERNAL_SUBNET"; then
    echo "路由已存在，先删除..."
    sudo ip route del $INTERNAL_SUBNET
fi

# 添加路由
sudo ip route add $INTERNAL_SUBNET via $ROBOT_WIFI_IP

# 验证
if ip route show | grep -q "$INTERNAL_SUBNET"; then
    echo "✓ 路由设置成功"
    ip route show | grep "$INTERNAL_SUBNET"
else
    echo "✗ 路由设置失败"
    exit 1
fi

# 测试连通性
echo ""
echo "测试连通性..."
ping -c 2 192.168.123.161
