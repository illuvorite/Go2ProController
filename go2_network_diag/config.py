"""
诊断工具配置模块
定义网络诊断的默认配置和参数
"""

from dataclasses import dataclass, field
from typing import List, Optional, Dict, Any
from pathlib import Path
import yaml
import json


@dataclass
class NetworkConfig:
    """网络配置"""
    # PC 端配置
    pc_interface: str = "wlo1"  # PC 无线网卡
    pc_ip: str = "192.168.1.106"
    
    # 机器狗无线接口
    robot_wifi_ip: str = "192.168.1.211"
    robot_wifi_interface: str = "wlan0"
    
    # 机器狗内部有线接口
    robot_eth_ip: str = "192.168.123.161"
    robot_eth_interface: str = "eth0"
    
    # 子网配置
    wifi_subnet: str = "192.168.1.0/24"
    eth_subnet: str = "192.168.123.0/24"


@dataclass
class DDSConfig:
    """DDS 通讯配置"""
    # CycloneDDS 常用端口
    discovery_port: int = 7400
    user_data_port_base: int = 7410
    
    # DDS 端口范围
    port_range: tuple = (7400, 7500)
    
    # RTPS 多播地址
    multicast_address: str = "239.255.0.1"
    
    # 常见 DDS Topic 端口
    common_ports: List[int] = field(default_factory=lambda: [
        7400, 7401, 7410, 7411, 7412, 7413, 7414, 7415, 7416, 7417,
        7420, 7421, 7430, 7431
    ])
    
    # RTPS 协议标识
    rtps_magic: bytes = b'RTPS'


@dataclass 
class CaptureConfig:
    """抓包配置"""
    # 抓包时长（秒）
    capture_duration: int = 30
    
    # 包数量限制
    packet_limit: int = 10000
    
    # 缓冲区大小
    buffer_size: int = 65536
    
    # BPF 过滤器
    bpf_filter: str = "udp"
    
    # 保存 pcap 文件
    save_pcap: bool = True
    pcap_dir: str = "./captures"


@dataclass
class DiagConfig:
    """诊断工具主配置"""
    network: NetworkConfig = field(default_factory=NetworkConfig)
    dds: DDSConfig = field(default_factory=DDSConfig)
    capture: CaptureConfig = field(default_factory=CaptureConfig)
    
    # 诊断报告配置
    report_dir: str = "./reports"
    report_format: str = "html"  # html, json, txt
    
    # 日志配置
    log_level: str = "INFO"
    log_file: Optional[str] = None
    
    # SSH 配置（用于远程诊断机器狗）
    robot_ssh_user: str = "root"
    robot_ssh_port: int = 22
    robot_ssh_key: Optional[str] = None
    
    # 超时配置
    ping_timeout: float = 2.0
    connect_timeout: float = 5.0
    
    @classmethod
    def from_yaml(cls, path: str) -> "DiagConfig":
        """从 YAML 文件加载配置"""
        with open(path, 'r', encoding='utf-8') as f:
            data = yaml.safe_load(f)
        return cls._from_dict(data)
    
    @classmethod
    def from_json(cls, path: str) -> "DiagConfig":
        """从 JSON 文件加载配置"""
        with open(path, 'r', encoding='utf-8') as f:
            data = json.load(f)
        return cls._from_dict(data)
    
    @classmethod
    def _from_dict(cls, data: Dict[str, Any]) -> "DiagConfig":
        """从字典创建配置"""
        config = cls()
        
        if 'network' in data:
            for key, value in data['network'].items():
                if hasattr(config.network, key):
                    setattr(config.network, key, value)
        
        if 'dds' in data:
            for key, value in data['dds'].items():
                if hasattr(config.dds, key):
                    setattr(config.dds, key, value)
        
        if 'capture' in data:
            for key, value in data['capture'].items():
                if hasattr(config.capture, key):
                    setattr(config.capture, key, value)
        
        for key in ['report_dir', 'report_format', 'log_level', 'log_file',
                    'robot_ssh_user', 'robot_ssh_port', 'robot_ssh_key',
                    'ping_timeout', 'connect_timeout']:
            if key in data:
                setattr(config, key, data[key])
        
        return config
    
    def to_yaml(self, path: str):
        """保存配置到 YAML 文件"""
        data = self._to_dict()
        with open(path, 'w', encoding='utf-8') as f:
            yaml.dump(data, f, allow_unicode=True, default_flow_style=False)
    
    def to_json(self, path: str):
        """保存配置到 JSON 文件"""
        data = self._to_dict()
        with open(path, 'w', encoding='utf-8') as f:
            json.dump(data, f, ensure_ascii=False, indent=2)
    
    def _to_dict(self) -> Dict[str, Any]:
        """转换为字典"""
        return {
            'network': {
                'pc_interface': self.network.pc_interface,
                'pc_ip': self.network.pc_ip,
                'robot_wifi_ip': self.network.robot_wifi_ip,
                'robot_wifi_interface': self.network.robot_wifi_interface,
                'robot_eth_ip': self.network.robot_eth_ip,
                'robot_eth_interface': self.network.robot_eth_interface,
                'wifi_subnet': self.network.wifi_subnet,
                'eth_subnet': self.network.eth_subnet,
            },
            'dds': {
                'discovery_port': self.dds.discovery_port,
                'user_data_port_base': self.dds.user_data_port_base,
                'port_range': list(self.dds.port_range),
                'multicast_address': self.dds.multicast_address,
                'common_ports': self.dds.common_ports,
            },
            'capture': {
                'capture_duration': self.capture.capture_duration,
                'packet_limit': self.capture.packet_limit,
                'buffer_size': self.capture.buffer_size,
                'bpf_filter': self.capture.bpf_filter,
                'save_pcap': self.capture.save_pcap,
                'pcap_dir': self.capture.pcap_dir,
            },
            'report_dir': self.report_dir,
            'report_format': self.report_format,
            'log_level': self.log_level,
            'log_file': self.log_file,
            'robot_ssh_user': self.robot_ssh_user,
            'robot_ssh_port': self.robot_ssh_port,
            'robot_ssh_key': self.robot_ssh_key,
            'ping_timeout': self.ping_timeout,
            'connect_timeout': self.connect_timeout,
        }


# 默认 Go2 配置实例
DEFAULT_GO2_CONFIG = DiagConfig()
