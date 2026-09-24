#!/usr/bin/env python3
"""
PCAP 文件分析工具
用于查看和分析抓包文件内容

功能:
1. 显示包统计摘要
2. 列出所有数据包
3. 查看特定包的详细信息
4. 分析 DDS/RTPS 协议
5. 过滤特定 IP/端口的流量
6. 导出分析结果
"""

import sys
import os
import struct
import time
from datetime import datetime
from typing import List, Dict, Optional, Any
from collections import defaultdict

# 添加项目路径
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

try:
    from scapy.all import rdpcap, IP, UDP, TCP, ICMP, Raw, Ether
    SCAPY_AVAILABLE = True
except ImportError:
    SCAPY_AVAILABLE = False
    print("警告: Scapy 未安装，功能受限")
    print("安装: pip install scapy")

try:
    from rich.console import Console
    from rich.table import Table
    from rich.panel import Panel
    from rich.syntax import Syntax
    from rich import print as rprint
    RICH_AVAILABLE = True
except ImportError:
    RICH_AVAILABLE = False
    rprint = print


class PCAPViewer:
    """PCAP 文件查看器"""
    
    # RTPS 子消息类型
    RTPS_SUBMSG_TYPES = {
        0x01: "PAD",
        0x06: "ACKNACK",
        0x07: "HEARTBEAT",
        0x08: "GAP",
        0x09: "INFO_TS",
        0x0C: "INFO_SRC",
        0x0D: "INFO_REPLY_IP4",
        0x0E: "INFO_DST",
        0x0F: "INFO_REPLY",
        0x12: "NACK_FRAG",
        0x13: "HEARTBEAT_FRAG",
        0x15: "DATA",
        0x16: "DATA_FRAG",
    }
    
    # DDS 厂商 ID
    VENDOR_IDS = {
        (0x01, 0x01): "RTI Connext",
        (0x01, 0x02): "PrismTech OpenSplice",
        (0x01, 0x03): "OCI OpenDDS",
        (0x01, 0x0F): "Eclipse CycloneDDS",
        (0x01, 0x10): "eProsima Fast DDS",
    }
    
    def __init__(self, pcap_file: str):
        self.pcap_file = pcap_file
        self.packets = []
        self.parsed_packets = []
        self.console = Console() if RICH_AVAILABLE else None
        
    def load(self) -> bool:
        """加载 PCAP 文件"""
        if not SCAPY_AVAILABLE:
            print("错误: 需要安装 Scapy 才能读取 PCAP 文件")
            return False
        
        try:
            print(f"正在加载 {self.pcap_file}...")
            self.packets = rdpcap(self.pcap_file)
            print(f"成功加载 {len(self.packets)} 个数据包")
            self._parse_packets()
            return True
        except Exception as e:
            print(f"加载失败: {e}")
            return False
    
    def _parse_packets(self):
        """解析所有数据包"""
        self.parsed_packets = []
        
        for i, pkt in enumerate(self.packets):
            parsed = {
                'index': i + 1,
                'time': float(pkt.time),
                'length': len(pkt),
                'src_ip': None,
                'dst_ip': None,
                'src_port': None,
                'dst_port': None,
                'protocol': 'OTHER',
                'is_rtps': False,
                'rtps_info': {},
                'payload': b'',
                'raw': pkt
            }
            
            if pkt.haslayer(IP):
                ip = pkt[IP]
                parsed['src_ip'] = ip.src
                parsed['dst_ip'] = ip.dst
                
                if pkt.haslayer(UDP):
                    udp = pkt[UDP]
                    parsed['src_port'] = udp.sport
                    parsed['dst_port'] = udp.dport
                    parsed['protocol'] = 'UDP'
                    
                    if pkt.haslayer(Raw):
                        payload = bytes(pkt[Raw].load)
                        parsed['payload'] = payload
                        
                        # 检查是否为 RTPS
                        if payload[:4] == b'RTPS':
                            parsed['is_rtps'] = True
                            parsed['rtps_info'] = self._parse_rtps(payload)
                
                elif pkt.haslayer(TCP):
                    tcp = pkt[TCP]
                    parsed['src_port'] = tcp.sport
                    parsed['dst_port'] = tcp.dport
                    parsed['protocol'] = 'TCP'
                    
                    if pkt.haslayer(Raw):
                        parsed['payload'] = bytes(pkt[Raw].load)
                
                elif pkt.haslayer(ICMP):
                    parsed['protocol'] = 'ICMP'
            
            self.parsed_packets.append(parsed)
    
    def _parse_rtps(self, data: bytes) -> Dict:
        """解析 RTPS 协议"""
        if len(data) < 20:
            return {}
        
        info = {
            'version': f"{data[4]}.{data[5]}",
            'vendor_id': (data[6], data[7]),
            'vendor_name': self.VENDOR_IDS.get((data[6], data[7]), f"Unknown(0x{data[6]:02X}{data[7]:02X})"),
            'guid_prefix': data[8:20].hex(),
            'submessages': []
        }
        
        # 解析子消息
        offset = 20
        while offset + 4 <= len(data):
            try:
                kind = data[offset]
                flags = data[offset + 1]
                is_little_endian = flags & 0x01
                
                if is_little_endian:
                    length = struct.unpack('<H', data[offset + 2:offset + 4])[0]
                else:
                    length = struct.unpack('>H', data[offset + 2:offset + 4])[0]
                
                submsg = {
                    'kind': kind,
                    'kind_name': self.RTPS_SUBMSG_TYPES.get(kind, f"UNKNOWN(0x{kind:02X})"),
                    'flags': flags,
                    'length': length
                }
                info['submessages'].append(submsg)
                
                offset += 4 + length
                if offset % 4 != 0:
                    offset += 4 - (offset % 4)
            except:
                break
        
        return info
    
    def print_summary(self):
        """打印统计摘要"""
        if not self.parsed_packets:
            print("没有数据包")
            return
        
        # 统计
        stats = {
            'total': len(self.parsed_packets),
            'udp': 0,
            'tcp': 0,
            'icmp': 0,
            'rtps': 0,
            'total_bytes': 0,
            'src_ips': defaultdict(int),
            'dst_ips': defaultdict(int),
            'ports': defaultdict(int),
            'conversations': defaultdict(lambda: {'packets': 0, 'bytes': 0}),
            'rtps_vendors': defaultdict(int),
            'rtps_submsg_types': defaultdict(int),
        }
        
        for pkt in self.parsed_packets:
            stats['total_bytes'] += pkt['length']
            
            if pkt['protocol'] == 'UDP':
                stats['udp'] += 1
            elif pkt['protocol'] == 'TCP':
                stats['tcp'] += 1
            elif pkt['protocol'] == 'ICMP':
                stats['icmp'] += 1
            
            if pkt['is_rtps']:
                stats['rtps'] += 1
                vendor = pkt['rtps_info'].get('vendor_name', 'Unknown')
                stats['rtps_vendors'][vendor] += 1
                
                for sm in pkt['rtps_info'].get('submessages', []):
                    stats['rtps_submsg_types'][sm['kind_name']] += 1
            
            if pkt['src_ip']:
                stats['src_ips'][pkt['src_ip']] += 1
            if pkt['dst_ip']:
                stats['dst_ips'][pkt['dst_ip']] += 1
            if pkt['src_port']:
                stats['ports'][pkt['src_port']] += 1
            if pkt['dst_port']:
                stats['ports'][pkt['dst_port']] += 1
            
            # 会话统计
            if pkt['src_ip'] and pkt['dst_ip']:
                conv_key = f"{pkt['src_ip']}:{pkt['src_port']} -> {pkt['dst_ip']}:{pkt['dst_port']}"
                stats['conversations'][conv_key]['packets'] += 1
                stats['conversations'][conv_key]['bytes'] += pkt['length']
        
        # 打印
        print("\n" + "=" * 70)
        print("📊 PCAP 文件分析摘要")
        print("=" * 70)
        
        print(f"\n📁 文件: {self.pcap_file}")
        print(f"📦 总数据包: {stats['total']}")
        print(f"📏 总字节数: {stats['total_bytes']} ({stats['total_bytes']/1024:.2f} KB)")
        
        if self.parsed_packets:
            first_time = self.parsed_packets[0]['time']
            last_time = self.parsed_packets[-1]['time']
            duration = last_time - first_time
            print(f"⏱️  时间跨度: {duration:.3f} 秒")
        
        print(f"\n--- 协议分布 ---")
        print(f"  UDP:  {stats['udp']} 包")
        print(f"  TCP:  {stats['tcp']} 包")
        print(f"  ICMP: {stats['icmp']} 包")
        print(f"  RTPS/DDS: {stats['rtps']} 包")
        
        print(f"\n--- IP 地址统计 ---")
        print("  源 IP:")
        for ip, count in sorted(stats['src_ips'].items(), key=lambda x: -x[1])[:5]:
            print(f"    {ip}: {count} 包")
        print("  目标 IP:")
        for ip, count in sorted(stats['dst_ips'].items(), key=lambda x: -x[1])[:5]:
            print(f"    {ip}: {count} 包")
        
        print(f"\n--- 活跃端口 ---")
        dds_ports = {p: c for p, c in stats['ports'].items() if 7400 <= p <= 7500}
        if dds_ports:
            print("  DDS 端口:")
            for port, count in sorted(dds_ports.items()):
                print(f"    {port}: {count} 包")
        
        if stats['rtps'] > 0:
            print(f"\n--- DDS/RTPS 分析 ---")
            print("  厂商分布:")
            for vendor, count in stats['rtps_vendors'].items():
                print(f"    {vendor}: {count} 包")
            
            print("  子消息类型:")
            for sm_type, count in sorted(stats['rtps_submsg_types'].items(), key=lambda x: -x[1]):
                print(f"    {sm_type}: {count}")
        
        print(f"\n--- 主要通讯流 ---")
        sorted_convs = sorted(stats['conversations'].items(), key=lambda x: -x[1]['packets'])[:10]
        for conv, data in sorted_convs:
            print(f"  {conv}")
            print(f"    {data['packets']} 包, {data['bytes']} 字节")
        
        print("=" * 70)
    
    def list_packets(self, start: int = 1, count: int = 20, filter_str: str = None):
        """列出数据包"""
        packets_to_show = self.parsed_packets[start-1:start-1+count]
        
        if filter_str:
            packets_to_show = [
                p for p in packets_to_show
                if filter_str in str(p['src_ip']) or 
                   filter_str in str(p['dst_ip']) or
                   filter_str in str(p['src_port']) or
                   filter_str in str(p['dst_port']) or
                   (filter_str.lower() == 'rtps' and p['is_rtps'])
            ]
        
        if RICH_AVAILABLE:
            table = Table(title=f"数据包列表 (第 {start} - {start + len(packets_to_show) - 1} 个)")
            table.add_column("#", style="cyan", width=6)
            table.add_column("时间", width=12)
            table.add_column("源", width=22)
            table.add_column("目标", width=22)
            table.add_column("协议", width=8)
            table.add_column("长度", width=8)
            table.add_column("信息", width=20)
            
            for pkt in packets_to_show:
                src = f"{pkt['src_ip'] or '?'}:{pkt['src_port'] or '?'}"
                dst = f"{pkt['dst_ip'] or '?'}:{pkt['dst_port'] or '?'}"
                
                info = ""
                if pkt['is_rtps']:
                    vendor = pkt['rtps_info'].get('vendor_name', '')[:15]
                    sm_count = len(pkt['rtps_info'].get('submessages', []))
                    info = f"[green]RTPS[/green] {vendor} ({sm_count} SM)"
                
                table.add_row(
                    str(pkt['index']),
                    f"{pkt['time']:.6f}"[-12:],
                    src,
                    dst,
                    pkt['protocol'],
                    str(pkt['length']),
                    info
                )
            
            self.console.print(table)
        else:
            print(f"\n数据包列表 (第 {start} - {start + len(packets_to_show) - 1} 个)")
            print("-" * 100)
            print(f"{'#':<6} {'时间':<14} {'源':<22} {'目标':<22} {'协议':<8} {'长度':<8} {'信息'}")
            print("-" * 100)
            
            for pkt in packets_to_show:
                src = f"{pkt['src_ip'] or '?'}:{pkt['src_port'] or '?'}"
                dst = f"{pkt['dst_ip'] or '?'}:{pkt['dst_port'] or '?'}"
                info = "RTPS" if pkt['is_rtps'] else ""
                print(f"{pkt['index']:<6} {pkt['time']:<14.6f} {src:<22} {dst:<22} {pkt['protocol']:<8} {pkt['length']:<8} {info}")
    
    def show_packet(self, index: int):
        """显示单个数据包的详细信息"""
        if index < 1 or index > len(self.parsed_packets):
            print(f"错误: 数据包索引 {index} 超出范围 (1-{len(self.parsed_packets)})")
            return
        
        pkt = self.parsed_packets[index - 1]
        
        print("\n" + "=" * 70)
        print(f"📦 数据包 #{pkt['index']} 详细信息")
        print("=" * 70)
        
        print(f"\n--- 基本信息 ---")
        print(f"  时间戳: {pkt['time']}")
        print(f"  长度: {pkt['length']} 字节")
        print(f"  协议: {pkt['protocol']}")
        
        print(f"\n--- 网络层 ---")
        print(f"  源 IP: {pkt['src_ip']}")
        print(f"  目标 IP: {pkt['dst_ip']}")
        print(f"  源端口: {pkt['src_port']}")
        print(f"  目标端口: {pkt['dst_port']}")
        
        if pkt['is_rtps']:
            print(f"\n--- RTPS/DDS 信息 ---")
            rtps = pkt['rtps_info']
            print(f"  版本: {rtps.get('version', '?')}")
            print(f"  厂商: {rtps.get('vendor_name', '?')}")
            print(f"  GUID 前缀: {rtps.get('guid_prefix', '?')}")
            
            submessages = rtps.get('submessages', [])
            if submessages:
                print(f"\n  子消息 ({len(submessages)} 个):")
                for i, sm in enumerate(submessages):
                    print(f"    [{i+1}] {sm['kind_name']} (长度: {sm['length']})")
        
        if pkt['payload']:
            print(f"\n--- 载荷 (前 200 字节) ---")
            payload_hex = pkt['payload'][:200].hex()
            # 格式化十六进制显示
            hex_lines = [payload_hex[i:i+32] for i in range(0, len(payload_hex), 32)]
            for i, line in enumerate(hex_lines):
                # 每两个字符加空格
                formatted = ' '.join(line[j:j+2] for j in range(0, len(line), 2))
                print(f"  {i*16:04x}: {formatted}")
            
            # ASCII 预览
            print(f"\n--- ASCII 预览 ---")
            ascii_str = ''.join(
                chr(b) if 32 <= b < 127 else '.'
                for b in pkt['payload'][:200]
            )
            print(f"  {ascii_str[:80]}")
        
        print("=" * 70)
    
    def filter_packets(
        self,
        src_ip: str = None,
        dst_ip: str = None,
        port: int = None,
        protocol: str = None,
        rtps_only: bool = False
    ) -> List[Dict]:
        """过滤数据包"""
        result = []
        
        for pkt in self.parsed_packets:
            if src_ip and pkt['src_ip'] != src_ip:
                continue
            if dst_ip and pkt['dst_ip'] != dst_ip:
                continue
            if port and pkt['src_port'] != port and pkt['dst_port'] != port:
                continue
            if protocol and pkt['protocol'] != protocol.upper():
                continue
            if rtps_only and not pkt['is_rtps']:
                continue
            result.append(pkt)
        
        return result
    
    def analyze_bidirectional(self, ip1: str, ip2: str):
        """分析两个 IP 之间的双向通讯"""
        print(f"\n=== 双向通讯分析: {ip1} <-> {ip2} ===\n")
        
        to_ip2 = [p for p in self.parsed_packets if p['src_ip'] == ip1 and p['dst_ip'] == ip2]
        to_ip1 = [p for p in self.parsed_packets if p['src_ip'] == ip2 and p['dst_ip'] == ip1]
        
        print(f"{ip1} -> {ip2}:")
        print(f"  数据包: {len(to_ip2)}")
        print(f"  字节数: {sum(p['length'] for p in to_ip2)}")
        print(f"  RTPS 包: {sum(1 for p in to_ip2 if p['is_rtps'])}")
        
        print(f"\n{ip2} -> {ip1}:")
        print(f"  数据包: {len(to_ip1)}")
        print(f"  字节数: {sum(p['length'] for p in to_ip1)}")
        print(f"  RTPS 包: {sum(1 for p in to_ip1 if p['is_rtps'])}")
        
        if len(to_ip2) > 0 and len(to_ip1) == 0:
            print(f"\n⚠️  警告: 只有 {ip1} -> {ip2} 的单向流量，未收到响应！")
            print("可能原因:")
            print("  1. 目标未运行或未响应")
            print("  2. 防火墙阻止了响应")
            print("  3. 路由配置问题")
    
    def export_analysis(self, output_file: str):
        """导出分析结果"""
        import json
        
        export_data = {
            'file': self.pcap_file,
            'total_packets': len(self.parsed_packets),
            'packets': []
        }
        
        for pkt in self.parsed_packets:
            export_pkt = {
                'index': pkt['index'],
                'time': pkt['time'],
                'length': pkt['length'],
                'src_ip': pkt['src_ip'],
                'dst_ip': pkt['dst_ip'],
                'src_port': pkt['src_port'],
                'dst_port': pkt['dst_port'],
                'protocol': pkt['protocol'],
                'is_rtps': pkt['is_rtps'],
            }
            if pkt['is_rtps']:
                export_pkt['rtps_info'] = pkt['rtps_info']
            export_data['packets'].append(export_pkt)
        
        with open(output_file, 'w', encoding='utf-8') as f:
            json.dump(export_data, f, ensure_ascii=False, indent=2)
        
        print(f"分析结果已导出到: {output_file}")


def interactive_mode(viewer: PCAPViewer):
    """交互模式"""
    print("\n" + "=" * 70)
    print("🔍 PCAP 交互式分析器")
    print("=" * 70)
    print("\n可用命令:")
    print("  summary       - 显示统计摘要")
    print("  list [n]      - 列出数据包 (n=起始位置)")
    print("  show <n>      - 显示第 n 个包的详细信息")
    print("  filter <条件> - 过滤数据包 (如: filter 192.168.1.211)")
    print("  rtps          - 只显示 RTPS/DDS 数据包")
    print("  bidir <ip1> <ip2> - 分析双向通讯")
    print("  export <file> - 导出分析结果为 JSON")
    print("  help          - 显示帮助")
    print("  quit          - 退出")
    print("-" * 70)
    
    current_start = 1
    
    while True:
        try:
            cmd = input("\n> ").strip()
            if not cmd:
                continue
            
            parts = cmd.split()
            command = parts[0].lower()
            
            if command in ('quit', 'exit', 'q'):
                print("再见！")
                break
            
            elif command == 'summary':
                viewer.print_summary()
            
            elif command == 'list':
                start = int(parts[1]) if len(parts) > 1 else current_start
                viewer.list_packets(start=start)
                current_start = start + 20
            
            elif command == 'show':
                if len(parts) < 2:
                    print("用法: show <数据包编号>")
                else:
                    viewer.show_packet(int(parts[1]))
            
            elif command == 'filter':
                if len(parts) < 2:
                    print("用法: filter <IP/端口/协议>")
                else:
                    viewer.list_packets(filter_str=parts[1])
            
            elif command == 'rtps':
                rtps_packets = viewer.filter_packets(rtps_only=True)
                print(f"\nRTPS/DDS 数据包: {len(rtps_packets)} 个")
                for pkt in rtps_packets[:20]:
                    vendor = pkt['rtps_info'].get('vendor_name', '')[:20]
                    print(f"  #{pkt['index']} {pkt['src_ip']}:{pkt['src_port']} -> {pkt['dst_ip']}:{pkt['dst_port']} [{vendor}]")
                if len(rtps_packets) > 20:
                    print(f"  ... 还有 {len(rtps_packets) - 20} 个")
            
            elif command == 'bidir':
                if len(parts) < 3:
                    print("用法: bidir <ip1> <ip2>")
                else:
                    viewer.analyze_bidirectional(parts[1], parts[2])
            
            elif command == 'export':
                if len(parts) < 2:
                    output = viewer.pcap_file.replace('.pcap', '_analysis.json')
                else:
                    output = parts[1]
                viewer.export_analysis(output)
            
            elif command == 'help':
                print("\n命令帮助:")
                print("  summary       - 显示文件统计摘要")
                print("  list [n]      - 从第 n 个包开始列出 (默认继续)")
                print("  show <n>      - 显示第 n 个包的详细信息")
                print("  filter <str>  - 按 IP/端口过滤显示")
                print("  rtps          - 只显示 RTPS/DDS 包")
                print("  bidir <ip1> <ip2> - 分析两个 IP 间的双向流量")
                print("  export [file] - 导出分析结果为 JSON")
            
            else:
                print(f"未知命令: {command}，输入 help 查看帮助")
                
        except KeyboardInterrupt:
            print("\n使用 quit 退出")
        except Exception as e:
            print(f"错误: {e}")


def main():
    """主函数"""
    if len(sys.argv) < 2:
        print("用法: python pcap_viewer.py <pcap文件> [命令]")
        print("\n命令:")
        print("  (无)      - 进入交互模式")
        print("  summary   - 显示统计摘要")
        print("  list      - 列出数据包")
        print("  show <n>  - 显示第 n 个包")
        print("\n示例:")
        print("  python pcap_viewer.py capture.pcap")
        print("  python pcap_viewer.py capture.pcap summary")
        sys.exit(1)
    
    pcap_file = sys.argv[1]
    
    if not os.path.exists(pcap_file):
        print(f"错误: 文件不存在: {pcap_file}")
        sys.exit(1)
    
    viewer = PCAPViewer(pcap_file)
    
    if not viewer.load():
        sys.exit(1)
    
    # 检查是否有命令参数
    if len(sys.argv) > 2:
        command = sys.argv[2].lower()
        
        if command == 'summary':
            viewer.print_summary()
        elif command == 'list':
            viewer.list_packets()
        elif command == 'show' and len(sys.argv) > 3:
            viewer.show_packet(int(sys.argv[3]))
        else:
            print(f"未知命令: {command}")
    else:
        # 先显示摘要，再进入交互模式
        viewer.print_summary()
        interactive_mode(viewer)


if __name__ == '__main__':
    main()
