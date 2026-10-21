# Go2 Pro 本体自带动作清单（2026-10-20 实测）

> 实测对象：Go2 Pro，**网线直连**，运动模式 `mcf`，固件握手 `data2=2`（固件 < 1.1.15，走内置静态密钥）。
> 复现命令：`python3 tools/probe_actions.py 192.168.123.161`
> 探测过程**不执行任何一次性动作**；下面标「未实测」的一律是"会让狗真的动"的指令。

---

## 0. 一句话结论

这台狗当前跑在 **MCF（Multi-Control Framework）模式**，因此**只受理 MCF 指令集的 api_id**：

- **43 条** MCF 动作里，**24 条已实测受理**（`code=0`）
- **22 条** normal 专属动作全部被拒（`code=3203`）——其中 5 条已实测确认
- 剩 **19 条**会真的让狗动（空翻/起立/舞蹈…），未实测

> ⚠ 项目里 `client/core/sport_library.cpp` 现在按「normal 优先 + 3203 才回退 MCF」来发指令。
> 对这台狗**这个顺序是反的**：normal 专属 id 一律 3203，白白多发一次再回退。
> 正确做法是按 `motion_switcher` 返回的模式选指令集。

---

## 1. 连接信息

| 项 | 值 |
|---|---|
| 机器狗 IP（以太网口） | `192.168.123.161` |
| 本机以太网 | `192.168.123.100/24`（无网关，直连狗的交换机） |
| 开放端口 | `9991`（WebRTC 信令）、`8551`（RTSP 视频）、`80`；**无 22/SSH** |
| 信令握手 | `GET :9991/con_notify` → `{"data2":2}` → 静态 GCM key 解密（固件 < 1.1.15） |
| 连接方式 | `UnitreeWebRTCConnection(WebRTCConnectionMethod.LocalSTA, ip=...)`，DataChannel 校验通过 |
| 运动模式 | `rt/api/motion_switcher/request` api_id 1001 → `{"form":"0","name":"mcf"}` |

> 排查坑：本机若开着 clash/verge 的 **TUN 模式**，会把 `192.168.123.0/24` 的 TCP 探测全部劫持，
> 表现为"整段网段的 8551/9991 都是通的"。**探测前先关 TUN**，否则结果是假的。

## 2. 机器狗当前状态快照

| 来源 | 内容 |
|---|---|
| `rt/lf/sportmodestate` | `mode=0`、`gait_type=0`、`body_height=0.313 m`、`foot_raise_height=0`、`error_code=100` |
| `rt/lf/lowstate` | 12 个电机（q 角度有值 → **处于站立承重状态**），电机温度 25~31 °C，IMU 温度 79 °C |
| 同上 · 电池 | `soc=62%`、`power_v=29.06 V`、`current=-2127 mA` |
| `rt/multiplestate` | `obstaclesAvoidSwitch=true`、`uwbSwitch=true`、`volume=10`、`brightness=0` |
| `rt/api/obstacles_avoid/request` 1002 | `{"enable":true}` → **避障已开** |
| `rt/api/sport/request` 2055 | `{"data":true}` → **自动恢复已开** |
| `rt/api/sport/request` 1026 | `{"data":1}` → 速度档位 = 1 |

> `rt/sportmodestate`（不带 `lf/`）**没有数据**，这台狗只发 `rt/lf/sportmodestate`。
> `rt/servicestate`、`rt/selftest`、`rt/lf/bmsstate`、`rt/lf/mainboardstate` 也都没有数据。

---

## 3. 回执码语义（实测确认）

| 回执 | 含义 |
|---|---|
| `code = 0` | 该 api_id **被受理**。⚠ 动作类 id 会**真的执行** |
| `code = 3203` | 该 api_id **不被受理**（不存在，或不属于当前指令集）—— 实测 `1999 / 9999 / 12345` 全部 3203 |
| 超时无回执 | 服务没响应（Move 这类 no-reply 指令就是这种情况） |

> ⚠ **超时之后不要再用同一条连接连发指令**（2026-10-20 实测踩到）：
> 一次 `wait_for` 超时取消掉 pending future 之后，**同一条 WebRTC 连接上后续所有
> `req` 都收不到回执**（连刚测通过的 1004/1002 都变成超时），但**指令本身照样发到狗、
> 狗照样执行**。表现是"回执全超时、但 `body_height` 一直在变"。
> 结论：**超时 ≠ 不支持**。判定支持与否必须**每条动作单独开一条连接**、
> 并给足回执超时（舞蹈这类长动作 30s 起）。

**一条重要的反面结论**：固件**不校验参数**。给 `GetState(1034)` / `GetSpeedLevel(1026)` /
`GetAutoRecovery(2055)` 发畸形参数 `{"__probe__":1}`，回执依然是 `code=0` 且数据正常。
→ **不能靠"发错参数"来做免执行探测**，唯一可靠的免执行手段就是本文用的两类：
① 只读查询；② 开关类按安全方向置位。

---

## 4. 动作清单

### 4.1 已实测受理（33 条）

| api_id | 名称 | 中文 | 实测方式 |
|---|---|---|---|
| 1003 | StopMove | 停止移动 | 直接发（本身是"停"） |
| 1007 | Euler | 姿态角 | `{"x":0,"y":0,"z":0}`（零姿态角） |
| 1008 | Move | 移动 | `{"x":0,"y":0,"z":0}`（零速度） |
| 1015 | SpeedLevel | 速度档位 | `{"data":1}`（= 当前值） |
| 1019 | ContinuousGait | 持续步态 | `{"data":false}` |
| 1026 | GetSpeedLevel | 查速度档位 | 只读 |
| 1027 | SwitchJoystick | 摇杆模式 | `{"data":false}` |
| 1028 | Pose | 摆姿势 | `{"data":false}` |
| 1034 | GetState | 查运动状态 | 只读（回执 data 为空） |
| 1061 | StaticWalk | 静步态 | `{"data":false}` |
| 1062 | TrotRun | 小跑步态 | `{"data":false}` |
| 1063 | EconomicGait | 经济步态 | `{"data":false}` |
| 2044 | HandStand | 倒立 | `{"data":false}` |
| 2045 | FreeWalk | 自由行走 | `{"data":false}` |
| 2046 | FreeBound | 跳跃奔跑 | `{"data":false}` |
| 2047 | FreeJump | 自由跳跃 | `{"data":false}` |
| 2048 | FreeAvoid | 自由避障 | `{"data":true}`（往安全方向） |
| 2049 | ClassicWalk | 经典步态 | `{"data":false}` |
| 2050 | BackStand | 后仰站立 | `{"data":false}` |
| 2051 | CrossStep | 交叉步 | `{"data":false}` |
| 2054 | SetAutoRecovery | 设自动恢复 | `{"data":true}`（往安全方向） |
| 2055 | GetAutoRecovery | 查自动恢复 | 只读 |
| 2056 | LeadFollow | 领航跟随 | `{"data":false}` |
| 2058 | SwitchAvoidMode | 避障模式 | `{"data":true}`（往安全方向） |

**以下 9 条是"真发出去、狗真做了"**（回执 `code=0`，且 `rt/lf/sportmodestate` 的
`body_height` 出现对应变化）—— 这是唯一有物理证据的一档：

| api_id | 名称 | body_height 变化 | 结论 |
|---|---|---|---|
| 1004 | StandUp 站立 | 0.322 → 0.334 | ✅ 站起来了 |
| 1002 | BalanceStand 平衡站立 | 0.334 → 0.327 | ✅ |
| 1005 | StandDown 趴下 | 0.327 → **0.086** | ✅ 确实趴下了 |
| 1006 | RecoveryStand 恢复站立 | 0.086 → 0.323 | ✅ 从趴姿站起 |
| 1009 | Sit 坐下 | 0.323 → **0.235** | ✅ 坐下了 |
| 1010 | RiseSit 起立(坐姿) | 0.235 → 0.326 | ✅ 从坐姿站起 |
| 1016 | Hello 打招呼 | 0.326 → 0.326 | ✅（站立中做） |
| 1017 | Stretch 伸懒腰 | 0.326 → 0.323 | ✅（站立中做） |
| 1020 | Content 满意 | 0.323 → 0.308 | ✅（站立中做） |

### 4.2 MCF 独占动作（normal 集里没有，共 17 条）

| api_id | 名称 | 中文 | 状态 |
|---|---|---|---|
| 1061 / 1062 / 1063 | StaticWalk / TrotRun / EconomicGait | 静步态 / 小跑步态 / 经济步态 | ✅ 已实测 |
| 2041 | LeftFlip | 左空翻 | ❓ 未实测（高危） |
| 2043 | BackFlip | 后空翻 | ❓ 未实测（高危） |
| 2044~2058 | HandStand / FreeWalk / FreeBound / FreeJump / FreeAvoid / ClassicWalk / BackStand / CrossStep / SetAutoRecovery / GetAutoRecovery / LeadFollow / SwitchAvoidMode | 倒立 / 自由行走 / 跳跃奔跑 / 自由跳跃 / 自由避障 / 经典步态 / 后仰站立 / 交叉步 / 设自动恢复 / 查自动恢复 / 领航跟随 / 避障模式 | ✅ 已实测 |

### 4.3 尚未实测（10 条）

| api_id | 名称 | 中文 | 状态 |
|---|---|---|---|
| 1001 | Damp | 阻尼（关电机瘫软） | ❓ 回执超时，结论不明（见 §3 注） |
| 1022 | Dance1 | 舞蹈 1 | ❓ 回执超时，结论不明 |
| 1023 | Dance2 | 舞蹈 2 | ❓ 回执超时，结论不明 |
| 1029 | Scrape | 拜年(作揖) | ❓ 回执超时，结论不明 |
| 1036 | Heart | 比心（normal 集叫 FingerHeart） | ❓ 回执超时，结论不明 |
| 1030 | FrontFlip | 前空翻 | ⚠ 高危，未测 |
| 1031 | FrontJump | 前跳 | ⚠ 高危，未测 |
| 1032 | FrontPounce | 前扑 | ⚠ 高危，未测 |
| 2041 | LeftFlip | 左空翻 | ⚠ 高危，未测 |
| 2043 | BackFlip | 后空翻 | ⚠ 高危，未测 |

> 需要补测时：`python3 tools/probe_actions.py <ip> --exec`（**不含**高危动作）
> `python3 tools/probe_actions.py <ip> --exec --exec-risky`（含高危，需空旷场地）
> **狗必须落地、四周清空、有人看护。**

### 4.4 normal 专属动作（22 条）—— 这台狗**全部 3203 不受理**

| api_id | 名称 | 中文 | 判定 |
|---|---|---|---|
| 1011 | SwitchGait | 步态切换 | ✅ 已实测 3203 |
| 1013 | BodyHeight | 机身高度 | ✅ 已实测 3203 |
| 1014 | FootRaiseHeight | 抬腿高度 | ✅ 已实测 3203 |
| 1024 | GetBodyHeight | 查机身高度 | ✅ 已实测 3203 |
| 1025 | GetFootRaiseHeight | 查抬腿高度 | ✅ 已实测 3203 |
| 1012 | Trigger | 触发 | 未实测（按规律应 3203） |
| 1018 | TrajectoryFollow | 轨迹跟随 | 未实测 |
| 1021 | Wallow | 撒娇打滚 | 未实测 |
| 1033 | WiggleHips | 扭屁股 | 未实测 |
| 1035 | EconomicGait | 经济步态（MCF 里是 1063） | 未实测 |
| 1039 | StandOut | 站立展示 | 未实测 |
| 1042 | LeftFlip | 左空翻（MCF 里是 2041） | 未实测 |
| 1043 | RightFlip | 右空翻 | 未实测 |
| 1044 | BackFlip | 后空翻（MCF 里是 2043） | 未实测 |
| 1045 | LeadFollow / FreeWalk | 领航跟随 / 自由行走（MCF 里拆成 2056 / 2045） | 未实测 |
| 1050 | Standup | 起立(兼容) | 未实测 |
| 1051 | CrossWalk | 横向行走 | 未实测 |
| 1301 | Handstand | 倒立（MCF 里是 2044） | 未实测 |
| 1302 | CrossStep | 交叉步（MCF 里是 2051） | 未实测 |
| 1303 | OnesidedStep | 单边踏步 | 未实测 |
| 1304 | Bound | 跳跃奔跑（MCF 里是 2046） | 未实测 |
| 1305 | MoonWalk | 太空步 | 未实测 |

> 「未实测」这批**不要去试**：1033/1043/1051/1303/1305 一旦被受理，狗会真的扭/走/踏。
> 要确认它们在 MCF 下确实被拒，只能靠 4.1 的证据链（MCF 表里的 id 全通、表外的 id 全 3203）。

---

## 5. 三个来源的动作页对照

动作名来自**三个互不相同的来源**，必须分开看，否则会以为"App 上有的 = 狗能做的"：

| 来源 | 规模 | 底表 | 对这台狗 |
|---|---|---|---|
| ① 宇树**官方 App** | 9 个表演动作 + 17 个「模式」 | normal 指令集 | 表演动作基本都在册；**模式按钮大量落到 normal-only id → 3203** |
| ② **机械狗本地有、官方 App 做不了** | = MCF 独占的 17 条 | MCF 独占 id | ✅ 已实测 14 条全部受理 |
| ③ **天树探界遥控 App** | 28 个表演剪影 + 33 个模式剪影 | 混合 | 只有 11 个表演剪影有对应 sport 指令 |

### 5.1 官方 App 的 9 个表演动作

| App 上的名字 | api_id | 这台狗 |
|---|---|---|
| 伸懒腰 | 1017 Stretch | ✅ 在册（未实测执行） |
| 比心 | 1036 Heart | ✅ 在册（未实测执行） |
| 拜年 | 1029 Scrape | ✅ 在册（未实测执行） |
| 舞蹈 1 | 1022 Dance1 | ✅ 在册（未实测执行） |
| 舞蹈 2 | 1023 Dance2 | ✅ 在册（未实测执行） |
| 前跳 | 1031 FrontJump | ✅ 在册（未实测执行） |
| 扑人 | 1032 FrontPounce | ✅ 在册（未实测执行） |
| 翻身 | 1030 FrontFlip（天树剪影 `rollOver`「向前翻滚」对应它） | ✅ 在册（未实测执行，⚠高危） |
| **握手** | **sport 指令表里没有"握手"这个动作** | ❓ 天树有 `shakeHands` 剪影但动作表无对应键 → 要么属机械臂动作，要么不走 sport 通道 |

### 5.2 官方 App 的 17 个「模式」—— 固件里其实是开关/步态类 api_id

关键：**App 上叫"模式"的按钮，在固件里没有一个是"表演动作"**，全是 on/off 开关或步态选择。
而且它们在 normal 集里的 id 在这台狗上**一律 3203**，MCF 里的对应新 id **全部实测受理**：

| App 模式 | 语义 | normal id（3203） | MCF id（实测 ✅） |
|---|---|---|---|
| 阻尼 | Damp 瘫倒 | 1001 | 1001（同 id，在册未实测） |
| 直立 | StandUp | 1004 | 1004（同 id，在册未实测） |
| 卧倒 | StandDown | 1005 | 1005（同 id，在册未实测） |
| 坐下 | Sit / RiseSit | 1009 / 1010 | 1009 / 1010（同 id，在册未实测） |
| 摆姿势 | Pose | 1028 | 1028 ✅ |
| 交叉步 | CrossStep | 1302 | 2051 ✅ |
| 倒立 | HandStand | 1301 | 2044 ✅ |
| 跑步 / 跳跃跑 | TrotRun / Bound | 1304 | 1062 / 2046 ✅ |
| 并腿跑 | Bound | 1304 | 2046 ✅ |
| 经典 | ClassicWalk | —（normal 无） | 2049 ✅ |
| 续航 | EconomicGait | 1035 | 1063 ✅ |
| 牵引 | LeadFollow | 1045 | 2056 ✅ |
| 闪避 | FreeAvoid / SwitchAvoidMode | —（normal 无） | 2048 / 2058 ✅ |
| 常规 | 常规步态（疑似 SwitchGait） | 1011 | ❓ 待确认 |
| 灵动 | 疑似 FreeWalk / ContinuousGait | — | ❓ 待确认（2045 / 1019 均在册） |
| 锁定 | 疑似 SwitchJoystick（锁手柄） | — | ❓ 待确认（1027 ✅ 在册） |

> **这就是「官方 App 上点了没反应」的完整解释**：App 在 normal 模式下发的是 normal id，
> 这台狗在 MCF 模式下只认 MCF id → 全部 3203。
> 项目现有 `sport_library.cpp` 的「normal 优先 + 3203 才回退 MCF」恰好把这个顺序做反了。

### 5.3 「机械狗本地有、官方 App 做不了」= MCF 独占的 17 条

这批是 normal 指令集里**根本没有**、只有固件 MCF 侧才有的动作：

| api_id | 名称 | 中文 | 实测 |
|---|---|---|---|
| 1061 | StaticWalk | 静步态 | ✅ |
| 1062 | TrotRun | 小跑步态 | ✅ |
| 1063 | EconomicGait | 经济步态 | ✅ |
| 2041 | LeftFlip | 左空翻 | ✅ 在册未实测 ⚠ |
| 2043 | BackFlip | 后空翻 | ✅ 在册未实测 ⚠ |
| 2044 | HandStand | 倒立 | ✅ |
| 2045 | FreeWalk | 自由行走 | ✅ |
| 2046 | FreeBound | 跳跃奔跑 | ✅ |
| 2047 | FreeJump | 自由跳跃 | ✅ |
| 2048 | FreeAvoid | 自由避障 | ✅ |
| 2049 | ClassicWalk | 经典步态 | ✅ |
| 2050 | BackStand | 后仰站立 | ✅ |
| 2051 | CrossStep | 交叉步 | ✅ |
| 2054 | SetAutoRecovery | 设自动恢复 | ✅ |
| 2055 | GetAutoRecovery | 查自动恢复 | ✅ |
| 2056 | LeadFollow | 领航跟随 | ✅ |
| 2058 | SwitchAvoidMode | 避障模式 | ✅ |

其中 **2041 左空翻 / 2043 后空翻 / 2047 自由跳跃 / 2050 后仰站立 / 2049 经典步态** 最值得注意：
官方 App 在 normal 集里用的是 1042 / 1044 / 1301 / 1302 / 1304 这些**另一套 id**，
这台狗一个都不认（3203），但 MCF 侧的在册 id 全部受理。

### 5.4 天树探界 App 的 28 个表演剪影

天树那套图标分 `icon_active_*`（表演 28 个）/ `icon_model_*`（模式 33 个）。
**28 个表演剪影里只有 11 个在 sport 指令表里有对应 id**：

| 天树剪影 | api_id | 这台狗 |
|---|---|---|
| dance1 / dance2 | 1022 / 1023 | ✅ 在册 |
| happy（满意） | 1020 Content | ✅ 在册 |
| jumpForward（前跳） | 1031 FrontJump | ✅ 在册 |
| pounceForward（前扑） | 1032 FrontPounce | ✅ 在册 |
| newYear（拜年） | 1029 Scrape | ✅ 在册 |
| rollOver（向前翻滚） | 1030 FrontFlip | ✅ 在册 |
| showHeart（比心） | 1036 FingerHeart/Heart | ✅ 在册 |
| sitDown（坐下） | 1009 Sit | ✅ 在册 |
| stretch（伸懒腰） | 1017 Stretch | ✅ 在册 |
| lieDown（趴下） | 1005 StandDown | ✅ 在册 |
| highWave（挥手） | 1016 Hello | ✅ 在册 |
| hug（撒娇打滚） | 1021 Wallow | ❌ normal-only → 3203 |
| turnOver（侧身翻） | 1042 LeftFlip / 2041 LeftFlip | ❌ 1042 是 3203；2041 ✅ 在册 |
| turnWave（转身摇摆） | 1033 WiggleHips | ❌ normal-only → 3203 |
| **shakeHands（握手）** | — | ❓ sport 指令表里没有 |
| 飞吻 / 击掌 ×2 / 双手举高 / 轻挥手 / 双手比心 / 单手比心 / 奥特曼光线 / 拒绝 / 夹 / 前推 / 鞠躬 | — | ❌ **手臂动作**，需装机械臂 |

> 本机 `rt/lf/lowstate` 暴露 16 个电机槽位，**只有 12 个有读数**（Go2 Pro 标配 12 腿部关节），
> 其余 4 个全零 = 未安装的扩展槽 → **本机没装机械臂**，
> 上表最后一行那批手臂手势**一个也做不了**。
> 官方 App / 天树 App 仍会显示这些按钮 —— 这就是"App 上有 ≠ 这台狗能做"的最典型例子。

---

## 6. 对项目的直接影响

1. **选指令集要看 `motion_switcher` 的回执**，不能固定 normal 优先。
   现有 `sport_library.cpp` 的 `apiIdFor(a, mcfMode)` 依赖一个 `mcfMode` 布尔量，
   这个布尔量应当来自 `rt/api/motion_switcher/request` api_id 1001 返回的 `name` 字段。
2. **normal 专属 id 对这台狗是死路**：项目里 `kApiBodyHeight(1013)` / `kApiContinuousGait` 之类
   常量在这台狗上会拿到 3203，需要按机型能力表屏蔽。
3. **`GetState(1034)` 回执 data 是空的** —— 想读状态只能订阅 `rt/lf/sportmodestate`
   （注意是 `lf/` 那条；`rt/sportmodestate` 没数据）。
4. **避障/自动恢复默认是开的**（`{"enable":true}` / `{"data":true}`）。任何"关掉再打开"的
   自动化逻辑都要保证异常路径也能恢复，否则狗会失去安全兜底。

---

## 7. 复现

```bash
# WSL 里（需要 /root/go2ref 的 unitree_webrtc_connect + aiortc）
python3 /mnt/d/code/project/Go2ProController/tools/probe_actions.py 192.168.123.161

# 追加实测一次性动作（狗会真的动）
python3 /mnt/d/code/project/Go2ProController/tools/probe_actions.py 192.168.123.161 --exec
```

探测脚本默认只做免执行操作：只读查询 + normal-only 对照组 + 零副作用动作 + 开关类按安全方向置位。
