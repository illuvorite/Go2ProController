# Go2 遥控客户端（C++ / 跨平台）

Unitree Go2 的桌面**控制管理台**。原生代码、跨平台（Windows / Linux / macOS），
通过机器狗 WiFi 上的 **WebRTC 数据通道**下发运动指令。

支持：
- **局域网自动发现**：并行探测本机所有 /24 网段的 9991 信令端口，并经 `/con_notify`
  （base64 JSON，含 data1/data2）确认是 Go2
- **手动添加**：输入 IP 加入列表
- **单控 / 群控**：设备列表勾选——勾 1 台单控，勾多台群控；每台独立 WebRTC 连接

> 协议细节见 [`../docs/go2_webrtc_protocol.md`](../docs/go2_webrtc_protocol.md)。

## 技术栈

| 组件 | 选型 | 说明 |
|---|---|---|
| 语言 | C++17 | 原生机器码，便于加壳加固 |
| GUI | Dear ImGui + GLFW + OpenGL3 | 轻量、单 exe、MIT 授权 |
| WebRTC | libdatachannel | 轻量 C++ WebRTC，主打 DataChannel |
| 加密 | OpenSSL | AES-128/256-GCM、AES-256-ECB、RSA-PKCS1v15、MD5 |
| HTTP | cpp-httplib | 9991 信令 |
| JSON | nlohmann/json | — |

## 依赖安装

**Linux / WSL (Ubuntu 22.04)**

```bash
sudo apt update
sudo apt install -y cmake ninja-build g++ pkg-config \
    libssl-dev libglfw3-dev libgl1-mesa-dev xorg-dev \
    fonts-wqy-microhei
```

其余依赖（libdatachannel / imgui / nlohmann-json / cpp-httplib）由 CMake FetchContent 自动拉取。

## 构建

```bash
cd client
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

产物：
- `build/go2_remote` —— 主程序（约 2 MB）
- `build/crypto_test` —— 加密模块自测

## 运行

```bash
# 传入机器狗 IP 即自动连接（支持多台，错峰 600ms）
./build/go2_remote 192.168.2.111 192.168.2.112 192.168.2.113

# 不传参数则手动在界面里填 IP 并点「连接」（输入框支持粘贴逗号分隔的多个 IP）
./build/go2_remote
```

## 多机（Air + 多台 Pro）一键验证

不需要图形环境，也不需要三台同时手点「连接」——错峰握手 + 指标判定：

```bash
# 只验证连接与数据流稳定性（60 秒）
./build/go2_remote --verify 192.168.2.111,192.168.2.112,192.168.2.113 --seconds 60

# 追加动作回执校验（平衡站立 → 打招呼 → 停止，逐条看 code 是否 0）
./build/go2_remote --verify 192.168.2.111,192.168.2.112,192.168.2.113 --seconds 40 --actions

# 多网卡（WSL / Hyper-V / VMware 虚拟网卡）环境连不上时再试
./build/go2_remote --verify ... --bind-route     # 按路由绑定本机网卡
./build/go2_remote --verify ... --ipv4           # 只保留 IPv4 ICE 候选
```

判定标准：三台都 `就绪` + `最近(ms) < 5000` + `稳定(s) ≥ 测量时长×80%`；
退出码 `0` 通过、`2` 未达标。完整排查手册见
[`../docs/multi_go2_pro_solution.md`](../docs/multi_go2_pro_solution.md)。

**显示环境要求**：需要一个可用的图形环境。
- Windows 原生：直接运行
- WSL2：依赖 **WSLg**（Win11 自带），确认 `echo $DISPLAY` 有输出

## 两种固件信令流程都支持

| 固件 | 信令 | `data2` | 需要钥匙 | 备注 |
|---|---|---|---|---|
| Go2 ≥ 1.1.15 | `:9991` `con_notify` | 3 | **是**（云账号每设备 AES-128 key） | 新固件，联网后客户端会提示"需要钥匙" |
| Go2 1.1.11 ~ 1.1.14 | `:9991` `con_notify` | 2 | 否（内置静态 GCM key） | 与 Air 老固件一致 |
| Go2 < 1.1.11 | `:8081` `POST /offer` | — | 否（明文流程） | 客户端自动探测并回退，不用手工切换 |

连接时自动按 `9991 → 8081` 顺序探测端口；`data2=3` 的钥匙命中后会按 IP 记住并写入
`go2_keys_cache.json`（下次启动直接复用）。**两台 Pro 绑在不同宇树账号时分别登录即可，
钥匙会累加而不是覆盖。**

## 多机保活策略

- **错峰连接**：机器狗信令服务是单线程 accept，批量连接按 600 ms 间隔排队；
- **独立连接**：每台一个 PeerConnection / DataChannel / 心跳线程，互不影响；
- **失败重试**：信令阶段 3 次（1.2/2.4/3.6 s）；通道建立+校验总超时 20 s；
- **掉线自动重连**：就绪后 12 s 收不到任何数据（心跳应答或状态话题）即判定掉线，
  按 5→10→20→30 s 退避重连，直到用户手动断开；
- **限流保护**：机器狗信令有限流，短时间内反复重连会返回 **HTTP 429**；此时即使握手成功，
  最初几秒的指令也会被拒（`code=-1`）。客户端识别 429 并加长退避，**但请遵守：同一台狗两次连接间隔 ≥15 秒**；
- **首指令保护**：就绪后立即下发动作可能被拒（实测 1~2 秒内 `code=-1`），
  验证模式默认等 2.5 s（`--settle`），客户端另有"被拒后 900ms 自动重试一次"兜底；
- **断开安全**：所有断开路径先 `StopMove`，避免狗保持最后速度，也避免机器人侧残留僵尸会话。

实测（2026-09-22，Go2 Pro / 固件 data2=2）：
```
就绪 收包 355 最近 29ms 心跳 9/9 重连 0 状态主题帧 339 条
动作回执 成功 3 / 失败 0   →  PASS
```

## 已知问题

- `ui.cpp` 里"扫描局域网"用的是 `detach()` 线程并捕获 `mgr/ui` 引用，
  关闭窗口若正好撞上扫描收尾，退出阶段可能异常。修复方向：把扫描线程改成 joinable，
  或用 `shared_ptr` 持有状态。

## 先跑自测

```bash
./build/crypto_test
```

该测试用真实抓取的 `con_notify` 响应验证加密链路，**不依赖机器狗在线**：

```
[PASS] base64 编解码往返一致
[PASS] MD5("abc") 结果正确
[PASS] AES-256-ECB + PKCS7 往返一致
[PASS] AES-GCM 解密真实数据成功（验证通过）
[PASS] 路径后缀推导成功
[PASS] 真实公钥可解析并完成 RSA-2048 加密
[PASS] stripExtraFingerprints 只保留 sha-256
```

## 云账号与 data2=3 新固件（Go2 ≥ 1.1.15）

固件 ≥ 1.1.15 的 Go2 在 `con_notify` 里返回 `data2=3`：握手公钥改用**每设备 AES-128 key**
（32 位 hex）加密，不再用静态 key。该 key 绑定机器狗 SN，**只能用官方 App 绑定的
宇树账号从云端获取**：

1. 界面顶栏「云账号」：选区（global=海外账号 / cn=国内账号），填邮箱和密码
2. 点「登录获取钥匙」→ 走官方同款 API（`login/email` + `device/bind/list`）拉取
   账号绑定的设备与每设备 key，装载为候选钥匙
3. 对失败的新固件机器狗点「连」（或等自动重试）——连接时逐个尝试候选 key，
   GCM 校验通过的那个即为这台机器的钥匙

安全说明：密码仅以 MD5 形式经 TLS 发给宇树官方 API（与官方 App 行为一致），
本地不落盘、不做其他用途。

云连通性自测（无需账号）：`tests/cloud_test.cpp`。

## 界面功能

- **顶栏**：扫描局域网 / 手动添加 IP / 全部断开；勾选=受控（1 台单控，多台群控）
- **设备列表**：每台显示 IP、连接状态、电量、运动模式；可单独 连/断/移除
- **姿态 / 动作 / 模式**：恢复站立 / 站立 / 平衡站立 / 趴下 / 阻尼 / 打招呼 / 伸懒腰 / normal·ai —— 发给勾选的机器狗
- **遥控**：红色急停（发给**所有**就绪机器狗，最安全）、虚拟摇杆（按住 10Hz 群发，松手即停）、⟲⟳ 按住式转向、线速度/角速度滑条、快捷方向按钮
- **日志**：每条日志带 `[机器狗IP]` 前缀，多机并发时易分辨

## 发现机制说明（`discovery.cpp`）

1. `getifaddrs` 枚举本机 IPv4 网段（排除回环 / 链路本地），取 /24
2. 64 线程并行 TCP 探测 9991 端口（非阻塞 connect + poll）
3. 命中的主机逐台 `GET /con_notify` 确认：**响应是 base64 编码的 JSON**，
   解码后须含 `data1` / `data2` 才判定为 Go2

两个实测的坑（改动前必读）：
- 机器狗信令服务器（Boost.Beast）**单线程 accept**：裸 TCP 探测建立的连接若直接
  close，服务器要等 Keep-Alive 超时（约 4 秒）才释放，会堵死随后的 HTTP 确认。
  所以 `tcpPortOpen` 在返回前先 `shutdown(SHUT_WR)` 优雅收尾。
- `/con_notify` 返回 **base64** 而非明文 JSON，不能直接字符串匹配 `data1`。

## 使用注意

- ⚠️ **同一时刻只允许一条连接**。连接前请先断开手机 App，否则机器人会返回 `sdp: "reject"`。
- ⚠️ 机器人必须与电脑在同一局域网。
- ⚠️ 首次操作建议先在地面附近、周围无人时测试，先点「阻尼」或「平衡站立」。

## 三个必须遵守的协议约束（否则通道建不起来）

代码中已处理，修改时不要破坏：

1. offer 里**必须**有 `m=audio` + `m=video` 两条 m-line
2. offer 里**只能有一条** `a=fingerprint`（sha-256）—— 见 `stripExtraFingerprints()`
3. 构造 PeerConnection 时**不要配置 STUN**（`config.iceServers.clear()`）

## 目录结构

```
client/
├── CMakeLists.txt
├── patches/               # libdatachannel 的 Go2 兼容补丁（FetchContent 自动应用）
├── src/
│   ├── main.cpp           # GLFW + ImGui 主循环，多机回调接线
│   ├── crypto.{hpp,cpp}   # base64 / MD5 / AES-GCM / AES-ECB / RSA
│   ├── signaling.{hpp,cpp}# 9991 信令握手 + 指纹裁剪
│   ├── robot_client.{hpp,cpp} # 单台 WebRTC 通道 + 校验 + 心跳 + 指令
│   ├── robot_manager.{hpp,cpp}# 多机管理（每台一个 client）
│   ├── discovery.{hpp,cpp}# 局域网扫描 + Go2 确认
│   └── ui.{hpp,cpp}       # ImGui 界面（设备列表 / 单控群控 / 摇杆）
└── tests/
    ├── crypto_test.cpp    # 加密自测（用真实抓包数据）
    └── discovery_test.cpp # 发现模块自测（需在机器狗局域网内）
```

## 跨平台（Windows / Linux / macOS）

代码已做平台适配（`#ifdef _WIN32` 分支），三个平台都能原生编译运行：

| 平台 | 状态 | 说明 |
|---|---|---|
| **Linux / WSL(Ubuntu)** | ✅ 当前主力 | `cmake -S . -B build -G Ninja && cmake --build build` |
| **macOS** | ✅ 代码就绪 | 依赖 brew：`brew install cmake ninja openssl glfw`；GUI 原生窗口 |
| **Windows 原生** | ✅ 代码就绪 | MSVC + OpenSSL（vcpkg 或 Shining Light 安装包）；CMake 自动链接 `ws2_32`/`iphlpapi`；GUI 是原生窗口，**不需要 WSLg** |
| Windows + WSL | ✅ 现用方式 | 双击 `../start_go2.bat`（已含 WSLg 重置，避免窗口空白） |

平台相关代码集中在两处：
- `src/discovery.cpp`：网段枚举（Windows 用 `GetAdaptersAddresses`，POSIX 用 `getifaddrs`）、端口探测（Winsock / BSD socket）
- `src/local_keys.cpp`：钥匙文件目录（Windows `%USERPROFILE%`，POSIX `$HOME`）

### 钥匙文件位置（按平台自动查找，均**在项目之外**）

| 平台 | 路径 |
|---|---|
| Windows | `%USERPROFILE%\.go2\keys.json`（备选 `%APPDATA%\go2\keys.json`） |
| Linux / macOS | `~/.go2/keys.json`（兼容旧位置 `~/.go2_remote_keys.json`） |
| WSL | 上述两个 + 额外兼容读取 Windows 侧 `/mnt/c/Users/*/.go2/keys.json` |

也支持 `GO2_KEYS_FILE=<路径>` 显式指定，或 `GO2_AES_KEYS=hex1,hex2` 临时传入。

## 隐私与安全

**密钥绝不暴露**：
- 界面与日志只显示钥匙**数量**，从不显示钥匙内容；文件路径默认隐藏（需要时设 `GO2_VERBOSE_KEYS=1` 才打印）
- 客户端**不再搜索项目目录**加载钥匙（机制上杜绝钥匙随代码库外泄）
- 钥匙文件位于项目之外，仓库内加 `.gitignore` 防线

**隐私模式**（顶栏复选框）：勾选后界面与日志里的 IPv4 中间两段打码
（`192.168.0.169` → `192.168.*.***`），仅影响显示、不影响功能 —— 方便截图/演示时避免暴露网络拓扑。
