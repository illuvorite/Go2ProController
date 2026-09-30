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
├── icons.js                # Phosphor 码点 + 动作→图标表（与 ui.cpp 的 iconGlyph 同一张）
├── fonts/Phosphor.ttf      # 图标字体（= client/assets/fonts/Phosphor.ttf 的副本）
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
| `actions[]` | `{key, label, group(0-5), risky, toggle}`（group=1 是 Gait 组，前端跳过） |
| `toggles` | 开关型动作当前状态（`{key: bool}`） |
| `params` | `maxLinSpeed / yawRate / speedScale / bodyHeight / footRaise / speedLevel / gaitType / mcf / privacy` |
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
| `param` | `name, value` | 改遥控参数 / `mcf` / `privacy`（带范围钳制） |
| `estop` / `unestop` | — | 急停（停全部就绪狗 + 逐个关持续模式）/ 解除 |
| `damp` | — | 强制阻尼（狗会趴下） |
| `add` / `connect` / `remove` | `ip` | 添加并连接 / 连接 / 移除 |
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
- **网页界面不要显示 Gait 组**（group===1）：与 ImGui 端动作库口径一致（用户已裁掉）。

## 5. 维护清单（每次改前端）

1. 改 `client/assets/web/` 里的文件；
2. 复制变更文件到 `client/apps/android/app/src/main/assets/web/`；
3. 新增文件 → 在 `apps/android/native/main_android.cpp` 的 `kWebFiles[]` 加一行；
4. 桌面验证：`tools/web_preview.py` 看布局 + `ninja go2_remote` 起真服务；
5. 平板：`_run_gradle.bat` → `adb install -r`（启动时按文件大小自动覆盖导出，logcat 会打
   `网页界面资源: 更新 N 个`）。
