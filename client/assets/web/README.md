# 网页界面（Vue3，免构建）

Go2 控制台的**第二套界面**：与 ImGui 原生界面功能对应，但交互更现代（渐变 / 毛玻璃 / 过渡动效
在 CSS 里只是几行）。**只换界面、不碰协议** —— 协议 / 加密 / 钥匙库 / 运动指令表全在 C++，
前端只跟本机 HTTP 服务说话。

## 1. 运行在三处（同一份代码）

| 场景 | 怎么跑 |
|---|---|
| **桌面**（WSL/Linux/Windows 原生） | `go2_remote` 启动时内建服务，浏览器打开 stdout 打印的地址（默认 `http://localhost:8123`，端口被占自动 +1 重试 5 次） |
| **安卓** | app 启动即起服务并自动打开内嵌 WebView（`WebUiActivity`）；返回键回到 ImGui 界面，两套共存 |
| **纯预览**（不连狗，Windows 也有解） | `python tools/web_preview.py` —— 假数据顶掉 API，专门用来看/改界面 |

> 为什么不直接连 C++ 进程调试：WSL 里起的服务 Windows 浏览器连不上（回环地址不跨系统，
> 实测 `networkingMode=mirrored` 也一样），所以预览走 `tools/web_preview.py`。

## 2. 目录结构

```
assets/web/
├── index.html              # 入口；importmap 把 'vue' 指到本地 vendor（免构建，无 npm）
├── style.css               # 全部样式（深色玻璃风 / 断点 600·900·1600px）
├── api.js                  # fetch 封装：GET /api/state + POST /api/command
├── store.js                # 响应式单例（页面/弹窗/设备/参数）+ refresh/cmd/post
├── motion.js               # planMotion —— 与 core/motion.hpp 同一套急停语义
├── icons.js                # 图标码点 + 动作→图标表（**由 tools/icons/gen_icons.py 生成**）
├── fonts/                  # 图标字体：Phosphor.ttf / Phosphor-Bold.ttf / TianshuGo2.ttf
│                           #   （= client/assets/fonts/ 的副本，同步生成）
├── img/go2.jpg(@2x)        # 狗卡片配图（Wikimedia Commons，CC BY 3.0，见 CREDITS.txt）
├── vendor/vue.esm-browser.prod.js
└── components/
    ├── topbar.js           # 顶栏：品牌 + 受控/电量胶囊 + 设备/页面切换/更多/急停（4 键）
    ├── remote.js           # 遥控页：急停+阻尼同行 → 参数滑条 / 快捷 D-pad + 读数
    ├── actions.js          # 动作库页：分组瓷砖网格（跳过 Gait 组，与 ImGui 端一致）
    ├── band.js             # 摇杆带：受控狗卡片行（多选/横滑）+ 单控/群控 + 双摇杆
    ├── joystick.js         # 单个摇杆（pointer 事件，360° 全向，双杆可同时拖）
    ├── slider.js           # 苹果风滑条（细轨道+填充段+悬浮旋钮；松手才下发）
    ├── devices.js          # 设备弹窗：扫描/添加/改名/单控/连接/删除
    ├── settings.js         # 设置弹窗：MCF / 隐私 / 钥匙库 / 从机器狗找钥匙
    └── log.js              # 日志弹窗（自动滚底、异常标红、打开即清角标）
```

**改前端必须同步两处**：本目录是源，`client/apps/android/app/src/main/assets/web/` 是打进 APK 的
副本 —— 改完要复制过去并重打 APK。另外安卓端 `main_android.cpp` 有一张 `kWebFiles[]` 硬编码
文件清单，**新增文件要加一行**（APK 里的 assets 无法枚举，只能逐个导出）。

## 3. 后端 API（`ui/web_bridge.{hpp,cpp}`）

服务由 `go2::startWebUi(mgr, ui, port)` 在后台线程启动；静态目录 `assets/web`
（相对工作目录，桌面端 = `client/`，安卓端 = 应用专属目录）。

### `GET /api/state` → 全量快照（前端每 500ms 轮询）

| 字段 | 说明 |
|---|---|
| `robots[]` | `{ip, name, label, selected, battery, mode, state, ready}` |
| `selectedCount` / `target` / `batteryMin` | 受控台数 / 一句话（`单控 · 名字`）/ 受控里最低电量 |
| `actions[]` | `{key, label, group(0-5), risky, toggle, ack?}`（group=1 是 Gait 组，前端跳过；`ack` 为上次回执 code——0 成功 / 3203 固件不支持，未试过不下发该字段） |
| `toggles` | 开关型动作当前状态（`{key: bool}`） |
| `params` | `maxLinSpeed / yawRate / speedScale / bodyHeight / footRaise / speedLevel / gaitType / mcf / privacy / hideUnsupported` |
| `cmd` | 最近一次下发的 `{vx, vy, vz}` |
| `log[]` | 最近 40 条日志（新版在前） |
| `estop` / `problemUnread` / `keyCount` / `scanning` | 急停锁 / 未读异常数 / 钥匙数 / 局域网扫描中 |
| `keyScan` | `{running, note, candidates[]}` —— 「从机器狗找钥匙」的结果 |

### `POST /api/command`（body 为 JSON，返回 `{ok[, error]}`）

| cmd | 参数 | 说明 |
|---|---|---|
| `action` | `key` | 触发动作；开关型按 `toggles` 状态翻转 `{"data":bool}` |
| `select` | `mode: all/none/one/multi` (+`ip` / `ips[]`) | 群控全选 / 全取消 / 只控一台 / **精确勾选集合** |
| `move` | `x,y,z` 或 `stop:true` | 摇杆速度（前端 10Hz 节拍）；急停锁定中直接拒绝 |
| `quick` | `dir: fwd/back/left/right` | 快捷方向（用 `speedScale` 档位） |
| `param` | `name, value` | 改遥控参数 / `mcf` / `privacy` / `hideUnsupported`（带范围钳制） |
| `estop` / `unestop` | — | 急停（停全部就绪狗 + 逐个关持续模式）/ 解除 |
| `damp` | — | 强制阻尼（狗会趴下） |
| `add` / `connect` / `remove` | `ip` | 添加并连接 / 连接 / 移除 |
| `connectall` / `disconnectall` | — | 设备列表全部连接（错峰 600ms）/ 全部断开 |
| `clearlog` | — | 清空运行日志 |
| `rename` | `ip, name` | 改名（空名恢复显示 IP） |
| `scan` | — | 局域网扫描（与 ImGui 端同一份 `startScan`） |
| `keyprobe` | `ip?` | **从机器狗找钥匙**：扫端口 + Web 服务抓 32 位 hex（后台线程） |
| `keyapply` | `key, ip` | 采用候选钥匙（追加 keys.txt + addAesKeys + bindKey） |
| `logseen` | — | 清「日志」角标 |

## 4. 关键设计约定（改前端必读）

- **摇杆语义与 C++ 完全一致**：`motion.js` 是 `core/motion.hpp` 的镜像 —— 左杆平移、
  右杆横向转向、急停锁定时一律不发、松手补一次 stop。改一边必须改另一边。
- **10Hz 下发走 `post()` 不走 `cmd()`**：`cmd()` 每次回拉 state，摇杆 10Hz 会把轮询打爆。
- **拖滑条只改本地值，松手才 `commit`**：后端 500ms 才回一次快照，拖动中会被旧值拽回去。
- **图标表别退回关键词匹配**：`icons.js` 的 `ACTION_ICON` 与 `ui.cpp iconGlyph()` 是同一张
  按 key 精确对应的表（关键词会把"前跳/跳跃奔跑/自由跳跃"全撞成同一个图标）。
- **字重是渲染上下文的属性，不是图标的属性**：Phosphor 的 Regular 与 Bold
  **码点完全一致**（1530 个图标逐一核对），所以切字重只是换一份字体/一个 CSS class，
  字形串照旧可用。顶栏与急停等主操作区加粗，弹窗标题/狗卡片等次要层级保持 Regular。
  C++ 端对应的是 `theme::iconFontBold()`。
- **两套字体各司其职**（`gen_icons.py` 的 `TABLE` = 界面框架，`ACTION_ICON` = 动作）：
  - `Phosphor.ttf` / `Phosphor-Bold.ttf`（MIT）→ **界面框架**图标
    （设备/设置/急停/方向键…），字重靠 `.ic` / `.ic-b` 切。
  - `TianshuGo2.ttf` → **动作图标**：宇树官方 App「天树探界遥控」那套**人形动作剪影**
    （站立/坐下/拜年/翻滚/倒立…），由 `tools/icons/svg2font.py` 从其 APK 的 SVG 转出。
    **为什么要单独一套**：剪影能一眼读出在做什么动作，而抽象符号（天平=平衡、床=趴下、
    相机=摆姿势）读不出来 —— 那正是旧动作图标"廉价感"的根源。Phosphor 之类开源库
    **没有**这类动作剪影。
    ⚠ 授权：来自第三方 App 的专有资源，**仅限本地自用/内部部署**，
    对外开源分发前必须替换 —— 详见 `client/assets/fonts/TIANSHU_LICENSE_NOTE.md`。
- **动作图标按码位区间选字体**：天树私有区 U+E100~U+E1FF（UTF-8 首字节恒为 `0xEE`）。
  网页端靠 `font-family: 'Tianshu', 'Phosphor'` 的回退链，C++ 端靠
  `glyphIsTianshu()`（`ui_actions.cpp`）。**不要**在动作表里再维护一份
  "这个动作用哪个字体"的标记 —— 那是最容易漂移的地方。
- **`icons.js` 和 `fonts/` 都是生成产物，不要手改**：改图标请改
  [`tools/icons/gen_icons.py`](../../../tools/icons/gen_icons.py) 再
  `python tools/icons/gen_icons.py` —— 它同时产出 C++ 的 `ui/icons.hpp`、网页的
  `icons.js` 和全部字体，所以两端**不可能**再漂移。脚本会校验每个 Phosphor 图标名
  在 Regular/Bold 两套 CSS 里都存在、两套码点一致、每个天树引用都能解析。
- **换图标必须先用眼睛确认**：脚本只能校验"名字存在"，**名字存在 ≠ 字形好看、
  ≠ 语义对得上**。2026-10-06 踩过这个坑 —— 试着把前/后空翻换成 `flip-*`、
  左/右空翻换成 `arrow-circle-*`、扭屁股换成 `waves`，渲染出来分别是
  像"Λ"的怪形状、像时钟的圆圈箭头、像"≈"的数学符号，**全都比原来的还难看，
  已全部回退**。所以：改完跑 `python tools/web_preview.py` 打开
  `?page=actions` 看一眼，再决定留不留。
- **`?page=actions` 可直接落到动作库页**（预览 / 截图时省一次点击）。
- **网页界面不要显示 Gait 组**（group===1）：与 ImGui 端动作库口径一致（用户已裁掉）。

## 5. 维护清单（每次改前端）

1. 改 `client/assets/web/` 里的文件；
2. 复制变更文件到 `client/apps/android/app/src/main/assets/web/`；
3. 新增文件 → 在 `apps/android/native/main_android.cpp` 的 `kWebFiles[]` 加一行；
4. 桌面验证：`tools/web_preview.py` 看布局 + `ninja go2_remote` 起真服务；
5. 平板：`_run_gradle.bat` → `adb install -r`（启动时按文件大小自动覆盖导出，logcat 会打
   `网页界面资源: 更新 N 个`）。
