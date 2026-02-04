"""
网络包捕获模块
使用 scapy 进行网络包捕获和分析
"""

import os
import time
import threading
import queue
from typing import List, Dict, Optional, Callable, Any
from dataclasses import dataclass, field
from pathlib import Path
import logging
import subprocess

try:
    from scapy.all import (
        sniff, wrpcap, rdpcap, 
        IP, UDP, TCP, ICMP, Raw, Ether,
        AsyncSniffer, conf
    )
    SCAPY_AVAILABLE = True
except ImportError:
    SCAPY_AVAILABLE = False

from .config import DiagConfig


logger = logging.getLogger(__name__)


@dataclass
class CapturedPacket:
    """捕获的数据包"""
    timestamp: float
    src_ip: str
    dst_ip: str
    src_port: int
    dst_port: int
    protocol: str  # tcp/udp/icmp
    length: int
    payload: bytes
    raw_packet: Any = None
    
    # RTPS/DDS 相关
    is_rtps: bool = False
    rtps_info: Dict = field(default_factory=dict)


@dataclass 
class CaptureStats:
    """捕获统计"""
    total_packets: int = 0
    udp_packets: int = 0
    tcp_packets: int = 0
    icmp_packets: int = 0
    rtps_packets: int = 0
    
    bytes_total: int = 0
    bytes_from_robot: int = 0
    bytes_to_robot: int = 0
    
    start_time: float = 0
    end_time: float = 0
    duration: float = 0
    
    unique_src_ips: set = field(default_factory=set)
    unique_dst_ips: set = field(default_factory=set)
    unique_ports: set = field(default_factory=set)


class PacketCapture:
    """网络包捕获器"""
    
    def __init__(self, config: DiagConfig = None):
        self.config = config or DiagConfig()
        self.packets: List[CapturedPacket] = []
        self.stats = CaptureStats()
        self._sniffer = None
        self._running = False
        self._packet_queue = queue.Queue()
        self._callbacks: List[Callable] = []
        
        if not SCAPY_AVAILABLE:
            logger.warning("Scapy 未安装，部分功能不可用")
    
    def add_callback(self, callback: Callable[[CapturedPacket], None]):
        """添加包处理回调"""
        self._callbacks.append(callback)
    
    def _process_packet(self, packet):
        """处理捕获的数据包"""
        try:
            if not packet.haslayer(IP):
                return
            
            ip_layer = packet[IP]
            
            captured = CapturedPacket(
                timestamp=time.time(),
                src_ip=ip_layer.src,
                dst_ip=ip_layer.dst,
                src_port=0,
                dst_port=0,
                protocol='other',
                length=len(packet),
                payload=b'',
                raw_packet=packet
            )
            
            # UDP 包
            if packet.haslayer(UDP):
                udp = packet[UDP]
                captured.src_port = udp.sport
                captured.dst_port = udp.dport
                captured.protocol = 'udp'
                self.stats.udp_packets += 1
                
                # 检查是否为 RTPS 包
                if packet.haslayer(Raw):
                    payload = bytes(packet[Raw].load)
                    captured.payload = payload
                    if payload[:4] == b'RTPS':
                        captured.is_rtps = True
                        captured.rtps_info = self._parse_rtps_header(payload)
                        self.stats.rtps_packets += 1
            
            # TCP 包
            elif packet.haslayer(TCP):
                tcp = packet[TCP]
                captured.src_port = tcp.sport
                captured.dst_port = tcp.dport
                captured.protocol = 'tcp'
                self.stats.tcp_packets += 1
                
                if packet.haslayer(Raw):
                    captured.payload = bytes(packet[Raw].load)
            
            # ICMP 包
            elif packet.haslayer(ICMP):
                captured.protocol = 'icmp'
                self.stats.icmp_packets += 1
            
            # 更新统计
            self.stats.total_packets += 1
            self.stats.bytes_total += captured.length
            self.stats.unique_src_ips.add(captured.src_ip)
            self.stats.unique_dst_ips.add(captured.dst_ip)
            
            if captured.src_port:
                self.stats.unique_ports.add(captured.src_port)
            if captured.dst_port:
                self.stats.unique_ports.add(captured.dst_port)
            
            # 统计与机器狗的流量
            robot_ips = [
                self.config.network.robot_wifi_ip,
                self.config.network.robot_eth_ip
            ]
            if captured.src_ip in robot_ips:
                self.stats.bytes_from_robot += captured.length
            if captured.dst_ip in robot_ips:
                self.stats.bytes_to_robot += captured.length
            
            # 存储和回调
            self.packets.append(captured)
            self._packet_queue.put(captured)
            
            for callback in self._callbacks:
                try:
                    callback(captured)
                except Exception as e:
                    logger.error(f"回调处理错误: {e}")
                    
        except Exception as e:
            logger.error(f"处理数据包错误: {e}")
    
    def _parse_rtps_header(self, data: bytes) -> Dict:
        """解析 RTPS 协议头"""
        if len(data) < 20:
            return {}
        
        try:
            info = {
                'magic': data[:4].decode('ascii', errors='ignore'),
                'protocol_version': f"{data[4]}.{data[5]}",
                'vendor_id': data[6:8].hex(),
            }
            
            # GUID 前缀
            if len(data) >= 20:
                info['guid_prefix'] = data[8:20].hex()
            
            return info
        except Exception as e:
            return {'error': str(e)}
    
    def start_capture(
        self, 
        interface: str = None, 
        filter_str: str = None,
        duration: int = None,
        packet_count: int = None
    ):
        """
        开始捕获数据包
        
        Args:
            interface: 网络接口，默认使用配置中的 PC 接口
            filter_str: BPF 过滤器
            duration: 捕获时长（秒）
            packet_count: 最大包数量
        """
        if not SCAPY_AVAILABLE:
            logger.error("Scapy 未安装，无法捕获")
            return False
        
        interface = interface or self.config.network.pc_interface
        filter_str = filter_str or self.config.capture.bpf_filter
        duration = duration or self.config.capture.capture_duration
        packet_count = packet_count or self.config.capture.packet_limit
        
        self.packets = []
        self.stats = CaptureStats()
        self.stats.start_time = time.time()
        self._running = True
        
        logger.info(f"开始在 {interface} 上捕获，过滤器: {filter_str}")
        
        try:
            self._sniffer = AsyncSniffer(
                iface=interface,
                filter=filter_str,
                prn=self._process_packet,
                store=False,
                count=packet_count
            )
            self._sniffer.start()
            
            # 设置定时停止
            if duration:
                threading.Timer(duration, self.stop_capture).start()
            
            return True
            
        except PermissionError:
            logger.error("需要 root 权限才能捕获数据包")
            return False
        except Exception as e:
            logger.error(f"启动捕获失败: {e}")
            return False
    
    def stop_capture(self):
        """停止捕获"""
        self._running = False
        
        if self._sniffer:
            try:
                self._sniffer.stop()
            except:
                pass
        
        self.stats.end_time = time.time()
        self.stats.duration = self.stats.end_time - self.stats.start_time
        
        logger.info(f"捕获结束，共 {self.stats.total_packets} 个包")
    
    def capture_sync(
        self, 
        interface: str = None,
        filter_str: str = None,
        duration: int = 10,
        packet_count: int = None
    ) -> List[CapturedPacket]:
        """同步捕获（阻塞式）"""
        if not SCAPY_AVAILABLE:
            logger.error("Scapy 未安装")
            return []
        
        interface = interface or self.config.network.pc_interface
        filter_str = filter_str or self.config.capture.bpf_filter
        
        self.packets = []
        self.stats = CaptureStats()
        self.stats.start_time = time.time()
        
        logger.info(f"同步捕获 {duration} 秒...")
        
        try:
            packets = sniff(
                iface=interface,
                filter=filter_str,
                timeout=duration,
                count=packet_count,
                prn=self._process_packet,
                store=True
            )
            
            self.stats.end_time = time.time()
            self.stats.duration = self.stats.end_time - self.stats.start_time
            
            return self.packets
            
        except PermissionError:
            logger.error("需要 root 权限")
            return []
        except Exception as e:
            logger.error(f"捕获失败: {e}")
            return []
    
    def capture_dds_traffic(
        self, 
        interface: str = None,
        robot_ip: str = None,
        duration: int = 10
    ) -> List[CapturedPacket]:
        """专门捕获 DDS 流量"""
        interface = interface or self.config.network.pc_interface
        robot_ip = robot_ip or self.config.network.robot_wifi_ip
        
        # DDS 通常使用 7400-7500 端口
        filter_str = f"udp and (portrange 7400-7500 or host {robot_ip})"
        
        return self.capture_sync(
            interface=interface,
            filter_str=filter_str,
            duration=duration
        )
    
    def capture_from_robot(
        self,
        interface: str = None,
        robot_ip: str = None,
        duration: int = 10
    ) -> List[CapturedPacket]:
        """捕获来自机器狗的流量"""
        interface = interface or self.config.network.pc_interface
        robot_ip = robot_ip or self.config.network.robot_wifi_ip
        
        filter_str = f"src host {robot_ip}"
        
        return self.capture_sync(
            interface=interface,
            filter_str=filter_str,
            duration=duration
        )
    
    def save_pcap(self, filepath: str = None):
        """保存为 PCAP 文件"""
        if not SCAPY_AVAILABLE or not self.packets:
            return False
        
        if not filepath:
            Path(self.config.capture.pcap_dir).mkdir(parents=True, exist_ok=True)
            timestamp = time.strftime("%Y%m%d_%H%M%S")
            filepath = f"{self.config.capture.pcap_dir}/capture_{timestamp}.pcap"
        
        try:
            raw_packets = [p.raw_packet for p in self.packets if p.raw_packet]
            wrpcap(filepath, raw_packets)
            logger.info(f"已保存到 {filepath}")
            return True
        except Exception as e:
            logger.error(f"保存 PCAP 失败: {e}")
            return False
    
    def load_pcap(self, filepath: str) -> List[CapturedPacket]:
        """从 PCAP 文件加载"""
        if not SCAPY_AVAILABLE:
            return []
        
        try:
            packets = rdpcap(filepath)
            self.packets = []
            
            for packet in packets:
                self._process_packet(packet)
            
            return self.packets
        except Exception as e:
            logger.error(f"加载 PCAP 失败: {e}")
            return []
    
    def get_packets_by_filter(
        self,
        src_ip: str = None,
        dst_ip: str = None,
        src_port: int = None,
        dst_port: int = None,
        protocol: str = None,
        is_rtps: bool = None
    ) -> List[CapturedPacket]:
        """根据条件过滤数据包"""
        result = []
        
        for pkt in self.packets:
            if src_ip and pkt.src_ip != src_ip:
                continue
            if dst_ip and pkt.dst_ip != dst_ip:
                continue
            if src_port and pkt.src_port != src_port:
                continue
            if dst_port and pkt.dst_port != dst_port:
                continue
            if protocol and pkt.protocol != protocol:
                continue
            if is_rtps is not None and pkt.is_rtps != is_rtps:
                continue
            result.append(pkt)
        
        return result
    
    def get_conversation_summary(self) -> List[Dict]:
        """获取通讯会话摘要"""
        conversations = {}
        
        for pkt in self.packets:
            if pkt.protocol not in ('tcp', 'udp'):
                continue
            
            # 生成会话键
            key = tuple(sorted([
                (pkt.src_ip, pkt.src_port),
                (pkt.dst_ip, pkt.dst_port)
            ]))
            
            if key not in conversations:
                conversations[key] = {
                    'endpoints': key,
                    'protocol': pkt.protocol,
                    'packet_count': 0,
                    'bytes': 0,
                    'first_seen': pkt.timestamp,
                    'last_seen': pkt.timestamp,
                    'has_rtps': False
                }
            
            conv = conversations[key]
            conv['packet_count'] += 1
            conv['bytes'] += pkt.length
            conv['last_seen'] = pkt.timestamp
            if pkt.is_rtps:
                conv['has_rtps'] = True
        
        return list(conversations.values())
    
    def print_stats(self):
        """打印统计信息"""
        print("\n" + "=" * 50)
        print("捕获统计")
        print("=" * 50)
        print(f"总包数: {self.stats.total_packets}")
        print(f"  - UDP: {self.stats.udp_packets}")
        print(f"  - TCP: {self.stats.tcp_packets}")
        print(f"  - ICMP: {self.stats.icmp_packets}")
        print(f"  - RTPS/DDS: {self.stats.rtps_packets}")
        print(f"总字节: {self.stats.bytes_total}")
        print(f"来自机器狗: {self.stats.bytes_from_robot} bytes")
        print(f"发往机器狗: {self.stats.bytes_to_robot} bytes")
        print(f"持续时间: {self.stats.duration:.2f} 秒")
        print(f"唯一源 IP: {len(self.stats.unique_src_ips)}")
        print(f"唯一目标 IP: {len(self.stats.unique_dst_ips)}")
        print(f"唯一端口: {len(self.stats.unique_ports)}")
        print("=" * 50)


class TcpdumpCapture:
    """使用 tcpdump 的备用捕获方案"""
    
    def __init__(self, config: DiagConfig = None):
        self.config = config or DiagConfig()
        self._process = None
        self._output_file = None
    
    def capture(
        self,
        interface: str = None,
        filter_str: str = None,
        duration: int = 10,
        output_file: str = None
    ) -> str:
        """使用 tcpdump 捕获"""
        interface = interface or self.config.network.pc_interface
        filter_str = filter_str or "udp"
        
        if not output_file:
            Path(self.config.capture.pcap_dir).mkdir(parents=True, exist_ok=True)
            timestamp = time.strftime("%Y%m%d_%H%M%S")
            output_file = f"{self.config.capture.pcap_dir}/tcpdump_{timestamp}.pcap"
        
        self._output_file = output_file
        
        cmd = [
            'sudo', 'tcpdump',
            '-i', interface,
            '-w', output_file,
            '-c', str(self.config.capture.packet_limit),
            filter_str
        ]
        
        logger.info(f"执行: {' '.join(cmd)}")
        
        try:
            self._process = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE
            )
            
            # 等待指定时长
            time.sleep(duration)
            
            # 终止 tcpdump
            self._process.terminate()
            self._process.wait(timeout=5)
            
            logger.info(f"tcpdump 捕获完成: {output_file}")
            return output_file
            
        except Exception as e:
            logger.error(f"tcpdump 捕获失败: {e}")
            return None
    
    def capture_robot_traffic(
        self,
        robot_ip: str = None,
        duration: int = 10
    ) -> str:
        """捕获与机器狗的流量"""
        robot_ip = robot_ip or self.config.network.robot_wifi_ip
        filter_str = f"host {robot_ip}"
        
        return self.capture(
            filter_str=filter_str,
            duration=duration
        )
    
    def capture_dds_ports(self, duration: int = 10) -> str:
        """捕获 DDS 端口流量"""
        filter_str = "udp portrange 7400-7500"
        return self.capture(filter_str=filter_str, duration=duration)
    
    def stop(self):
        """停止捕获"""
        if self._process:
            self._process.terminate()
            try:
                self._process.wait(timeout=5)
            except:
                self._process.kill()
