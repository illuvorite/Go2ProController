#!/bin/bash
# 构建后重启 WSL 使用
echo "=== 清理僵尸进程 ==="
pkill -9 -f "ninja" 2>/dev/null
pkill -9 -f "go2_remote" 2>/dev/null
sleep 1
ps aux | grep -E "ninja|cmake|go2_remote" | grep -v grep || echo "(已清理干净)"
echo "=== 重新构建 ==="
cd /mnt/d/code/project/Go2ProController/client || exit 1
ninja -C build 2>&1 | tail -4
echo "ninja 退出码: $?"
echo "=== 回归测试（25s）==="
timeout 25 stdbuf -o0 -e0 ./build/go2_remote 192.168.2.111 > /root/reg4.log 2>&1
grep -aE "就绪|钥匙|校验" /root/reg4.log | head -6
echo "=== 完成 ==="
