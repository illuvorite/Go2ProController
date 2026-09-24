#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""宇树云 API 客户端（Python 版，协议对齐官方 App 与 client/src/unitree_cloud.cpp）。

作用：用宇树账号登录 → 拉取账号绑定的设备 → 拿到每台设备的 AES-128 key
      （data2=3 固件，即 Go2 ≥ 1.1.15，局域网 WebRTC 握手必需）。

⚠️ 本脚本会向宇树官方服务器发请求（global-robot-api.unitree.com / robot-api.unitree.com）。
   属于"用户本人操作"的范畴，请自行确认后再执行。

协议要点（勿随意删改，头字段少一个就可能 code 100 → 1003）：
  - 域名：global = global-robot-api.unitree.com ；cn = robot-api.unitree.com
  - AppSign = md5("XyvkwK45hp5PHfA8" + AppTimestamp + AppNonce)
  - 密码只传 md5(明文)，不落盘
  - 业务码 code == 100 为成功；1001 为 token 过期

用法：
  python _unitree_cloud.py ping                             # 免登录，验证云 API 与协议头
  python _unitree_cloud.py <账号> <密码> --region cn         # 登录并列出账号下全部设备（含 key）
  python _unitree_cloud.py <账号> <密码> --region cn --verify 192.168.123.161 --save
                                                           # 验证哪把 key 属于这台狗，并写入缓存
"""
import base64
import hashlib
import json
import random
import re
import socket
import sys
import time
import urllib.parse
import urllib.request

APP_SIGN_SECRET = "XyvkwK45hp5PHfA8"
HOSTS = {"global": "global-robot-api.unitree.com", "cn": "robot-api.unitree.com"}
CACHE = "go2_keys_cache.json"


def md5hex(s):
    return hashlib.md5(s.encode("utf-8")).hexdigest()


def rand_hex(nbytes):
    return "".join(random.choice("0123456789abcdef") for _ in range(nbytes * 2))


def make_headers(token=""):
    """与官方 App 一致的请求头；AppTimestamp / AppNonce / AppSign 必须每次重新生成。"""
    ts = str(int(time.time() * 1000))
    nonce = rand_hex(16)
    return {
        "DeviceId": "Samsung/Samsung/SM-S931B/s24/14/34",
        "DevicePlatform": "Android",
        "DeviceModel": "SM-S931B",
        "SystemVersion": "34",
        "AppVersion": "1.11.4",
        "AppLocale": "en_US",
        "Channel": "UMENG_CHANNEL",
        "User-Agent": ("Mozilla/5.0 (Linux; Android 14; SM-S931B Build/AP3A.240905.015.A2; wv) "
                       "AppleWebKit/537.36 (KHTML, like Gecko) Version/4.0 Chrome/127.0.6533.103 "
                       "Mobile Safari/537.36"),
        "AppTimezone": "UTC",
        "AppTimestamp": ts,
        "AppNonce": nonce,
        "AppSign": md5hex(APP_SIGN_SECRET + ts + nonce),
        "AppName": "Go2",
        "Token": token,
    }


def request(host, method, path, token="", form=None):
    headers = make_headers(token)
    data = None
    if method == "POST":
        data = urllib.parse.urlencode(form or {}).encode()
        headers["Content-Type"] = "application/x-www-form-urlencoded"
    req = urllib.request.Request(f"https://{host}{path}", data=data, headers=headers,
                                 method=method)
    try:
        with urllib.request.urlopen(req, timeout=20) as resp:
            body = resp.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        # 宇树用非标准状态码（如 567）传业务错误，必须把响应体读出来才知道真实原因
        detail = e.read().decode("utf-8", "replace")
        raise RuntimeError(f"HTTP {e.code}（非标准码）响应体：{detail[:600]}") from None
    try:
        return json.loads(body)
    except json.JSONDecodeError:
        raise RuntimeError(f"响应不是 JSON：{body[:300]}") from None


def check(resp, action):
    code = resp.get("code", -1)
    if code != 100:
        raise RuntimeError(f"{action} 失败：code={code} {resp.get('errorMsg', '')}")
    return resp.get("data")


def login(host, email, password):
    form = {"email": email, "password": md5hex(password)}
    data = check(request(host, "POST", "/login/email", form=form), "登录")
    token = data.get("accessToken", "")
    if not token:
        raise RuntimeError("登录成功但未返回 accessToken")
    return token, data.get("refreshToken", "")


def list_devices(host, token):
    data = check(request(host, "GET", "/device/bind/list", token=token), "拉取设备列表")
    return data if isinstance(data, list) else []


# ---------- 钥匙校验（与 _try_key.py 同一套逻辑） ----------

def fetch_con_notify(ip):
    s = socket.socket()
    s.settimeout(5)
    try:
        s.connect((ip, 9991))
        s.sendall(f"GET /con_notify HTTP/1.1\r\nHost: {ip}\r\nConnection: close\r\n\r\n".encode())
        data = b""
        while True:
            try:
                chunk = s.recv(4096)
            except socket.timeout:
                break
            if not chunk:
                break
            data += chunk
    finally:
        s.close()
    head, _, body = data.partition(b"\r\n\r\n")
    status = head.split(b"\r\n")[0].decode("latin1") if head else "<无响应>"
    if "200" not in status:
        raise RuntimeError(f"信令异常：{status}")
    txt = base64.b64decode(body.strip()).decode("utf-8", "replace")
    m1 = re.search(r'"data1"\s*:\s*"([^"]+)"', txt)
    m2 = re.search(r'"data2"\s*:\s*(\d+)', txt)
    if not m1:
        raise RuntimeError("响应里没有 data1")
    return m1.group(1), (int(m2.group(1)) if m2 else -1)


def try_key(data1_b64, key_hex):
    from cryptography.hazmat.primitives.ciphers.aead import AESGCM
    raw = base64.b64decode(data1_b64)
    tag, nonce, ct = raw[-16:], raw[-28:-16], raw[:-28]
    try:
        pt = AESGCM(bytes.fromhex(key_hex)).decrypt(nonce, ct + tag, None)
        return True, pt.decode("utf-8", "replace")
    except Exception:  # noqa: BLE001
        return False, ""


def save_cache(ip, key_hex):
    cache = {}
    try:
        with open(CACHE, encoding="utf-8") as f:
            cache = json.load(f)
    except Exception:  # noqa: BLE001
        pass
    cache[ip] = key_hex
    with open(CACHE, "w", encoding="utf-8") as f:
        json.dump(cache, f, indent=2)
    return cache


def main():
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except Exception:  # noqa: BLE001
        pass

    argv = sys.argv[1:]
    if not argv:
        print(__doc__)
        return 2

    region = "global"
    if "--region" in argv:
        region = argv[argv.index("--region") + 1]
    host = HOSTS.get(region, HOSTS["global"])
    verify_ip = argv[argv.index("--verify") + 1] if "--verify" in argv else None
    do_save = "--save" in argv

    if argv[0] == "ping":
        print(f"[1/1] GET https://{host}/system/pubKey ...")
        try:
            j = request(host, "GET", "/system/pubKey")
            ok = j.get("code") == 100
            print(f"      code={j.get('code')}  -> {'协议头被接受 ✓' if ok else '签名/头可能不对 ✗'}")
            return 0 if ok else 1
        except Exception as e:  # noqa: BLE001
            print(f"      失败：{e}")
            return 1

    if len(argv) < 2:
        print(__doc__)
        return 2
    email, password = argv[0], argv[1]

    print(f"[1/3] 登录（{region} / {host}）...")
    try:
        token, refresh = login(host, email, password)
    except Exception as e:  # noqa: BLE001
        print(f"      登录失败：{e}")
        return 1
    print(f"      OK，accessToken={token[:12]}...（refreshToken {'有' if refresh else '无'}）")

    print("[2/3] 拉取账号下绑定的设备 ...")
    try:
        devices = list_devices(host, token)
    except Exception as e:  # noqa: BLE001
        print(f"      失败：{e}")
        return 1
    if not devices:
        print("      账号下没有绑定任何设备 —— 钥匙取不到（换绑定的账号试试）")
        return 1

    print(f"      共 {len(devices)} 台：")
    keys = []
    for d in devices:
        sn = d.get("sn", "")
        alias = d.get("alias", "")
        mac = d.get("mac", "")
        online = d.get("online", False)
        key = d.get("key", "") or d.get("gcm_key", "")
        print(f"      - SN={sn}  别名={alias}  MAC={mac}  在线={online}  key={key or '<无>'}")
        if key:
            keys.append((sn, mac, key))

    if not keys:
        print("      设备列表里没有 key 字段，无法继续")
        return 1

    print("[3/3] 钥匙汇总（每台狗一把）：")
    for sn, mac, key in keys:
        print(f"      {sn}  {mac}  {key}")

    if verify_ip:
        print(f"\n验证阶段：向 {verify_ip}:9991 取 con_notify，逐个试 key ...")
        try:
            data1, data2 = fetch_con_notify(verify_ip)
        except Exception as e:  # noqa: BLE001
            print(f"  取 con_notify 失败：{e}")
            return 1
        print(f"  data2={data2}")
        hit = None
        for sn, mac, key in keys:
            ok, info = try_key(data1, key)
            print(f"  [{'✓ 命中' if ok else '✗ 不对'}] {sn}  {key}")
            if ok:
                hit = (sn, mac, key, info)
                break
        if hit:
            sn, mac, key, info = hit
            print(f"\n★ 钥匙属于这台狗：SN={sn} MAC={mac}")
            print(f"  key = {key}")
            print(f"  解密出的会话信息：{info[:300]}")
            if do_save:
                cache = save_cache(verify_ip, key)
                print(f"  已写入 {CACHE}：{cache}")
        else:
            print("\n账号里的 key 都不匹配这台狗 —— 说明这台狗绑在别的账号下")
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
