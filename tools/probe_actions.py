#!/usr/bin/env python3
"""查询机器狗本体支持的动作（api_id 能力探测）。

背景
----
宇树 Go2 Pro 的运动服务 `rt/api/sport/request` 在**两套 api_id 空间**里发号：

  · normal 集  —— 官方 App 在「normal 运动模式」下用的一组 id（1001~1051、1301~1305）
  · MCF 集     —— 固件 ≥1.1.7 引入的 Multi-Control Framework 的一组 id
                   （1001~1036 + 2041~2058），MCF 模式下只有这组 id 被受理

2026-10-20 实测这台 Go2 Pro：运动模式是 **mcf**，normal-only 的 id 一律回
`code=3203`（不支持）。所以"这台狗自带的动作"= MCF 表。

回执码语义（实测）
----------------
  code = 0     -> 该 api_id **被受理**（动作类 id 会真的执行！）
  code = 3203  -> 该 api_id **不被受理**（不存在，或不属于当前指令集）
  其他 code    -> 受理了但被拒绝（模式不对 / 状态不允许）

用法
----
  # 默认：只做"免执行"探测 —— 查询 + 开关类按安全方向置位 + 零副作用动作
  python3 tools/probe_actions.py 192.168.123.161

  # 追加实测"会真的让狗动"的一次性动作（需要狗落地、周围清空、有人看护）
  python3 tools/probe_actions.py 192.168.123.161 --exec

  # 连空翻 / 前跳 / 前扑 等高危动作一起实测（需空旷场地 + 全程看护）
  python3 tools/probe_actions.py 192.168.123.161 --exec --exec-risky

依赖：WSL 里的 unitree_webrtc_connect 参考实现（默认在 /root/go2ref）
      + aiortc / cryptography
"""
import argparse
import asyncio
import json
import logging
import sys
import types

for _n in ["sounddevice", "pyaudio", "pydub", "cv2"]:
    try:
        __import__(_n)
    except Exception:
        _m = types.ModuleType(_n)

        class _Any:
            def __getattr__(self, k):
                return _Any()

            def __call__(self, *a, **k):
                return _Any()

        _m.__getattr__ = lambda k: _Any()
        sys.modules[_n] = _m

sys.path.insert(0, "/root/go2ref")
logging.basicConfig(level=logging.CRITICAL)

from unitree_webrtc_connect import (  # noqa: E402
    UnitreeWebRTCConnection,
    WebRTCConnectionMethod,
    RTC_TOPIC,
)

# ---- 免执行：查询类（绝不让狗动）----
QUERIES = [
    (1034, ["state", "bodyHeight", "speedLevel", "gait", "continuousGait", "economicGait"], "GetState 查全身状态"),
    (1026, None, "GetSpeedLevel 查速度档位"),
    (2055, None, "GetAutoRecovery 查自动恢复（MCF 专属）"),
]

# ---- 免执行：normal-only 对照组（用来判定这台狗认不认 normal 指令集）----
NORMAL_ONLY = [
    (1011, {"data": 0}, "SwitchGait 步态切换"),
    (1013, {"data": 0.32}, "BodyHeight 机身高度"),
    (1014, {"data": 0.0}, "FootRaiseHeight 抬腿高度"),
    (1024, None, "GetBodyHeight 查机身高度"),
    (1025, None, "GetFootRaiseHeight 查抬腿高度"),
]

# ---- 免执行：零副作用的一次性动作（参数取当前值 / 全 0）----
NOOP_ACTIONS = [
    (1007, {"x": 0.0, "y": 0.0, "z": 0.0}, "Euler 姿态角（全 0 = 不变）"),
    (1015, {"data": 1}, "SpeedLevel 速度档位（= 当前值 1）"),
    (1003, None, "StopMove 停止移动"),
    (1008, {"x": 0.0, "y": 0.0, "z": 0.0}, "Move 移动（零速度）"),
]

# ---- 免执行：开关类，一律按"安全方向"置位 ----
# 关掉是安全的 -> false；会关掉安全功能的（避障/自动恢复）-> true
TOGGLES_OFF = [
    (1019, "ContinuousGait 持续步态"),
    (1027, "SwitchJoystick 摇杆模式"),
    (1028, "Pose 摆姿势"),
    (1061, "StaticWalk 静步态"),
    (1062, "TrotRun 小跑步态"),
    (1063, "EconomicGait 经济步态"),
    (2044, "HandStand 倒立"),
    (2045, "FreeWalk 自由行走"),
    (2046, "FreeBound 跳跃奔跑"),
    (2047, "FreeJump 自由跳跃"),
    (2049, "ClassicWalk 经典步态"),
    (2050, "BackStand 后仰站立"),
    (2051, "CrossStep 交叉步"),
    (2056, "LeadFollow 领航跟随"),
]
TOGGLES_ON = [
    (2048, "FreeAvoid 自由避障"),
    (2054, "SetAutoRecovery 自动恢复"),
    (2058, "SwitchAvoidMode 避障模式"),
]

# ---- --exec：会真的让狗动，一次性动作（不含高危）----
EXEC_ACTIONS = [
    (1002, None, "BalanceStand 平衡站立", False),
    (1004, None, "StandUp 站立", False),
    (1005, None, "StandDown 趴下", False),
    (1006, None, "RecoveryStand 恢复站立", False),
    (1009, None, "Sit 坐下", False),
    (1010, None, "RiseSit 起立(坐姿)", False),
    (1016, None, "Hello 打招呼", False),
    (1017, None, "Stretch 伸懒腰", False),
    (1020, None, "Content 满意", False),
    (1022, None, "Dance1 舞蹈1", False),
    (1023, None, "Dance2 舞蹈2", False),
    (1029, None, "Scrape 拜年(作揖)", False),
    (1036, None, "Heart 比心", False),
]

# ---- --exec-risky：高危动作（空翻 / 跳跃 / 前扑），需空旷场地 + 全程看护 ----
RISKY_ACTIONS = [
    (1030, {"data": True}, "FrontFlip 前空翻 ⚠高危", True),
    (1031, None, "FrontJump 前跳 ⚠高危", True),
    (1032, None, "FrontPounce 前扑 ⚠高危", True),
    (2041, {"data": True}, "LeftFlip 左空翻 ⚠高危", True),
    (2043, {"data": True}, "BackFlip 后空翻 ⚠高危", True),
]

# 阻尼会关掉电机、让狗瘫软倒地 —— 放最后，测完立刻发 StandUp 把它扶起来
DAMP_ACTION = (1001, None, "Damp 阻尼（关电机瘫软，测完立即扶起）", True)

RESULTS = []


async def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ip", nargs="?", default="192.168.123.161")
    ap.add_argument("--exec", action="store_true",
                    help="追加实测一次性动作（狗会真的动！需落地清场+看护）")
    ap.add_argument("--exec-risky", action="store_true",
                    help="连高危动作（空翻/前跳/前扑）一起实测 —— 需空旷场地")
    ap.add_argument("--gap", type=float, default=2.5, help="两条动作之间的间隔秒数")
    args = ap.parse_args()

    conn = UnitreeWebRTCConnection(WebRTCConnectionMethod.LocalSTA, ip=args.ip)
    print(f"连接 {args.ip} ...", flush=True)
    await conn.connect()
    print("已连接，数据通道就绪\n", flush=True)

    # 订阅运动状态：动作前后的 mode/progress 变化，是"它真的做了"的第一手证据
    STATE = {"mode": None, "progress": None, "body_height": None, "gait_type": None}

    def on_state(msg):
        d = msg.get("data", {})
        for k in ("mode", "progress", "body_height", "gait_type"):
            if k in d:
                STATE[k] = d[k]

    conn.datachannel.pub_sub.subscribe("rt/lf/sportmodestate", on_state)

    def snap():
        return f"mode={STATE['mode']} progress={STATE['progress']} " \
               f"body_h={STATE['body_height']} gait={STATE['gait_type']}"

    async def call(api_id, parameter, label, topic=None):
        topic = topic or RTC_TOPIC["SPORT_MOD"]
        opts = {"api_id": api_id}
        if parameter is not None:
            opts["parameter"] = parameter
        try:
            resp = await asyncio.wait_for(
                conn.datachannel.pub_sub.publish_request_new(topic, opts), timeout=6
            )
            code = resp.get("data", {}).get("header", {}).get("status", {}).get("code", -1)
            data = resp.get("data", {}).get("data", "")
            RESULTS.append((label, api_id, code, data))
            print(f"  [{code:>5}] {label:<38} api_id={api_id:<5} data={str(data)[:120]}", flush=True)
            return code
        except asyncio.TimeoutError:
            RESULTS.append((label, api_id, "TIMEOUT", ""))
            print(f"  [TIMEOUT] {label:<36} api_id={api_id}", flush=True)
        except Exception as e:
            RESULTS.append((label, api_id, "ERR", str(e)))
            print(f"  [ERR] {label:<36} {type(e).__name__}: {e}", flush=True)
        return None

    print("=" * 84)
    print("【0】机器狗当前运动模式")
    print("=" * 84)
    try:
        resp = await asyncio.wait_for(
            conn.datachannel.pub_sub.publish_request_new(
                RTC_TOPIC["MOTION_SWITCHER"], {"api_id": 1001}), timeout=6)
        d = resp.get("data", {}).get("data", "")
        print(f"  motion_switcher -> {d}")
        if d:
            info = json.loads(d)
            if info.get("name") != "mcf":
                print("  ⚠ 机器狗不在 mcf 模式！下面的 3203 判定要重新解读")
    except Exception as e:
        print(f"  <查询失败> {type(e).__name__}: {e}")

    for title, group in [
        ("【1】只读查询（狗不会动）", QUERIES),
        ("【2】normal-only 对照组（用于判定指令集）", NORMAL_ONLY),
        ("【3】零副作用的一次性动作", NOOP_ACTIONS),
    ]:
        print("\n" + "=" * 84)
        print(title)
        print("=" * 84)
        for api_id, param, label in group:
            await call(api_id, param, label)

    print("\n" + "=" * 84)
    print("【4】开关类动作（一律按安全方向置位）")
    print("=" * 84)
    for api_id, label in TOGGLES_OFF:
        await call(api_id, {"data": False}, label + " [关]")
    for api_id, label in TOGGLES_ON:
        await call(api_id, {"data": True}, label + " [开]")

    if args.exec or args.exec_risky:
        print("\n" + "=" * 84)
        print("【5】一次性动作实测 —— 狗会真的动！")
        print("=" * 84)
        print("  确认：狗已落地、四周清空、有人看护。8 秒后开始，Ctrl-C 可中止。")
        print(f"  当前状态: {snap()}")
        await asyncio.sleep(8)

        async def act(api_id, param, label, wait):
            print(f"\n  --- {label} (api_id={api_id}) ---", flush=True)
            await call(api_id, param, label)
            await asyncio.sleep(wait)
            print(f"      动作后状态: {snap()}", flush=True)

        # 先确保站立，再按"安全 → 有风险"顺序跑
        await act(1004, None, "StandUp 站立（起始：先站好）", 3.0)
        for api_id, param, label, _ in EXEC_ACTIONS:
            if api_id == 1004:
                continue  # 已经在开头做过了
            # 舞蹈/拜年这类动作固件不接受打断，做完才回下一个
            await act(api_id, param, label, args.gap if api_id in (1022, 1023) else 2.0)

        if args.exec_risky:
            for api_id, param, label, _ in RISKY_ACTIONS:
                print(f"\n  ⚠⚠ 高危动作：{label} —— 场地清空了吗？")
                await act(api_id, param, label, 4.0)

        # 阻尼放最后：关电机瘫软，测完立刻扶起来
        api_id, param, label, _ = DAMP_ACTION
        await act(api_id, param, label, 3.0)
        await act(1004, None, "StandUp 站立（收尾：把狗扶起来）", 4.0)
        await act(1002, None, "BalanceStand 平衡站立（收尾）", 3.0)
        print(f"\n  收尾后状态: {snap()}", flush=True)

    print("\n" + "=" * 84)
    print("【汇总】")
    print("=" * 84)
    ok = [r for r in RESULTS if r[2] == 0]
    rej = [r for r in RESULTS if r[2] == 3203]
    other = [r for r in RESULTS if r[2] not in (0, 3203)]
    print(f"受理 (code=0)    {len(ok):>3} 条: " + ", ".join(str(r[1]) for r in ok))
    print(f"不支持 (3203)    {len(rej):>3} 条: " + ", ".join(str(r[1]) for r in rej))
    if other:
        print(f"其它回执         {len(other):>3} 条: " + ", ".join(f"{r[1]}->{r[2]}" for r in other))

    await conn.disconnect()
    print("\n已断开", flush=True)


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\n已中止")
