"""
命令行接口模块
提供交互式的网络诊断功能
"""

import sys
import os
import time
import logging
from pathlib import Path
from typing import Optional

try:
    import click
    from rich.console import Console
    from rich.table import Table
    from rich.panel import Panel
    from rich.progress import Progress, SpinnerColumn, TextColumn
    from rich import print as rprint
    RICH_AVAILABLE = True
except ImportError:
    RICH_AVAILABLE = False
    click = None

from .config import DiagConfig
from .network_diag import NetworkDiagnostics
from .packet_capture import PacketCapture, TcpdumpCapture
from .dds_analyzer import DDSAnalyzer
from .config_validator import CycloneDDSValidator
from .traffic_monitor import TrafficMonitor, BidirectionalAnalyzer
from .report_generator import ReportGenerator


# 配置日志
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)

# Rich console
console = Console() if RICH_AVAILABLE else None


def print_banner():
    """打印欢迎信息"""
    banner = """
╔══════════════════════════════════════════════════════════════════╗
║         🐕 Unitree Go2 网络诊断工具 v1.0.0                       ║
║                                                                  ║
║   用于诊断 Go2 机器狗无线通讯异常问题                            ║
║   - 网络连通性检测                                               ║
║   - DDS/RTPS 协议分析                                            ║
║   - CycloneDDS 配置验证                                          ║
║   - 双向通讯诊断                                                 ║
╚══════════════════════════════════════════════════════════════════╝
    """
    if RICH_AVAILABLE:
        console.print(Panel(banner, style="blue"))
    else:
        print(banner)


def load_config(config_file: str = None) -> DiagConfig:
    """加载配置"""
    if config_file:
        if config_file.endswith('.yaml') or config_file.endswith('.yml'):
            return DiagConfig.from_yaml(config_file)
        elif config_file.endswith('.json'):
            return DiagConfig.from_json(config_file)
    return DiagConfig()


if click:
    @click.group()
    @click.option('--config', '-c', type=click.Path(exists=True), help='配置文件路径')
    @click.option('--verbose', '-v', is_flag=True, help='详细输出')
    @click.pass_context
    def cli(ctx, config, verbose):
        """Unitree Go2 网络诊断工具"""
        ctx.ensure_object(dict)
        ctx.obj['config'] = load_config(config)
        
        if verbose:
            logging.getLogger().setLevel(logging.DEBUG)
        
        print_banner()

    @cli.command()
    @click.option('--interface', '-i', help='网络接口 (默认: wlo1)')
    @click.option('--robot-ip', help='机器狗 WiFi IP (默认: 192.168.1.211)')
    @click.option('--report', '-r', type=click.Choice(['html', 'json', 'txt', 'all']), 
                  default='html', help='报告格式')
    @click.option('--output-dir', '-o', default='./reports', help='报告输出目录')
    @click.pass_context
    def diagnose(ctx, interface, robot_ip, report, output_dir):
        """运行完整的网络诊断"""
        config = ctx.obj['config']
        
        if interface:
            config.network.pc_interface = interface
        if robot_ip:
            config.network.robot_wifi_ip = robot_ip
        
        rprint("[bold blue]开始网络诊断...[/bold blue]\n")
        
        # 初始化组件
        net_diag = NetworkDiagnostics(config)
        dds_analyzer = DDSAnalyzer(config)
        report_gen = ReportGenerator(config)
        
        with Progress(
            SpinnerColumn(),
            TextColumn("[progress.description]{task.description}"),
            console=console
        ) as progress:
            # 网络诊断
            task = progress.add_task("运行网络诊断...", total=None)
            net_result = net_diag.run_full_diagnosis()
            report_gen.set_network_result(net_result)
            progress.update(task, description="[green]网络诊断完成[/green]")
            
            # Ping 结果
            rprint("\n[bold]Ping 测试结果:[/bold]")
            for ping in net_result.ping_results:
                status = "[green]通[/green]" if ping.success else "[red]不通[/red]"
                latency = f", 延迟={ping.latency_ms:.2f}ms" if ping.latency_ms else ""
                rprint(f"  {ping.target}: {status}{latency}")
            
            # DDS 端口扫描
            rprint("\n[bold]DDS 端口扫描:[/bold]")
            open_ports = [p for p in net_result.port_scan_results if p.is_open]
            if open_ports:
                for port in open_ports:
                    rprint(f"  [green]端口 {port.port} 开放[/green] - {port.service or 'DDS'}")
            else:
                rprint("  [yellow]未检测到开放的 DDS 端口[/yellow]")
            
            # 显示问题
            if net_result.issues_found:
                rprint("\n[bold red]发现的问题:[/bold red]")
                for issue in net_result.issues_found:
                    rprint(f"  • {issue}")
            
            if net_result.recommendations:
                rprint("\n[bold green]建议:[/bold green]")
                for rec in net_result.recommendations:
                    rprint(f"  ➜ {rec}")
        
        # 生成报告
        rprint(f"\n[bold]生成报告...[/bold]")
        if report == 'all':
            report_gen.generate_all(output_dir)
        else:
            timestamp = time.strftime("%Y%m%d_%H%M%S")
            output_path = f"{output_dir}/report_{timestamp}.{report}"
            report_gen.generate(report, output_path)
            rprint(f"[green]报告已保存到: {output_path}[/green]")
        
        net_diag.close()

    @cli.command()
    @click.option('--host', '-h', required=True, help='目标主机 IP')
    @click.option('--count', '-c', default=4, help='Ping 次数')
    @click.pass_context
    def ping(ctx, host, count):
        """Ping 测试"""
        config = ctx.obj['config']
        net_diag = NetworkDiagnostics(config)
        
        rprint(f"[bold]Ping {host}...[/bold]")
        result = net_diag.ping(host, count)
        
        if result.success:
            rprint(f"[green]成功[/green]: 延迟={result.latency_ms:.2f}ms, TTL={result.ttl}, 丢包率={result.packet_loss}%")
        else:
            rprint(f"[red]失败[/red]: {result.error}")
        
        net_diag.close()

    @cli.command()
    @click.option('--host', '-h', help='目标主机 IP (默认: 机器狗 WiFi IP)')
    @click.option('--ports', '-p', help='端口列表 (逗号分隔，默认: DDS 端口)')
    @click.pass_context
    def portscan(ctx, host, ports):
        """端口扫描"""
        config = ctx.obj['config']
        net_diag = NetworkDiagnostics(config)
        
        host = host or config.network.robot_wifi_ip
        
        if ports:
            port_list = [int(p.strip()) for p in ports.split(',')]
        else:
            port_list = config.dds.common_ports
        
        rprint(f"[bold]扫描 {host} 的 UDP 端口...[/bold]")
        
        table = Table(title="端口扫描结果")
        table.add_column("端口", style="cyan")
        table.add_column("状态")
        table.add_column("服务")
        
        for port in port_list:
            result = net_diag.scan_udp_port(host, port)
            if result.is_open:
                status = "[green]开放[/green]"
            elif result.is_open is False:
                status = "[red]关闭[/red]"
            else:
                status = "[yellow]未知[/yellow]"
            
            table.add_row(str(port), status, result.service or "-")
        
        console.print(table)
        net_diag.close()

    @cli.command()
    @click.option('--interface', '-i', help='网络接口')
    @click.option('--duration', '-d', default=10, help='捕获时长（秒）')
    @click.option('--filter', '-f', help='BPF 过滤器')
    @click.option('--save', '-s', is_flag=True, help='保存为 PCAP 文件')
    @click.pass_context
    def capture(ctx, interface, duration, filter, save):
        """抓包分析"""
        config = ctx.obj['config']
        
        interface = interface or config.network.pc_interface
        filter_str = filter or f"host {config.network.robot_wifi_ip}"
        
        rprint(f"[bold]在 {interface} 上捕获 {duration} 秒...[/bold]")
        rprint(f"过滤器: {filter_str}")
        
        capture = PacketCapture(config)
        
        with Progress(
            SpinnerColumn(),
            TextColumn("[progress.description]{task.description}"),
            console=console
        ) as progress:
            task = progress.add_task(f"捕获中...", total=duration)
            
            packets = capture.capture_sync(
                interface=interface,
                filter_str=filter_str,
                duration=duration
            )
            
            progress.update(task, description="[green]捕获完成[/green]")
        
        # 打印统计
        capture.print_stats()
        
        # DDS 分析
        if capture.stats.rtps_packets > 0:
            rprint("\n[bold]DDS/RTPS 分析:[/bold]")
            dds_analyzer = DDSAnalyzer(config)
            result = dds_analyzer.analyze_packets(packets)
            dds_analyzer.print_analysis_result(result)
        
        # 保存 PCAP
        if save:
            capture.save_pcap()

    @cli.command()
    @click.argument('xml_file', type=click.Path(exists=True))
    @click.pass_context
    def validate(ctx, xml_file):
        """验证 CycloneDDS 配置文件"""
        config = ctx.obj['config']
        validator = CycloneDDSValidator(config)
        
        rprint(f"[bold]验证配置文件: {xml_file}[/bold]\n")
        
        cyclone_cfg, issues = validator.validate_file(xml_file)
        
        if cyclone_cfg:
            validator.print_validation_result(cyclone_cfg, issues)
            
            errors = [i for i in issues if i.severity == 'error']
            if errors:
                rprint("\n[yellow]建议使用以下配置:[/yellow]")
                suggested = validator.generate_go2_wifi_config()
                rprint(f"[dim]{suggested}[/dim]")
        else:
            rprint("[red]无法解析配置文件[/red]")

    @cli.command()
    @click.option('--interface', '-i', help='网络接口')
    @click.option('--peers', '-p', help='Peer 地址 (逗号分隔)')
    @click.option('--output', '-o', default='cyclonedds.xml', help='输出文件路径')
    @click.pass_context
    def genconfig(ctx, interface, peers, output):
        """生成 CycloneDDS 配置文件"""
        config = ctx.obj['config']
        validator = CycloneDDSValidator(config)
        
        interface = interface or config.network.pc_interface
        
        if peers:
            peer_list = [p.strip() for p in peers.split(',')]
        else:
            peer_list = [config.network.robot_wifi_ip]
        
        xml_content = validator.generate_config(
            interface=interface,
            peers=peer_list,
            allow_multicast=False
        )
        
        validator.save_config(xml_content, output)
        rprint(f"[green]配置已生成: {output}[/green]")
        rprint("\n使用方法:")
        rprint(f"  export CYCLONEDDS_URI=file://{os.path.abspath(output)}")

    @cli.command()
    @click.option('--interface', '-i', help='网络接口')
    @click.option('--duration', '-d', default=0, help='监控时长（秒，0 为持续）')
    @click.pass_context
    def monitor(ctx, interface, duration):
        """实时流量监控"""
        config = ctx.obj['config']
        monitor = TrafficMonitor(config)
        
        interface = interface or config.network.pc_interface
        
        rprint(f"[bold]启动实时监控 (接口: {interface})...[/bold]")
        rprint("[dim]按 Ctrl+C 停止[/dim]\n")
        
        if not monitor.start(interface=interface):
            rprint("[red]启动监控失败，可能需要 root 权限[/red]")
            return
        
        try:
            start_time = time.time()
            while True:
                if duration > 0 and time.time() - start_time > duration:
                    break
                
                monitor.print_stats()
                time.sleep(2)
                
        except KeyboardInterrupt:
            rprint("\n[yellow]停止监控...[/yellow]")
        finally:
            monitor.stop()
            monitor.print_flows()

    @cli.command()
    @click.pass_context
    def routes(ctx):
        """显示路由表"""
        config = ctx.obj['config']
        net_diag = NetworkDiagnostics(config)
        
        rprint("[bold]路由表:[/bold]\n")
        
        routes = net_diag.get_routes()
        
        table = Table()
        table.add_column("目标", style="cyan")
        table.add_column("网关")
        table.add_column("接口")
        table.add_column("Metric")
        
        for route in routes:
            table.add_row(
                route.destination,
                route.gateway or "直连",
                route.interface,
                str(route.metric)
            )
        
        console.print(table)
        
        # 检查到机器狗的路由
        rprint(f"\n[bold]到机器狗的路由:[/bold]")
        robot_route = net_diag.check_route_to_host(config.network.robot_wifi_ip)
        if robot_route:
            rprint(f"  {config.network.robot_wifi_ip} via {robot_route.gateway or '直连'} dev {robot_route.interface}")
        
        net_diag.close()

    @cli.command()
    @click.pass_context  
    def interfaces(ctx):
        """显示网络接口信息"""
        config = ctx.obj['config']
        net_diag = NetworkDiagnostics(config)
        
        rprint("[bold]网络接口:[/bold]\n")
        
        interfaces = net_diag.get_interface_info()
        
        for iface in interfaces:
            status = "[green]UP[/green]" if iface.is_up else "[red]DOWN[/red]"
            wireless = " [WiFi]" if iface.is_wireless else ""
            
            rprint(f"[bold cyan]{iface.name}[/bold cyan]{wireless} {status}")
            
            if iface.mac_address:
                rprint(f"  MAC: {iface.mac_address}")
            
            for ip in iface.ipv4_addresses:
                rprint(f"  IPv4: {ip}")
            
            for ip in iface.ipv6_addresses[:2]:  # 只显示前两个 IPv6
                rprint(f"  IPv6: {ip}")
            
            rprint(f"  MTU: {iface.mtu}")
            
            if iface.stats:
                rprint(f"  收包: {iface.stats.get('packets_recv', 0)}, 发包: {iface.stats.get('packets_sent', 0)}")
            
            rprint()
        
        net_diag.close()

    @cli.command()
    @click.option('--duration', '-d', default=10, help='抓包时长（秒）')
    @click.pass_context
    def quicktest(ctx, duration):
        """快速诊断测试"""
        config = ctx.obj['config']
        
        rprint("[bold blue]═══ 快速诊断测试 ═══[/bold blue]\n")
        
        net_diag = NetworkDiagnostics(config)
        
        # 1. Ping 测试
        rprint("[bold]1. 连通性测试[/bold]")
        for target in [config.network.robot_wifi_ip, config.network.robot_eth_ip]:
            result = net_diag.ping(target, count=2)
            status = "[green]✓[/green]" if result.success else "[red]✗[/red]"
            latency = f" ({result.latency_ms:.1f}ms)" if result.latency_ms else ""
            rprint(f"   {status} {target}{latency}")
        
        # 2. DDS 端口
        rprint("\n[bold]2. DDS 端口检测[/bold]")
        dds_ports = [7400, 7410, 7411]
        for port in dds_ports:
            result = net_diag.scan_udp_port(config.network.robot_wifi_ip, port, timeout=1)
            status = "[green]开放[/green]" if result.is_open else "[yellow]未响应[/yellow]" if result.is_open is None else "[red]关闭[/red]"
            rprint(f"   端口 {port}: {status}")
        
        # 3. 抓包测试
        rprint(f"\n[bold]3. 抓包测试 ({duration}秒)[/bold]")
        capture = PacketCapture(config)
        packets = capture.capture_dds_traffic(duration=duration)
        
        rtps_count = sum(1 for p in packets if p.is_rtps)
        from_robot = sum(1 for p in packets if p.src_ip == config.network.robot_wifi_ip)
        to_robot = sum(1 for p in packets if p.dst_ip == config.network.robot_wifi_ip)
        
        rprint(f"   总包数: {len(packets)}")
        rprint(f"   RTPS 包: {rtps_count}")
        rprint(f"   发往机器狗: {to_robot}")
        rprint(f"   来自机器狗: {from_robot}")
        
        # 4. 双向通讯判断
        rprint("\n[bold]4. 双向通讯诊断[/bold]")
        if to_robot > 0 and from_robot > 0:
            rprint("   [green]✓ 双向通讯正常[/green]")
        elif to_robot > 0 and from_robot == 0:
            rprint("   [red]✗ PC 发送正常，但未收到机器狗响应[/red]")
            rprint("   [yellow]可能原因:[/yellow]")
            rprint("   - 机器狗 DDS 绑定在 eth0 而非 wlan0")
            rprint("   - 机器狗防火墙阻止了回包")
            rprint("   - DDS 配置问题")
        else:
            rprint("   [yellow]未检测到与机器狗的通讯[/yellow]")
        
        rprint("\n[bold blue]═══ 诊断完成 ═══[/bold blue]")
        
        net_diag.close()


def main():
    """主入口"""
    if not click or not RICH_AVAILABLE:
        print("错误: 请先安装依赖")
        print("  pip install click rich")
        sys.exit(1)
    
    cli(obj={})


if __name__ == '__main__':
    main()
