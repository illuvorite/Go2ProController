#!/usr/bin/env python3
"""
Unitree Go2 网络诊断工具
主程序入口

使用方法:
    python main.py diagnose          # 运行完整诊断
    python main.py ping -h <ip>      # Ping 测试
    python main.py capture           # 抓包分析
    python main.py monitor           # 实时监控
    python main.py validate <file>   # 验证配置
    python main.py genconfig         # 生成配置
    python main.py quicktest         # 快速测试
    python main.py --help            # 帮助信息
"""

import sys
import os

# 添加项目路径
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from go2_network_diag.cli import main

if __name__ == '__main__':
    main()
