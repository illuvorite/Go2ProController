#!/usr/bin/env python3
"""
Unitree Go2 网络诊断工具安装配置
"""

from setuptools import setup, find_packages
from pathlib import Path

# 读取 README
readme_path = Path(__file__).parent / "README.md"
long_description = readme_path.read_text(encoding='utf-8') if readme_path.exists() else ""

setup(
    name="go2-network-diag",
    version="1.0.0",
    author="Go2 Network Diagnostics Team",
    author_email="",
    description="Unitree Go2 机器狗网络诊断工具",
    long_description=long_description,
    long_description_content_type="text/markdown",
    url="",
    packages=find_packages(),
    classifiers=[
        "Development Status :: 4 - Beta",
        "Intended Audience :: Developers",
        "License :: OSI Approved :: MIT License",
        "Operating System :: POSIX :: Linux",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3.8",
        "Programming Language :: Python :: 3.9",
        "Programming Language :: Python :: 3.10",
        "Programming Language :: Python :: 3.11",
        "Topic :: System :: Networking :: Monitoring",
    ],
    python_requires=">=3.8",
    install_requires=[
        "click>=8.1.0",
        "rich>=13.0.0",
        "psutil>=5.9.0",
        "netifaces>=0.11.0",
        "lxml>=4.9.0",
        "PyYAML>=6.0",
        "jinja2>=3.1.0",
    ],
    extras_require={
        "capture": [
            "scapy>=2.5.0",
        ],
        "full": [
            "scapy>=2.5.0",
            "pyshark>=0.6",
            "pandas>=2.0.0",
        ],
        "dev": [
            "pytest>=7.0.0",
            "pytest-asyncio>=0.21.0",
        ],
    },
    entry_points={
        "console_scripts": [
            "go2-diag=go2_network_diag.cli:main",
        ],
    },
)
