#!/usr/bin/env python3
"""纯 CycloneDDS 直连机器狗内网运控 PC（不经过官方 SDK 的通道封装）。

为什么不用官方 SDK：`ChannelFactoryInitialize` 可能自带并强制自己的 DDS 配置，
把我们的单播 Peer 配置覆盖掉（实测：包根本没发出去）。这里直接用 CycloneDDS
的 Python API + 官方 IDL 类型，配置完全由 CYCLONEDDS_URI 决定。

用法:
    python3 _dds_raw.py <狗IP> [state|balance|hello|stop|damp]
"""

import os
import sys
import time

DOG_IP = sys.argv[1] if len(sys.argv) > 1 else "192.168.2.105"
ACTION = sys.argv[2] if len(sys.argv) > 2 else "state"
ROBOT_INTERNAL = "192.168.123.161"
LOCAL_IP = os.environ.get("GO2_LOCAL_IP", "192.168.2.108")

CONFIG_PATH = os.environ.get("GO2_DDS_CFG", "/tmp/go2_dds_unicast.xml")
if not os.environ.get("GO2_DDS_CFG"):
  with open(CONFIG_PATH, "w", encoding="utf-8") as f:
    f.write(f"""<?xml version="1.0" encoding="UTF-8"?>
<CycloneDDS xmlns="https://cdds.io/config">
  <Domain id="any">
    <General>
      <NetworkInterfaceAddress>{LOCAL_IP}</NetworkInterfaceAddress>
      <AllowMulticast>false</AllowMulticast>
    </General>
    <Discovery>
      <Peers>
        <Peer address="{ROBOT_INTERNAL}"/>
      </Peers>
      <ParticipantIndex>auto</ParticipantIndex>
    </Discovery>
  </Domain>
</CycloneDDS>
""")
os.environ["CYCLONEDDS_URI"] = "file://" + CONFIG_PATH

from cyclonedds.domain import DomainParticipant                     # noqa: E402
from cyclonedds.topic import Topic                                  # noqa: E402
from cyclonedds.sub import DataReader                               # noqa: E402
from cyclonedds.pub import DataWriter                               # noqa: E402
from unitree_sdk2py.idl.unitree_go.msg.dds_ import SportModeState_  # noqa: E402
from unitree_sdk2py.idl.unitree_api.msg.dds_ import Request_, Response_  # noqa: E402
from unitree_sdk2py.idl.default import unitree_api_msg_dds__Request_  # noqa: E402
import json                                                         # noqa: E402


def main():
    print(f"目标 {DOG_IP}；本机 {LOCAL_IP}；DDS 单播对端 {ROBOT_INTERNAL}")
    print(f"CYCLONEDDS_URI={os.environ['CYCLONEDDS_URI']}")

    dp = DomainParticipant(0)
    print("参与者已创建，订阅 rt/sportmodestate ...")
    tp_state = Topic(dp, "rt/sportmodestate", SportModeState_)
    rd = DataReader(dp, tp_state)

    got = []
    deadline = time.time() + 8
    while time.time() < deadline and not got:
        got = rd.take()
        if not got:
            time.sleep(0.1)

    if not got:
        print("✗ 8 秒内没收到 rt/sportmodestate —— DDS 没通")
        return 1

    s = got[-1]
    print(f"✓ 收到状态帧：mode={s.mode} gait={s.gait_type} 电量={s.battery_level}% "
          f"位置={[round(v, 3) for v in s.position[:3]]}")

    if ACTION == "state":
        n0 = len(got)
        time.sleep(5)
        extra = rd.take()
        print(f"✓ 后续 5 秒又收到 {len(extra)} 帧（累计 {n0 + len(extra)}），数据流正常")
        return 0

    # ---- 下发运动指令（走 rt/api/sport/request，与官方 SDK 完全相同的 api_id）----
    API_IDS = {"damp": 1001, "balance": 1002, "stop": 1003,
               "standup": 1004, "standdown": 1005, "recover": 1006, "hello": 1016}
    if ACTION not in API_IDS:
        print(f"未知动作 {ACTION}；可选 {list(API_IDS) + ['state']}")
        return 1

    tp_req = Topic(dp, "rt/api/sport/request", Request_)
    tp_resp = Topic(dp, "rt/api/sport/response", Response_)
    wr = DataWriter(dp, tp_req)
    rd_resp = DataReader(dp, tp_resp)
    time.sleep(1.0)          # 等发现完成再发，避免刚建通道就发丢包

    req = unitree_api_msg_dds__Request_()
    req.header.identity.id = 1
    req.header.identity.api_id = API_IDS[ACTION]
    req.parameter = ""
    print(f"下发 api_id={API_IDS[ACTION]}（{ACTION}）...")
    wr.write(req)

    # 读回执（status.code == 0 表示成功）
    deadline = time.time() + 6
    while time.time() < deadline:
        res = rd_resp.take()
        if res:
            r = res[-1]
            code = None
            try:
                code = r.header.status.code
            except Exception:  # noqa: BLE001
                pass
            print(f"✓ 收到回执：api_id={r.header.identity.api_id} code={code} "
                  f"data={getattr(r, 'data', '')!r}")
            break
        time.sleep(0.1)
    else:
        print("⚠ 没收到回执（指令可能已执行；也可能响应主题名不同）")

    time.sleep(1.5)
    st = rd.take()
    if st:
        print(f"动作后状态：mode={st[-1].mode} 位置={[round(v, 3) for v in st[-1].position[:3]]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
