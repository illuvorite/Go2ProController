# Go2 遥控客户端（C++ / 跨平台）

Unitree Go2 的**控制管理台**。原生代码、跨平台（Windows / Linux / macOS），
通过机器狗 WiFi 上的 **WebRTC 数据通道**下发运动指令。
有两套界面：**ImGui 桌面/安卓原生界面** 与 **Vue3 网页界面**（见 [`assets/web/README.md`](assets/web/README.md)）。

支持：
- **局域网自动发现**：并行探测本机所有 /24 网段的 9991 信令端口，并经 `/con_notify`
  （base64 JSON，含 data1/data2）确认是 Go2；另有 SN 多播发现
- **手动添加**：输入 IP 加入列表
- **单控 / 群控**：设备列表勾选——勾 1 台单控，勾多台群控；每台独立 WebRTC 连接
- **双摇杆遥控 / 完整动作库 / 急停（含持续模式关闭）/ 强制阻尼**（详见下文「界面功能」）

> 协议细节见 [`../docs/go2_webrtc_protocol.md`](../docs/go2_webrtc_protocol.md)。

## 技术栈

| 组件 | 选型 | 说明 |
|---|---|---|
| 语言 | C++17 | 原生机器码，便于加壳加固 |
| GUI（原生） | Dear ImGui + GLFW + OpenGL3 | 轻量、单 exe、MIT 授权 |
| GUI（网页） | Vue3（免构建）+ cpp-httplib 本机服务 | 同一份 core，见 `assets/web/README.md` |
| WebRTC | libdatachannel | 轻量 C++ WebRTC，主打 DataChannel |
| 加密 | OpenSSL | AES-128/256-GCM、AES-256-ECB、RSA-PKCS1v15、MD5 |
| HTTP | cpp-httplib | 9991 信令 + 网页界面服务 |
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
# 推荐：用预设（跨平台/交叉编译/消毒器都在 CMakePresets.json 里）
cmake --preset linux
cmake --build build/linux --parallel 2

# 等价的手工写法
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
```

产物：
- `build/linux/go2_remote` —— 桌面版（ImGui + 网页界面后端）
- `build/linux/go2_serve` —— 无图形版（核心 + HTTP 服务，服务器/容器里也能跑）
- `build/linux/crypto_test` 等一串自测可执行文件（见「测试与验证」）

> ★ 构建目录**不要改名/搬位置**：`CMakeCache.txt` 里存的是绝对路径，改名后必须删掉
>   `build/CMakeCache.txt` 与 `CMakeFiles/` 重新配置。依赖已经下载在 `build/_deps/`，
>   重新配置时可以用 `-DFETCHCONTENT_SOURCE_DIR_<NAME>=<abs>/build/_deps/<name>-src` 复用，
>   不必重新下载（`CMakePresets.json` 的 `linux-asan` / `linux-tsan` 已经这么做了）。

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

- ~~`ui.cpp` 的"扫描局域网"用 `detach()` 线程并捕获 `mgr/ui` 引用，退出阶段可能异常。~~
  已修：扫描线程改成 **joinable 并由 `joinScans()` 收尾**（退出路径会等它结束），
  见 `ui/ui_state.cpp`。同类隐患的处理约定写在 `ui.hpp` 的「线程契约」注释里。
- `ui/ui.cpp` 仍是 **2000+ 行的单文件**（绘制 + 布局 + 摇杆 + 弹窗混在一起）。
  群控**语义**已经抽到 `ui/command_service.*`（不再是两份实现），但**视图层**还没拆。
  拆分方案见 `docs/optimization_plan.md` 的 M3-2。
- `client/assets/web/fonts/Phosphor.ttf`、`client/assets/fonts/Phosphor.ttf`、
  `client/apps/android/app/src/main/assets/fonts/Phosphor.ttf` 是同一字体的三份副本
  （分别给网页、桌面、安卓 C++ 侧读，加载路径不同）。合并需要先统一运行时资源定位，
  暂时保留。

## 测试与验证

不需要机器狗、也不需要图形环境，一条命令跑全量纯逻辑自测：

```bash
cd client/build/linux
ctest --output-on-failure
```

| 自测 | 覆盖什么 |
|---|---|
| `crypto_test` | base64 / MD5 / AES-ECB / AES-GCM / RSA / SDP 裁剪（用真实抓取的报文） |
| `motion_test` | 双摇杆决策与急停语义（纯函数） |
| `layout_test` | 断点布局常量的 `static_assert` + 跨 TU 链接 |
| `sport_library_test` | 指令表不变量（key 唯一、两套 api_id 兜底、持续模式 id 集合、参数打包） |
| `command_service_test` | 群控/急停语义（目标集合、锁定后不许动、开关簿记）—— 用假 sink，不碰网络 |
| `ui_state_thread_test` | 界面线程 × Web 线程并发读写同一份 `UiState`（并发安全回归） |
| `discovery_test` / `cloud_test` | **手动**：需要真机 / 会访问宇树官方服务器，标了 `DISABLED` |

按需（不跑真机）也能做的更强验证：

```bash
# ① 全套定量指标取证（构建、ctest、静态指标、前端自检一把过）
bash scripts/verify.sh --build

# ② 数据竞争：TSan 跑并发自测，并带"正对照"证明 TSan 真的有效
cmake --preset linux-tsan && cmake --build build/linux-tsan --parallel 2
bash scripts/verify_tsan.sh          # 期望：正式 0 条、正对照 ≥1 条

# ③ 越界 / use-after-free
cmake --preset linux-asan && cmake --build build/linux-asan --parallel 2
ctest --test-dir build/linux-asan --output-on-failure

# ④ 免构建前端（assets/web）的语法与引用检查
node scripts/web_check.mjs
```

> 手动跑需要真机的测试：`ctest -R discovery_test --output-on-failure`
> （`cloud_test` 会请求宇树官方服务器 —— 按项目纪律由使用者本人执行）

## 云账号与 data2=3 新固件（Go2 ≥ 1.1.15）

固件 ≥ 1.1.15 的 Go2 在 `con_notify` 里返回 `data2=3`：握手公钥改用**每设备 AES-128 key**
（32 位 hex）加密，不再用静态 key。该 key 绑定机器狗 SN，获取途径：

1. **云账号**（正路）：`core/unitree_cloud.{hpp,cpp}` 是完整实现（`login/email` +
   `device/bind/list`，与官方 App 同款 API），配套自测 `tests/cloud_test.cpp`；
   命令行版见 [`../tools/unitree_cloud.py`](../tools/unitree_cloud.py)。
   ⚠ 会访问宇树官方服务器，且第三方客户端可能被 TLS 指纹风控（HTTP 567），请自行评估。
   界面里的登录表单**未实现** —— 拿到钥匙后粘进设置页或写进 `keys.json` 即可。
2. **从狗的内网提取**（不连电脑）：设置页「从机器狗找钥匙」（网页界面）扫狗的端口抓取候选。
3. **电脑 adb / 狗侧脚本**：见 [`../docs/multi_go2_pro_solution.md`](../docs/multi_go2_pro_solution.md)。

对失败的新固件机器狗点「连」（或等自动重试）——连接时逐个尝试候选 key，
GCM 校验通过的那个即为这台机器的钥匙，并按 IP 记入 `go2_keys_cache.json`。

## 界面功能（ImGui 原生界面）

界面自上而下分两块：**页面区**（遥控 / 动作库，都铺满整屏宽）与 **摇杆带**（两个下角悬浮摇杆）。

- **顶栏**（一行 4 个按钮）：`设备` 弹窗 · `页面切换`（遥控 / 动作库）· `⋯更多`（设置 / 日志，带异常角标）·
  **`■ 急停`**（两个页面都能一眼看到，`空格键` 同样有效）
- **遥控页**：红色全宽急停（发给**所有**就绪机器狗，最安全）、强制阻尼（二次确认）、
  「参数 | 快捷」两栏（苹果风格滑条 + 行尾「重置」 + D-pad 方向键）、实时速度读数
- **动作库页（常驻整屏，不是弹窗）**：固定页眉（指令发给谁 / MCF 固件 / 隐藏不支持的）+ 可滚动动作网格；
  按整屏宽度自动分列（最多 8 列）；组顺序 = 基础姿态 / 表演动作 / 跳跃特技 / 状态查询 / 其他进阶；
  每个动作带 Phosphor 图标（`icons.hpp`）
- **摇杆带**：双摇杆恒在屏幕两个下角（任何页面都盖不住）；**两杆中间是受控狗卡片行 +
  「单控 / 群控」面板** —— 卡片 = Go2 实拍图 + 名字 + 电量 + 勾选框（点一张勾一台，可多选，
  多台横向滑动；矮屏 / 窄面板自动退回"按钮 + 受控对象"紧凑形态）；点「单控」弹出设备列表挑一台；
  **弹窗打开时整条摇杆带被盖住**（不画、不响应、数值清零）
- **设备弹窗**：扫描局域网 / 手动添加 IP / 全部连接 / 全部断开 + 设备卡片
  （名称/IP、状态、电量、运动模式，以及 `改名` / `单控` / `连接·断开` / `移除`）
- **日志弹窗**：自动滚动 / 只看异常 / 清空；每条日志带 `[机器狗IP]` 前缀，多机并发时易分辨
- **改名**：设备卡片「改名」按钮，名字持久化在 `robot_names.json`（应用运行目录），
  显示在设备卡片与摇杆带面板上；留空 = 恢复显示 IP
- **触摸**：按住任意位置拖动即可滚动（不用去抓右边滚动条）；滑条/按钮按断点放大到 ≥44dp

> 网页界面（Vue3）的功能与此对应但交互不同（狗卡片勾选受控等），见 [`assets/web/README.md`](assets/web/README.md)。

### 两个踩过的坑（改这里必读）

1）摇杆带中间的「单控 / 群控」面板是个独立小窗，**千万不要给它加 `ImGuiWindowFlags_NoBringToFrontOnFocus`** ——
实测会让它永远排在最底层，被整屏不透明的主窗口整个盖掉（现象：面板"消失"，但 `Begin` 返回 true、
窗口里也有顶点）。排查方法：临时 `#include <imgui_internal.h>` 打印 `g.Windows[i]->Name/Hidden/Active`
的顺序（当时打印出来面板在 index 0、主窗口在 index 3）。

2）**同一窗口里重复使用的控件，标签必须自带区分度**（或 `PushID` 包一层）。
这个版本的 ImGui `io.ConfigDebugHighlightIdConflicts` **默认开着（Release 也照报）**：
三个参数滑条曾经都用 `"##p"`，界面直接弹出红框提示
`3 visible items with conflicting ID`。现在滑条 ID 由标签派生（`##线速度上限` …），
`paramRowF()` 里已有注释说明。

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
├── CMakeLists.txt          # go2_core(静态库) + go2_remote/go2_serve(exe) + 自测目标
├── CMakePresets.json       # linux / windows-msvc / macos / android-arm64 / linux-asan / linux-tsan
├── .clang-format           # 格式化规范（不要求全仓重排，见文件头注释）
├── core/                   # ★ 纯逻辑层（禁止平台头）
│   ├── robot_client.{hpp,cpp}   # 单台 WebRTC 通道 + 校验 + 心跳 + 指令
│   ├── robot_manager.{hpp,cpp}  # 多机管理（每台一个 client + 钥匙绑定）
│   ├── signaling.{hpp,cpp}      # 9991/8081 信令握手 + 指纹裁剪
│   ├── crypto.{hpp,cpp}         # base64 / MD5 / AES-GCM / AES-ECB / RSA
│   ├── discovery.{hpp,cpp}      # 局域网扫描 + SN 多播 + Go2 确认
│   ├── sport_library.{hpp,cpp}  # 宇树动作指令表（normal / MCF 双指令集）+ kApiMove
│   ├── motion.hpp               # 双摇杆 + 急停决策（纯函数）
│   ├── local_keys.{hpp,cpp}     # 钥匙加载 + 落盘位置（userDataDir=~/.go2，不在工程里）
│   └── unitree_cloud.{hpp,cpp}  # 宇树云接口（云账号取钥匙路线）
├── platform/net.hpp        # 平台网络层唯一接缝（POSIX / Winsock）
├── ui/                     # ImGui 界面层 + 网页界面后端
│   ├── command_service.{hpp,cpp} # ★ 群控语义唯一实现（急停/锁定/群控/开关），桌面·网页·安卓共用
│   ├── ui.cpp              # 绘制（仍是单文件，拆分见 docs/optimization_plan.md M3-2）
│   ├── ui_state.{hpp,cpp}  # UiState：跨线程共享状态的线程契约 + 扫描线程管理
│   └── web_bridge.{hpp,cpp}# 本机 HTTP 服务（/api/state、/api/command）+ 访问控制
├── apps/
│   ├── desktop/main.cpp    # 桌面入口（GUI / 无界面验证 / 内建网页服务）
│   ├── serve/main.cpp      # 无图形入口（只带 core + HTTP 服务）
│   └── android/            # 安卓端（见 apps/android/README.md）
├── assets/                 # fonts/ + web/（Vue3 前端，**全仓库唯一一份**）
├── patches/                # libdatachannel 的 Go2 兼容补丁（FetchContent 自动应用）
├── third_party/            # 手放的单头库（httplib.h / nlohmann）
└── tests/                  # crypto / motion / layout / sport_library / command_service /
                            # ui_state_thread + 手动测试（discovery / cloud）
```

仓库根的 `scripts/` 里是跨语言的验证脚本：

| 脚本 | 用途 |
|---|---|
| `scripts/verify.sh` | 一键取证：构建 + ctest + 方案 §5 的静态指标 |
| `scripts/verify_tsan.sh` | TSan 数据竞争验证（含"正对照"证明工具有效） |
| `scripts/web_check.mjs` | 免构建前端的语法 / import / importmap / 资源引用检查 |

`.github/workflows/ci.yml` 在每次 push / PR 上跑：全量编译 + `ctest` + 前端自检。

## 跨平台（Windows / Linux / macOS）

代码已做平台适配（`#ifdef _WIN32` 分支），三个平台都能原生编译运行：

| 平台 | 状态 | 说明 |
|---|---|---|
| **Linux / WSL(Ubuntu)** | ✅ 当前主力 | `cmake -S . -B build -G Ninja && cmake --build build` |
| **macOS** | ✅ 代码就绪 | 依赖 brew：`brew install cmake ninja openssl glfw`；GUI 原生窗口 |
| **Windows 原生** | ✅ 代码就绪 | MSVC + OpenSSL（vcpkg 或 Shining Light 安装包）；CMake 自动链接 `ws2_32`/`iphlpapi`；GUI 是原生窗口，**不需要 WSLg** |
| Windows + WSL | ✅ 现用方式 | 双击 `../start_go2.bat`（已含 WSLg 重置，避免窗口空白） |

平台相关代码集中在两处：
- `core/discovery.cpp`：网段枚举（Windows 用 `GetAdaptersAddresses`，POSIX 用 `getifaddrs`）、端口探测（Winsock / BSD socket）
- `core/local_keys.cpp`：钥匙文件目录（Windows `%USERPROFILE%`，POSIX `$HOME`）
- `platform/net.hpp`：socket 创建 / 关闭 / poll 等的归一化 helper（新增网络代码一律走这里）

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
