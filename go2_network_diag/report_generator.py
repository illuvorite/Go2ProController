"""
诊断报告生成模块
生成 HTML、JSON、TXT 格式的诊断报告
"""

import json
import time
from datetime import datetime
from typing import List, Dict, Any, Optional
from pathlib import Path
import logging

try:
    from jinja2 import Template
    JINJA2_AVAILABLE = True
except ImportError:
    JINJA2_AVAILABLE = False

from .config import DiagConfig
from .network_diag import NetworkDiagResult, PingResult, PortScanResult, RouteInfo, InterfaceInfo
from .dds_analyzer import DDSAnalysisResult, DDSParticipant
from .config_validator import CycloneDDSConfig, ValidationIssue


logger = logging.getLogger(__name__)


# HTML 报告模板
HTML_TEMPLATE = '''
<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Unitree Go2 网络诊断报告</title>
    <style>
        :root {
            --primary-color: #2563eb;
            --success-color: #16a34a;
            --warning-color: #d97706;
            --error-color: #dc2626;
            --bg-color: #f8fafc;
            --card-bg: #ffffff;
            --text-color: #1e293b;
            --border-color: #e2e8f0;
        }
        
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }
        
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, 'Helvetica Neue', Arial, sans-serif;
            background-color: var(--bg-color);
            color: var(--text-color);
            line-height: 1.6;
        }
        
        .container {
            max-width: 1200px;
            margin: 0 auto;
            padding: 20px;
        }
        
        header {
            background: linear-gradient(135deg, var(--primary-color), #1d4ed8);
            color: white;
            padding: 30px;
            border-radius: 12px;
            margin-bottom: 20px;
            box-shadow: 0 4px 6px rgba(0, 0, 0, 0.1);
        }
        
        header h1 {
            font-size: 2rem;
            margin-bottom: 10px;
        }
        
        header .meta {
            opacity: 0.9;
            font-size: 0.9rem;
        }
        
        .card {
            background: var(--card-bg);
            border-radius: 12px;
            padding: 20px;
            margin-bottom: 20px;
            box-shadow: 0 2px 4px rgba(0, 0, 0, 0.05);
            border: 1px solid var(--border-color);
        }
        
        .card h2 {
            font-size: 1.25rem;
            margin-bottom: 15px;
            padding-bottom: 10px;
            border-bottom: 2px solid var(--primary-color);
            color: var(--primary-color);
        }
        
        .card h3 {
            font-size: 1rem;
            margin: 15px 0 10px;
            color: #475569;
        }
        
        .status-badge {
            display: inline-block;
            padding: 4px 12px;
            border-radius: 20px;
            font-size: 0.85rem;
            font-weight: 500;
        }
        
        .status-success {
            background: #dcfce7;
            color: var(--success-color);
        }
        
        .status-warning {
            background: #fef3c7;
            color: var(--warning-color);
        }
        
        .status-error {
            background: #fee2e2;
            color: var(--error-color);
        }
        
        table {
            width: 100%;
            border-collapse: collapse;
            margin: 10px 0;
        }
        
        th, td {
            text-align: left;
            padding: 12px;
            border-bottom: 1px solid var(--border-color);
        }
        
        th {
            background: #f1f5f9;
            font-weight: 600;
            color: #475569;
        }
        
        tr:hover {
            background: #f8fafc;
        }
        
        .issue-list {
            list-style: none;
        }
        
        .issue-list li {
            padding: 10px 15px;
            margin: 8px 0;
            border-radius: 8px;
            display: flex;
            align-items: flex-start;
        }
        
        .issue-list li.error {
            background: #fee2e2;
            border-left: 4px solid var(--error-color);
        }
        
        .issue-list li.warning {
            background: #fef3c7;
            border-left: 4px solid var(--warning-color);
        }
        
        .issue-list li.info {
            background: #dbeafe;
            border-left: 4px solid var(--primary-color);
        }
        
        .recommendation {
            background: #f0fdf4;
            padding: 15px;
            border-radius: 8px;
            margin: 10px 0;
            border-left: 4px solid var(--success-color);
        }
        
        .code-block {
            background: #1e293b;
            color: #e2e8f0;
            padding: 15px;
            border-radius: 8px;
            font-family: 'Consolas', 'Monaco', monospace;
            font-size: 0.9rem;
            overflow-x: auto;
            margin: 10px 0;
        }
        
        .summary-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 15px;
            margin: 15px 0;
        }
        
        .summary-item {
            background: #f1f5f9;
            padding: 15px;
            border-radius: 8px;
            text-align: center;
        }
        
        .summary-item .value {
            font-size: 1.5rem;
            font-weight: 600;
            color: var(--primary-color);
        }
        
        .summary-item .label {
            font-size: 0.85rem;
            color: #64748b;
            margin-top: 5px;
        }
        
        footer {
            text-align: center;
            padding: 20px;
            color: #64748b;
            font-size: 0.85rem;
        }
    </style>
</head>
<body>
    <div class="container">
        <header>
            <h1>🐕 Unitree Go2 网络诊断报告</h1>
            <div class="meta">
                <div>生成时间: {{ timestamp }}</div>
                <div>诊断工具版本: 1.0.0</div>
            </div>
        </header>
        
        <!-- 诊断概要 -->
        <div class="card">
            <h2>📊 诊断概要</h2>
            <div class="summary-grid">
                <div class="summary-item">
                    <div class="value">{{ summary.issues_count }}</div>
                    <div class="label">发现问题</div>
                </div>
                <div class="summary-item">
                    <div class="value">{{ summary.ping_success_rate }}%</div>
                    <div class="label">Ping 成功率</div>
                </div>
                <div class="summary-item">
                    <div class="value">{{ summary.dds_packets }}</div>
                    <div class="label">DDS 数据包</div>
                </div>
                <div class="summary-item">
                    <div class="value">
                        <span class="status-badge {{ 'status-success' if summary.bidirectional else 'status-error' }}">
                            {{ '正常' if summary.bidirectional else '异常' }}
                        </span>
                    </div>
                    <div class="label">双向通讯</div>
                </div>
            </div>
        </div>
        
        <!-- 网络环境 -->
        <div class="card">
            <h2>🌐 网络环境</h2>
            <h3>PC 端</h3>
            <table>
                <tr><th>接口</th><td>{{ network.pc_interface }}</td></tr>
                <tr><th>IP 地址</th><td>{{ network.pc_ip }}</td></tr>
                <tr><th>状态</th><td>
                    <span class="status-badge {{ 'status-success' if network.pc_interface_up else 'status-error' }}">
                        {{ '已启用' if network.pc_interface_up else '未启用' }}
                    </span>
                </td></tr>
            </table>
            
            <h3>机器狗</h3>
            <table>
                <tr><th>WiFi IP</th><td>{{ network.robot_wifi_ip }}</td></tr>
                <tr><th>内网 IP</th><td>{{ network.robot_eth_ip }}</td></tr>
            </table>
        </div>
        
        <!-- Ping 测试结果 -->
        <div class="card">
            <h2>📡 连通性测试</h2>
            <table>
                <thead>
                    <tr>
                        <th>目标</th>
                        <th>状态</th>
                        <th>延迟</th>
                        <th>TTL</th>
                    </tr>
                </thead>
                <tbody>
                    {% for ping in ping_results %}
                    <tr>
                        <td>{{ ping.target }}</td>
                        <td>
                            <span class="status-badge {{ 'status-success' if ping.success else 'status-error' }}">
                                {{ '通' if ping.success else '不通' }}
                            </span>
                        </td>
                        <td>{{ ping.latency_ms|default('-', true) }} ms</td>
                        <td>{{ ping.ttl|default('-', true) }}</td>
                    </tr>
                    {% endfor %}
                </tbody>
            </table>
        </div>
        
        <!-- DDS 端口扫描 -->
        <div class="card">
            <h2>🔌 DDS 端口扫描</h2>
            <table>
                <thead>
                    <tr>
                        <th>端口</th>
                        <th>协议</th>
                        <th>状态</th>
                        <th>服务</th>
                    </tr>
                </thead>
                <tbody>
                    {% for port in port_scan %}
                    <tr>
                        <td>{{ port.port }}</td>
                        <td>{{ port.protocol|upper }}</td>
                        <td>
                            {% if port.is_open == true %}
                            <span class="status-badge status-success">开放</span>
                            {% elif port.is_open == false %}
                            <span class="status-badge status-error">关闭</span>
                            {% else %}
                            <span class="status-badge status-warning">未知</span>
                            {% endif %}
                        </td>
                        <td>{{ port.service|default('-', true) }}</td>
                    </tr>
                    {% endfor %}
                </tbody>
            </table>
        </div>
        
        <!-- DDS 分析 -->
        {% if dds_analysis %}
        <div class="card">
            <h2>📦 DDS/RTPS 分析</h2>
            <div class="summary-grid">
                <div class="summary-item">
                    <div class="value">{{ dds_analysis.total_rtps_packets }}</div>
                    <div class="label">RTPS 数据包</div>
                </div>
                <div class="summary-item">
                    <div class="value">{{ dds_analysis.discovery_pc_to_robot }}</div>
                    <div class="label">PC→狗 Discovery</div>
                </div>
                <div class="summary-item">
                    <div class="value">{{ dds_analysis.discovery_robot_to_pc }}</div>
                    <div class="label">狗→PC Discovery</div>
                </div>
                <div class="summary-item">
                    <div class="value">{{ dds_analysis.participants|length }}</div>
                    <div class="label">DDS 参与者</div>
                </div>
            </div>
            
            {% if dds_analysis.participants %}
            <h3>检测到的 DDS 参与者</h3>
            <table>
                <thead>
                    <tr>
                        <th>GUID</th>
                        <th>IP</th>
                        <th>厂商</th>
                        <th>Discovery</th>
                        <th>Data</th>
                    </tr>
                </thead>
                <tbody>
                    {% for p in dds_analysis.participants %}
                    <tr>
                        <td><code>{{ p.guid_prefix[:16] }}...</code></td>
                        <td>{{ p.ip_address }}:{{ p.port }}</td>
                        <td>{{ p.vendor }}</td>
                        <td>{{ p.discovery_count }}</td>
                        <td>{{ p.data_count }}</td>
                    </tr>
                    {% endfor %}
                </tbody>
            </table>
            {% endif %}
        </div>
        {% endif %}
        
        <!-- CycloneDDS 配置 -->
        {% if cyclone_config %}
        <div class="card">
            <h2>⚙️ CycloneDDS 配置验证</h2>
            <table>
                <tr><th>网络接口</th><td>{{ cyclone_config.network_interface|default('未指定', true) }}</td></tr>
                <tr><th>多播</th><td>{{ '启用' if cyclone_config.allow_multicast else '禁用' }}</td></tr>
                <tr><th>Peers</th><td>{{ cyclone_config.peers|join(', ') if cyclone_config.peers else '无' }}</td></tr>
            </table>
            
            {% if config_issues %}
            <h3>配置问题</h3>
            <ul class="issue-list">
                {% for issue in config_issues %}
                <li class="{{ issue.severity }}">
                    <strong>[{{ issue.element }}]</strong> {{ issue.message }}
                    {% if issue.suggestion %}
                    <br><small>建议: {{ issue.suggestion }}</small>
                    {% endif %}
                </li>
                {% endfor %}
            </ul>
            {% endif %}
        </div>
        {% endif %}
        
        <!-- 路由表 -->
        <div class="card">
            <h2>🛤️ 路由信息</h2>
            <table>
                <thead>
                    <tr>
                        <th>目标</th>
                        <th>网关</th>
                        <th>接口</th>
                    </tr>
                </thead>
                <tbody>
                    {% for route in routes %}
                    <tr>
                        <td>{{ route.destination }}</td>
                        <td>{{ route.gateway|default('-', true) }}</td>
                        <td>{{ route.interface }}</td>
                    </tr>
                    {% endfor %}
                </tbody>
            </table>
        </div>
        
        <!-- 问题与建议 -->
        <div class="card">
            <h2>🔍 诊断结论</h2>
            
            {% if issues %}
            <h3>发现的问题</h3>
            <ul class="issue-list">
                {% for issue in issues %}
                <li class="warning">{{ issue }}</li>
                {% endfor %}
            </ul>
            {% else %}
            <p class="status-badge status-success">未发现明显问题</p>
            {% endif %}
            
            {% if recommendations %}
            <h3>建议操作</h3>
            {% for rec in recommendations %}
            <div class="recommendation">
                {{ loop.index }}. {{ rec }}
            </div>
            {% endfor %}
            {% endif %}
        </div>
        
        <!-- 推荐配置 -->
        {% if suggested_config %}
        <div class="card">
            <h2>📝 推荐的 CycloneDDS 配置</h2>
            <p>将以下内容保存为 <code>cyclonedds.xml</code> 并设置环境变量 <code>CYCLONEDDS_URI=file://path/to/cyclonedds.xml</code></p>
            <div class="code-block">{{ suggested_config }}</div>
        </div>
        {% endif %}
        
        <footer>
            <p>Unitree Go2 网络诊断工具 | 生成于 {{ timestamp }}</p>
        </footer>
    </div>
</body>
</html>
'''


class ReportGenerator:
    """诊断报告生成器"""
    
    def __init__(self, config: DiagConfig = None):
        self.config = config or DiagConfig()
        self._data = {}
    
    def set_network_result(self, result: NetworkDiagResult):
        """设置网络诊断结果"""
        self._data['network_result'] = result
    
    def set_dds_analysis(self, result: DDSAnalysisResult):
        """设置 DDS 分析结果"""
        self._data['dds_analysis'] = result
    
    def set_cyclone_config(
        self, 
        config: CycloneDDSConfig, 
        issues: List[ValidationIssue]
    ):
        """设置 CycloneDDS 配置验证结果"""
        self._data['cyclone_config'] = config
        self._data['config_issues'] = issues
    
    def set_bidirectional_result(self, result: Dict[str, Any]):
        """设置双向通讯分析结果"""
        self._data['bidirectional'] = result
    
    def set_suggested_config(self, xml_content: str):
        """设置推荐配置"""
        self._data['suggested_config'] = xml_content
    
    def _prepare_template_data(self) -> Dict[str, Any]:
        """准备模板数据"""
        data = {
            'timestamp': datetime.now().strftime('%Y-%m-%d %H:%M:%S'),
            'summary': {
                'issues_count': 0,
                'ping_success_rate': 0,
                'dds_packets': 0,
                'bidirectional': False
            },
            'network': {
                'pc_interface': self.config.network.pc_interface,
                'pc_ip': self.config.network.pc_ip,
                'pc_interface_up': True,
                'robot_wifi_ip': self.config.network.robot_wifi_ip,
                'robot_eth_ip': self.config.network.robot_eth_ip
            },
            'ping_results': [],
            'port_scan': [],
            'routes': [],
            'issues': [],
            'recommendations': []
        }
        
        # 网络诊断结果
        if 'network_result' in self._data:
            nr = self._data['network_result']
            
            data['network']['pc_interface_up'] = nr.pc_interface.is_up
            
            # Ping 结果
            data['ping_results'] = [
                {
                    'target': p.target,
                    'success': p.success,
                    'latency_ms': f"{p.latency_ms:.2f}" if p.latency_ms else None,
                    'ttl': p.ttl
                }
                for p in nr.ping_results
            ]
            
            # 计算 ping 成功率
            if nr.ping_results:
                success_count = sum(1 for p in nr.ping_results if p.success)
                data['summary']['ping_success_rate'] = int(
                    success_count / len(nr.ping_results) * 100
                )
            
            # 端口扫描
            data['port_scan'] = [
                {
                    'port': p.port,
                    'protocol': p.protocol,
                    'is_open': p.is_open,
                    'service': p.service
                }
                for p in nr.port_scan_results
            ]
            
            # 路由
            data['routes'] = [
                {
                    'destination': r.destination,
                    'gateway': r.gateway,
                    'interface': r.interface
                }
                for r in nr.routes[:20]  # 限制显示数量
            ]
            
            data['issues'].extend(nr.issues_found)
            data['recommendations'].extend(nr.recommendations)
        
        # DDS 分析结果
        if 'dds_analysis' in self._data:
            da = self._data['dds_analysis']
            
            data['dds_analysis'] = {
                'total_rtps_packets': da.total_rtps_packets,
                'total_submessages': da.total_submessages,
                'discovery_pc_to_robot': da.discovery_packets_pc_to_robot,
                'discovery_robot_to_pc': da.discovery_packets_robot_to_pc,
                'participants': [
                    {
                        'guid_prefix': p.guid_prefix,
                        'ip_address': p.ip_address,
                        'port': p.port,
                        'vendor': p.vendor,
                        'discovery_count': p.discovery_count,
                        'data_count': p.data_count
                    }
                    for p in da.participants
                ]
            }
            
            data['summary']['dds_packets'] = da.total_rtps_packets
            data['issues'].extend(da.issues)
            data['recommendations'].extend(da.recommendations)
        
        # CycloneDDS 配置
        if 'cyclone_config' in self._data:
            cc = self._data['cyclone_config']
            data['cyclone_config'] = {
                'network_interface': cc.network_interface,
                'allow_multicast': cc.allow_multicast,
                'peers': cc.peers
            }
        
        if 'config_issues' in self._data:
            data['config_issues'] = [
                {
                    'severity': i.severity,
                    'element': i.element,
                    'message': i.message,
                    'suggestion': i.suggestion
                }
                for i in self._data['config_issues']
            ]
        
        # 双向通讯
        if 'bidirectional' in self._data:
            data['summary']['bidirectional'] = self._data['bidirectional'].get('bidirectional', False)
        
        # 推荐配置
        if 'suggested_config' in self._data:
            data['suggested_config'] = self._data['suggested_config']
        
        # 更新问题计数
        data['summary']['issues_count'] = len(data['issues'])
        
        # 去重
        data['issues'] = list(dict.fromkeys(data['issues']))
        data['recommendations'] = list(dict.fromkeys(data['recommendations']))
        
        return data
    
    def generate_html(self, output_path: str = None) -> str:
        """生成 HTML 报告"""
        if not JINJA2_AVAILABLE:
            logger.error("Jinja2 未安装，无法生成 HTML 报告")
            return ""
        
        template = Template(HTML_TEMPLATE)
        data = self._prepare_template_data()
        html_content = template.render(**data)
        
        if output_path:
            Path(output_path).parent.mkdir(parents=True, exist_ok=True)
            with open(output_path, 'w', encoding='utf-8') as f:
                f.write(html_content)
            logger.info(f"HTML 报告已保存到 {output_path}")
        
        return html_content
    
    def generate_json(self, output_path: str = None) -> str:
        """生成 JSON 报告"""
        data = self._prepare_template_data()
        json_content = json.dumps(data, ensure_ascii=False, indent=2)
        
        if output_path:
            Path(output_path).parent.mkdir(parents=True, exist_ok=True)
            with open(output_path, 'w', encoding='utf-8') as f:
                f.write(json_content)
            logger.info(f"JSON 报告已保存到 {output_path}")
        
        return json_content
    
    def generate_txt(self, output_path: str = None) -> str:
        """生成纯文本报告"""
        data = self._prepare_template_data()
        
        lines = [
            "=" * 70,
            "Unitree Go2 网络诊断报告",
            "=" * 70,
            f"生成时间: {data['timestamp']}",
            "",
            "--- 诊断概要 ---",
            f"发现问题: {data['summary']['issues_count']}",
            f"Ping 成功率: {data['summary']['ping_success_rate']}%",
            f"DDS 数据包: {data['summary']['dds_packets']}",
            f"双向通讯: {'正常' if data['summary']['bidirectional'] else '异常'}",
            "",
            "--- 网络环境 ---",
            f"PC 接口: {data['network']['pc_interface']}",
            f"PC IP: {data['network']['pc_ip']}",
            f"机器狗 WiFi IP: {data['network']['robot_wifi_ip']}",
            f"机器狗内网 IP: {data['network']['robot_eth_ip']}",
            "",
            "--- Ping 测试 ---",
        ]
        
        for ping in data['ping_results']:
            status = "通" if ping['success'] else "不通"
            latency = f", 延迟={ping['latency_ms']}ms" if ping['latency_ms'] else ""
            lines.append(f"  {ping['target']}: {status}{latency}")
        
        lines.extend([
            "",
            "--- DDS 端口扫描 ---",
        ])
        
        for port in data['port_scan']:
            if port['is_open']:
                lines.append(f"  {port['port']}/{port['protocol']}: 开放")
        
        if data.get('dds_analysis'):
            da = data['dds_analysis']
            lines.extend([
                "",
                "--- DDS 分析 ---",
                f"RTPS 数据包: {da['total_rtps_packets']}",
                f"PC→狗 Discovery: {da['discovery_pc_to_robot']}",
                f"狗→PC Discovery: {da['discovery_robot_to_pc']}",
            ])
        
        if data['issues']:
            lines.extend([
                "",
                "--- 发现的问题 ---",
            ])
            for i, issue in enumerate(data['issues'], 1):
                lines.append(f"  {i}. {issue}")
        
        if data['recommendations']:
            lines.extend([
                "",
                "--- 建议 ---",
            ])
            for i, rec in enumerate(data['recommendations'], 1):
                lines.append(f"  {i}. {rec}")
        
        lines.extend([
            "",
            "=" * 70,
        ])
        
        txt_content = "\n".join(lines)
        
        if output_path:
            Path(output_path).parent.mkdir(parents=True, exist_ok=True)
            with open(output_path, 'w', encoding='utf-8') as f:
                f.write(txt_content)
            logger.info(f"TXT 报告已保存到 {output_path}")
        
        return txt_content
    
    def generate(self, format: str = 'html', output_path: str = None) -> str:
        """生成指定格式的报告"""
        if format == 'html':
            return self.generate_html(output_path)
        elif format == 'json':
            return self.generate_json(output_path)
        elif format == 'txt':
            return self.generate_txt(output_path)
        else:
            raise ValueError(f"不支持的报告格式: {format}")
    
    def generate_all(self, output_dir: str = None):
        """生成所有格式的报告"""
        output_dir = output_dir or self.config.report_dir
        Path(output_dir).mkdir(parents=True, exist_ok=True)
        
        timestamp = time.strftime("%Y%m%d_%H%M%S")
        
        self.generate_html(f"{output_dir}/report_{timestamp}.html")
        self.generate_json(f"{output_dir}/report_{timestamp}.json")
        self.generate_txt(f"{output_dir}/report_{timestamp}.txt")
        
        logger.info(f"所有报告已生成到 {output_dir}")
