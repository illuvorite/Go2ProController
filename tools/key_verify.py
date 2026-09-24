#!/usr/bin/env python3
"""验证候选 AES-128 钥匙是否属于某台机器狗，通过就写进客户端缓存。

原理：data2=3 固件用**每设备 AES-128-GCM** 加密 con_notify 的 data1。
用候选 key 解密，GCM 的认证标签校验通过 = 这把钥匙就是这台狗的（唯一可靠判据）。

用法:
    python3 _verify_key.py <狗IP> <32位hex>
    python3 _verify_key.py <狗IP> --file keys.txt      # 逐个试
    python3 _verify_key.py <狗IP> --scan-str <字符串>   # 从任意字符串里提取 32 位 hex 逐个试

验证通过后会写入 go2_keys_cache.json（{ip: key}），C++ 客户端启动时自动装载，
之后无线连接就不需要再做任何账号操作。
"""

import base64
import json
import re
import sys
import urllib.request

CACHE = "go2_keys_cache.json"


def fetch_data1(ip):
    url = f"http://{ip}:9991/con_notify"
    # 显式绕开系统代理：本机若配了 http_proxy，私有 IP 会被代理转发，
    # 目标离线时会返回代理的 502，误判成机器狗的问题。
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    with opener.open(url, timeout=8) as resp:
        body = resp.read().decode()
    obj = json.loads(base64.b64decode(body).decode("utf-8", "replace"))
    return obj.get("data1", ""), obj.get("data2")


def try_key(data1_b64, key_hex):
    from cryptography.hazmat.primitives.ciphers.aead import AESGCM
    key = bytes.fromhex(key_hex)
    if len(key) != 16:
        return False, "不是 16 字节（32 位 hex）"
    raw = base64.b64decode(data1_b64)
    if len(raw) < 28:
        return False, "data1 过短"
    tag, nonce, ct = raw[-16:], raw[-28:-16], raw[:-28]
    try:
        pt = AESGCM(key).decrypt(nonce, ct + tag, None).decode("utf-8", "replace")
        return True, pt[:60]
    except Exception as e:  # noqa: BLE001
        return False, f"GCM 校验失败（{type(e).__name__}）"


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    ip = sys.argv[1]
    args = sys.argv[2:]

    candidates = []
    if args[0] == "--file":
        with open(args[1], encoding="utf-8") as f:
            candidates = [ln.strip().lower() for ln in f
                          if re.fullmatch(r"[0-9a-fA-F]{32}", ln.strip())]
    elif args[0] == "--scan-str":
        candidates = sorted(set(re.findall(r"\b[0-9a-f]{32}\b", " ".join(args[1:]).lower())))
    else:
        candidates = [args[0].strip().lower()]

    candidates = [c for c in candidates if re.fullmatch(r"[0-9a-f]{32}", c)]
    if not candidates:
        print("没有可用的 32 位 hex 候选")
        return 1

    print(f"抓取 {ip} 的 con_notify ...")
    data1, data2 = fetch_data1(ip)
    print(f"data2={data2}（3 = 每设备钥匙；2 = 静态 key 即可）")

    for k in candidates:
        ok, info = try_key(data1, k)
        print(f"  试 {k} -> {'✓ 命中！' if ok else '✗ ' + info}")
        if ok:
            cache = {}
            try:
                with open(CACHE, encoding="utf-8") as f:
                    cache = json.load(f)
            except Exception:  # noqa: BLE001
                pass
            cache[ip] = k
            with open(CACHE, "w", encoding="utf-8") as f:
                json.dump(cache, f, indent=2)
            print(f"\n✓ 这把钥匙属于 {ip}，已写入 {CACHE}")
            print("  现在直接用无线客户端连它即可（无需任何账号）")
            return 0

    print("\n全部候选都不匹配。若来源是 /unitree/etc/key/aes_key.bin，"
          "它可能是 RSA 包裹的 —— 需要从信令进程内存里取明文 key。")
    return 1


if __name__ == "__main__":
    sys.exit(main())
