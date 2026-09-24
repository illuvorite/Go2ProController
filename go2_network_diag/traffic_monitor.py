"""
实时流量监控模块
监控网络流量并提供实时分析
"""

# 让类型注解延迟求值：rich 未安装时 Table 等名字不存在，
# 否则下面的 `-> Table` 会在导入期直接抛 NameError，整个 CLI 都起不来。
from __future__ import annotations

import time
import threading
import queue
from typing import List, Dict, Optional, Callable, Any
from dataclasses import dataclass, field
from collections import deque
from datetime import datetime
import logging

try:
    from rich.console import Console
    from rich.live import Live
    from rich.table import Table
    from rich.panel import Panel
    from rich.layout import Layout
    RICH_AVAILABLE = True
except ImportError:
    RICH_AVAILABLE = False

from .config import DiagConfig
from .packet_capture import PacketCapture, CapturedPacket
from .dds_analyzer import DDSAnalyzer


logger = logging.getLogger(__name__)


@dataclass
class TrafficStats:
    """流量统计"""
    window_start: float = 0
    window_duration: float = 1.0  # 统计窗口（秒）
    
    packets_per_second: float = 0
    bytes_per_second: float = 0
    
    udp_pps: float = 0
    tcp_pps: float = 0
    rtps_pps: float = 0
    
    # 方向统计
    inbound_pps: float = 0
    outbound_pps: float = 0
    inbound_bps: float = 0
    outbound_bps: float = 0
    
    # 最近的包
    recent_packets: deque = field(default_factory=lambda: deque(maxlen=100))


@dataclass
class FlowRecord:
    """流记录"""
    src_ip: str
    dst_ip: str
    src_port: int
    dst_port: int
    protocol: str
    
    packet_count: int = 0
    byte_count: int = 0
    first_seen: float = 0
    last_seen: float = 0
    
    has_rtps: bool = False
    
    @property
    def flow_key(self) -> str:
        return f"{self.src_ip}:{self.src_port}->{self.dst_ip}:{self.dst_port}/{self.protocol}"
    
    @property
    def duration(self) -> float:
        return self.last_seen - self.first_seen if self.last_seen > self.first_seen else 0


class TrafficMonitor:
    """实时流量监控器"""
    
    def __init__(self, config: DiagConfig = None):
        self.config = config or DiagConfig()
        self.capture = PacketCapture(config)
        self.dds_analyzer = DDSAnalyzer(config)
        
        self._running = False
        self._stats = TrafficStats()
        self._flows: Dict[str, FlowRecord] = {}
        self._packet_buffer: deque = deque(maxlen=1000)
        self._callbacks: List[Callable] = []
        
        self._stats_lock = threading.Lock()
        self._update_thread = None
        
        # 监控目标
        self._monitor_ips = [
            self.config.network.robot_wifi_ip,
            self.config.network.robot_eth_ip
        ]
    
    def add_callback(self, callback: Callable[[TrafficStats], None]):
        """添加统计更新回调"""
        self._callbacks.append(callback)
    
    def _packet_handler(self, packet: CapturedPacket):
        """处理捕获的数据包"""
        self._packet_buffer.append(packet)
        
        with self._stats_lock:
            self._stats.recent_packets.append(packet)
        
        # 更新流记录
        self._update_flow(packet)
    
    def _update_flow(self, packet: CapturedPacket):
        """更新流记录"""
        if packet.protocol not in ('tcp', 'udp'):
            return
        
        flow_key = f"{packet.src_ip}:{packet.src_port}->{packet.dst_ip}:{packet.dst_port}/{packet.protocol}"
        
        if flow_key not in self._flows:
            self._flows[flow_key] = FlowRecord(
                src_ip=packet.src_ip,
                dst_ip=packet.dst_ip,
                src_port=packet.src_port,
                dst_port=packet.dst_port,
                protocol=packet.protocol,
                first_seen=packet.timestamp
            )
        
        flow = self._flows[flow_key]
        flow.packet_count += 1
        flow.byte_count += packet.length
        flow.last_seen = packet.timestamp
        
        if packet.is_rtps:
            flow.has_rtps = True
    
    def _calculate_stats(self):
        """计算统计数据"""
        now = time.time()
        window = self._stats.window_duration
        
        # 只统计窗口内的包
        packets_in_window = [
            p for p in self._stats.recent_packets
            if now - p.timestamp <= window
        ]
        
        if not packets_in_window:
            with self._stats_lock:
                self._stats.packets_per_second = 0
                self._stats.bytes_per_second = 0
                self._stats.udp_pps = 0
                self._stats.tcp_pps = 0
                self._stats.rtps_pps = 0
                self._stats.inbound_pps = 0
                self._stats.outbound_pps = 0
                self._stats.inbound_bps = 0
                self._stats.outbound_bps = 0
            return
        
        pc_ip = self.config.network.pc_ip
        
        total_packets = len(packets_in_window)
        total_bytes = sum(p.length for p in packets_in_window)
        
        udp_count = sum(1 for p in packets_in_window if p.protocol == 'udp')
        tcp_count = sum(1 for p in packets_in_window if p.protocol == 'tcp')
        rtps_count = sum(1 for p in packets_in_window if p.is_rtps)
        
        inbound = [p for p in packets_in_window if p.dst_ip == pc_ip]
        outbound = [p for p in packets_in_window if p.src_ip == pc_ip]
        
        with self._stats_lock:
            self._stats.window_start = now - window
            self._stats.packets_per_second = total_packets / window
            self._stats.bytes_per_second = total_bytes / window
            
            self._stats.udp_pps = udp_count / window
            self._stats.tcp_pps = tcp_count / window
            self._stats.rtps_pps = rtps_count / window
            
            self._stats.inbound_pps = len(inbound) / window
            self._stats.outbound_pps = len(outbound) / window
            self._stats.inbound_bps = sum(p.length for p in inbound) / window
            self._stats.outbound_bps = sum(p.length for p in outbound) / window
    
    def _stats_update_loop(self):
        """统计更新循环"""
        while self._running:
            self._calculate_stats()
            
            for callback in self._callbacks:
                try:
                    callback(self._stats)
                except Exception as e:
                    logger.error(f"统计回调错误: {e}")
            
            time.sleep(0.5)  # 每 0.5 秒更新
    
    def start(self, interface: str = None, filter_str: str = None):
        """启动监控"""
        interface = interface or self.config.network.pc_interface
        filter_str = filter_str or f"host {self.config.network.robot_wifi_ip} or host {self.config.network.robot_eth_ip}"
        
        self._running = True
        self._flows = {}
        
        # 添加包处理回调
        self.capture.add_callback(self._packet_handler)
        
        # 启动捕获
        success = self.capture.start_capture(
            interface=interface,
            filter_str=filter_str,
            duration=None,  # 持续捕获
            packet_count=None
        )
        
        if not success:
            logger.error("启动捕获失败")
            return False
        
        # 启动统计更新线程
        self._update_thread = threading.Thread(target=self._stats_update_loop)
        self._update_thread.daemon = True
        self._update_thread.start()
        
        logger.info(f"监控已启动，接口: {interface}")
        return True
    
    def stop(self):
        """停止监控"""
        self._running = False
        self.capture.stop_capture()
        
        if self._update_thread:
            self._update_thread.join(timeout=2)
        
        logger.info("监控已停止")
    
    def get_stats(self) -> TrafficStats:
        """获取当前统计"""
        with self._stats_lock:
            return TrafficStats(
                window_start=self._stats.window_start,
                window_duration=self._stats.window_duration,
                packets_per_second=self._stats.packets_per_second,
                bytes_per_second=self._stats.bytes_per_second,
                udp_pps=self._stats.udp_pps,
                tcp_pps=self._stats.tcp_pps,
                rtps_pps=self._stats.rtps_pps,
                inbound_pps=self._stats.inbound_pps,
                outbound_pps=self._stats.outbound_pps,
                inbound_bps=self._stats.inbound_bps,
                outbound_bps=self._stats.outbound_bps
            )
    
    def get_flows(self, sort_by: str = 'packet_count', limit: int = 20) -> List[FlowRecord]:
        """获取流记录"""
        flows = list(self._flows.values())
        
        if sort_by == 'packet_count':
            flows.sort(key=lambda f: f.packet_count, reverse=True)
        elif sort_by == 'byte_count':
            flows.sort(key=lambda f: f.byte_count, reverse=True)
        elif sort_by == 'last_seen':
            flows.sort(key=lambda f: f.last_seen, reverse=True)
        
        return flows[:limit]
    
    def get_robot_flows(self) -> List[FlowRecord]:
        """获取与机器狗相关的流"""
        return [
            f for f in self._flows.values()
            if f.src_ip in self._monitor_ips or f.dst_ip in self._monitor_ips
        ]
    
    def get_rtps_flows(self) -> List[FlowRecord]:
        """获取 RTPS/DDS 流"""
        return [f for f in self._flows.values() if f.has_rtps]
    
    def print_stats(self):
        """打印当前统计"""
        stats = self.get_stats()
        
        print("\n" + "=" * 60)
        print("实时流量统计")
        print("=" * 60)
        print(f"总包速率: {stats.packets_per_second:.1f} pps")
        print(f"总带宽: {stats.bytes_per_second:.1f} Bps ({stats.bytes_per_second * 8 / 1000:.2f} Kbps)")
        print(f"")
        print(f"UDP: {stats.udp_pps:.1f} pps")
        print(f"TCP: {stats.tcp_pps:.1f} pps")
        print(f"RTPS/DDS: {stats.rtps_pps:.1f} pps")
        print(f"")
        print(f"入站: {stats.inbound_pps:.1f} pps, {stats.inbound_bps:.1f} Bps")
        print(f"出站: {stats.outbound_pps:.1f} pps, {stats.outbound_bps:.1f} Bps")
        print("=" * 60)
    
    def print_flows(self, limit: int = 10):
        """打印流信息"""
        flows = self.get_flows(limit=limit)
        
        print("\n" + "=" * 80)
        print("活跃流")
        print("=" * 80)
        print(f"{'源':<22} {'目标':<22} {'协议':<6} {'包数':<8} {'字节':<10} {'RTPS'}")
        print("-" * 80)
        
        for flow in flows:
            src = f"{flow.src_ip}:{flow.src_port}"
            dst = f"{flow.dst_ip}:{flow.dst_port}"
            rtps = "Yes" if flow.has_rtps else "No"
            print(f"{src:<22} {dst:<22} {flow.protocol:<6} {flow.packet_count:<8} {flow.byte_count:<10} {rtps}")
        
        print("=" * 80)


class LiveMonitorDisplay:
    """使用 Rich 的实时监控显示"""
    
    def __init__(self, monitor: TrafficMonitor):
        self.monitor = monitor
        self._running = False
    
    def _generate_table(self) -> Table:
        """生成统计表格"""
        stats = self.monitor.get_stats()
        
        table = Table(title="实时流量监控")
        table.add_column("指标", style="cyan")
        table.add_column("值", style="green")
        
        table.add_row("总包速率", f"{stats.packets_per_second:.1f} pps")
        table.add_row("总带宽", f"{stats.bytes_per_second * 8 / 1000:.2f} Kbps")
        table.add_row("UDP", f"{stats.udp_pps:.1f} pps")
        table.add_row("TCP", f"{stats.tcp_pps:.1f} pps")
        table.add_row("RTPS/DDS", f"{stats.rtps_pps:.1f} pps")
        table.add_row("入站", f"{stats.inbound_pps:.1f} pps / {stats.inbound_bps:.1f} Bps")
        table.add_row("出站", f"{stats.outbound_pps:.1f} pps / {stats.outbound_bps:.1f} Bps")
        
        return table
    
    def _generate_flows_table(self) -> Table:
        """生成流表格"""
        flows = self.monitor.get_robot_flows()[:10]
        
        table = Table(title="机器狗相关流量")
        table.add_column("源", style="cyan")
        table.add_column("目标", style="cyan")
        table.add_column("协议")
        table.add_column("包数", justify="right")
        table.add_column("RTPS")
        
        for flow in flows:
            rtps = "[green]Yes[/green]" if flow.has_rtps else "[red]No[/red]"
            table.add_row(
                f"{flow.src_ip}:{flow.src_port}",
                f"{flow.dst_ip}:{flow.dst_port}",
                flow.protocol.upper(),
                str(flow.packet_count),
                rtps
            )
        
        return table
    
    def run(self, duration: int = None):
        """运行实时显示"""
        if not RICH_AVAILABLE:
            logger.error("Rich 库未安装，无法使用实时显示")
            return
        
        console = Console()
        
        try:
            with Live(console=console, refresh_per_second=2) as live:
                start_time = time.time()
                
                while True:
                    if duration and time.time() - start_time > duration:
                        break
                    
                    # 生成显示内容
                    layout = Layout()
                    layout.split_column(
                        Layout(Panel(self._generate_table()), name="stats"),
                        Layout(Panel(self._generate_flows_table()), name="flows")
                    )
                    
                    live.update(layout)
                    time.sleep(0.5)
                    
        except KeyboardInterrupt:
            pass


class BidirectionalAnalyzer:
    """双向通讯分析器"""
    
    def __init__(self, config: DiagConfig = None):
        self.config = config or DiagConfig()
    
    def analyze(self, packets: List[CapturedPacket]) -> Dict[str, Any]:
        """分析双向通讯"""
        pc_ip = self.config.network.pc_ip
        robot_wifi_ip = self.config.network.robot_wifi_ip
        robot_eth_ip = self.config.network.robot_eth_ip
        robot_ips = [robot_wifi_ip, robot_eth_ip]
        
        result = {
            'pc_to_robot': {
                'packets': 0,
                'bytes': 0,
                'rtps_packets': 0,
                'ports': set()
            },
            'robot_to_pc': {
                'packets': 0,
                'bytes': 0,
                'rtps_packets': 0,
                'ports': set()
            },
            'bidirectional': False,
            'issues': [],
            'recommendations': []
        }
        
        for pkt in packets:
            # PC -> Robot
            if pkt.src_ip == pc_ip and pkt.dst_ip in robot_ips:
                result['pc_to_robot']['packets'] += 1
                result['pc_to_robot']['bytes'] += pkt.length
                if pkt.is_rtps:
                    result['pc_to_robot']['rtps_packets'] += 1
                if pkt.dst_port:
                    result['pc_to_robot']['ports'].add(pkt.dst_port)
            
            # Robot -> PC
            elif pkt.src_ip in robot_ips and pkt.dst_ip == pc_ip:
                result['robot_to_pc']['packets'] += 1
                result['robot_to_pc']['bytes'] += pkt.length
                if pkt.is_rtps:
                    result['robot_to_pc']['rtps_packets'] += 1
                if pkt.src_port:
                    result['robot_to_pc']['ports'].add(pkt.src_port)
        
        # 转换 set 为 list（JSON 序列化需要）
        result['pc_to_robot']['ports'] = list(result['pc_to_robot']['ports'])
        result['robot_to_pc']['ports'] = list(result['robot_to_pc']['ports'])
        
        # 判断双向性
        result['bidirectional'] = (
            result['pc_to_robot']['packets'] > 0 and 
            result['robot_to_pc']['packets'] > 0
        )
        
        # 问题诊断
        if result['pc_to_robot']['packets'] > 0 and result['robot_to_pc']['packets'] == 0:
            result['issues'].append("PC 向机器狗发送了数据，但未收到任何响应")
            result['recommendations'].append("检查机器狗的 DDS 网络接口绑定配置")
            result['recommendations'].append("检查机器狗的防火墙规则")
            result['recommendations'].append("确认机器狗的回程路由配置正确")
        
        if result['pc_to_robot']['rtps_packets'] > 0 and result['robot_to_pc']['rtps_packets'] == 0:
            result['issues'].append("PC 发送了 RTPS 包，但未收到机器狗的 RTPS 响应")
            result['recommendations'].append("机器狗 DDS 可能绑定在 eth0 而非 wlan0 接口")
        
        return result
    
    def print_result(self, result: Dict[str, Any]):
        """打印分析结果"""
        print("\n" + "=" * 60)
        print("双向通讯分析结果")
        print("=" * 60)
        
        print("\n--- PC -> 机器狗 ---")
        print(f"  数据包: {result['pc_to_robot']['packets']}")
        print(f"  字节数: {result['pc_to_robot']['bytes']}")
        print(f"  RTPS 包: {result['pc_to_robot']['rtps_packets']}")
        print(f"  使用端口: {result['pc_to_robot']['ports'][:10]}")
        
        print("\n--- 机器狗 -> PC ---")
        print(f"  数据包: {result['robot_to_pc']['packets']}")
        print(f"  字节数: {result['robot_to_pc']['bytes']}")
        print(f"  RTPS 包: {result['robot_to_pc']['rtps_packets']}")
        print(f"  使用端口: {result['robot_to_pc']['ports'][:10]}")
        
        print(f"\n双向通讯: {'是' if result['bidirectional'] else '否'}")
        
        if result['issues']:
            print(f"\n--- 发现问题 ---")
            for issue in result['issues']:
                print(f"  - {issue}")
        
        if result['recommendations']:
            print(f"\n--- 建议 ---")
            for rec in result['recommendations']:
                print(f"  - {rec}")
        
        print("=" * 60)
