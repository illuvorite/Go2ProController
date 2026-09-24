"""
DDS/RTPS 协议分析模块
用于分析 CycloneDDS 通讯数据
"""

import struct
import time
from typing import List, Dict, Optional, Any, Tuple
from dataclasses import dataclass, field
from enum import IntEnum
import logging

from .config import DiagConfig
from .packet_capture import CapturedPacket, PacketCapture


logger = logging.getLogger(__name__)


class RTPSSubmessageKind(IntEnum):
    """RTPS 子消息类型"""
    PAD = 0x01
    ACKNACK = 0x06
    HEARTBEAT = 0x07
    GAP = 0x08
    INFO_TS = 0x09
    INFO_SRC = 0x0C
    INFO_REPLY_IP4 = 0x0D
    INFO_DST = 0x0E
    INFO_REPLY = 0x0F
    NACK_FRAG = 0x12
    HEARTBEAT_FRAG = 0x13
    DATA = 0x15
    DATA_FRAG = 0x16


@dataclass
class RTPSHeader:
    """RTPS 协议头"""
    magic: str  # 'RTPS'
    protocol_version: Tuple[int, int]  # (major, minor)
    vendor_id: Tuple[int, int]
    guid_prefix: bytes  # 12 bytes


@dataclass
class RTPSSubmessage:
    """RTPS 子消息"""
    kind: int
    flags: int
    length: int
    data: bytes
    
    @property
    def kind_name(self) -> str:
        try:
            return RTPSSubmessageKind(self.kind).name
        except ValueError:
            return f"UNKNOWN(0x{self.kind:02X})"
    
    @property
    def is_data(self) -> bool:
        return self.kind in (RTPSSubmessageKind.DATA, RTPSSubmessageKind.DATA_FRAG)
    
    @property
    def is_discovery(self) -> bool:
        """判断是否为发现消息"""
        return self.kind in (
            RTPSSubmessageKind.DATA,
            RTPSSubmessageKind.HEARTBEAT,
            RTPSSubmessageKind.ACKNACK
        )


@dataclass
class DDSParticipant:
    """DDS 参与者信息"""
    guid_prefix: str
    ip_address: str
    port: int
    first_seen: float
    last_seen: float
    vendor: str = "unknown"
    discovery_count: int = 0
    data_count: int = 0
    
    def update(self, timestamp: float, is_data: bool = False):
        self.last_seen = timestamp
        if is_data:
            self.data_count += 1
        else:
            self.discovery_count += 1


@dataclass
class DDSTopic:
    """DDS Topic 信息"""
    name: str
    type_name: str
    participants: List[str] = field(default_factory=list)
    message_count: int = 0


@dataclass
class DDSAnalysisResult:
    """DDS 分析结果"""
    timestamp: float
    duration: float
    total_rtps_packets: int
    total_submessages: int
    
    participants: List[DDSParticipant]
    topics: List[DDSTopic]
    
    # 问题诊断
    discovery_packets_pc_to_robot: int = 0
    discovery_packets_robot_to_pc: int = 0
    data_packets_pc_to_robot: int = 0
    data_packets_robot_to_pc: int = 0
    
    issues: List[str] = field(default_factory=list)
    recommendations: List[str] = field(default_factory=list)


class DDSAnalyzer:
    """DDS 协议分析器"""
    
    # 已知 DDS 厂商 ID
    VENDOR_IDS = {
        (0x01, 0x01): "RTI Connext",
        (0x01, 0x02): "PrismTech OpenSplice",
        (0x01, 0x03): "OCI OpenDDS",
        (0x01, 0x0F): "Eclipse Cyclone DDS",
        (0x01, 0x10): "eProsima Fast DDS",
    }
    
    def __init__(self, config: DiagConfig = None):
        self.config = config or DiagConfig()
        self.participants: Dict[str, DDSParticipant] = {}
        self.topics: Dict[str, DDSTopic] = {}
        self._submessage_stats: Dict[str, int] = {}
    
    def parse_rtps_header(self, data: bytes) -> Optional[RTPSHeader]:
        """解析 RTPS 头部（20字节）"""
        if len(data) < 20:
            return None
        
        magic = data[:4]
        if magic != b'RTPS':
            return None
        
        try:
            return RTPSHeader(
                magic='RTPS',
                protocol_version=(data[4], data[5]),
                vendor_id=(data[6], data[7]),
                guid_prefix=data[8:20]
            )
        except Exception as e:
            logger.error(f"解析 RTPS 头失败: {e}")
            return None
    
    def parse_submessages(self, data: bytes) -> List[RTPSSubmessage]:
        """解析 RTPS 子消息"""
        submessages = []
        
        # 跳过 RTPS 头部（20字节）
        offset = 20
        
        while offset + 4 <= len(data):
            try:
                kind = data[offset]
                flags = data[offset + 1]
                
                # 小端/大端
                is_little_endian = flags & 0x01
                
                if is_little_endian:
                    length = struct.unpack('<H', data[offset + 2:offset + 4])[0]
                else:
                    length = struct.unpack('>H', data[offset + 2:offset + 4])[0]
                
                # 子消息数据
                submsg_data = data[offset + 4:offset + 4 + length]
                
                submessages.append(RTPSSubmessage(
                    kind=kind,
                    flags=flags,
                    length=length,
                    data=submsg_data
                ))
                
                # 移动到下一个子消息
                offset += 4 + length
                
                # 4 字节对齐
                if offset % 4 != 0:
                    offset += 4 - (offset % 4)
                    
            except Exception as e:
                logger.debug(f"解析子消息失败: {e}")
                break
        
        return submessages
    
    def get_vendor_name(self, vendor_id: Tuple[int, int]) -> str:
        """获取厂商名称"""
        return self.VENDOR_IDS.get(vendor_id, f"Unknown({vendor_id[0]:02X}:{vendor_id[1]:02X})")
    
    def analyze_packet(self, packet: CapturedPacket) -> Dict[str, Any]:
        """分析单个 RTPS 数据包"""
        result = {
            'is_rtps': False,
            'header': None,
            'submessages': [],
            'participant_guid': None,
            'vendor': None
        }
        
        if not packet.payload or len(packet.payload) < 20:
            return result
        
        header = self.parse_rtps_header(packet.payload)
        if not header:
            return result
        
        result['is_rtps'] = True
        result['header'] = header
        result['vendor'] = self.get_vendor_name(header.vendor_id)
        result['participant_guid'] = header.guid_prefix.hex()
        
        # 解析子消息
        submessages = self.parse_submessages(packet.payload)
        result['submessages'] = submessages
        
        # 更新参与者信息
        guid_hex = header.guid_prefix.hex()
        if guid_hex not in self.participants:
            self.participants[guid_hex] = DDSParticipant(
                guid_prefix=guid_hex,
                ip_address=packet.src_ip,
                port=packet.src_port,
                first_seen=packet.timestamp,
                last_seen=packet.timestamp,
                vendor=result['vendor']
            )
        
        has_data = any(sm.is_data for sm in submessages)
        self.participants[guid_hex].update(packet.timestamp, has_data)
        
        # 统计子消息类型
        for sm in submessages:
            kind_name = sm.kind_name
            self._submessage_stats[kind_name] = self._submessage_stats.get(kind_name, 0) + 1
        
        return result
    
    def analyze_packets(self, packets: List[CapturedPacket]) -> DDSAnalysisResult:
        """分析一批数据包"""
        start_time = time.time()
        
        self.participants = {}
        self.topics = {}
        self._submessage_stats = {}
        
        rtps_count = 0
        submsg_count = 0
        
        # 流量统计
        pc_ip = self.config.network.pc_ip
        robot_wifi_ip = self.config.network.robot_wifi_ip
        robot_eth_ip = self.config.network.robot_eth_ip
        robot_ips = [robot_wifi_ip, robot_eth_ip]
        
        discovery_pc_to_robot = 0
        discovery_robot_to_pc = 0
        data_pc_to_robot = 0
        data_robot_to_pc = 0
        
        for packet in packets:
            if packet.protocol != 'udp':
                continue
            
            result = self.analyze_packet(packet)
            
            if result['is_rtps']:
                rtps_count += 1
                submsg_count += len(result['submessages'])
                
                # 统计方向
                for sm in result['submessages']:
                    if sm.is_discovery:
                        if packet.src_ip == pc_ip and packet.dst_ip in robot_ips:
                            discovery_pc_to_robot += 1
                        elif packet.src_ip in robot_ips and packet.dst_ip == pc_ip:
                            discovery_robot_to_pc += 1
                    
                    if sm.is_data:
                        if packet.src_ip == pc_ip and packet.dst_ip in robot_ips:
                            data_pc_to_robot += 1
                        elif packet.src_ip in robot_ips and packet.dst_ip == pc_ip:
                            data_robot_to_pc += 1
        
        # 问题诊断
        issues = []
        recommendations = []
        
        # 检查是否有来自机器狗的响应
        robot_participants = [
            p for p in self.participants.values()
            if p.ip_address in robot_ips
        ]
        
        if not robot_participants:
            issues.append("未检测到任何来自机器狗的 RTPS 数据包")
            recommendations.append("检查机器狗 DDS 服务是否运行")
            recommendations.append("检查机器狗防火墙是否阻止了 UDP 流量")
        
        if discovery_pc_to_robot > 0 and discovery_robot_to_pc == 0:
            issues.append("PC 发送了 Discovery 消息，但未收到机器狗的响应")
            recommendations.append("检查机器狗 DDS 的网络接口绑定配置")
            recommendations.append("机器狗可能只在 eth0 接口响应，而不是 wlan0")
        
        if data_pc_to_robot > 0 and data_robot_to_pc == 0:
            issues.append("PC 发送了 DATA 消息，但未收到机器狗的数据回传")
            recommendations.append("检查 DDS QoS 配置")
            recommendations.append("验证 Topic 名称是否匹配")
        
        # 检查 CycloneDDS 版本
        cyclone_participants = [
            p for p in self.participants.values()
            if 'Cyclone' in p.vendor
        ]
        if not cyclone_participants:
            issues.append("未检测到 CycloneDDS 参与者")
        
        duration = time.time() - start_time
        
        return DDSAnalysisResult(
            timestamp=time.time(),
            duration=duration,
            total_rtps_packets=rtps_count,
            total_submessages=submsg_count,
            participants=list(self.participants.values()),
            topics=list(self.topics.values()),
            discovery_packets_pc_to_robot=discovery_pc_to_robot,
            discovery_packets_robot_to_pc=discovery_robot_to_pc,
            data_packets_pc_to_robot=data_pc_to_robot,
            data_packets_robot_to_pc=data_robot_to_pc,
            issues=issues,
            recommendations=recommendations
        )
    
    def analyze_capture(self, capture: PacketCapture) -> DDSAnalysisResult:
        """分析 PacketCapture 的数据"""
        return self.analyze_packets(capture.packets)
    
    def get_submessage_stats(self) -> Dict[str, int]:
        """获取子消息类型统计"""
        return self._submessage_stats.copy()
    
    def print_analysis_result(self, result: DDSAnalysisResult):
        """打印分析结果"""
        print("\n" + "=" * 60)
        print("DDS/RTPS 分析结果")
        print("=" * 60)
        
        print(f"\n--- 基本统计 ---")
        print(f"RTPS 数据包数: {result.total_rtps_packets}")
        print(f"子消息数: {result.total_submessages}")
        print(f"分析耗时: {result.duration:.3f} 秒")
        
        print(f"\n--- 流量方向统计 ---")
        print(f"PC -> Robot Discovery: {result.discovery_packets_pc_to_robot}")
        print(f"Robot -> PC Discovery: {result.discovery_packets_robot_to_pc}")
        print(f"PC -> Robot Data: {result.data_packets_pc_to_robot}")
        print(f"Robot -> PC Data: {result.data_packets_robot_to_pc}")
        
        print(f"\n--- DDS 参与者 ({len(result.participants)}) ---")
        for p in result.participants:
            print(f"  GUID: {p.guid_prefix[:16]}...")
            print(f"    IP: {p.ip_address}:{p.port}")
            print(f"    Vendor: {p.vendor}")
            print(f"    Discovery: {p.discovery_count}, Data: {p.data_count}")
        
        print(f"\n--- 子消息类型统计 ---")
        for kind, count in sorted(self._submessage_stats.items(), key=lambda x: -x[1]):
            print(f"  {kind}: {count}")
        
        if result.issues:
            print(f"\n--- 发现的问题 ({len(result.issues)}) ---")
            for i, issue in enumerate(result.issues, 1):
                print(f"  {i}. {issue}")
        
        if result.recommendations:
            print(f"\n--- 建议 ({len(result.recommendations)}) ---")
            for i, rec in enumerate(result.recommendations, 1):
                print(f"  {i}. {rec}")
        
        print("=" * 60)
    
    def check_discovery_bidirectional(
        self, 
        packets: List[CapturedPacket]
    ) -> Dict[str, Any]:
        """检查 Discovery 是否双向正常"""
        pc_ip = self.config.network.pc_ip
        robot_ips = [
            self.config.network.robot_wifi_ip,
            self.config.network.robot_eth_ip
        ]
        
        pc_to_robot = []
        robot_to_pc = []
        
        for pkt in packets:
            if not pkt.is_rtps:
                continue
            
            if pkt.src_ip == pc_ip and pkt.dst_ip in robot_ips:
                pc_to_robot.append(pkt)
            elif pkt.src_ip in robot_ips and pkt.dst_ip == pc_ip:
                robot_to_pc.append(pkt)
        
        return {
            'pc_to_robot_count': len(pc_to_robot),
            'robot_to_pc_count': len(robot_to_pc),
            'bidirectional': len(pc_to_robot) > 0 and len(robot_to_pc) > 0,
            'pc_to_robot_packets': pc_to_robot[:10],  # 前10个
            'robot_to_pc_packets': robot_to_pc[:10]
        }
    
    def extract_locators(self, packets: List[CapturedPacket]) -> List[Dict]:
        """从 Discovery 包中提取 Locator 信息"""
        locators = []
        
        for pkt in packets:
            if not pkt.is_rtps or len(pkt.payload) < 100:
                continue
            
            # 简单搜索 IPv4 地址模式
            payload = pkt.payload
            for i in range(len(payload) - 4):
                # 检查是否像 IP 地址（192.168.x.x 或 10.x.x.x）
                b = payload[i:i+4]
                if (b[0] == 192 and b[1] == 168) or (b[0] == 10):
                    ip = f"{b[0]}.{b[1]}.{b[2]}.{b[3]}"
                    locators.append({
                        'ip': ip,
                        'found_in_packet': {
                            'src': pkt.src_ip,
                            'dst': pkt.dst_ip,
                            'port': pkt.dst_port
                        },
                        'offset': i
                    })
        
        # 去重
        seen = set()
        unique_locators = []
        for loc in locators:
            if loc['ip'] not in seen:
                seen.add(loc['ip'])
                unique_locators.append(loc)
        
        return unique_locators


def quick_dds_analysis(capture: PacketCapture, config: DiagConfig = None) -> DDSAnalysisResult:
    """快速 DDS 分析"""
    analyzer = DDSAnalyzer(config)
    return analyzer.analyze_capture(capture)
