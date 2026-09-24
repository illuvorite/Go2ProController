#!/bin/bash
# 临时禁用防火墙（用于诊断网络问题）
# 警告：这会降低系统安全性，仅用于调试

echo "警告：此脚本将禁用防火墙，仅用于诊断目的！"
echo "按 Enter 继续，Ctrl+C 取消..."
read

echo "禁用 UFW..."
sudo ufw disable

echo "清空 iptables 规则..."
sudo iptables -F
sudo iptables -X
sudo iptables -P INPUT ACCEPT
sudo iptables -P FORWARD ACCEPT
sudo iptables -P OUTPUT ACCEPT

echo ""
echo "当前 iptables 状态:"
sudo iptables -L -n

echo ""
echo "✓ 防火墙已禁用"
echo "注意：诊断完成后请重新启用防火墙！"
echo "  sudo ufw enable"
