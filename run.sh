#!/bin/bash
# Unitree Go2 网络诊断工具快速启动脚本

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# 检查 Python 环境
if ! command -v python3 &> /dev/null; then
    echo "错误: 未找到 Python3"
    exit 1
fi

# 检查是否安装了依赖
python3 -c "import click, rich" 2>/dev/null
if [ $? -ne 0 ]; then
    echo "安装依赖..."
    pip3 install -r requirements.txt
fi

# 检查是否需要 root 权限
NEED_SUDO=""
if [[ "$1" == "capture" || "$1" == "monitor" || "$1" == "diagnose" || "$1" == "quicktest" ]]; then
    if [ "$EUID" -ne 0 ]; then
        NEED_SUDO="sudo"
        echo "提示: 此命令需要 root 权限进行抓包"
    fi
fi

# 运行主程序
$NEED_SUDO python3 main.py "$@"
