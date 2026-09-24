#!/usr/bin/env python3
"""机器狗运动指令被拒（回执 code=-1）时的诊断 + 恢复流程。

现象：连上就绪，但 BalanceStand/StandUp/Move/StopMove 等全部 code=-1，
只有 RecoveryStand(1006) 返回 0 —— 说明机器人处于"保护/异常状态"，
需要用正确顺序把它唤醒。

本脚本会：
  1) 读状态（mode / gait / error_code / 姿态 rpy / 电量 / 电机温度与错误位）
  2) 安全判断：机体倾斜过大或电量过低时**不发动作**
  3) 按标准顺序尝试恢复：RecoveryStand -> BalanceStand -> StandUp -> StopMove
  4) 每一步都打印回执 code 与动作后的状态，定位卡在哪

用法: python3 _recover.py [ip]        默认 192.168.2.107
"""

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
    UnitreeWebRTCConnection, WebRTCConnectionMethod, RTC_TOPIC, SPORT_CMD)

IP = sys.argv[1] if len(sys.argv) > 1 else "192.168.2.107"

LATEST = {"sport": None, "low": None}


def on_sport(msg):
    LATEST["sport"] = msg.get("data", {})


def on_low(msg):
    LATEST["low"] = msg.get("data", {})


def show_state(tag):
    s = LATEST.get("sport") or {}
    lo = LATEST.get("low") or {}
    rpy = (s.get("imu_state") or {}).get("rpy") or [0, 0, 0]
    soc = ((lo.get("bms_state") or {}).get("soc"))
    temps = [m.get("temperature") for m in (lo.get("motor_state") or [])][:12]
    print(f"\n[{tag}] mode={s.get('mode')} gait={s.get('gait_type')} "
          f"error_code={s.get('error_code')} progress={s.get('progress')}")
    print(f"      姿态 rpy=({rpy[0]:+.2f},{rpy[1]:+.2f},{rpy[2]:+.2f}) "
          f"高度={ (s.get('position') or [0,0,0])[2]:.3f} 电量={soc}%")
    if temps:
        print(f"      电机温度={temps}")
    return s, lo


async def req(conn, label, topic, api_id, parameter=None):
    payload = {"api_id": api_id}
    if parameter is not None:
        payload["parameter"] = parameter
    resp = await conn.datachannel.pub_sub.publish_request_new(topic, payload)
    code = (resp or {}).get("data", {}).get("header", {}).get("status", {}).get("code")
    print(f"  {label:<28} code={code}")
    return code


async def main():
    print(f"连接 {IP} ...")
    conn = UnitreeWebRTCConnection(WebRTCConnectionMethod.LocalSTA, ip=IP)
    await conn.connect()
    print("已连接")

    conn.datachannel.pub_sub.subscribe(RTC_TOPIC["SPORT_MOD_STATE"], on_sport)
    conn.datachannel.pub_sub.subscribe(RTC_TOPIC["LF_SPORT_MOD_STATE"], on_sport)
    conn.datachannel.pub_sub.subscribe(RTC_TOPIC["LOW_STATE"], on_low)
    await asyncio.sleep(3)

    s, lo = show_state("当前状态")

    # ---- 安全判断 ----
    rpy = (s.get("imu_state") or {}).get("rpy") or [0, 0, 0]
    soc = ((lo.get("bms_state") or {}).get("soc")) or 100
    tilt = max(abs(rpy[0]), abs(rpy[1]))
    if tilt > 1.0:
        print(f"\n⚠ 机体倾斜 {tilt:.2f} rad（>57°），可能翻倒/侧躺 —— 为安全起见不发动作。")
        print("   请先把狗摆正到趴卧姿态（腿朝下、机身水平），再运行本脚本。")
        await conn.disconnect()
        return 2
    if soc < 15:
        print(f"\n⚠ 电量仅 {soc}% —— 运动服务通常会拒指令，请先充电。")
        await conn.disconnect()
        return 2

    # ---- 运动模式 ----
    await req(conn, "查询运动模式", RTC_TOPIC["MOTION_SWITCHER"], 1001)
    await asyncio.sleep(0.5)

    # ---- 标准恢复顺序 ----
    print("\n按标准顺序尝试唤醒：")
    seq = [
        ("RecoveryStand(1006)", RTC_TOPIC["SPORT_MOD"], SPORT_CMD["RecoveryStand"], None),
        ("BalanceStand(1002)", RTC_TOPIC["SPORT_MOD"], SPORT_CMD["BalanceStand"], None),
        ("StandUp(1004)", RTC_TOPIC["SPORT_MOD"], SPORT_CMD["StandUp"], None),
        ("StopMove(1003)", RTC_TOPIC["SPORT_MOD"], SPORT_CMD["StopMove"], None),
    ]
    first_ok = None
    for label, topic, api, param in seq:
        code = await req(conn, label, topic, api, param)
        if code == 0 and first_ok is None:
            first_ok = label
        await asyncio.sleep(2.0)
        show_state(f"动作后({label})")

    # ---- 只有恢复类成功时的进一步尝试 ----
    if first_ok in ("RecoveryStand(1006)",):
        print("\n只有 RecoveryStand 成功 —— 试试切换运动模式再下发（有些固件要从 mcf 切出去）：")
        await req(conn, "切 normal", RTC_TOPIC["MOTION_SWITCHER"], 1002,
                  json.dumps({"name": "normal"}))
        await asyncio.sleep(2)
        await req(conn, "BalanceStand(1002) 再试", RTC_TOPIC["SPORT_MOD"],
                  SPORT_CMD["BalanceStand"])
        await asyncio.sleep(2)
        show_state("再试后")
        print("\n如果仍全部失败，请看状态里的 error_code：常见含义")
        print("  100     通信固件异常（MCU/电机通信）—— 常见于长期放置后，需断电重启机器狗")
        print("  非 0    其它硬件/保护状态，同样建议断电重启")
        print("  若 1005/1008 曾成功过、现在全 -1，也请检查是否插着充电器或机身被架起")

    await conn.disconnect()
    return 0


asyncio.run(main())
