#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""从平板 App 日志里提取所有连接过的机器狗（SN ↔ 设备 key ↔ IP 线索）。

来源：Unitree Go2 App（com.unitree.doggo2）外部存储日志
      /sdcard/Android/data/com.unitree.doggo2/cache/log/<日期>
步骤：先 `adb pull /sdcard/Android/data/com.unitree.doggo2/ _tab_data`，再跑本脚本。

原理（时序锚点，已按真实日志校准）：
  - 日志里 `chekTime :时间戳验签  key:<32位hex>` 直接打印当前设备 key；
  - 它之前最近的「锚点行」标明当前是哪台狗，锚点有四种写法：
      ① `sn :刚绑定的sn:XXX`            （BLE 绑定结果，最可靠）
      ② `connect :----- 开始连接  sn:XXX ----`
      ③ `udp :{"sn": "XXX", ..., "ip": "YYY"}`   （多播发现，同时给出 IP）
      ④ `ble :sn：XXX`
  - 因此用「状态跟踪」（而非向上回溯）关联，才不会串台。

用法: python -X utf8 _tab_dogs.py [数据目录，默认 _tab_data]
"""
import os
import re
import sys
from collections import defaultdict

ROOT = sys.argv[1] if len(sys.argv) > 1 else "_tab_data"
LOGDIR = os.path.join(ROOT, "cache", "log")

KEY_RE = re.compile(r"chekTime.*?key:([0-9a-fA-F]{32})")
SN_TEXT_RE = re.compile(r"B42D[0-9A-Z]{8,16}")

# 锚点：只有这些才能真正代表"当前操作的是哪台狗"
# ⚠️ 「多播发现 udp: {...}」刻意不作为锚点 —— App 会扫到局域网里所有狗，
#    却只连其中一台（实测 2026-09-22：扫到 B42D1000P85E64CG，实际连的是 B42D2000P6CDL807，
#    chekTime 打印的是后者钥匙）。
ANCHORS = [
    (re.compile(r"刚绑定的sn[:：]\s*(\S+)"), "BLE绑定"),
    (re.compile(r"开始连接\s+sn[:：]\s*(\S+)"), "开始连接"),
    (re.compile(r"ble\s*:\s*sn[:：]\s*(\S+)"), "BLE广播"),
]
# 多播发现行只用来收集 IP（不参与归属判定）
IP_RE = re.compile(r'"sn":\s*"(B42D[0-9A-Z]+)"[^}]*?"ip":\s*"([^"]+)"')


def main():
    if not os.path.isdir(LOGDIR):
        print(f"找不到日志目录：{LOGDIR}")
        return 1

    pairs = []                          # (文件, 行号, key, sn, 锚点类型)
    sn_ips = defaultdict(set)           # sn -> {ip}
    sn_dates = defaultdict(set)         # sn -> {日期}
    cur, cur_src = None, ""

    for fn in sorted(os.listdir(LOGDIR)):
        path = os.path.join(LOGDIR, fn)
        if not os.path.isfile(path):
            continue
        lines = open(path, encoding="utf-8", errors="ignore").read().splitlines()

        for sn in SN_TEXT_RE.findall("\n".join(lines)):
            sn_dates[sn].add(fn)

        for i, line in enumerate(lines):
            # 多播发现：只收集 IP，不改变"当前设备"
            for m in IP_RE.finditer(line):
                sn_ips[m.group(1)].add(m.group(2))

            # 更新当前设备（锚点）
            for rex, src in ANCHORS:
                m = rex.search(line)
                if m:
                    cur, cur_src = m.group(1), src
                    break

            m = KEY_RE.search(line)
            if m:
                pairs.append((fn, i + 1, m.group(1).lower(), cur, cur_src))

    print(f"=== chekTime 验签记录（{len(pairs)} 条）===")
    for fn, ln, key, sn, src in pairs:
        print(f"  {fn}  行{ln:<5} key={key}  SN={sn or '<未知>'}  [锚点:{src}]")

    print("\n=== SN ↔ key ↔ IP 汇总 ===")
    sn2key = defaultdict(set)
    for _, _, key, sn, _ in pairs:
        if sn:
            sn2key[sn].add(key)
    for sn in sorted(set(list(sn2key) + list(sn_dates))):
        keys = sn2key.get(sn, set())
        ips = ",".join(sorted(sn_ips.get(sn, []))) or "-"
        dates = ",".join(sorted(sn_dates.get(sn, [])))
        print(f"  {sn:<20} key: {', '.join(sorted(keys)) or '<未捕获>'}")
        print(f"  {'':<20} IP: {ips}")
        print(f"  {'':<20} 日期: {dates}")

    print("\n=== 可直接使用的钥匙清单 ===")
    for sn, keys in sorted(sn2key.items()):
        for k in sorted(keys):
            print(f"  {k}    # {sn}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
