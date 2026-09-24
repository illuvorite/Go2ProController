"""
CycloneDDS 配置验证模块
验证和生成 CycloneDDS XML 配置文件
"""

import os
import re
from typing import List, Dict, Optional, Any, Tuple
from dataclasses import dataclass, field
from pathlib import Path
import logging

try:
    from lxml import etree
    LXML_AVAILABLE = True
except ImportError:
    import xml.etree.ElementTree as etree
    LXML_AVAILABLE = False

from .config import DiagConfig


logger = logging.getLogger(__name__)


@dataclass
class ValidationIssue:
    """验证问题"""
    severity: str  # error, warning, info
    element: str
    message: str
    suggestion: Optional[str] = None


@dataclass
class CycloneDDSConfig:
    """解析后的 CycloneDDS 配置"""
    # General
    network_interface: Optional[str] = None
    allow_multicast: bool = True
    enable_shm: bool = True
    
    # Discovery
    peers: List[str] = field(default_factory=list)
    participant_index: str = "auto"
    
    # 其他配置
    domain_id: str = "any"
    max_message_size: int = 65500
    fragment_size: int = 1344
    
    # 原始 XML
    raw_xml: Optional[str] = None


class CycloneDDSValidator:
    """CycloneDDS 配置验证器"""
    
    def __init__(self, config: DiagConfig = None):
        self.config = config or DiagConfig()
        self._issues: List[ValidationIssue] = []
    
    def parse_config(self, xml_path: str) -> Optional[CycloneDDSConfig]:
        """解析 CycloneDDS XML 配置文件"""
        try:
            with open(xml_path, 'r', encoding='utf-8') as f:
                xml_content = f.read()
            return self.parse_config_string(xml_content)
        except Exception as e:
            logger.error(f"读取配置文件失败: {e}")
            return None
    
    def parse_config_string(self, xml_content: str) -> Optional[CycloneDDSConfig]:
        """解析 XML 字符串"""
        try:
            root = etree.fromstring(xml_content.encode('utf-8'))
            cfg = CycloneDDSConfig(raw_xml=xml_content)
            
            # 解析 Domain
            domain = root.find('.//Domain')
            if domain is not None:
                domain_id = domain.get('id', 'any')
                cfg.domain_id = domain_id
            
            # 解析 General
            general = root.find('.//General')
            if general is not None:
                # NetworkInterfaceAddress
                nic = general.find('NetworkInterfaceAddress')
                if nic is not None and nic.text:
                    cfg.network_interface = nic.text.strip()
                
                # AllowMulticast
                multicast = general.find('AllowMulticast')
                if multicast is not None and multicast.text:
                    cfg.allow_multicast = multicast.text.strip().lower() in ('true', '1', 'yes')
                
                # EnableShm (Shared Memory)
                shm = general.find('EnableShm') or general.find('SharedMemory/Enable')
                if shm is not None and shm.text:
                    cfg.enable_shm = shm.text.strip().lower() in ('true', '1', 'yes')
            
            # 解析 Discovery
            discovery = root.find('.//Discovery')
            if discovery is not None:
                # Peers
                peers = discovery.find('Peers')
                if peers is not None:
                    for peer in peers.findall('Peer'):
                        addr = peer.get('address') or (peer.text.strip() if peer.text else None)
                        if addr:
                            cfg.peers.append(addr)
                
                # ParticipantIndex
                part_idx = discovery.find('ParticipantIndex')
                if part_idx is not None and part_idx.text:
                    cfg.participant_index = part_idx.text.strip()
            
            # 解析 Internal
            internal = root.find('.//Internal')
            if internal is not None:
                max_msg = internal.find('MaxMessageSize')
                if max_msg is not None and max_msg.text:
                    try:
                        cfg.max_message_size = int(max_msg.text.strip())
                    except ValueError:
                        pass
                
                frag = internal.find('FragmentSize')
                if frag is not None and frag.text:
                    try:
                        cfg.fragment_size = int(frag.text.strip())
                    except ValueError:
                        pass
            
            return cfg
            
        except Exception as e:
            logger.error(f"解析 XML 失败: {e}")
            return None
    
    def validate(self, cfg: CycloneDDSConfig) -> List[ValidationIssue]:
        """验证配置"""
        self._issues = []
        
        # 验证网络接口
        self._validate_network_interface(cfg)
        
        # 验证多播设置
        self._validate_multicast(cfg)
        
        # 验证 Peer 配置
        self._validate_peers(cfg)
        
        # 验证跨网段配置
        self._validate_cross_subnet(cfg)
        
        # 验证 ParticipantIndex
        self._validate_participant_index(cfg)
        
        return self._issues
    
    def _add_issue(
        self, 
        severity: str, 
        element: str, 
        message: str, 
        suggestion: str = None
    ):
        """添加验证问题"""
        self._issues.append(ValidationIssue(
            severity=severity,
            element=element,
            message=message,
            suggestion=suggestion
        ))
    
    def _validate_network_interface(self, cfg: CycloneDDSConfig):
        """验证网络接口配置"""
        if not cfg.network_interface:
            self._add_issue(
                'warning',
                'NetworkInterfaceAddress',
                '未指定网络接口，DDS 可能使用错误的接口',
                f'建议设置为: <NetworkInterfaceAddress>{self.config.network.pc_interface}</NetworkInterfaceAddress>'
            )
        elif cfg.network_interface != self.config.network.pc_interface:
            self._add_issue(
                'info',
                'NetworkInterfaceAddress',
                f'网络接口配置为 {cfg.network_interface}，期望为 {self.config.network.pc_interface}'
            )
    
    def _validate_multicast(self, cfg: CycloneDDSConfig):
        """验证多播配置"""
        if cfg.allow_multicast:
            self._add_issue(
                'warning',
                'AllowMulticast',
                '多播已启用，但跨网段通讯通常需要禁用多播',
                '建议设置: <AllowMulticast>false</AllowMulticast>'
            )
    
    def _validate_peers(self, cfg: CycloneDDSConfig):
        """验证 Peer 配置"""
        if not cfg.peers:
            self._add_issue(
                'error',
                'Discovery/Peers',
                '未配置 Peer 地址，无法进行跨网段发现',
                f'建议添加: <Peer address="{self.config.network.robot_wifi_ip}"/>'
            )
        else:
            robot_ips = [
                self.config.network.robot_wifi_ip,
                self.config.network.robot_eth_ip
            ]
            
            has_robot_peer = any(p in robot_ips for p in cfg.peers)
            if not has_robot_peer:
                self._add_issue(
                    'warning',
                    'Discovery/Peers',
                    f'Peer 列表中未包含机器狗 IP ({robot_ips})',
                    f'建议添加: <Peer address="{self.config.network.robot_wifi_ip}"/>'
                )
            
            # 检查 Peer 格式
            for peer in cfg.peers:
                if not self._is_valid_ip_or_hostname(peer):
                    self._add_issue(
                        'error',
                        'Discovery/Peers',
                        f'无效的 Peer 地址: {peer}'
                    )
    
    def _validate_cross_subnet(self, cfg: CycloneDDSConfig):
        """验证跨网段配置"""
        robot_eth_ip = self.config.network.robot_eth_ip
        
        # 如果 Peer 包含 192.168.123.x，需要特殊配置
        for peer in cfg.peers:
            if peer.startswith('192.168.123.'):
                self._add_issue(
                    'info',
                    'Discovery/Peers',
                    f'Peer 包含内部网段地址 {peer}，需要确保路由配置正确',
                    f'执行: sudo ip route add 192.168.123.0/24 via {self.config.network.robot_wifi_ip}'
                )
    
    def _validate_participant_index(self, cfg: CycloneDDSConfig):
        """验证 ParticipantIndex"""
        if cfg.participant_index != 'auto':
            try:
                idx = int(cfg.participant_index)
                if idx < 0 or idx > 119:
                    self._add_issue(
                        'warning',
                        'ParticipantIndex',
                        f'ParticipantIndex {idx} 超出推荐范围 (0-119)'
                    )
            except ValueError:
                if cfg.participant_index.lower() != 'none':
                    self._add_issue(
                        'error',
                        'ParticipantIndex',
                        f'无效的 ParticipantIndex: {cfg.participant_index}'
                    )
    
    def _is_valid_ip_or_hostname(self, addr: str) -> bool:
        """检查是否为有效的 IP 或主机名"""
        # IPv4 地址
        ipv4_pattern = r'^(\d{1,3}\.){3}\d{1,3}$'
        if re.match(ipv4_pattern, addr):
            parts = addr.split('.')
            return all(0 <= int(p) <= 255 for p in parts)
        
        # 主机名
        hostname_pattern = r'^[a-zA-Z0-9]([a-zA-Z0-9-]*[a-zA-Z0-9])?(\.[a-zA-Z0-9]([a-zA-Z0-9-]*[a-zA-Z0-9])?)*$'
        return bool(re.match(hostname_pattern, addr))
    
    def validate_file(self, xml_path: str) -> Tuple[Optional[CycloneDDSConfig], List[ValidationIssue]]:
        """验证配置文件"""
        cfg = self.parse_config(xml_path)
        if cfg is None:
            return None, [ValidationIssue(
                severity='error',
                element='file',
                message=f'无法解析配置文件: {xml_path}'
            )]
        
        issues = self.validate(cfg)
        return cfg, issues
    
    def generate_config(
        self,
        interface: str = None,
        peers: List[str] = None,
        allow_multicast: bool = False,
        participant_index: str = "auto",
        domain_id: str = "any"
    ) -> str:
        """生成 CycloneDDS 配置文件"""
        interface = interface or self.config.network.pc_interface
        peers = peers or [self.config.network.robot_wifi_ip]
        
        peers_xml = '\n                '.join(
            f'<Peer address="{p}"/>' for p in peers
        )
        
        xml = f'''<?xml version="1.0" encoding="UTF-8"?>
<CycloneDDS xmlns="https://cdds.io/config" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">
    <Domain id="{domain_id}">
        <General>
            <!-- 指定网络接口 -->
            <NetworkInterfaceAddress>{interface}</NetworkInterfaceAddress>
            
            <!-- 跨网段通讯需要禁用多播 -->
            <AllowMulticast>{"true" if allow_multicast else "false"}</AllowMulticast>
        </General>
        
        <Discovery>
            <!-- 单播发现对端 -->
            <Peers>
                {peers_xml}
            </Peers>
            <ParticipantIndex>{participant_index}</ParticipantIndex>
        </Discovery>
        
        <Tracing>
            <!-- 启用调试日志（可选） -->
            <!-- <Verbosity>finest</Verbosity> -->
            <!-- <OutputFile>cyclonedds.log</OutputFile> -->
        </Tracing>
    </Domain>
</CycloneDDS>
'''
        return xml
    
    def generate_go2_wifi_config(self) -> str:
        """生成适用于 Go2 无线连接的配置"""
        return self.generate_config(
            interface=self.config.network.pc_interface,
            peers=[
                self.config.network.robot_wifi_ip,
                self.config.network.robot_eth_ip
            ],
            allow_multicast=False,
            participant_index="auto"
        )
    
    def generate_go2_wired_config(self) -> str:
        """生成适用于 Go2 有线连接的配置"""
        return self.generate_config(
            interface="eth0",  # 假设有线接口为 eth0
            peers=[self.config.network.robot_eth_ip],
            allow_multicast=True,
            participant_index="auto"
        )
    
    def save_config(self, xml_content: str, path: str):
        """保存配置文件"""
        Path(path).parent.mkdir(parents=True, exist_ok=True)
        with open(path, 'w', encoding='utf-8') as f:
            f.write(xml_content)
        logger.info(f"配置已保存到 {path}")
    
    def print_validation_result(
        self, 
        cfg: CycloneDDSConfig, 
        issues: List[ValidationIssue]
    ):
        """打印验证结果"""
        print("\n" + "=" * 60)
        print("CycloneDDS 配置验证结果")
        print("=" * 60)
        
        print("\n--- 配置概要 ---")
        print(f"Domain ID: {cfg.domain_id}")
        print(f"网络接口: {cfg.network_interface or '未指定'}")
        print(f"允许多播: {cfg.allow_multicast}")
        print(f"ParticipantIndex: {cfg.participant_index}")
        print(f"Peers: {', '.join(cfg.peers) if cfg.peers else '无'}")
        
        if issues:
            errors = [i for i in issues if i.severity == 'error']
            warnings = [i for i in issues if i.severity == 'warning']
            infos = [i for i in issues if i.severity == 'info']
            
            if errors:
                print(f"\n--- 错误 ({len(errors)}) ---")
                for issue in errors:
                    print(f"  [ERROR] {issue.element}: {issue.message}")
                    if issue.suggestion:
                        print(f"          建议: {issue.suggestion}")
            
            if warnings:
                print(f"\n--- 警告 ({len(warnings)}) ---")
                for issue in warnings:
                    print(f"  [WARN] {issue.element}: {issue.message}")
                    if issue.suggestion:
                        print(f"         建议: {issue.suggestion}")
            
            if infos:
                print(f"\n--- 提示 ({len(infos)}) ---")
                for issue in infos:
                    print(f"  [INFO] {issue.element}: {issue.message}")
                    if issue.suggestion:
                        print(f"         建议: {issue.suggestion}")
        else:
            print("\n配置验证通过，未发现问题。")
        
        print("=" * 60)
    
    def suggest_fixes(self, issues: List[ValidationIssue]) -> str:
        """根据问题生成修复建议的配置"""
        # 收集需要的修改
        needs_interface = any(i.element == 'NetworkInterfaceAddress' for i in issues)
        needs_multicast_off = any(i.element == 'AllowMulticast' for i in issues)
        needs_peers = any('Peers' in i.element for i in issues)
        
        if needs_interface or needs_multicast_off or needs_peers:
            return self.generate_go2_wifi_config()
        
        return ""


def validate_cyclonedds_config(xml_path: str, config: DiagConfig = None) -> bool:
    """便捷函数：验证 CycloneDDS 配置"""
    validator = CycloneDDSValidator(config)
    cfg, issues = validator.validate_file(xml_path)
    
    if cfg:
        validator.print_validation_result(cfg, issues)
        return len([i for i in issues if i.severity == 'error']) == 0
    
    return False
