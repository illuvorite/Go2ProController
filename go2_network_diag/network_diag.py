"""
网络诊断核心模块
提供 ICMP ping、端口扫描、路由分析、接口信息等功能
"""

import asyncio
import socket
import struct
import time
import subprocess
import os
import re
from typing import List, Dict, Optional, Tuple, Any
from dataclasses import dataclass, field
from concurrent.futures import ThreadPoolExecutor
import logging

try:
    import netifaces
except ImportError:
    netifaces = None

try:
    import psutil
except ImportError:
    psutil = None

from .config import DiagConfig


logger = logging.getLogger(__name__)


@dataclass
class PingResult:
    """Ping 结果"""
    target: str
    success: bool
    latency_ms: Optional[float] = None
    ttl: Optional[int] = None
    packet_loss: float = 0.0
    error: Optional[str] = None
    
    def __str__(self):
        if self.success:
            return f"Ping {self.target}: 成功, 延迟={self.latency_ms:.2f}ms, TTL={self.ttl}"
        return f"Ping {self.target}: 失败, 错误={self.error}"


@dataclass
class PortScanResult:
    """端口扫描结果"""
    host: str
    port: int
    protocol: str  # tcp/udp
    is_open: bool
    service: Optional[str] = None
    banner: Optional[str] = None
    latency_ms: Optional[float] = None


@dataclass
class RouteInfo:
    """路由信息"""
    destination: str
    gateway: str
    interface: str
    flags: str
    metric: int = 0


@dataclass
class InterfaceInfo:
    """网络接口信息"""
    name: str
    mac_address: Optional[str] = None
    ipv4_addresses: List[str] = field(default_factory=list)
    ipv6_addresses: List[str] = field(default_factory=list)
    is_up: bool = False
    is_wireless: bool = False
    mtu: int = 1500
    stats: Dict[str, int] = field(default_factory=dict)


@dataclass
class NetworkDiagResult:
    """网络诊断综合结果"""
    timestamp: float
    pc_interface: InterfaceInfo
    ping_results: List[PingResult]
    port_scan_results: List[PortScanResult]
    routes: List[RouteInfo]
    iptables_rules: List[str]
    issues_found: List[str]
    recommendations: List[str]


class NetworkDiagnostics:
    """网络诊断工具类"""
    
    def __init__(self, config: DiagConfig = None):
        self.config = config or DiagConfig()
        self.executor = ThreadPoolExecutor(max_workers=20)
    
    # ==================== ICMP Ping ====================
    
    def ping(self, host: str, count: int = 4, timeout: float = None) -> PingResult:
        """
        执行 ICMP ping 测试
        
        Args:
            host: 目标主机 IP 或域名
            count: ping 次数
            timeout: 超时时间（秒）
            
        Returns:
            PingResult 对象
        """
        timeout = timeout or self.config.ping_timeout
        
        try:
            # 使用系统 ping 命令
            cmd = ['ping', '-c', str(count), '-W', str(int(timeout)), host]
            result = subprocess.run(
                cmd, 
                capture_output=True, 
                text=True, 
                timeout=timeout * count + 5
            )
            
            output = result.stdout + result.stderr
            
            # 解析结果
            if result.returncode == 0:
                # 提取延迟
                latency_match = re.search(
                    r'(?:rtt|round-trip)\s+min/avg/max/(?:mdev|stddev)\s*=\s*[\d.]+/([\d.]+)/[\d.]+',
                    output
                )
                latency = float(latency_match.group(1)) if latency_match else None
                
                # 提取 TTL
                ttl_match = re.search(r'ttl[=\s]+(\d+)', output, re.IGNORECASE)
                ttl = int(ttl_match.group(1)) if ttl_match else None
                
                # 提取丢包率
                loss_match = re.search(r'(\d+(?:\.\d+)?)\s*%\s*packet\s*loss', output)
                loss = float(loss_match.group(1)) if loss_match else 0.0
                
                return PingResult(
                    target=host,
                    success=True,
                    latency_ms=latency,
                    ttl=ttl,
                    packet_loss=loss
                )
            else:
                return PingResult(
                    target=host,
                    success=False,
                    error=f"Ping 失败: {output.strip()}"
                )
                
        except subprocess.TimeoutExpired:
            return PingResult(
                target=host,
                success=False,
                error="Ping 超时"
            )
        except Exception as e:
            return PingResult(
                target=host,
                success=False,
                error=str(e)
            )
    
    async def ping_async(self, host: str, count: int = 4, timeout: float = None) -> PingResult:
        """异步 ping"""
        loop = asyncio.get_event_loop()
        return await loop.run_in_executor(
            self.executor, 
            lambda: self.ping(host, count, timeout)
        )
    
    async def ping_multiple(self, hosts: List[str], count: int = 4) -> List[PingResult]:
        """并行 ping 多个主机"""
        tasks = [self.ping_async(host, count) for host in hosts]
        return await asyncio.gather(*tasks)
    
    # ==================== 端口扫描 ====================
    
    def scan_tcp_port(self, host: str, port: int, timeout: float = None) -> PortScanResult:
        """扫描单个 TCP 端口"""
        timeout = timeout or self.config.connect_timeout
        start_time = time.time()
        
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(timeout)
            result = sock.connect_ex((host, port))
            latency = (time.time() - start_time) * 1000
            
            if result == 0:
                # 尝试获取 banner
                banner = None
                try:
                    sock.settimeout(1)
                    banner = sock.recv(1024).decode('utf-8', errors='ignore').strip()
                except:
                    pass
                
                sock.close()
                return PortScanResult(
                    host=host,
                    port=port,
                    protocol='tcp',
                    is_open=True,
                    banner=banner,
                    latency_ms=latency,
                    service=self._get_service_name(port, 'tcp')
                )
            else:
                sock.close()
                return PortScanResult(
                    host=host,
                    port=port,
                    protocol='tcp',
                    is_open=False
                )
        except Exception as e:
            return PortScanResult(
                host=host,
                port=port,
                protocol='tcp',
                is_open=False
            )
    
    def scan_udp_port(self, host: str, port: int, timeout: float = None) -> PortScanResult:
        """
        扫描单个 UDP 端口
        注意：UDP 扫描不如 TCP 可靠
        """
        timeout = timeout or self.config.connect_timeout
        start_time = time.time()
        
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.settimeout(timeout)
            
            # 发送空数据包或 RTPS 探测包
            if 7400 <= port <= 7500:
                # DDS RTPS 探测
                probe_data = b'RTPS\x02\x01\x01\x00'  # RTPS header
            else:
                probe_data = b'\x00'
            
            sock.sendto(probe_data, (host, port))
            
            try:
                data, addr = sock.recvfrom(1024)
                latency = (time.time() - start_time) * 1000
                sock.close()
                return PortScanResult(
                    host=host,
                    port=port,
                    protocol='udp',
                    is_open=True,
                    latency_ms=latency,
                    service=self._get_service_name(port, 'udp'),
                    banner=data[:50].hex() if data else None
                )
            except socket.timeout:
                sock.close()
                # UDP 超时可能意味着端口开放但没有响应，或被过滤
                return PortScanResult(
                    host=host,
                    port=port,
                    protocol='udp',
                    is_open=None,  # 不确定
                    service=self._get_service_name(port, 'udp')
                )
                
        except Exception as e:
            return PortScanResult(
                host=host,
                port=port,
                protocol='udp',
                is_open=False
            )
    
    async def scan_ports_async(
        self, 
        host: str, 
        ports: List[int], 
        protocol: str = 'tcp'
    ) -> List[PortScanResult]:
        """异步扫描多个端口"""
        loop = asyncio.get_event_loop()
        
        if protocol == 'tcp':
            scan_func = self.scan_tcp_port
        else:
            scan_func = self.scan_udp_port
        
        tasks = [
            loop.run_in_executor(self.executor, lambda p=p: scan_func(host, p))
            for p in ports
        ]
        return await asyncio.gather(*tasks)
    
    def scan_dds_ports(self, host: str) -> List[PortScanResult]:
        """扫描 DDS 常用端口"""
        results = []
        for port in self.config.dds.common_ports:
            result = self.scan_udp_port(host, port, timeout=1.0)
            results.append(result)
        return results
    
    def _get_service_name(self, port: int, protocol: str) -> Optional[str]:
        """获取端口对应的服务名"""
        try:
            return socket.getservbyport(port, protocol)
        except:
            # DDS 常用端口映射
            dds_ports = {
                7400: "DDS Discovery",
                7401: "DDS Discovery",
                7410: "DDS User Data",
                7411: "DDS User Data",
                7412: "DDS User Data",
                7413: "DDS User Data",
                7414: "DDS User Data",
                7415: "DDS User Data",
                7416: "DDS User Data",
                7417: "DDS User Data",
            }
            return dds_ports.get(port)
    
    # ==================== 路由分析 ====================
    
    def get_routes(self) -> List[RouteInfo]:
        """获取系统路由表"""
        routes = []
        
        try:
            result = subprocess.run(
                ['ip', 'route', 'show'],
                capture_output=True,
                text=True,
                timeout=5
            )
            
            for line in result.stdout.strip().split('\n'):
                if not line:
                    continue
                    
                parts = line.split()
                if len(parts) < 3:
                    continue
                
                route = RouteInfo(
                    destination=parts[0],
                    gateway='',
                    interface='',
                    flags='',
                    metric=0
                )
                
                for i, part in enumerate(parts):
                    if part == 'via' and i + 1 < len(parts):
                        route.gateway = parts[i + 1]
                    elif part == 'dev' and i + 1 < len(parts):
                        route.interface = parts[i + 1]
                    elif part == 'metric' and i + 1 < len(parts):
                        route.metric = int(parts[i + 1])
                
                routes.append(route)
                
        except Exception as e:
            logger.error(f"获取路由表失败: {e}")
        
        return routes
    
    def check_route_to_host(self, host: str) -> Optional[RouteInfo]:
        """检查到目标主机的路由"""
        try:
            result = subprocess.run(
                ['ip', 'route', 'get', host],
                capture_output=True,
                text=True,
                timeout=5
            )
            
            output = result.stdout.strip()
            if not output:
                return None
            
            parts = output.split()
            route = RouteInfo(
                destination=host,
                gateway='',
                interface='',
                flags='',
                metric=0
            )
            
            for i, part in enumerate(parts):
                if part == 'via' and i + 1 < len(parts):
                    route.gateway = parts[i + 1]
                elif part == 'dev' and i + 1 < len(parts):
                    route.interface = parts[i + 1]
                elif part == 'src' and i + 1 < len(parts):
                    route.flags = f"src={parts[i + 1]}"
            
            return route
            
        except Exception as e:
            logger.error(f"检查路由失败: {e}")
            return None
    
    def add_route(self, destination: str, gateway: str) -> bool:
        """添加静态路由"""
        try:
            cmd = ['sudo', 'ip', 'route', 'add', destination, 'via', gateway]
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=10)
            if result.returncode == 0:
                logger.info(f"成功添加路由: {destination} via {gateway}")
                return True
            else:
                logger.error(f"添加路由失败: {result.stderr}")
                return False
        except Exception as e:
            logger.error(f"添加路由异常: {e}")
            return False
    
    # ==================== 网络接口信息 ====================
    
    def get_interface_info(self, interface: str = None) -> List[InterfaceInfo]:
        """获取网络接口信息"""
        interfaces = []
        
        if netifaces:
            iface_names = [interface] if interface else netifaces.interfaces()
            
            for name in iface_names:
                try:
                    info = InterfaceInfo(name=name)
                    
                    # MAC 地址
                    addrs = netifaces.ifaddresses(name)
                    if netifaces.AF_LINK in addrs:
                        info.mac_address = addrs[netifaces.AF_LINK][0].get('addr')
                    
                    # IPv4 地址
                    if netifaces.AF_INET in addrs:
                        info.ipv4_addresses = [
                            a.get('addr') for a in addrs[netifaces.AF_INET]
                        ]
                    
                    # IPv6 地址
                    if netifaces.AF_INET6 in addrs:
                        info.ipv6_addresses = [
                            a.get('addr').split('%')[0] 
                            for a in addrs[netifaces.AF_INET6]
                        ]
                    
                    # 判断是否为无线接口
                    info.is_wireless = name.startswith(('wl', 'wlan', 'wifi'))
                    
                    # 检查接口状态
                    info.is_up = self._check_interface_up(name)
                    
                    # 获取 MTU
                    info.mtu = self._get_interface_mtu(name)
                    
                    # 获取统计信息
                    if psutil:
                        stats = psutil.net_io_counters(pernic=True)
                        if name in stats:
                            s = stats[name]
                            info.stats = {
                                'bytes_sent': s.bytes_sent,
                                'bytes_recv': s.bytes_recv,
                                'packets_sent': s.packets_sent,
                                'packets_recv': s.packets_recv,
                                'errin': s.errin,
                                'errout': s.errout,
                                'dropin': s.dropin,
                                'dropout': s.dropout,
                            }
                    
                    interfaces.append(info)
                    
                except Exception as e:
                    logger.error(f"获取接口 {name} 信息失败: {e}")
        else:
            # 使用命令行方式
            interfaces = self._get_interfaces_from_cmd(interface)
        
        return interfaces
    
    def _check_interface_up(self, interface: str) -> bool:
        """检查接口是否启用"""
        try:
            with open(f'/sys/class/net/{interface}/operstate', 'r') as f:
                return f.read().strip().lower() == 'up'
        except:
            return False
    
    def _get_interface_mtu(self, interface: str) -> int:
        """获取接口 MTU"""
        try:
            with open(f'/sys/class/net/{interface}/mtu', 'r') as f:
                return int(f.read().strip())
        except:
            return 1500
    
    def _get_interfaces_from_cmd(self, interface: str = None) -> List[InterfaceInfo]:
        """通过命令行获取接口信息"""
        interfaces = []
        
        try:
            if interface:
                cmd = ['ip', 'addr', 'show', interface]
            else:
                cmd = ['ip', 'addr', 'show']
            
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=5)
            
            current_iface = None
            for line in result.stdout.split('\n'):
                # 接口行
                match = re.match(r'^\d+:\s+(\w+):', line)
                if match:
                    if current_iface:
                        interfaces.append(current_iface)
                    name = match.group(1)
                    current_iface = InterfaceInfo(name=name)
                    current_iface.is_up = 'UP' in line
                    current_iface.is_wireless = name.startswith(('wl', 'wlan', 'wifi'))
                    
                    mtu_match = re.search(r'mtu\s+(\d+)', line)
                    if mtu_match:
                        current_iface.mtu = int(mtu_match.group(1))
                
                # MAC 地址
                elif current_iface and 'link/ether' in line:
                    mac_match = re.search(r'link/ether\s+([\da-f:]+)', line)
                    if mac_match:
                        current_iface.mac_address = mac_match.group(1)
                
                # IPv4 地址
                elif current_iface and 'inet ' in line:
                    ip_match = re.search(r'inet\s+([\d.]+)', line)
                    if ip_match:
                        current_iface.ipv4_addresses.append(ip_match.group(1))
                
                # IPv6 地址
                elif current_iface and 'inet6' in line:
                    ip_match = re.search(r'inet6\s+([\da-f:]+)', line)
                    if ip_match:
                        current_iface.ipv6_addresses.append(ip_match.group(1))
            
            if current_iface:
                interfaces.append(current_iface)
                
        except Exception as e:
            logger.error(f"获取接口信息失败: {e}")
        
        return interfaces
    
    # ==================== 防火墙检查 ====================
    
    def get_iptables_rules(self) -> List[str]:
        """获取 iptables 规则"""
        rules = []
        
        try:
            # INPUT 链
            result = subprocess.run(
                ['sudo', 'iptables', '-L', 'INPUT', '-n', '-v'],
                capture_output=True,
                text=True,
                timeout=10
            )
            rules.append("=== INPUT 链 ===")
            rules.extend(result.stdout.strip().split('\n'))
            
            # OUTPUT 链
            result = subprocess.run(
                ['sudo', 'iptables', '-L', 'OUTPUT', '-n', '-v'],
                capture_output=True,
                text=True,
                timeout=10
            )
            rules.append("\n=== OUTPUT 链 ===")
            rules.extend(result.stdout.strip().split('\n'))
            
            # FORWARD 链
            result = subprocess.run(
                ['sudo', 'iptables', '-L', 'FORWARD', '-n', '-v'],
                capture_output=True,
                text=True,
                timeout=10
            )
            rules.append("\n=== FORWARD 链 ===")
            rules.extend(result.stdout.strip().split('\n'))
            
        except Exception as e:
            rules.append(f"获取 iptables 规则失败: {e}")
        
        return rules
    
    def check_ufw_status(self) -> Dict[str, Any]:
        """检查 UFW 防火墙状态"""
        try:
            result = subprocess.run(
                ['sudo', 'ufw', 'status', 'verbose'],
                capture_output=True,
                text=True,
                timeout=10
            )
            
            output = result.stdout.strip()
            status = {
                'active': 'Status: active' in output,
                'output': output
            }
            return status
            
        except Exception as e:
            return {'active': None, 'error': str(e)}
    
    # ==================== 综合诊断 ====================
    
    def run_full_diagnosis(self) -> NetworkDiagResult:
        """运行完整的网络诊断"""
        logger.info("开始网络诊断...")
        
        issues = []
        recommendations = []
        
        # 获取 PC 接口信息
        pc_interfaces = self.get_interface_info(self.config.network.pc_interface)
        pc_interface = pc_interfaces[0] if pc_interfaces else InterfaceInfo(name=self.config.network.pc_interface)
        
        if not pc_interface.is_up:
            issues.append(f"PC 无线接口 {self.config.network.pc_interface} 未启用")
            recommendations.append(f"请启用接口: sudo ip link set {self.config.network.pc_interface} up")
        
        # Ping 测试
        ping_targets = [
            self.config.network.robot_wifi_ip,
            self.config.network.robot_eth_ip,
        ]
        
        ping_results = []
        for target in ping_targets:
            result = self.ping(target)
            ping_results.append(result)
            if not result.success:
                issues.append(f"无法 ping 通 {target}")
                if target == self.config.network.robot_eth_ip:
                    recommendations.append(
                        f"添加静态路由: sudo ip route add {self.config.network.eth_subnet} "
                        f"via {self.config.network.robot_wifi_ip}"
                    )
        
        # 端口扫描
        port_results = self.scan_dds_ports(self.config.network.robot_wifi_ip)
        
        open_ports = [r for r in port_results if r.is_open]
        if not open_ports:
            issues.append("未检测到任何开放的 DDS 端口")
            recommendations.append("检查机器狗 DDS 服务是否运行，以及防火墙配置")
        
        # 获取路由表
        routes = self.get_routes()
        
        # 检查到机器狗的路由
        robot_route = self.check_route_to_host(self.config.network.robot_wifi_ip)
        if robot_route and robot_route.interface != self.config.network.pc_interface:
            issues.append(f"到机器狗的路由使用了错误的接口: {robot_route.interface}")
            recommendations.append(f"检查路由配置，确保使用 {self.config.network.pc_interface} 接口")
        
        # 获取 iptables 规则
        iptables_rules = self.get_iptables_rules()
        
        return NetworkDiagResult(
            timestamp=time.time(),
            pc_interface=pc_interface,
            ping_results=ping_results,
            port_scan_results=port_results,
            routes=routes,
            iptables_rules=iptables_rules,
            issues_found=issues,
            recommendations=recommendations
        )
    
    def traceroute(self, host: str, max_hops: int = 30) -> List[Dict[str, Any]]:
        """执行 traceroute"""
        hops = []
        
        try:
            result = subprocess.run(
                ['traceroute', '-n', '-m', str(max_hops), host],
                capture_output=True,
                text=True,
                timeout=60
            )
            
            for line in result.stdout.strip().split('\n')[1:]:  # 跳过标题行
                parts = line.split()
                if len(parts) >= 2:
                    hop_num = parts[0]
                    if parts[1] == '*':
                        hops.append({
                            'hop': int(hop_num),
                            'ip': '*',
                            'latency': None
                        })
                    else:
                        hops.append({
                            'hop': int(hop_num),
                            'ip': parts[1],
                            'latency': parts[2] if len(parts) > 2 else None
                        })
                        
        except Exception as e:
            logger.error(f"Traceroute 失败: {e}")
        
        return hops
    
    def check_ip_forwarding(self) -> bool:
        """检查 IP 转发是否启用"""
        try:
            with open('/proc/sys/net/ipv4/ip_forward', 'r') as f:
                return f.read().strip() == '1'
        except:
            return False
    
    def close(self):
        """关闭资源"""
        self.executor.shutdown(wait=False)


# 便捷函数
def quick_ping(host: str) -> PingResult:
    """快速 ping 测试"""
    diag = NetworkDiagnostics()
    result = diag.ping(host)
    diag.close()
    return result


def quick_port_scan(host: str, ports: List[int], protocol: str = 'udp') -> List[PortScanResult]:
    """快速端口扫描"""
    diag = NetworkDiagnostics()
    results = []
    
    if protocol == 'tcp':
        for port in ports:
            results.append(diag.scan_tcp_port(host, port))
    else:
        for port in ports:
            results.append(diag.scan_udp_port(host, port))
    
    diag.close()
    return results
