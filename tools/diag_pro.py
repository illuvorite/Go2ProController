#!/usr/bin/env python3
"""临时诊断：连上机器狗后打印运动状态、运动模式、以及各动作指令的原始回执。

用法: python3 _diag_pro.py [ip]
只读取状态 + 尝试一次 BalanceStand，不做位移。
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


async def req(conn, label, topic, api_id, parameter=None):
    payload = {"api_id": api_id}
    if parameter is not None:
        payload["parameter"] = parameter
    print(f"\n>>> {label}  topic={topic} api_id={api_id} parameter={parameter!r}", flush=True)
    try:
        resp = await conn.datachannel.pub_sub.publish_request_new(topic, payload)
    except Exception as e:
        print(f"    异常: {type(e).__name__}: {e}", flush=True)
        return None
    print("    原始回执: " + json.dumps(resp, ensure_ascii=False)[:600], flush=True)
    return resp


async def main():
    conn = UnitreeWebRTCConnection(WebRTCConnectionMethod.LocalSTA, ip=IP)
    print(f"连接 {IP} ...", flush=True)
    await conn.connect()
    print("已连接，通道就绪", flush=True)

    # ---- 订阅运动状态并打印前几帧 ----
    frames = []

    def on_state(msg):
        if len(frames) < 3:
            frames.append(msg)
            d = msg.get("data", {})
            keys = ["mode", "mode_name", "gait_type", "progress", "battery_level",
                    "position", "velocity", "body_height", "error_code", "foot_raise_height"]
            brief = {k: d.get(k) for k in keys if k in d}
            print(f"\n[状态帧] {json.dumps(brief, ensure_ascii=False)}", flush=True)

    conn.datachannel.pub_sub.subscribe(RTC_TOPIC["SPORT_MOD_STATE"], on_state)
    await asyncio.sleep(3)

    await req(conn, "查询运动模式", RTC_TOPIC["MOTION_SWITCHER"], 1001)
    await asyncio.sleep(1)
    await req(conn, "BalanceStand", RTC_TOPIC["SPORT_MOD"], SPORT_CMD["BalanceStand"])
    await asyncio.sleep(2)
    await req(conn, "StopMove", RTC_TOPIC["SPORT_MOD"], SPORT_CMD["StopMove"])
    await asyncio.sleep(1)

    await conn.disconnect()


asyncio.run(main())
