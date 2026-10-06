# Go2ProController 优化方案（2026-10-06）

> 依据：`client/` 源码实测 + `docs/go2_webrtc_protocol.md` + 既有 `docs/multi_go2_pro_solution.md` 记忆。
> 所有"问题"条目均带**代码位置证据**，不用推测。
> 本文档用于后续执行与验收，验收标准见 §5。
> **执行状态见文末 §7**（哪些已落地、哪些仍未做，都带取证命令）。

---

## 0. 一页总览

| 阶段 | 主题 | 解决的问题 | 工作量 | 是否阻塞其它阶段 |
|---|---|---|---|---|
| **M1** | 正确性与安全（P0） | 跨线程数据竞争、Web 控制接口无鉴权暴露、孤儿测试 | 1~2 天 | 是（后续重构都要在稳定基线上做） |
| **M2** | 可验证性（P0/P1） | 无告警开关、无 sanitizer、无 CI、无测试运行器 | 1~2 天 | 是（M3/M4 靠它兜底） |
| **M3** | 结构重构（P1） | `ui.cpp` 2208 行巨石、`ui.cpp` 与 `web_bridge.cpp` 双份调度语义 | 3~5 天 | 否 |
| **M4** | 工程卫生（P2） | Web 资源双份副本、根目录杂物、依赖未锁定、无格式化规范 | 2~3 天 | 否 |
| **M5** | 接缝预留（P3，可选） | 为 DDS/G1 扩展预留 `ITransport` | 后续单独立项 | — |

**总原则**：M1+M2 是"必须做"，做完项目从"能跑"变成"可回归"；M3+M4 是"该做"；M5 只留接缝、不实施。

---

## 1. 优化目标与预期效果

| 编号 | 目标 | 预期效果（可观测） |
|---|---|---|
| **G1** | **正确性**：消除多线程下的未定义行为 | 急停/群控状态读写不再有数据竞争；长时间运行不出现随机崩溃 |
| **G2** | **安全**：收紧控制面暴露面、保护敏感数据 | Web 控制接口默认只在本机可访问；密钥类文件彻底离开源码树 |
| **G3** | **可验证性**：把"人工验证"变成"一条命令" | `ctest` 全绿；ASan/TSan 跑一轮无报告；改断点/动作表能被测试拦住 |
| **G4** | **可维护性**：消除巨石与重复语义 | 单文件最大 ≤ 700 行；业务语义只有一份实现 |
| **G5** | **可扩展性**：为多机型（G1/DDS）铺路 | 传输层与指令表可插拔，不需要改 `RobotManager` |

**预期效果量化（基线 → 目标）**

| 指标 | 当前基线 | 目标 |
|---|---|---|
| 最大源文件行数 | `ui/ui.cpp` **2208** | ≤ 700 |
| 已注册测试目标 | 3（crypto / motion / layout） | ≥ 8（含上表 5 个孤儿+新增） |
| 孤儿测试文件（存在但不参与构建） | **2**（`discovery_test.cpp` / `cloud_test.cpp`） | 0 |
| 编译器告警开关 | **无** | `-Wall -Wextra`，新增告警 0 |
| Sanitizer 配置 | **无** | ASan / TSan 两套 preset，各跑通一轮 |
| CI | **无** | 至少"构建 + 测试"一条流水线 |
| 跨线程裸共享成员 | ≥ **8** 处 | 0（全部 atomic 或加锁） |
| Web 资源重复副本 | **21** 个文件（字节级相同） | 0（构建期单一来源） |
| Web 控制接口监听 | `0.0.0.0`（无鉴权） | 默认 `127.0.0.1`，跨机访问需显式开启 |

---

## 2. 当前主要问题清单（带代码证据）

### P0 · 正确性与安全

| # | 问题 | 证据 | 影响 |
|---|---|---|---|
| **A1** | **`UiState` 跨线程裸共享**：ImGui 渲染线程与 `httplib` 工作线程同时读写同一批成员，且大多无锁/非原子 | `client/ui/web_bridge.cpp:209` `ui.estop = true;`；`:252` `ui.movingSent = false;`；`:266` `ui.cmdVx = ...`；`:223` `ui.toggles.clear();`；`:314`；`client/ui/ui_state.cpp:99` 注释"names 只在界面线程读写，不需要加锁" | 数据竞争（UB）；`std::map/std::set` 在读取中重哈希 → **随机崩溃**；急停状态读到撕裂值 → **安全语义失效** |
| **A2** | **`names` 的无锁不变量被违反**：注释声明只在界面线程读写，实际 Web 端 `rename` 在 HTTP 线程写 | `client/ui/ui_state.cpp:98-99` 注释 vs `client/ui/web_bridge.cpp:242` `ui.setName(...)` | 同上；`robot_names.json` 并发写可能损坏 |
| **A3** | **Web 控制接口默认绑 `0.0.0.0` 且无鉴权**，与头文件明确声明的意图相反 | `client/ui/web_bridge.hpp:15` "⚠ 只监听 127.0.0.1 …… 也没有鉴权，别改成 0.0.0.0" vs `client/ui/web_bridge.cpp:386` `const std::string bindHost = ... : "0.0.0.0";` | 同一 Wi-Fi 下**任何人**可调用 `/api/command` 让狗动起来；文档与代码矛盾 |
| **A4** | **两个测试文件未注册进构建**，静默腐烂 | `client/tests/discovery_test.cpp`、`client/tests/cloud_test.cpp` 存在，但 `client/CMakeLists.txt` 只声明了 `crypto_test` / `motion_test` / `layout_test`（共 3 个 `add_executable`） | 这两个测试**从未被编译过**；`Discovery` 与 `unitree_cloud` 无任何回归保护 |
| **A5** | **无 `enable_testing()` / `add_test()`**，测试不是 `ctest` 可发现的目标 | `client/CMakeLists.txt` 全文无 `enable_testing` / `add_test`（已核对 199 行） | 无法一键跑全量测试，"跑了哪些"靠记忆 |
| **A6** | **无编译告警开关**，且项目历史上已发生过悬垂引用导致的随机段错误 | `client/CMakeLists.txt`（无 `-Wall/-Wextra/-Werror`）；`docs/multi_go2_pro_solution.md` §9.3 记录 `pc_->onLocalDescription` 捕获栈局部变量 → use-after-free | 同类 bug 仍会静默通过编译 |
| **A7** | **无 sanitizer 配置** | 无 ASan/TSan preset；`client/CMakePresets.json` 只有 5 个普通 preset | 数据竞争/越界/泄漏只能靠现场踩 |

### P1 · 可维护性

| # | 问题 | 证据 | 影响 |
|---|---|---|---|
| **B1** | **`ui/ui.cpp` 2208 行巨石**：绘制、布局、摇杆命中、参数面板、动作页、弹窗、扫描入口全在一个 TU | `client/ui/ui.cpp` = 2208 行（实测） | 改动相互干扰；编译时间与 review 成本高；无法单独测试 |
| **B2** | **业务语义双份实现**：Web 端与桌面端各自写一遍急停/移动/快捷/动作的调度逻辑 | `client/ui/web_bridge.cpp:208-224`（急停：stopMove + disablePersistentModes + toggles.clear）与 `client/ui/ui.cpp:865-881`（同一序列，另一份写法） | 两端口径会漂移；**安全动作语义不一致是事故风险** |
| **B3** | **群控循环被复制 6 次**：`forEachSelected` 只在 `ui.cpp` 的匿名命名空间里有一份，Web 端只能内联重写 | `client/ui/ui.cpp:416-424`（唯一实现）；`web_bridge.cpp:58/217/221/250/264/280` 内联 6 处 | 新增一条群控指令要改两处以上 |
| **B4** | **`core/robot_client.cpp` 899 行、`RobotManager` 无测试** | 实测行数；`tests/` 无 `robot_client_test` / `robot_manager_test` | 最复杂、最容易出并发 bug 的模块零覆盖 |
| **B5** | **无 `.clang-format` / `.clang-tidy`** | 仓库根与 `client/` 均无（已扫描，仅第三方目录内有） | 风格漂移 |

### P2 · 工程卫生与可扩展性

| # | 问题 | 证据 | 影响 |
|---|---|---|---|
| **C1** | **Web 资源在安卓端整份重复**：21 个文件字节级相同 | `client/assets/web/**` 与 `client/apps/android/app/src/main/assets/web/**` 逐文件哈希比对：**21/21 same** | 改网页要同步两处；按既定纪律"安卓只同步一次"→ 两边已注定漂移 |
| **C2** | **密钥缓存在源码工作树内** | `client/go2_keys_cache.json` 实际存在于 `client/`（已被 `.gitignore` 忽略） | 打包/复制/`git add -f` 时泄漏；敏感数据不应落在仓库目录 |
| **C3** | **根目录杂物** | 根目录存在 `go2_remote_android.apk`、`last_run.log`、`launch_trace.txt`、`reports/`（部分已忽略） | 与"根目录只放入口/配置/文档"的项目约定不符 |
| **C4** | **Python 依赖仅 `>=` 无锁定** | `requirements.txt` 全为 `pkg>=x`（scapy/pyshark/pandas 等 18 项） | 换机器/隔月复现环境行为不一致 |
| **C5** | **扫描线程悬垂引用** | `client/ui/ui_state.cpp:216-251` detached 线程按引用捕获 `&mgr, &ui` | 退出/重启时 use-after-free |
| **C6** | **构建目录与绝对路径强耦合** | 记忆：改名会坏 `build/CMakeCache.txt`；需手删重配 | 换机器/改目录名成本高（`CMakePresets.json` 已可缓解，但未写入 README 主流程） |
| **C7** | **协议知识全靠实测，无回放基座** | `docs/go2_webrtc_protocol.md` 为实测结论；无"录制-回放"测试 | 协议解析改动无法自动回归 |

---

## 3. 具体优化措施与优先级

### M1 · 正确性与安全（P0，必须先做）

| 措施 | 具体动作 | 涉及文件 | 验收 |
|---|---|---|---|
| **M1-1** 消除跨线程裸共享 | ① `UiState::estop` → `std::atomic<bool>`；② `movingSent`、`cmdVx/Vy/Vz` → atomic 或改为"仅在 UI 线程写、Web 线程只读"的发布语义；③ `names` / `toggles` / `activeToggleIds` 统一加 `std::mutex`（`namesMutex`、`toggleMutex`）并**同步删掉"不需要加锁"的注释**；④ 新增 `UiState::snapshot()` 返回一致性 POD 副本（对齐 `RobotManager::snapshot()` 的既有风格），Web 端只读快照 | `client/ui/ui.hpp`、`client/ui/ui_state.cpp`、`client/ui/web_bridge.cpp` | TSan 跑一轮"Web 急停 + 桌面摇杆"并发操作，0 报告；`grep` 确认 `ui.estop` 等无裸访问 |
| **M1-2** 收紧 Web 控制面 | ① 默认监听改 `127.0.0.1`，`GO2_WEB_HOST=0.0.0.0` 才放开（保留现有 WSL→Windows 的可用路径）；② 放开非回环时**强制要求 token**（`GO2_WEB_TOKEN`，`/api/command` 校验头部）；③ 修掉 `web_bridge.hpp` 与 `.cpp` 的文档矛盾，注释与代码同源 | `client/ui/web_bridge.cpp`、`client/ui/web_bridge.hpp`、`client/apps/serve/main.cpp`、`client/apps/desktop/main.cpp` | 默认启动后从另一台机器访问失败；本机可正常；带 token 可跨机；文档与代码一致 |
| **M1-3** 激活孤儿测试 | 把 `tests/discovery_test.cpp`、`tests/cloud_test.cpp` 加入 `CMakeLists.txt`；修掉因长期未编译而暴露的编译错误 | `client/CMakeLists.txt` | 两个可执行能构建并通过 |
| **M1-4** 修扫描线程悬垂 | detached 线程不再按引用捕获 `mgr/ui`；改为 `RobotManager`/`UiState` 生命周期由 `shared_ptr` 或显式 join 管理；至少在关闭路径等待扫描结束 | `client/ui/ui_state.cpp`、`client/apps/desktop/main.cpp`、`client/apps/serve/main.cpp` | ASan 下"扫描中退出"无 use-after-free |
| **M1-5** 密钥出仓 | `go2_keys_cache.json` 默认路径从 `client/`（cwd）改到用户目录（如 `~/.go2/`，与 `keys.json` 同源策略）；兼容读取旧路径一次并提示迁移；删除工作树内残留文件 | `client/core/robot_manager.hpp`（默认路径）、`client/core/local_keys.cpp` | `client/` 下不再生成密钥文件；旧文件自动迁移 |

### M2 · 可验证性（P0/P1）

| 措施 | 具体动作 | 涉及文件 | 验收 |
|---|---|---|---|
| **M2-1** 告警开关 | `go2_core` / `go2_remote` / `go2_serve` 统一加 `-Wall -Wextra`（先不加 `-Werror`，告警清零后再加）；顺手修高危告警 | `client/CMakeLists.txt` | 构建无新增告警；记录一次告警数基线 |
| **M2-2** Sanitizer preset | 新增 `asan` / `tsan` 两个 configurePreset（`-fsanitize=address|thread -fno-omit-frame-pointer`，Debug） | `client/CMakePresets.json` | `go2_remote` 在两个 preset 下可构建 |
| **M2-3** ctest 接入 | `enable_testing()` + 每个测试 `add_test()`；补 `discovery_test` / `cloud_test` | `client/CMakeLists.txt` | `ctest --output-on-failure` 全绿 |
| **M2-4** 补关键纯逻辑测试 | 新增：`sport_library_test`（api_id 表 + `apiIdFor` 双指令集回退）、`command_service_test`（M3 抽出的调度语义：单控/群控/急停/持续模式开关）、`robot_manager_test`（错峰队列、退避序列、钥匙累加，用 fake client） | `client/tests/*` | 新增 ≥ 3 个测试目标；急停序列被测试钉死 |
| **M2-5** CI | GitHub Actions 两条 job：① Linux 构建 `go2_core/go2_serve` + `ctest`；② 前端自检（复用现有 node `--check` + import 路径校验脚本，固化为仓库脚本） | `.github/workflows/ci.yml`、`scripts/` | PR 上自动出红/绿 |
| **M2-6** 协议回放基座 | 把 `docs` 里的实测报文固化成 fixture（`con_notify` 响应、offer/answer、`res` 回执），写 `signaling_replay_test` 断言解析结果 | `client/tests/`、`client/tests/fixtures/` | 改解析逻辑被测试拦住 |

### M3 · 结构重构（P1）

| 措施 | 具体动作 | 涉及文件 | 验收 |
|---|---|---|---|
| **M3-1** 抽 `CommandService`（**最高价值**） | 把"单控/群控/急停/持续模式/移动/快捷/动作/参数"的**语义**从 UI 与 Web 两份实现里抽到 `core/command_service.{hpp,cpp}`，只暴露 `execute(Command, UiState&, RobotManager&) -> Result`；`forEachSelected` 一并迁入 | 新增 `client/core/command_service.{hpp,cpp}`；改 `client/ui/ui.cpp`、`client/ui/web_bridge.cpp` | 两端口径只剩一份；Web 端不再内联 6 处群控循环；`command_service_test` 覆盖急停与开关语义 |
| **M3-2** 拆 `ui.cpp` | 按职责拆 4~5 个 TU：`ui_chrome.cpp`（顶栏/胶囊/玻璃质感）、`ui_remote.cpp`（遥控页 + 摇杆带 + 命中区）、`ui_actions.cpp`（动作页/网格/图标）、`ui_popups.cpp`（设备/设置/日志弹窗）、`ui_joystick.cpp`（摇杆绘制与触摸） | `client/ui/*.cpp`、`client/CMakeLists.txt` | `ui.cpp` ≤ 700 行；`GO2_SHOT=1` 截图与重构前逐像素对比（允许极小差异） |
| **M3-3** 视觉回归留痕 | 固定 3 个断点尺寸跑 `GO2_SHOT=1`，把 PPM→PNG 存 `docs/screenshots/` 作为基线 | `scripts/`、`docs/screenshots/` | 拆分前后截图可 diff，差异有人工确认 |

### M4 · 工程卫生（P2）

| 措施 | 具体动作 | 验收 |
|---|---|---|
| **M4-1** Web 资源单一来源 | 以 `client/assets/web/` 为唯一源，安卓构建期拷贝（Gradle `copy` task 或 `setup.sh` 步骤），删除 `app/src/main/assets/web/` 的 21 个副本 | 仓库内只剩一份；Gradle 构建后 APK 内含资源 |
| **M4-2** 根目录清理 | `go2_remote_android.apk`、`last_run.log`、`launch_trace.txt` 移入 `reports/`/构建产物目录或删除；确认 `.gitignore` 覆盖 | 根目录只剩入口/配置/文档 |
| **M4-3** 依赖锁定 | `requirements.txt` 拆成 `requirements.txt`（直接依赖）+ `requirements.lock.txt`（`pip freeze` 全量）；README 写明用 lock | 新机器一条命令装出同版本环境 |
| **M4-4** 格式化规范 | 加 `.clang-format`（基于 LLVM，100 列），先只对 `core/`、`ui/` 生效并整仓跑一遍 | 格式统一；review 不再纠结排版 |
| **M4-5** 构建流程入 README | 把 `CMakePresets.json` 的 `cmake --preset linux` 作为推荐主流程写进 `client/README.md`，缓解改路径坏缓存 | 新机器按 README 一次成功 |

### M5 · 接缝预留（P3，只留口子不实施）

- 抽 `core/transport.hpp`（`ITransport`：`send / onData / onState / open / close`），让 `RobotClient` 依赖接口而非直接依赖 `rtc::DataChannel`。
- **不在本方案内实现 DDS**；G1 的完整评估见 `MEMORY.md` 的「G1 扩展评估」条目与 `2026-10-06` 日志。

---

## 4. 限制条件与风险

| # | 限制/风险 | 说明 | 缓解 |
|---|---|---|---|
| **R1** | **无硬件在环 CI** | 任何"连得上狗"的验证都需要真机 + 同网段 + ≥15 s 重连纪律，CI 无法覆盖 | CI 只跑纯逻辑与回放测试；真机验证保留为人工 checklist（`docs/multi_go2_pro_solution.md` §8 已有判定标准） |
| **R2** | **构建强依赖 WSL + FetchContent** | CI 首次构建要联网拉 imgui/libdatachannel 等，耗时与网络不稳定 | CI 缓存 `build/_deps`；或改用系统包 |
| **R3** | **收紧 Web 监听会打断现有用法** | 记忆明确记录"Windows 连不上 WSL 内服务（mirrored 也不行）"，所以当初才绑 `0.0.0.0` | 保留 `GO2_WEB_HOST` 逃生门 + token；默认安全、显式放开 |
| **R4** | **M3 重构期功能冻结** | 拆 `ui.cpp` 与抽 `CommandService` 期间不宜并行加功能 | M1/M2 先做完并打 tag，M3 单独分支；用截图 + `ctest` 双保险 |
| **R5** | **修数据竞争会"暴露"既有隐藏 bug** | 并发修复后，原先被竞态掩盖的时序问题可能显性化（行为变化） | M1-1 与 M2-2（TSan）同批做，先在 TSan 下复现再修 |
| **R6** | **`-Wall/-Wextra` 会产生大量存量告警** | 2208 行的 `ui.cpp` + 899 行的 `robot_client.cpp`，告警可能上百条 | 先只开告警不加 `-Werror`，把基线数量记录为 KPI，分批归零 |
| **R7** | **视觉回归无法自动断言** | ImGui 渲染是字节级像素，跨 GPU/驱动会有噪声 | 只做"人工确认的 diff"（M3-3），不做自动判等 |
| **R8** | **纪律约束** | 用户要求"AI 不主动 commit"、"安卓只同步一次" | 所有改动留在工作区由用户提交；M4-1 的资源同步需用户确认时机 |
| **R9** | **协议侧不可控** | 狗固件版本差异（`data2=2/3`、8081/9991）是外部变量 | 回放测试覆盖已知分支；真机验证矩阵固定 3 台在册设备 |

---

## 5. 如何衡量优化成果

### 5.1 定量指标（每条都可命令化取证）

| # | 指标 | 取证命令/方法 | 基线 | 目标 |
|---|---|---|---|---|
| 1 | ctest 全绿 | `ctest --test-dir build/linux --output-on-failure` | 0（未接入） | ≥ 8 测试、0 失败 |
| 2 | TSan 报告数 | `cmake --preset tsan` 后跑 Web 急停 + 桌面摇杆并发 60 s | 未测 | **0** |
| 3 | ASan 报告数 | `cmake --preset asan` 后跑"扫描中退出""连接后断开" | 未测 | **0** |
| 4 | 新增编译告警 | `-Wall -Wextra` 下构建日志告警条数 | 未开 | 归零（先记录基线） |
| 5 | 最大源文件行数 | 行数统计脚本 | 2208 | ≤ 700 |
| 6 | 群控循环实现处数 | `grep -c "for (const auto& ip : ui.selectedIps())"` | 7（1 + 6 内联） | 1 |
| 7 | 跨线程裸共享成员数 | `grep "ui\.\(estop\|movingSent\|cmdVx\|toggles\|names\)" web_bridge.cpp` | ≥ 8 | 0 |
| 8 | Web 资源重复副本 | 源目录 vs 安卓 assets 逐文件哈希 | 21 | 0 |
| 9 | 孤儿测试文件数 | 对比 `tests/*.cpp` 与 `CMakeLists.txt` | 2 | 0 |
| 10 | Web 默认监听地址 | 启动日志 + 异机 `curl` | `0.0.0.0` 可访问 | 默认仅本机；显式开+token 才跨机 |
| 11 | 密钥文件位置 | 检查 `client/` 下是否出现 `*keys*.json` | 存在 | 不存在（落用户目录） |
| 12 | 真机稳定性（保持） | `./go2_remote --verify <3台> --seconds 600` | `状态=就绪 / 稳定≥80% / 重连 0` | 不低于基线 |

### 5.2 验收门槛（阶段门）

- **M1 完成门**：指标 2/3/9/10/11 达标，且真机 `--verify`（指标 12）不退化。
- **M2 完成门**：指标 1/4 达标；CI 在 PR 上跑通一次。
- **M3 完成门**：指标 5/6 达标；3 个断点截图 diff 经人工确认。
- **M4 完成门**：指标 7/8 达标；新机器按 README 一次构建成功。

### 5.3 定性评估

- 新接手者能否在 1 小时内说清"一条动作指令从 Web 端到狗"的完整链路（M3-1 后链路唯一）。
- 新增一条群控指令需要改几处？目标：**1 处**（指令表 + 语义层）。
- 出现"急停后又动了"时，能否用单一入口排查到持续模式开关（M3-1 后语义收敛到 `CommandService`）。

---

## 6. 执行顺序建议（可直接照做）

```text
① M1-4（悬垂）→ M1-1（数据竞争）→ M1-2（监听/鉴权）→ M1-3（孤儿测试）→ M1-5（密钥出仓）
② M2-1（告警）→ M2-2（sanitizer）→ M2-3（ctest）→ M2-4（补测试）→ M2-5（CI）→ M2-6（回放）
③ M3-1（CommandService）→ M3-2（拆 ui.cpp）→ M3-3（截图基线）
④ M4-1（Web 资源单源）→ M4-2（根目录）→ M4-3（依赖锁定）→ M4-4（格式化）→ M4-5（README）
⑤ （可选）M5 接缝预留
```

> 每完成一个里程碑，按 §5.2 的门槛取证并归档到 `reports/`；真机相关验证遵循既有纪律：连接间隔 ≥15 s、一台狗只保留一个连接。

---

## 7. 执行状态（2026-10-06 落地）

一键取证：`bash scripts/verify.sh --build`（构建 + ctest + 本文所有静态指标）。

### 7.1 已完成

| 措施 | 落地内容 | 取证 |
|---|---|---|
| **M1-1** 消除跨线程裸共享 | `UiState` 里跨线程读写的标量全部改 `std::atomic`；`names`/`toggles`/`activeToggleIds` 改为**私有 + 互斥锁 + 访问器**（`toggleState/setToggle/clearToggles/*Snapshot`、`nameOf/labelOf/setName`）；删掉"names 不需要加锁"的错误注释，`ui.hpp` 顶部新增**线程契约**说明 | `ui_state_thread_test`（4 线程 ×1000 迭代）在 **TSan 下 0 条 data race**；`&ui.estop` / `ui.toggles\b` 等禁止写法命中 **0** |
| **M1-2** 收紧 Web 控制面 | 默认监听改回 `127.0.0.1`；非回环监听**必须**给 `GO2_WEB_TOKEN`，否则**拒绝启动**；令牌经 `X-Go2-Token` 头或 `?token=` 校验（只拦 `/api/*`）；`assets/web/api.js` 支持 `?token=` 自动记住并附带；`web_bridge.hpp` 的文档与代码对齐 | `web_bridge.cpp` 的绑定与 pre-routing 校验；`node scripts/web_check.mjs` 全绿 |
| **M1-3** 激活孤儿测试 | `discovery_test` / `cloud_test` 纳入构建；二者需要真机/会访问官方服务器 → 注册为 `DISABLED` + `LABELS manual`，不污染默认 ctest | `bash scripts/verify.sh` 的"指标 9：未注册测试数 = 0" |
| **M1-4** 修扫描线程悬垂 | 扫描线程从 `detach()` 改为**持有 `std::thread`**，新增 `joinScans()`；桌面/服务入口退出前调用 | 截图自测退出路径不再 `std::terminate`（对照：改动前 exit 时 dump core） |
| **M1-5** 密钥出仓 | 新增 `local_keys` 的 `userDataDir()/defaultKeysCachePath()/defaultKeysTxtPath()/ensureParentDir()`；缓存与 `keys.txt` 默认落到 `~/.go2/`（安卓仍用 `GO2_KEYS_FILE` 同目录）；`RobotManager` 启动时**自动迁移**旧的 `./go2_keys_cache.json` 并删除它；顺带修掉"载入用自定义路径、保存写回默认路径"的老 bug | `client/` 下已无密钥文件；迁移日志见运行输出 |
| **M2-1** 告警开关 | `go2_add_warnings()` 给自有目标统一加 `-Wall -Wextra`（MSVC `/W4`）；存量 8 条告警全部清零（`crypto.cpp` 的 OpenSSL 3.0 弃用 4 条用**局部 pragma + 注释说明**抑制，`key_probe` 未用参数、`web_bridge` 死函数各清掉） | `verify.sh --build` 报 **告警数 = 0** |
| **M2-2** Sanitizer preset | 新增 `linux-asan` / `linux-tsan` 两个 preset（复用 `build/_deps` 源码，不重新下载）；CMake 里 `GO2_SANITIZE=address\|thread` 只作用于自有目标 | `bash scripts/verify_tsan.sh` |
| **M2-3** ctest 接入 | `enable_testing()` + 每个测试 `add_test()` | `ctest` 8 个启用测试全绿 |
| **M2-4** 补关键测试 | 新增 `sport_library_test`（指令表不变量）、`command_service_test`（群控/急停语义，假 sink）、`ui_state_thread_test`（并发回归）；抽 `tests/test_util.hpp`、`tests/fake_sink.hpp` | `ctest` |
| **M2-5** CI | `.github/workflows/ci.yml`：全量编译（含 `go2_remote`，不漏 `ui.cpp`）+ `ctest` + 前端自检 | 工作流文件 |
| **M2-2'** Sanitizer 正对照 | 新增 `tests/sanitizer_probe_test.cpp`：设 `GO2_PROBE=asan\|tsan` 时**故意**越界 / 制造竞争，用来证明"0 报告"不是"工具没生效"。**运行期开关**（不再用编译期宏）→ 验证脚本不用重配置、不用二次构建 | ASan：正对照报 1 条 heap-buffer-overflow；TSan：正对照报 1 条 data race |
| **M2-6** 协议回放基座 | 把**回执**解析从 `robot_client.cpp` 抽成纯函数 `core/protocol.{hpp,cpp}`（`protocol::parseResponse`），11 份固化报文放在 `client/tests/fixtures/`（成功 / 3202 / 3203 / error_code 两种位置 / status 缺失 / 非对象 / 报文截断 / 非运动 topic / 未知 api_id）；新增 `protocol_replay_test`（44 项断言）。信令侧（con_notify / offer·answer / base64 / AES-GCM / SDP 裁剪）已由 `crypto_test` 用真实抓包覆盖，不重复 | `ctest`：`protocol_replay_test: 44 项检查, 0 项失败` |
| **M3-2** 拆 `ui.cpp` | `ui/ui.cpp` **2340 → 572 行**，拆出 5 个 TU：`ui_chrome`（顶栏+急停，222）、`ui_actions`（动作库页+图标表，609）、`ui_remote`（遥控页，244）、`ui_popups`（设备/设置/日志，442）、`ui_joystick`（摇杆绘制+命中区，351）；共享面集中在新头 `ui/ui_internal.hpp`（小控件、弹窗 ID、全局状态、页面声明）。**被搬走的函数体一字未改** —— 靠 `namespace go2::uix` + `using` 让原来的非限定调用继续成立 | **4 个断点尺寸逐像素比对：不同像素 0 个**（见 M3-3）；编译告警 0 |
| **M3-3** 截图视觉基线 | ① 桌面端新增 `GO2_WIN_W/GO2_WIN_H`（按尺寸抓图）与 `GO2_SHOT_PATH`、`GO2_SHOT_EXIT=1`（拍完走完整退出路径，顺带回归退出 bug）；② 新增 `scripts/imgtool.py`（纯标准库 PPM/PNG 读写 + 逐像素 diff）与 `scripts/screenshot.sh`（capture / compare）；③ 基线图存 `docs/screenshots/`（4 张，共 328 KB）：`large-tall 1440x880`、`expanded-regular 1100x720`、`medium-short 820x460`、`compact-short 540x440`，正好覆盖宽 600/900/1280 与高 480/800 两组断点 | `bash scripts/screenshot.sh compare` → 4/4 **0 像素差异**（本机渲染逐位可复现，容差其实用不上） |
| **M5** `ITransport` 传输接缝 | 新增 `core/transport.hpp`（接口：name/label/isOpen/send/close + 5 个回调）与 `core/webrtc_transport.{hpp,cpp}`（实现，回调走 `weak_from_this` 保护，修掉"通道回调打进已析构对象"的隐患）。`RobotClient` 的**唯一发送出口 `sendRaw()`** 与断开路径改走接口；狗自建通道也统一包装（存 `extraChannels_` 保活）。**只抽数据面** —— 信令/加密/心跳留在 `RobotClient`（那是机型协议栈，不属于通道） | 编译 0 告警；`ctest` 全绿；`go2_remote` 启停冒烟无崩溃 |
| **ASan 实测** | `bash scripts/verify_asan.sh`：先增量构建再跑（避免跑陈旧二进制 —— 踩过），ctest + 两个退出场景（"退出时扫描线程仍在跑""启动后立刻退出"）全部 0 报告，正对照 1 报告 | 见 M2-2' |
| **M3-1** 抽 `CommandService` | 新增 `ui/command_service.{hpp,cpp}`：`CommandSink` 窄接口 + `ManagerSink` + `cmd::` 全套语义（急停/锁定/群控目标集合/开关簿记/指令集兜底/参数打包）；桌面端、网页端、安卓端**共用同一份**。群控循环从 7 处变 1 处；**顺带修掉网页端打包参数丢 `speedLevel/姿态角/自定义 JSON` 的实际 bug** | `grep -rho 'for (const auto& ip : ui.selectedIps())' ui/ \| wc -l` = **0** |
| **M4-1** Web 资源单源 | 删除安卓端 22 个字节级重复副本；`app/build.gradle` 用 `syncWebAssets`(Sync) + `assets.srcDirs` 在构建期同步；`android/native/CMakeLists.txt` 补上 `command_service.cpp` | 安卓 `assembleDebug` 成功；APK 内 `assets/web/**` 22 项完整、`assets/fonts/Phosphor.ttf` 仍在 |
| **M4-2** 根目录清理 | `last_run.log` / `launch_trace.txt` 归档进 `reports/`（未删除）；`go2_remote_android.apk` 保留（是给人装的产物，删掉反而添乱） | 根目录列表 |
| **M4-3** 依赖锁定 | 生成 `requirements.lock.txt`（89 行，含生成方式与时间；来源是本仓库 `.venv`） | 文件内容 |
| **M4-4** 格式化规范 | 新增 `client/.clang-format`（LLVM 基底 + 100 列 + 指针左贴 + `SortIncludes: false` 保护成段注释）；**刻意不重排全仓**，规则写在文件头 | YAML 可解析（无 `clang-format` 可执行文件，未做机器校验） |
| **M4-5** README | `client/README.md` 补：预设构建、测试与验证（ctest/TSan/ASan/web_check）、目录结构、已知问题更新；根 `README.md` 补网页服务访问控制 | — |
| **额外** 退出期崩溃 | 桌面入口退出时未调 `stopWebUi()`，全局 `std::thread` 在静态析构期带着 joinable 状态被销毁 → `std::terminate` + dump core。已加显式调用 + 静态守卫双保险 | 截图自测退出无 `terminate` |

### 7.2 尚未完成（如实记录）

| 措施 | 状态 | 说明与建议 |
|---|---|---|
| **真机回归（指标 12）** | ⛔ **外部阻塞：探测不到设备** | 2026-10-06 实测：WSL 侧有 `192.168.2.0/24 dev eth2` 路由，但 `192.168.2.10~16/20/100~102` 的 `8081 / 9991 / 18080` **全部无应答** → 当前没有在线的机器狗。按 §5.2，M1 的完成门要求真机 `--verify` 不退化，这一步只能等设备上电后现场补：`./go2_remote --verify <ip> --seconds 600`（纪律：重连间隔 ≥15 s、一台狗只留一个连接、就绪后 ~10 s 预热）。**在此之前，M1/M5 的"真机不退化"这一项没有证据。** |
| `core/robot_client.cpp` 仍 1034 行 | ⏳ 后续可选 | 超过 700 行阈值，但它是协议/连接核心（`runSignaling` 274 行 + `handleMessage` 113 行），**没有截图可作回归基线**，拆分必须配真机回归才有意义，故本轮不动。建议：等指标 12 可跑之后再把它按 `robot_signaling.cpp` / `robot_messages.cpp` 切开（函数已在类外定义，切文件是纯搬移）。 |
| G1 / DDS 传输实现 | ⏳ 立项后做 | M5 只落了接口与 WebRTC 实现；`DdsTransport`（`rt/api/sport/request` + `rt/lowcmd`）留到 G1 立项。注意**多机 DDS 隔离**是前置条件（见 G1 评估：同域同名 topic 会互相串扰）。 |
| `clang-format` 机器校验 | ⚠️ | 环境里没有 `clang-format` 可执行文件，配置只做了 YAML 语法校验。装了之后先跑 `clang-format --style=file --dry-run -Werror <新改的文件>`（注意：对 `ui_actions.cpp` 这类**整段搬移**的文件先别跑，会产生大量与逻辑无关的 diff） |
| 提交 | ⏳ 按纪律未做 | 本轮全部改动留在工作区，由使用者决定提交时机与粒度（见 `MEMORY.md` 的工程约定：AI 不主动 commit） |

