#!/usr/bin/env python3
"""
简单诊断示例
展示如何在 Python 代码中使用诊断工具
"""

import sys
import os
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from go2_network_diag import (
    DiagConfig,
    NetworkDiagnostics,
    PacketCapture,
    DDSAnalyzer,
    CycloneDDSValidator,
    ReportGenerator
)


def main():
    # 创建配置
    config = DiagConfig()
    
    # 可以自定义配置
    config.network.pc_interface = "wlo1"
    config.network.robot_wifi_ip = "192.168.1.211"
    
    print("=" * 50)
    print("Unitree Go2 网络诊断示例")
    print("=" * 50)
    
    # 1. 网络诊断
    print("\n1. 网络连通性检测")
    print("-" * 30)
    
    net_diag = NetworkDiagnostics(config)
    
    # Ping 测试
    result = net_diag.ping(config.network.robot_wifi_ip)
    if result.success:
        print(f"✓ Ping {config.network.robot_wifi_ip}: 成功, 延迟={result.latency_ms:.2f}ms")
    else:
        print(f"✗ Ping {config.network.robot_wifi_ip}: 失败 - {result.error}")
    
    # 端口扫描
    print("\n2. DDS 端口扫描")
    print("-" * 30)
    
    for port in [7400, 7410, 7411]:
        result = net_diag.scan_udp_port(config.network.robot_wifi_ip, port, timeout=1)
        status = "开放" if result.is_open else "未知" if result.is_open is None else "关闭"
        print(f"  端口 {port}: {status}")
    
    # 3. CycloneDDS 配置验证
    print("\n3. 生成推荐的 CycloneDDS 配置")
    print("-" * 30)
    
    validator = CycloneDDSValidator(config)
    xml_config = validator.generate_go2_wifi_config()
    print(xml_config)
    
    # 4. 完整诊断（需要 root 权限进行抓包）
    print("\n4. 运行完整诊断")
    print("-" * 30)
    
    full_result = net_diag.run_full_diagnosis()
    
    if full_result.issues_found:
        print("发现的问题:")
        for issue in full_result.issues_found:
            print(f"  - {issue}")
    else:
        print("未发现明显问题")
    
    if full_result.recommendations:
        print("\n建议:")
        for rec in full_result.recommendations:
            print(f"  → {rec}")
    
    # 5. 生成报告
    print("\n5. 生成诊断报告")
    print("-" * 30)
    
    report_gen = ReportGenerator(config)
    report_gen.set_network_result(full_result)
    report_gen.set_suggested_config(xml_config)
    
    # 生成文本报告
    txt_report = report_gen.generate_txt()
    print(txt_report[:500] + "...")
    
    # 保存 HTML 报告
    # report_gen.generate_html("report.html")
    
    net_diag.close()
    
    print("\n诊断完成！")


if __name__ == "__main__":
    main()
