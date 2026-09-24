#!/usr/bin/env python3
"""用候选钥匙试解机器狗 con_notify 的 data1（AES-GCM），判定钥匙对不对。

data1 结构（实测）：base64( ciphertext || nonce(12) || tag(16) )
  nonce = raw[-28:-16]   tag = raw[-16:]   ct = raw[:-28]

用法:
  python3 _try_key.py <IP> <32位hex> [更多32位hex ...]
  python3 _try_key.py <IP> --file candidates.txt      # 每行一个 hex，忽略 # 注释
  python3 _try_key.py <IP> --static                   # 先试内置静态 key
"""
import base64
import re
import socket
import sys
import time

STATIC_KEY = bytes([232, 86, 130, 189, 22, 84, 155, 0,
                    142, 4, 166, 104, 43, 179, 235, 227])


def fetch(ip, port=9991, retries=3):
    for i in range(retries):
        s = socket.socket()
        s.settimeout(5)
        try:
            s.connect((ip, port))
            s.sendall(f"GET /con_notify HTTP/1.1\r\nHost: {ip}\r\nConnection: close\r\n\r\n".encode())
            data = b""
            while True:
                try:
                    c = s.recv(4096)
                except socket.timeout:
                    break
                if not c:
                    break
                data += c
        finally:
            s.close()
        head, _, body = data.partition(b"\r\n\r\n")
        status = head.split(b"\r\n")[0].decode("latin1")
        if "429" in status:
            print(f"  [{status}] 被限流，等 9 秒重试 ...")
            time.sleep(9)
            continue
        if "200" not in status:
            print(f"  [{status}] 信令异常")
            return None
        txt = base64.b64decode(body.strip()).decode("utf-8", "replace")
        m1 = re.search(r'"data1"\s*:\s*"([^"]+)"', txt)
        m2 = re.search(r'"data2"\s*:\s*(\d+)', txt)
        if not m1:
            print("  没拿到 data1")
            return None
        return m1.group(1), (int(m2.group(1)) if m2 else -1)
    return None


def try_key(data1_b64, key, label):
    from cryptography.hazmat.primitives.ciphers.aead import AESGCM
    raw = base64.b64decode(data1_b64)
    if len(raw) < 28:
        return False, "data1 过短"
    tag, nonce, ct = raw[-16:], raw[-28:-16], raw[:-28]
    try:
        pt = AESGCM(key).decrypt(nonce, ct + tag, None)
        return True, pt.decode("utf-8", "replace")
    except Exception as e:  # noqa: BLE001
        return False, type(e).__name__


def main():
    ip = sys.argv[1] if len(sys.argv) > 1 else "192.168.0.169"
    args = sys.argv[2:]
    keys = []
    if "--static" in args:
        keys.append(("内置静态key", STATIC_KEY))
    if "--file" in args:
        path = args[args.index("--file") + 1]
        with open(path, encoding="utf-8", errors="ignore") as f:
            for line in f:
                line = line.split("#")[0].strip()
                if re.fullmatch(r"[0-9a-fA-F]{32}", line):
                    keys.append((f"{path}:{line}", bytes.fromhex(line)))
    for a in args:
        if re.fullmatch(r"[0-9a-fA-F]{32}", a):
            keys.append((a, bytes.fromhex(a)))

    print(f"读取 {ip}:9991/con_notify ...")
    got = fetch(ip)
    if not got:
        sys.exit(1)
    data1, data2 = got
    print(f"  data2 = {data2}，data1 = {len(base64.b64decode(data1))} 字节")
    if not keys:
        print("没有待测钥匙（用 32位hex / --file / --static 给出）")
        sys.exit(2)

    hit = None
    for label, k in keys:
        ok, info = try_key(data1, k, label)
        print(f"  [{'✓ 命中' if ok else '✗ 不对'}] {label}  {'' if ok else info}")
        if ok:
            hit = (label, k, info)
            break
    if hit:
        label, k, pt = hit
        print(f"\n★ 找到正确钥匙: {label}")
        print(f"  hex = {k.hex()}")
        print(f"  解密出的会话信息: {pt[:300]}")
    else:
        print("\n候选钥匙全部不对（或这台狗的钥匙不在候选里）")


if __name__ == "__main__":
    main()
