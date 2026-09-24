#!/bin/bash
# 临时构建+回归脚本
cd /mnt/d/code/project/Go2ProController/client || exit 1
echo "=== build ==="
cmake --build build 2>&1 | tail -4
echo "=== regression (25s) ==="
timeout 25 stdbuf -o0 -e0 ./build/go2_remote 192.168.2.111 > /root/reg3.log 2>&1
echo "=== 关键日志 ==="
grep -aE "就绪|钥匙|校验" /root/reg3.log | head -6
