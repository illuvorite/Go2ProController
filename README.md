# Unitree Go2 网络诊断工具

🐕 专为 Unitree Go2 机器狗设计的网络诊断工具，用于排查无线通讯异常问题。

## 📋 背景

在进行 Unitree Go2 机器狗二次开发时，可能会遇到以下问题：

- 有线连接（192.168.123.x）正常，但无线连接（192.168.1.x）异常
- 网络层（ICMP）通畅，但应用层（DDS）无数据回传
- PC 端能 ping 通机器狗，但 DDS SDK 无法正常通讯

本工具旨在帮助定位和诊断这类问题。

## ✨ 功能特性

- **网络连通性检测**
  - ICMP Ping 测试
  - UDP/TCP 端口扫描
  - 路由表分析
  - 网络接口信息

- **DDS/RTPS 协议分析**
  - 实时包捕获
  - RTPS 协议解析
  - DDS 参与者发现
  - 子消息类型统计

- **CycloneDDS 配置**
  - 配置文件验证
  - 自动生成适配配置
  - 跨网段配置建议

- **双向通讯诊断**
  - PC→机器狗流量分析
  - 机器狗→PC流量分析
  - 通讯问题定位

- **报告生成**
  - HTML 格式（美观）
  - JSON 格式（程序处理）
  - TXT 格式（简洁）

## 🚀 快速开始

### 安装

```bash
# 克隆项目
cd /path/to/network_get

# 安装依赖
pip install -r requirements.txt

# 或使用 pip 安装（开发模式）
pip install -e .
```

### 基本使用

```bash
# 运行完整诊断
sudo python main.py diagnose

# 快速测试
sudo python main.py quicktest

# 查看帮助
python main.py --help
```

## 📖 命令详解

### 1. 完整诊断

```bash
sudo python main.py diagnose [OPTIONS]

选项:
  -i, --interface TEXT   网络接口 (默认: wlo1)
  --robot-ip TEXT        机器狗 WiFi IP (默认: 192.168.1.211)
  -r, --report TEXT      报告格式 [html|json|txt|all]
  -o, --output-dir TEXT  报告输出目录
```

### 2. Ping 测试

```bash
sudo python main.py ping -h <目标IP> [-c 次数]

示例:
  sudo python main.py ping -h 192.168.1.211 -c 4
```

### 3. 端口扫描

```bash
sudo python main.py portscan [OPTIONS]

选项:
  -h, --host TEXT   目标主机 IP
  -p, --ports TEXT  端口列表（逗号分隔）

示例:
  sudo python main.py portscan -h 192.168.1.211 -p 7400,7410,7411
```

### 4. 抓包分析

```bash
sudo python main.py capture [OPTIONS]

选项:
  -i, --interface TEXT  网络接口
  -d, --duration INT    捕获时长（秒）
  -f, --filter TEXT     BPF 过滤器
  -s, --save            保存为 PCAP 文件

示例:
  sudo python main.py capture -d 10 -s
```

### 5. 配置验证

```bash
python main.py validate <配置文件路径>

示例:
  python main.py validate cyclonedds.xml
```

### 6. 生成配置

```bash
python main.py genconfig [OPTIONS]

选项:
  -i, --interface TEXT  网络接口
  -p, --peers TEXT      Peer 地址（逗号分隔）
  -o, --output TEXT     输出文件路径

示例:
  python main.py genconfig -o my_cyclonedds.xml
```

### 7. 实时监控

```bash
sudo python main.py monitor [OPTIONS]

选项:
  -i, --interface TEXT  网络接口
  -d, --duration INT    监控时长（秒，0 为持续）
```

### 8. 快速测试

```bash
sudo python main.py quicktest [-d 抓包时长]
```

## 🔧 配置说明

### 默认配置

工具默认使用以下网络配置：

| 参数 | 默认值 | 说明 |
|------|--------|------|
| PC 无线接口 | wlo1 | PC 端无线网卡名称 |
| PC IP | 192.168.1.106 | PC 的无线 IP |
| 机器狗 WiFi IP | 192.168.1.211 | Go2 的无线接口 IP |
| 机器狗内网 IP | 192.168.123.161 | Go2 内部运控 PC IP |

### 自定义配置

创建 `config.yaml` 文件：

```yaml
network:
  pc_interface: wlo1
  pc_ip: 192.168.1.106
  robot_wifi_ip: 192.168.1.211
  robot_eth_ip: 192.168.123.161

dds:
  common_ports: [7400, 7401, 7410, 7411, 7412, 7413, 7414, 7415, 7416, 7417]

capture:
  capture_duration: 30
  save_pcap: true
  pcap_dir: ./captures

report_dir: ./reports
```

使用自定义配置：

```bash
sudo python main.py -c config.yaml diagnose
```

## 🔍 常见问题诊断

### 问题1：PC 能 ping 通机器狗，但 DDS 无数据

**可能原因：**

1. **DDS 接口绑定问题**
   - 机器狗内部 DDS 可能只绑定在 eth0 接口
   - 解决：检查机器狗的 DDS 配置

2. **多播配置问题**
   - 跨网段通讯需要禁用多播
   - 解决：在 cyclonedds.xml 中设置 `<AllowMulticast>false</AllowMulticast>`

3. **Peer 配置缺失**
   - 需要显式指定通讯对端
   - 解决：添加 `<Peer address="192.168.1.211"/>`

### 问题2：添加静态路由后仍无法通讯

```bash
# 添加到机器狗内网的路由
sudo ip route add 192.168.123.0/24 via 192.168.1.211
```

如果仍然不通，检查：
- 机器狗是否开启了 IP 转发
- 机器狗内部防火墙规则

### 问题3：RTPS Discovery 包发出但无响应

运行诊断工具查看双向流量：

```bash
sudo python main.py capture -d 10
```

如果只有 PC→机器狗的包，没有回包，说明：
- 机器狗 DDS 服务可能未在 wlan0 监听
- 需要修改机器狗端的 DDS 配置

## 📝 推荐的 CycloneDDS 配置

```xml
<?xml version="1.0" encoding="UTF-8"?>
<CycloneDDS xmlns="https://cdds.io/config">
    <Domain id="any">
        <General>
            <!-- 指定无线网卡 -->
            <NetworkInterfaceAddress>wlo1</NetworkInterfaceAddress>
            
            <!-- 跨网段必须禁用多播 -->
            <AllowMulticast>false</AllowMulticast>
        </General>
        
        <Discovery>
            <!-- 单播发现对端 -->
            <Peers>
                <Peer address="192.168.1.211"/>
            </Peers>
            <ParticipantIndex>auto</ParticipantIndex>
        </Discovery>
    </Domain>
</CycloneDDS>
```

设置环境变量：

```bash
export CYCLONEDDS_URI=file:///path/to/cyclonedds.xml
```

## 📊 诊断报告示例

运行诊断后会生成详细报告：

```
═══════════════════════════════════════════════════════════════
Unitree Go2 网络诊断报告
═══════════════════════════════════════════════════════════════
生成时间: 2024-01-15 14:30:00

--- 诊断概要 ---
发现问题: 2
Ping 成功率: 50%
DDS 数据包: 156
双向通讯: 异常

--- 发现的问题 ---
1. PC 发送了 RTPS 包，但未收到机器狗的 RTPS 响应
2. 无法 ping 通 192.168.123.161

--- 建议 ---
1. 添加静态路由: sudo ip route add 192.168.123.0/24 via 192.168.1.211
2. 检查机器狗 DDS 的网络接口绑定配置
═══════════════════════════════════════════════════════════════
```

## 🛠️ 开发

### 项目结构

```
network_get/
├── go2_network_diag/
│   ├── __init__.py          # 包初始化
│   ├── config.py             # 配置模块
│   ├── network_diag.py       # 网络诊断核心
│   ├── packet_capture.py     # 包捕获模块
│   ├── dds_analyzer.py       # DDS 分析模块
│   ├── config_validator.py   # 配置验证模块
│   ├── traffic_monitor.py    # 流量监控模块
│   ├── report_generator.py   # 报告生成器
│   └── cli.py                # 命令行接口
├── main.py                   # 主程序入口
├── setup.py                  # 安装配置
├── requirements.txt          # 依赖列表
├── README.md                 # 说明文档
└── usage.md                  # 原始需求文档
```

### 运行测试

```bash
pip install -e .[dev]
pytest
```

## 📄 许可证

MIT License

## 🤝 贡献

欢迎提交 Issue 和 Pull Request！

## 📞 联系

如有问题，请提交 Issue。
