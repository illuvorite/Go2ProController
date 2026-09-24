# 跨平台方案：让 Go2 控制管理台跑在任意电脑与手机上

> 目标：**Windows / macOS / Linux 桌面 + Android / iOS 手机**都能运行，且功能与体验一致。
> 本文给出可行性评估、兼容性因素、目标架构、分阶段实施路线、已知限制与风险。

---

## 0. 结论先行（TL;DR）

| 结论 | 内容 |
|---|---|
| **主路线** | 保留现有 C++ 核心，**抽出 `core` 静态库 + 平台适配层**，桌面继续用 GLFW/ImGui，移动端用 **SDL2 + GLES/Metal + ImGui** |
| **桌面三平台** | 改动面很小（只有 3 个源文件用到 POSIX 头，约 1900 行），**1~2 周**可交付 Windows/macOS/Linux 构建 |
| **移动端** | 复用同一份核心，新增触摸 UI（双摇杆多点触控）+ 权限 + 安全存储 + 生命周期处理，**各 1~2 周** |
| **浏览器/纯 Web** | **不推荐**：局域网 HTTP 请求受混合内容 / Private Network Access 限制，WASM 也无法直接收发 UDP；除非配套「本地桥接代理」 |
| **非同一局域网（4G 遥控）** | 需要额外做「局域网中继网关」（机器狗只允许一条连接，且只在局域网可达） |
| **最硬的限制** | iOS 后台无法保持连接（切后台必须停车断开）；手机做不了有线 DDS 调试 |

---

## 1. 现状盘点（可行性评估）

### 1.1 代码规模与耦合

| 模块 | 行数 | 平台耦合 | 跨平台动作 |
|---|---|---|---|
| `ui.cpp` / `theme.cpp` / `ui.hpp` | ~1500 | ImGui 纯逻辑（无平台调用） | 触摸/DPI/布局适配 |
| `main.cpp` | 681 | GLFW + OpenGL + WSLg 提示 | 按平台选窗口/渲染后端、生命周期 |
| `robot_client.cpp` | 1038 | **POSIX socket 头**（`arpa/inet.h` 等） | 抽 `platform::net` |
| `signaling.cpp` | 422 | **POSIX socket**（端口探测）+ httplib（已跨平台） | 抽 `platform::net` |
| `discovery.cpp` | 407 | **POSIX socket**，已有 `#ifdef _WIN32` 分支 | 补 macOS/Android/iOS 分支 |
| `local_keys.cpp` | 152 | 已有平台分支（配置目录） | 补移动端安全存储 |
| `robot_manager.cpp` / `crypto.cpp` / `sport_library.cpp` | ~880 | 无（纯逻辑 + OpenSSL） | 无 |
| `motion.hpp` / `crypto.hpp` 等头 | ~400 | 无 | 无 |

**结论：平台耦合集中在 3 个源文件 + 1 个入口**，其余（协议、加密、状态机、动作表、遥控决策）是天然可移植的纯逻辑，可 100% 复用。

### 1.2 已有的跨平台基础

- `discovery.cpp` / `robot_client.cpp` / `local_keys.cpp` 已有 `#ifdef _WIN32` 分支
- 依赖本身跨平台：libdatachannel（Win/macOS/Linux/Android/iOS 均有支持）、Dear ImGui、nlohmann-json、OpenSSL
- 关键业务逻辑已抽成纯函数（`motion.hpp::planMotion` 配 17 项单测）→ 可直接进 CI 矩阵

---

## 2. 目标平台与功能矩阵

### 2.1 平台范围

| 平台 | 最低版本 | 架构 | 说明 |
|---|---|---|---|
| Windows | 10 1809+ | x64（可选 arm64） | 主要办公环境 |
| macOS | 12+ | universal2（Intel + Apple Silicon） | OpenGL 已弃用但仍可用 |
| Linux | Ubuntu 22.04+ / Debian 12+ | x64、arm64 | 现状已验证 |
| Android | 8.0（API 26）+ | arm64-v8a、x86_64 | 平板/手机 |
| iOS | 14+ | arm64 | 需本地网络权限 |

### 2.2 功能矩阵（哪些必须一致 / 哪些平台特化 / 哪些不支持）

| 功能 | 桌面 | Android | iOS | 说明 |
|---|---|---|---|---|
| 局域网发现（网段扫描 + SN 多播） | ✅ | ✅ | ✅ | 移动端需多播锁 / 本地网络权限 |
| 手动添加 IP | ✅ | ✅ | ✅ | |
| 单控 / 群控 + 设备卡片 | ✅ | ✅ | ✅ | 移动端竖屏改列表式布局 |
| 双摇杆遥控 | ✅（鼠标） | ✅（触摸多点） | ✅（触摸多点） | 触摸需自绘多点触控 |
| 急停（锁定式） | ✅ 空格键 | ✅ 大按钮 | ✅ 大按钮 | 移动端把急停做成常驻悬浮钮 |
| 动作库（50+ 指令 / 双指令集） | ✅ | ✅ | ✅ | 完全一致（同一份指令表） |
| 无界面验证 `--verify/--probe` | ✅ | ⚠️ | ⚠️ | 移动端做成「诊断」页 |
| 视频画面（后续功能） | ✅ | ✅ | ✅ | 编码用硬解（MediaCodec / VideoToolbox） |
| DDS / 抓包诊断（Python CLI） | ✅ | ❌ | ❌ | 需 raw socket / 网卡混杂模式 |
| 有线网口调试 | ✅ | ❌ | ❌ | 手机无网口 |
| 后台保持连接 | ✅ | ⚠️ 受省电限制 | ❌ 系统会挂起 | iOS 切后台必须停车 |

---

## 3. 兼容性因素清单（逐项）

### 3.1 网络层（最大的一块）

| 因素 | 差异与坑 | 处理方式 |
|---|---|---|
| Socket API | Winsock（`WSAStartup`/`closesocket`/`WSAGetLastError`）vs BSD socket（`close`/`errno`） | 统一封装：`platform::net::{init, close, lastError, wouldBlock}` |
| 可读/可写等待 | Windows 的 `WSAPoll` 有已知缺陷 | 统一用 `select`（三端一致）；macOS 上 `select` 也有 fd 上限 → 用 `poll` 分支 |
| 非阻塞语义 | Windows：`WSAEWOULDBLOCK`；POSIX：`EAGAIN` | 由封装层归一 |
| 多播加入 | 需 `SO_REUSEADDR` + 绑定到具体网卡；Windows 要 `bind` 到本地地址才对 | 在封装层实现 `joinMulticast(iface, group, port)` |
| **Android 多播** | 默认被过滤：需要 `WifiManager.MulticastLock` + `CHANGE_WIFI_MULTICAST_STATE` 权限 | App 启动时申请并持锁（JNI 调用） |
| **iOS 本地网络** | iOS 14+ 访问局域网需用户授权：`NSLocalNetworkUsageDescription` + `NSBonjourServices`；未声明会**静默失败** | Info.plist 声明 + 首次访问引导弹窗 |
| 接口枚举 | `getifaddrs`（POSIX/macOS/iOS/Android）vs `GetAdaptersAddresses`（Windows） | 已分支，补移动端返回真实 WiFi 接口 |
| 路由选择 | 判断出口源地址（`routeLocalAddress`）在移动端要区分 WiFi/蜂窝 | 移动端强制只用 WiFi 接口，蜂窝直接提示不可达 |
| 后台限制 | Android Doze/省电；iOS 后台 socket 挂起 | 切后台即断开并停车，回前台自动重连 |

### 3.2 加密与密钥

- OpenSSL 在三平台均可用；Android 用 NDK 预编译或 vcpkg 交叉编译；iOS 建议静态链接 OpenSSL 或换 BoringSSL
- 算法完全一致（AES-128-GCM / RSA-OAEP / MD5），**协议层不需要分平台**
- **密钥存储必须平台化**（当前是明文 `keys.json`）：
  - Windows：DPAPI（`CryptProtectData`）
  - macOS/iOS：Keychain
  - Linux：libsecret / 文件 + 0600
  - Android：Keystore + EncryptedSharedPreferences
  - 移动端被逆向的风险远高于桌面 → 明文钥匙文件**不可接受**

### 3.3 UI / 图形 / 输入

| 因素 | 说明 | 处理方式 |
|---|---|---|
| 窗口系统 | GLFW **不支持** Android/iOS | 移动端换 **SDL2/SDL3**（桌面也可统一切 SDL2） |
| 渲染后端 | 桌面 OpenGL 3.3；macOS 弃用 OpenGL（可用但告警）；iOS 只有 Metal；Android 用 GLES3 | ImGui 后端按平台选：`imgui_impl_opengl3`（桌面/Android）、`imgui_impl_metal`（iOS/macOS 未来） |
| 多点触控 | ImGui 默认把触摸映射为单点鼠标 → **双摇杆必须两点同时按** | 在 SDL 层接管 `SDL_FingerDown/Motion/Up`，把触点分配到「左半屏/右半屏」两个自绘摇杆；急停按钮走独立区域 |
| DPI / 缩放 | Windows 125%/150% 缩放下 ImGui 会糊；手机分辨率差异大 | 统一 `io.FontGlobalScale` + 动态取 `SDL_GetDisplayDPI` / `glfwGetWindowContentScale`；主题尺寸用「可缩放 token」 |
| 中文字体 | 现在依赖系统字体（Noto/wqy/msyh），手机上不一定有 | **内嵌字体**：思源黑体 / Noto Sans SC 子集化（1~3 MB，OFL 授权允许嵌入） |
| 布局 | 手机竖屏窄、横屏宽；有刘海与手势条 | ✅ **已做**：断点布局 `ui/layout.{hpp,cpp}` —— 4 宽度档（<600 / 600–899 / 900–1279 / ≥1280 dp）× 3 高度档（<480 / 480–799 / ≥800）→ 3 种模式（三栏 / 双栏 / 单栏+悬浮双摇杆），再叠加「矮屏修正」「触摸修正」两条规则与 8dp 断点滞回；安全区用于顶栏内边距与摇杆落点。单元测试 `tests/layout_test.cpp` 用 60+ 条 static_assert 在编译期把各机型档位数值钉死 |
| DPI / 密度归一 | SDL2 在 Android 返回**物理分辨率** → ImGui 单位 = 物理像素，3x 屏上 54 单位按钮只有约 18dp | ✅ **已做**：`main_android.cpp::detectPixelScale()` + 每帧折 `DisplaySize` / 设 `DisplayFramebufferScale`，让 ImGui 单位 = dp（断点数值才有一致含义） |
| 字号 | 原先烘焙进图集（桌面 23/18/15、手机 ×1.55 再烘一份），加载后改不了 | ✅ **已做**：ImGui 1.92 动态字体 → 只加载一份字体，`PushFont(font, 字号)` 运行时给（`ui/theme.cpp::setUiFontSizes`）。另：`ScaleAllSizes` 是就地相乘，`applyUiScale()` 每次从 `applyTheme()` 的基线重算，避免累乘 |

### 3.4 文件、路径、生命周期

| 因素 | 差异 | 处理方式 |
|---|---|---|
| 配置目录 | `%APPDATA%` / `~/Library/Application Support` / `$XDG_CONFIG_HOME` / Android `filesDir` / iOS `Application Support` | `platform::paths::appDataDir()` |
| 相对路径 | 当前 `go2_keys_cache.json` 按**运行目录**相对路径读（踩过坑） | 一律换成基于配置目录的绝对路径 |
| 应用生命周期 | 手机随时被切后台/杀进程；桌面关窗即退出 | 统一 `onPause()`（停车 + 优雅断开）/ `onResume()`（重连）；移动端禁止「后台自走」 |
| 电源/省电 | 手机定时器漂移、蜂窝切换 | 心跳与 10Hz 下发用 `steady_clock` 补偿；改为「变化即发 + 低频保活」 |

### 3.5 构建 / 依赖 / 工具链

```cmake
# 目标形态：CMake Presets + vcpkg manifest（三端一致）
CMakePresets.json        # windows-x64 / macos-universal / linux-x64 / android-arm64 / ios-arm64
vcpkg.json               # openssl, libdatachannel, sdl2, nlohmann-json, (imgui 自带 CMake)
```

| 平台 | 工具链 | 产物 |
|---|---|---|
| Windows | MSVC 2022 + vcpkg（首次编译依赖约 10~20 分钟） | 便携 zip / MSI |
| macOS | Xcode CLT + Homebrew（或 vcpkg） | `.app` → 签名+公证 → `.dmg` |
| Linux | gcc/clang + 系统包 | AppImage / `.deb` / Flatpak |
| Android | NDK r26+ + Gradle + CMake 外部构建 | `.aab`（Play）/ `.apk` |
| iOS | Xcode 15+ + CMake 生成 Xcode 工程 | `.ipa`（TestFlight / App Store） |

CI（GitHub Actions）矩阵：`windows-latest / macos-14 / ubuntu-22.04` 三端每次提交都构建 + 跑 `motion_test`、`crypto_test`；移动端单独 job 出 AAB/IPA。

### 3.6 分发、签名与合规

| 平台 | 要求 | 风险 |
|---|---|---|
| Windows | Authenticode 代码签名 | 无签名 → SmartScreen「未知发布者」警告 |
| macOS | Developer ID 签名 + 公证（notarization） | 未公证 → Gatekeeper 直接拦截 |
| Linux | AppImage 可免安装；Flatpak 要声明网络权限 | Flatpak 沙箱默认禁多播/局域网 |
| Android | targetSdk 用最新、隐私政策、AAB；多播/位置权限说明 | 上架审核 + 权限申报 |
| iOS | Local Network 用途说明；设备控制类 App 需解释用途 | 审核不确定；可先用 TestFlight 分发 |
| 开源合规 | libdatachannel(MIT)、ImGui(MIT)、GLFW(zlib)、SDL2(zlib)、OpenSSL(Apache-2.0)、字体(OFL) | 保留 LICENSE 与 NOTICE 即可 |

---

## 4. 三种技术路线对比

| | **A. 原生多端（C++ 核心 + 多 UI 壳）** | **B. Flutter / RN 壳 + C++ FFI** | **C. Web（浏览器直连）** |
|---|---|---|---|
| 做法 | core 静态库 + GLFW(桌面)/SDL2(移动) + ImGui | Dart/JS 写 UI，核心经 FFI/JNI 调用 | 前端用浏览器 WebRTC + 信令 |
| 复用率 | 核心 100% | 核心 100%，UI 重写 | 需重写协议（JS） |
| 开发量 | 中（UI 一次写、多端编译） | 中（UI 生态好，绑定层需写） | 小（一套 Web 跑遍） |
| 风险 | 移动端触摸/渲染需自研 | 平台插件需自己写（多播/WebRTC） | **高**：局域网 HTTP 被混合内容/PNA 拦截；WASM 无 UDP；iOS Safari 限制严 |
| 结论 | ✅ **推荐主路线** | ⚠️ 备选（若团队更熟 Flutter） | ❌ 仅在有「本地桥接代理」时才可行 |

> 关于 C 的补充：若确实想要"打开网页就能用"，唯一可行形态是 **「Web 前端 + 本机小代理」**：
> 代理（C++ core，几十 KB）在用户电脑/手机上监听 `ws://127.0.0.1:port`，负责与机器狗通信，网页只连本机。
> 代价是每个用户仍要装一个东西 —— 与方案 A 相比没有省事，只是 UI 用 Web 写。

---

## 5. 目标架构

```
network_get/
├── core/                       # ★ 纯 C++17，禁止出现 POSIX/Win32 头
│   ├── crypto/                 #   AES-GCM / RSA / MD5 / Base64（仅依赖 OpenSSL）
│   ├── protocol/               #   9991 / 8081 信令编解码、SDP 处理
│   ├── robot/                  #   robot_client / robot_manager（用 platform::net）
│   ├── discovery/              #   网段扫描 + SN 多播（用 platform::net）
│   ├── sport/                  #   sport_library（normal / MCF 指令表）
│   └── motion/                 #   planMotion 等纯决策逻辑（已有单测）
├── platform/                   # ★ 平台适配层（每个平台一份实现）
│   ├── net.{hpp,cpp}           #   socket / 多播 / 接口枚举 / 路由
│   ├── paths.{hpp,cpp}         #   配置目录 / 缓存目录
│   ├── storage.{hpp,cpp}       #   安全存储（DPAPI / Keychain / Keystore / libsecret）
│   ├── lifecycle.{hpp,cpp}     #   前台/后台回调
│   └── font.{hpp,cpp}          #   内嵌字体 + DPI 缩放
├── ui/                         # ★ 共享界面（ImGui 组件化）
│   ├── cards/ joystick/ actions/ log/   # 设备卡片 / 双摇杆 / 动作库 / 日志
│   └── layout.{hpp,cpp}        #   桌面三栏 / 手机单列 的断点布局
├── apps/
│   ├── desktop/                #   GLFW(或 SDL2) + ImGui 入口（现 main.cpp）
│   ├── android/                #   Gradle + NDK + SDL2 + JNI 胶水
│   ├── ios/                    #   Xcode 工程 + SDL2 + Metal 后端
│   └── cli/                    #   现有 --verify/--probe 无界面模式
└── tools/ , docs/ , examples/  # 不变
```

**分层铁律**（保证一致性）：

1. `core` 里不出现任何平台头文件；需要网络/存储/时间一律走 `platform::*`
2. **协议、指令表、错误码、日志格式只有一份**（core）→ 各端行为天然一致
3. UI 只做"渲染 + 事件"，业务判断全部调 core
4. 版本能力靠**能力探测**（连上后读 `con_notify.data2`、固件版本、`motion_switcher` 模式）决定，
   **不按平台分支**（同一台狗在 Windows 和 iPhone 上表现必须一模一样）

---

## 6. 实施路线图

> **✅ P0 已完成（2026-09-23）**：`client/` 已分层为 `core/`（纯逻辑，18 个文件）、
> `platform/net.hpp`（平台接缝，POSIX/Winsock 双分支 + initSockets/closeSocket/waitSocket 等 helper）、
> `ui/`（界面）、`apps/desktop/`（入口）；CMake 改为 `go2_core` 静态库 + `go2_remote` 界面目标；
> 新增 `CMakePresets.json`（linux / windows-msvc / macos-universal / android-arm64）与 `vcpkg.json`。
> 验收：Linux 构建通过、`motion_test`（17/17）与 `crypto_test` 全绿。
> 遗留（P1 做）：调用点仍是 POSIX 风格（`close`/`poll`/`inet_pton` 等），需要时在 Windows 上按编译错误逐点迁到 `platform::` helper。

| 阶段 | 内容 | 工期 | 验收标准 |
|---|---|---|---|
| **P0 重构** ✅ | 拆 `core/ platform/ ui/ apps/`；新增平台接缝头；CMake Presets + vcpkg manifest | 已完成 | Linux 构建通过，`motion_test`/`crypto_test` 全过，功能与现在一致 |
| **P1 Windows** | Winsock 封装落地、多播、接口枚举、高 DPI、便携 zip | 3~5 天 | Windows 上完成"发现 → 连接 → 摇杆 → 急停 → 动作"全流程 |
| **P2 macOS** | brew/vcpkg OpenSSL、universal2、Metal 或 OpenGL 后端、签名+公证、dmg | 3~5 天 | macOS（Intel + M 系列）功能对齐，双击即用 |
| **P3 Android** | SDL2 + GLES3 + 触摸双摇杆 + 多播锁 + Keystore + 生命周期 + AAB | 1~2 周 | 手机/平板完成全流程，切后台自动停车 |
| ↳ **P3 脚手架已完成（2026-09-23）** | `client/apps/android/`：`setup.sh`（下载 SDL2/ImGui → 展开 SDL 的 android-project → 注入权限/横屏/包名/minSdk/MulticastLock/CMake 路径）、`native/main_android.cpp`（SDL2+GLES3+ImGui，复用 `drawUi`，含前后台停车与重连）、`native/CMakeLists.txt`（go2_core+go2_ui+SDL2+ImGui）、`README.md`（构建步骤与依赖） | 已完成 | 待用户机器上首次构建（需 Android SDK/NDK + vcpkg arm64-android 依赖）→ 按报错迭代 |
| ↳ P3 待做 | Keystore 存钥匙、MulticastLock 已在 Java 侧就绪、AAB + 权限申报、安全区精确值（读 WindowInsets 经 JNI）、触屏 tooltip 改长按、遥控时保持屏幕常亮 | 1~2 周 | |
| ↳ **P3 响应式布局已完成（2026-09-24）** | 新增 `ui/layout.{hpp,cpp}`（断点算法，constexpr 纯函数）；`ui/ui.cpp` 按断点重排（三栏 / 双栏 / 单栏+悬浮摇杆，顶栏与动作库自适应）；`ui/theme.{hpp,cpp}` 改动态字号 + 样式基线快照；`main_android.cpp` 做密度归一、摇杆几何取自布局、触摸与鼠标按输入方式切换、摇杆不得抢走急停/阻尼的触摸；`AndroidManifest.xml` 放开竖屏（`fullSensor`）；新增 `tests/layout_test.cpp`（编译期断言）。详见 `docs/responsive_layout_plan.html` 与提交记录 | 已完成 | 4 个改动文件编到目标文件通过；60+ 条 static_assert 全过（真机验收待做） |
| **P4 iOS** | SDL2/Uikit + Metal + Local Network 权限 + Keychain + TestFlight | 1~2 周 | iPhone/iPad 全流程通过审核内测 |
| **P5 远程中继**（可选） | 局域网常驻网关（树莓派/小主机）：对外暴露加密 WebSocket，内部独占连狗；多客户端串行化 | 1 周 | 手机 4G 下可控，且与局域网直连体验一致 |

> 每阶段结束都跑同一套回归清单（发现/连接/校验/遥控/急停/动作/断线重连），确保"功能一致"。

---

## 7. 如何确保各端体验一致

1. **单一实现**：协议/加密/指令表/状态机只在 `core` 有一份；各端 UI 只是"皮肤"
2. **共享 UI 组件**：`ui/cards|joystick|actions|log` 编译进所有端，主题 token（配色/圆角/字号）来自 `theme.cpp` 同一份
3. **能力探测代替平台分支**：所有"能不能用某动作/某模式"的判断都基于固件与回执，与平台无关
4. **统一错误码 + 日志格式**：`[回执] 动作名 (api N) —— 失败（code=3203）：原因`，任何端贴出来都能对照排查
5. **CI 矩阵回归**：core 单测在三平台 CI 跑；协议层用**录制的报文回放**测试（不依赖真机）
6. **交互等价表**：把"桌面操作 ↔ 手机手势"写成对照表（如 空格键 ↔ 常驻急停悬浮钮；鼠标拖拽 ↔ 触摸双摇杆），逐项确认可达
7. **同一份文档**：`README.md` + 本文 + `docs/go2_webrtc_protocol.md` 各端共用，不写平台特有的"小抄"

---

## 8. 已知限制与不可行项（必须提前对齐预期）

1. **iOS 后台**：系统会在切后台后挂起 socket 与定时器 → **不可能**后台持续遥控；切后台必须停车断开
2. **Android 后台**：Doze/省电会限制；长时后台同样不可靠
3. **纯浏览器方案**：局域网 HTTP（`http://192.168.x.x:9991`）会被混合内容 / Private Network Access 拦截，
   WASM 也无法直接发 UDP → 除非配本地代理
4. **手机做不了**：有线 DDS 调试、抓包（需混杂模式）、网口直连（`192.168.123.x`）—— 这些留在桌面/CLI
5. **机器狗只允许一条 WebRTC 连接**：多端同时控制必须串行（先连者占位，其余提示"被占用"）
6. **新固件 DDS 已加密**：不存在"绕过 WebRTC 的捷径"，所有平台都只能走 WebRTC 数据通道
7. **macOS OpenGL 弃用**：可用但会告警；长期需切 Metal 后端（ImGui 有现成实现）
8. **首次构建成本**：vcpkg 编译 OpenSSL + libdatachannel 在 Windows 上约 10~20 分钟（CI 需缓存）

---

## 9. 风险与缓解

| 风险 | 影响 | 缓解 |
|---|---|---|
| 移动端多点触控在 ImGui 下实现复杂 | 双摇杆体验差 | 先做"单摇杆 + 转向按钮"最小可用版，再迭代双摇杆；摇杆区域用原生层自绘，不经 ImGui |
| libdatachannel 在移动端集成未知 | iOS/Android 可能需自编译 | 该库支持 Android/iOS，先在 P3 做 spike（半天验证能否建通道） |
| iOS 审核不确定 | 上架受阻 | 先 TestFlight 内测；准备好用途说明与隐私政策 |
| 多平台 bug 分叉 | 维护成本上升 | 所有平台差异收敛在 `platform/`，禁止在 `core`/`ui` 里写平台分支 |
| 字体/内嵌资源体积 | APK/IPA 变大 | 字体子集化（只保留常用 3500 字 + 符号） |
| 手机性能/发热 | 高频推流与 UI 重绘 | 渲染限帧（30/60 fps 可选）、日志与状态刷新降频 |

---

## 10. 需要改动的具体文件（P0/P1 落到代码）

| 文件 | 改动 |
|---|---|
| `client/src/robot_client.cpp` | 移除 `arpa/inet.h` 等直接包含，改用 `platform/net.hpp`；错误码/超时归一 |
| `client/src/signaling.cpp` | 端口探测与 socket 收发改用 `platform::net` |
| `client/src/discovery.cpp` | 保留 `#ifdef`，但实现体下沉到 `platform/net` 的接口枚举与多播加入 |
| `client/src/local_keys.cpp` | 配置目录走 `platform::paths`；钥匙读取走 `platform::storage`（不再明文） |
| `client/src/main.cpp` | 拆成 `apps/desktop/main.cpp`（GLFW）+ `apps/cli/main.cpp`（现有 --verify） |
| `client/src/ui.cpp` | 抽出组件；增加触摸/DPI/断点布局 |
| `client/CMakeLists.txt` | 拆 `core`(STATIC) / `platform`(按平台选源) / `apps`；加 Presets 与 vcpkg manifest |
| 新增 | `platform/{net,paths,storage,lifecycle,font}.{hpp,cpp}`、`CMakePresets.json`、`vcpkg.json`、CI workflow |

---

## 附：一句话总结

**核心（协议/加密/指令/决策）只有一份，UI 与平台能力各自适配** —— 桌面先把三平台打通（1~2 周），
移动端复用同一核心换 SDL2 + 触摸 UI（各 1~2 周），浏览器直连不可行，跨网段遥控需要局域网中继网关。
