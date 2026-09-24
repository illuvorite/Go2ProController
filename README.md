# Go2ProController · Unitree Go2 控制与网络诊断工具集

面向 **Unitree Go2 系列机器狗**（Air / Pro / EDU）的二次开发工具集，包含两个可独立使用的组件：

| 组件 | 语言 | 作用 |
|---|---|---|
| **[`client/`](client/)** — Go2 控制管理台 | C++17 + Dear ImGui | 局域网自动发现、多机管理、**单控 / 群控**、双摇杆遥控、急停、完整动作库；编译为**单个可执行文件**，无运行时依赖 |
| **[`go2_network_diag/`](go2_network_diag/)** + [`main.py`](main.py) — 网络诊断 CLI | Python 3.8+ | 连通性检测、抓包与 DDS/RTPS 分析、**WebRTC 控制链路体检**、HTML/JSON/TXT 报告 |

> 背景：Go2 的控制链路在不同固件下差异很大（信令端口 9991 / 8081、`data2=1/2/3` 三种加密模式），
> 且官方 SDK 走 DDS（新固件已加密）。本工具集把 **WebRTC 控制通道** 与 **网络层诊断** 两条路都打通，
> 并沉淀了完整实测结论：见 [`docs/go2_webrtc_protocol.md`](docs/go2_webrtc_protocol.md)、
> [`docs/multi_go2_pro_solution.md`](docs/multi_go2_pro_solution.md)。

---

## 目录

- [功能特性](#功能特性)
- [环境要求](#环境要求)
- [安装](#安装)
- [使用说明](#使用说明)
- [目录结构](#目录结构)
- [配置项说明](#配置项说明)
- [常见问题（FAQ）](#常见问题faq)
- [辅助工具](#辅助工具)
- [许可证](#许可证)

---

## 功能特性

### 1. Go2 控制管理台（`client/`）

**设备发现与多机管理**

- 局域网自动发现：本机所有网段扫描（并发探测 9991/8081 信令端口）+ **UDP 多播按 SN 发现**（`231.1.1.1:10131`）
- 手动添加（支持一次粘贴多个 IP，逗号/分号/空格分隔）
- 多设备列表：勾选即受控 —— **勾 1 台 = 单控，勾多台 = 群控**（所有动作/遥控/急停都按勾选分发）；
  顶栏「单控 / 群控」一键切换（单控=只留一台，群控=全选），设备卡片上也有「单控」快捷按钮
- 每台狗可**改名**：设备卡片「改名」→ 名字显示在设备卡片与摇杆带上，存 `robot_names.json`（下次启动还在）
- 错峰连接（机器狗信令服务为单线程 accept，同时握手会互相堵死）
- 掉线自动重连（指数退避，上限 30s）、链路统计（RTT / 收包 / 心跳 / 在线时长 / 重连次数）

**连接与安全兼容（已实测）**

- 新固件 `data2=3`（Go2 ≥ 1.1.15）：`:9991` 加密信令（RSA + AES），需要**每设备 AES-128 钥匙**
- 旧固件 `data2=1/2`：`:8081` 明文信令 / 内置静态钥匙，无需设备钥匙
- 校验握手（`md5("UnitreeGo2_" + key)`）、2 秒心跳保活
- **Go2 兼容补丁**：Go2 的 WebRTC 栈不回 DCEP ACK，而 libdatachannel 会因此永远不"打开"数据通道
  → [`client/patches/apply_go2_fix.cmake`](client/patches/) 在构建时自动打补丁（幂等）
- 隐私模式：界面与日志里的 IP 可一键打码

**遥控**

- **双摇杆**：左杆 = 全向平移（前后 / 左右 / 斜向），右杆 = 原地转向；10Hz 持续下发，**松手即停**
- 速度上限、转向角速度可调；快捷方向按钮（前进 / 后退 / 左转 / 右转）
- **急停（锁定式）**：一键停速度 + **逐个关闭全部"持续模式"开关**（自由行走 / 领航跟随 / 交叉步 …）
  + 补发 StopMove；锁定期间摇杆与快捷步一律不下发；`空格键` 随时触发
- **强制阻尼（Damp）**：兜底手段（狗会软腿趴下），二次确认
- 断开连接前自动停车

**界面布局（上下两块）**

- 上半 = **页面区**：顶栏页签在「遥控 / 动作库」两页间切换，**两页都铺满整屏宽**。
  **动作库是常驻整屏页面**（不是弹窗）：按整屏宽度平铺按钮（最多 8 列）；
  常用姿势/表演类动作在上，**步态 / 速度 / 身高（带滑条的那组）排在最下面**
- 下半 = **摇杆带**：双摇杆恒悬浮在屏幕两个下角（半径按短边 0.15 取、上限 88dp）；
  **两杆中间是「单控 / 群控」面板** —— 点「单控」弹出设备列表挑一台，点「群控」全选，
  下面一行显示当前受控对象（`单控 · 名字` / `群控 · N 台`）
- 顶栏一行 6 个按钮：`遥控` `动作库` · `设备` `设置` `日志` · **`■ 急停`**
  （急停在两个页面都能一眼看到；`空格键` 同样随时急停；窄屏自动拆两行）
- 控件是**苹果风格滑条**：灰色胶囊轨道 + 主色填充段 + 白色圆形旋钮，数值居中显示
- 其余弹窗（设备 / 设置 / 日志）打开时**盖住摇杆带**：摇杆不画、不响应、数值清零
  （避免"眼睛看弹窗、手指还推着摇杆"）
- 触摸下**按住任意位置拖动即可滚动**（不用去抓右边的滚动条）

**动作库**

- 宇树动作库 50+ 条指令，按 6 组分类（基础姿态 / 表演动作 / 步态·速度·身高 / 跳跃特技 / 状态查询 / 其他）
- **normal 与 MCF 双指令集**自动匹配：发送错指令集被拒时**自动换另一套 api_id 重试一次**
- 参数类型自适应（无参 / 开关 `{"data":bool}` / 整数 / 浮点 / 姿态角 / 自定义 JSON）
- **可用性标注**：每条动作显示 ✓ / ✗（上次回执结果，悬停看失败原因）；「隐藏不支持的」可过滤固件没有的动作
- 开关型指令（自由行走 / 经典步态 / 倒立 / 跳跃奔跑 …）画成 `[开] / [关]` 按钮，急停时自动全部关闭
- **官方 App 快捷**：机身高度三档、姿态（左倾 / 右倾 / 低头 / 抬头 / 方向回正，自动进入"摆姿势"模式）、
  运动模式（normal / ai / mcf）、舞蹈编排（舞 1 → 舞 2）

**其它**

- 运行日志分级着色 + 「只看异常」过滤；界面主题集中在 [`client/src/theme.cpp`](client/src/theme.cpp)（深色 + 三级中文字号）
- 无界面验证模式：连接稳定性、动作回执校验、**动作可用性探测**（只发原地不动的安全指令）

### 2. 网络诊断 CLI（`main.py`）

- **连通性**：ICMP Ping、TCP/UDP 端口扫描、路由表与网卡信息
- **抓包分析**：实时抓包、RTPS/DDS 子消息解析与统计、双向流量对比
- **WebRTC 控制链路体检**（`webrtc` 子命令，多机排查首选）：双信令端口探测、`con_notify` 握手与 `data2` 判定、
  SN 多播发现、逐台给出结论与建议
- **CycloneDDS 配置**：校验现有配置 / 按网卡与对端自动生成
- **报告**：HTML（美观）/ JSON（程序处理）/ TXT（简洁）

---

## 环境要求

### 控制台（`client/`）

| 项 | 要求 |
|---|---|
| 系统 | **Ubuntu 22.04 / WSL2**（源码为 POSIX，实机验证于 WSL2 + WSLg；Windows 原生不支持） |
| 编译器 | g++ / clang++（**C++17**） |
| 构建 | CMake ≥ 3.16、Ninja（或 make） |
| 依赖库 | OpenSSL、OpenGL（系统包）；libdatachannel / Dear ImGui / GLFW / nlohmann-json **构建时自动拉取** |
| 网络 | 与机器狗同网段（WiFi 或网口直连 `192.168.123.x`） |
| 显示 | Linux 桌面或 **WSLg**（Windows 11 / Win10 带 WSLg） |

### 诊断工具（`go2_network_diag/`）

| 项 | 要求 |
|---|---|
| Python | ≥ 3.8（推荐 3.10+） |
| 依赖 | 见 [`requirements.txt`](requirements.txt)（scapy / pyshark / rich / pandas …） |
| 权限 | 抓包与分析需要 `sudo`（或具备 `CAP_NET_RAW`） |

---

## 安装

### 1. 获取代码

```bash
git clone <repo-url> Go2ProController
cd Go2ProController
```

### 2. 构建控制台

```bash
cd client
cmake -S . -B build -G Ninja
cmake --build build -j
```

> 首次构建会自动拉取并编译依赖（约 200MB，需要网络），并自动应用 Go2 兼容补丁。
> 产物：`client/build/go2_remote`（单文件可执行，约 3 MB）。

自测（不需要机器狗）：

```bash
./build/crypto_test      # 加密/信令链路自测（用真实抓包数据）
./build/motion_test      # 双摇杆 + 急停决策自测（17 项）
```

### 3. 安装诊断工具

```bash
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
# 或安装为可调用命令： .venv/bin/pip install -e .
```

### 4. Windows 一键启动（可选）

双击 [`start_go2.bat`](start_go2.bat)：自动重置 WSLg → 启动控制台 → 日志写入 `last_run.log`
（`client/build/go2_remote` 需要先构建好）。

---

## 使用说明

### A. 控制台（图形界面）

```bash
cd client
./build/go2_remote 192.168.0.169            # 启动并自动连接指定机器狗
./build/go2_remote                          # 启动后手动扫描 / 输入 IP
```

典型流程：

1. **连接**：启动会自动连接命令行给的 IP；否则点顶栏「设备」→「扫描局域网」或输入 IP 后点「添加」
2. **选择受控对象**：点**两个摇杆中间**的「单控」（弹出设备列表选一台）/「群控」（全选），
   或在「设备」弹窗里逐台勾选；面板下方会显示当前是 `单控 · 名字` 还是 `群控 · N 台`
3. **遥控**：顶栏「遥控」页拖左杆走位、右杆转向，松手即停；`空格键` 或「■ 急停」立即停车
4. **动作**：顶栏「动作库」页（常驻整屏）点各类动作；开关型指令点一次开、再点一次关
5. **改名**：顶栏「设备」→ 卡片上的「改名」（留空 = 恢复显示 IP）
6. **急停后**：把两个摇杆回中 → 点「解除急停」才能继续遥控

### B. 控制台（无界面验证 / 自检）

```bash
cd client/build
./go2_remote --verify 192.168.0.169 --seconds 60              # 连接稳定性报告
./go2_remote --verify 192.168.0.169 --actions                 # 校验动作回执（站立→打招呼→停止）
./go2_remote --verify 192.168.0.169 --probe --seconds 80      # 动作可用性探测（只发原地不动的安全指令）
./go2_remote --help                                           # 全部选项
```

### C. 诊断工具

```bash
# 完整诊断（需要 sudo 抓包）
sudo .venv/bin/python main.py diagnose -i wlo1 --robot-ip 192.168.0.169 -r all

# WebRTC 控制链路体检（不需要 sudo，多机排查首选）
.venv/bin/python main.py webrtc -i 192.168.0.169,192.168.123.161
.venv/bin/python main.py webrtc --scan --subnet 192.168.0.0/23

# 抓包 / 监控 / 配置校验 / 生成配置
sudo .venv/bin/python main.py capture -d 10 -s
sudo .venv/bin/python main.py monitor -i wlo1
.venv/bin/python main.py validate examples/cyclonedds_wifi.xml
.venv/bin/python main.py genconfig -i wlo1 -o my_cyclonedds.xml

# 其它：ping / portscan / quicktest
sudo .venv/bin/python main.py quicktest
```

完整的子命令与选项：`python main.py --help`。

---

## 目录结构

```
Go2ProController/
├── client/                          # ★ Go2 控制管理台（C++17 + Dear ImGui，分层架构）
│   ├── CMakeLists.txt               # 构建：go2_core(静态库) + go2_remote(界面) + 自测
│   ├── CMakePresets.json            # 预设：linux / windows-msvc(vcpkg) / macos-universal / android-arm64
│   ├── vcpkg.json                   # Windows / macOS 依赖清单（Linux 走系统包 + FetchContent）
│   ├── core/                        # ★ 纯逻辑层（禁止出现平台头，跨平台可复用）
│   │   ├── robot_client.{hpp,cpp}   #   单机 WebRTC 通道（信令→ICE→DTLS→SCTP→指令/回执）
│   │   ├── robot_manager.{hpp,cpp}  #   多机管理（错峰连接 / 自动重连 / 钥匙绑定）
│   │   ├── signaling.{hpp,cpp}      #   9991 / 8081 信令与加密
│   │   ├── crypto.{hpp,cpp}         #   MD5 / AES-GCM / Base64 / RSA
│   │   ├── discovery.{hpp,cpp}      #   局域网发现（网段扫描 + SN 多播）
│   │   ├── sport_library.{hpp,cpp}  #   宇树动作指令表（normal / MCF 双指令集）
│   │   ├── motion.hpp               #   双摇杆 + 急停决策（纯函数，便于自测）
│   │   ├── local_keys.{hpp,cpp}     #   每设备 AES 钥匙加载（项目外安全存储）
│   │   └── unitree_cloud.{hpp,cpp}  #   宇树云接口（可选，需自行运行）
│   ├── platform/
│   │   └── net.hpp                  # ★ 平台网络层唯一接缝（POSIX / Winsock 分支 + 归一化 helper）
│   ├── ui/                          # ★ 界面层
│   │   ├── ui.{hpp,cpp}             #   设备卡片 / 双摇杆 / 动作库 / 日志
│   │   └── theme.{hpp,cpp}          #   配色 / 圆角 / 三级中文字号
│   ├── apps/desktop/main.cpp        # 桌面入口（图形界面 / 无界面验证模式）
│   ├── apps/android/                # ★ Android 端（SDL2 + GLES3 + 同一份 core/ui）
│   │   ├── setup.sh                 #   一键装配（下载 SDL2/ImGui + 展开 Gradle 工程 + 注入配置）
│   │   ├── native/                  #   main_android.cpp + 原生 CMake
│   │   └── README.md                #   构建步骤 / 依赖（vcpkg arm64-android）/ 常见报错
│   ├── patches/apply_go2_fix.cmake  # Go2 兼容补丁：DCEP ACK（构建时自动应用，幂等）
│   ├── tests/                       # crypto / motion / discovery 自测
│   └── README.md                    # 控制台详细说明
├── go2_network_diag/                 # ★ Python 诊断包
│   ├── cli.py                        # 命令行接口（diagnose / webrtc / capture / monitor …）
│   ├── network_diag.py               # 连通性诊断核心
│   ├── packet_capture.py             # 抓包
│   ├── dds_analyzer.py               # DDS/RTPS 分析
│   ├── webrtc_check.py               # WebRTC 控制链路体检（双端口 / data2 / 钥匙）
│   ├── config_validator.py           # CycloneDDS 配置校验与生成
│   ├── traffic_monitor.py            # 实时流量监控
│   └── report_generator.py           # 报告生成（HTML / JSON / TXT）
├── docs/
│   ├── go2_webrtc_protocol.md        # WebRTC 协议实测记录（信令 / SDP / 加密 / 指令格式）
│   └── multi_go2_pro_solution.md     # 多机方案：原因分析 / 排查过程 / 钥匙路线 / 验证
├── examples/                         # 诊断工具示例配置（config.yaml / cyclonedds_wifi.xml）
├── scripts/                          # 部署辅助脚本（路由 / 防火墙 / 环境检查）
├── tools/                            # 排查运维辅助脚本（非运行时依赖，见 tools/README.md）
├── main.py                           # 诊断工具入口
├── setup.py / requirements.txt       # Python 打包与依赖
├── run.sh                            # 诊断工具快速启动脚本
├── start_go2.bat                     # Windows 一键启动控制台（含 WSLg 重置）
├── usage.md                          # 原始需求文档（无线通讯异常排查）
├── reports/  captures/               # 运行产物（已 gitignore，可随时删除）
└── README.md                         # 本文件
```

---

## 配置项说明

### 1. 每设备钥匙（`data2=3` 新固件必需）

优先级（由高到低）：

1. 环境变量 `GO2_KEYS_FILE=/path/to/keys.json`
2. 环境变量 `GO2_AES_KEYS=<32位hex>[,<32位hex>...]`
3. 用户配置目录：Windows `%USERPROFILE%\.go2\keys.json`，Linux `~/.go2/keys.json`

`keys.json` 结构（钥匙**不要**放进项目目录，项目 `.gitignore` 已屏蔽相关文件名）：

```json
{
  "devices": [
    { "sn": "B42D2000P6CDL807", "key": "<32位hex>", "verified": true, "lastIp": "192.168.0.169" }
  ]
}
```

> 只要有一次握手成功，控制台会把「IP → 钥匙」写入运行目录下的 `go2_keys_cache.json`，
> 下次启动直接复用（该文件是运行产物，同样不要提交）。

### 2. 控制台环境变量

| 变量 | 作用 |
|---|---|
| `GO2_KEYS_FILE` / `GO2_AES_KEYS` | 指定钥匙来源（见上） |
| `GO2_BIND_ROUTE=1` | 多网卡时按路由绑定本机网卡（WSL / 虚拟网卡场景推荐） |
| `GO2_IPV4_ONLY=1` | ICE 只保留 IPv4 候选 |
| `GO2_KEEP_CANDIDATES=a,b` | 只保留指定本地地址的 ICE 候选 |
| `GO2_VERBOSE_KEYS=1` | 日志里打印钥匙来源路径（默认只打印数量） |
| `GO2_MOVE_TEST=1` | 启动后自动做一次极低速前进自检（0.12 m/s × 1.5s，然后停车） |
| `GO2_DEMO=1` | 启动后自动演示：平衡站立 → 打招呼 → 停止 |
| `GO2_SHOT=1` | 渲染 60 帧后把帧缓冲落盘（诊断"窗口空白"用） |

### 3. 控制台命令行选项

```
go2_remote [ip ...]                    图形界面，参数为预连接目标
go2_remote --verify ip1,ip2 [选项]      无界面验证模式

--verify            无界面验证：连接稳定性报告
--actions           验证时执行安全动作并校验回执
--probe             动作可用性探测（只发原地不动的安全指令）
--seconds N         验证时长（默认 60）
--stagger MS        错峰连接间隔（默认 600）
--settle MS         就绪后等待再动作（默认 2500）
--set-mode NAME     就绪后切换运动模式（normal / ai / mcf）
--dump-state        打印机器人状态帧（诊断用）
--keys HEX,HEX      直接提供每设备 AES 钥匙
--bind-route        按路由绑定本机网卡
--ipv4              只保留 IPv4 ICE 候选
```

### 4. 诊断工具配置

复制 [`examples/config.yaml`](examples/config.yaml) 后按实际环境修改（网卡名、PC IP、机器狗 IP、抓包时长、报告目录等）：

```yaml
network:
  pc_interface: wlo1
  pc_ip: 192.168.1.106
  robot_wifi_ip: 192.168.1.211
  robot_eth_ip: 192.168.123.161
capture:
  capture_duration: 30
  save_pcap: true
report_dir: ./reports
```

```bash
sudo python main.py -c config.yaml diagnose
```

CycloneDDS 配置（`cyclonedds_*.xml`）可用 `python main.py validate` 校验、`genconfig` 生成。

---

## 常见问题（FAQ）

**Q1：连不上，或连上又掉线**
- 机器狗**同一时刻只允许一条 WebRTC 连接** —— 先关掉手机 App / 其它遥控软件
- 同一台狗两次连接间隔 **≥ 15 秒**，连续重连会触发 HTTP 429 限流（表现为"连上但指令被拒"）
- `data2=3` 的固件必须提供**正确的每设备钥匙**，否则信令阶段就失败
- 多网卡 / WSL 环境建议加 `--bind-route`

**Q2：刚连上就发指令，提示失败（`code=-1` / `error_code=1013`）**
连接就绪后约 **10 秒内**运动服务处于预热期，指令会被拒。客户端会自动重试；手工操作时等十几秒即可。

**Q3：某个动作点了没反应（`code=3203`）**
该 `api_id` **不在当前固件的指令表里**。Go2 Pro 等 **MCF 固件**与普通固件是两套 id：
勾选/取消「MCF 固件」开关，或直接让工具自动回退（被拒后会用另一套 id 重试一次）。
确认固件确实没有的动作，可勾「隐藏不支持的」过滤掉。

**Q4：动作报 `code=3202`**
多见于**开关型指令**（自由行走 / 领航跟随 / 交叉步 / 经典步态 …）缺少参数 —— 它们必须带
`{"data": true|false}`。本工具的开关按钮已自动处理。

**Q5：急停按了，狗还在走**
- 那类"持续模式"（自由行走 / 领航跟随 / 交叉步 …）是 on/off 开关，**StopMove 停不掉**，
  必须用同一个 `api_id` 带 `{"data": false}` 关闭 —— 本工具的急停已自动逐个关闭
- **一次性动作**（舞蹈 / 空翻 / 拜年 …）执行期间固件不接受打断，只能等它做完；
  需要立即停住请用「**强制阻尼**」（狗会软腿趴下）
- **不要连按急停**：每次都会下发一串指令，连按会把数据通道灌爆，反而更停不住

**Q6：窗口打开了但是空白 / 标题带 `[WARN:COPY MODE]`**
WSLg 呈现层退化（应用本身正常）。关掉残留窗口 → `wsl --shutdown` → 重新启动
（`start_go2.bat` 已内置重置）。在 WSL 里反复重启应用后容易出现。

**Q7：怎么拿到 `data2=3` 固件的设备钥匙？**
钥匙按设备的 AES-128，只有三个来源：① 绑定过这台狗的**宇树账号**（官方 App 能连上就说明本地有钥匙）；
② 设备内 shell（需 root / UART）；③ 从**用户自己的**平板 App 数据里离线提取。
完整过程与工具见 [`docs/multi_go2_pro_solution.md`](docs/multi_go2_pro_solution.md) 与 [`tools/`](tools/)。
> 注意：`tools/unitree_cloud.py` 会访问宇树官方服务器，请自行确认后手动执行。

**Q8：Windows 上能直接跑控制台吗？**
不能（源码使用 POSIX 头文件与 WSLg 显示）。请用 **WSL2**：`start_go2.bat` 已封装好启动流程。
诊断工具（Python）在 Windows / Linux 均可运行。

**Q9：界面上的 IP 不想被人看到？**
勾选「隐私模式」，界面与日志会把 IPv4 中间两段打码（仅显示层，不影响功能）。

---

## 辅助工具

[`tools/`](tools/) 收录排查与运维脚本（**不参与构建**，用途与用法见 [tools/README.md](tools/README.md)）：
钥匙验证（`key_try.py` / `key_verify.py`）、平板日志提取（`tab_dogs.py`）、对照诊断（`diag_pro.py`）、
恢复流程（`recover.py`）、固件/`data2` 速查（`con_notify.py`）、抓包查看（`pcap_viewer.py`）、云接口（`unitree_cloud.py`）等。

---

## 许可证

本项目采用 **MIT License**，详见 [LICENSE](LICENSE)。

> 免责声明：本项目为第三方工具，与宇树科技（Unitree Robotics）无官方关联。
> 请仅在**你自己拥有或已获授权**的设备上使用；涉及设备解锁、固件修改等操作风险自负。
