"""WebRTC 控制链路体检（Unitree Go2 / G1 的局域网信令）

对应 `docs/go2_webrtc_protocol.md` 与客户端实现（`client/src/signaling.cpp`）：

============================  ==================================================
信令端                        说明
============================  ==================================================
``:9991/con_notify``          **新固件**（Go2 > 1.1.11 / 全部 G1、R1）。
                              ``data2 == 2`` 用内置静态 GCM key 解；
                              ``data2 == 3`` 需要云账号里的**每设备 AES-128 key**
                              （官方 App 绑定账号 → device/bind/list 的 dev.key）
``:8081/offer``               **旧固件**（Go2 < 1.1.11），明文 JSON 流程，
                              不需要任何钥匙
============================  ==================================================

两者都不通 → ``LocalSignalingPortError``：不在同一网段 / 未开机 / 防火墙 /
连的是 AP 热点但 IP 用错（AP 模式机器狗自身地址恒为 ``192.168.12.1``）。

多机排查时先跑这个体检，能直接区分：
  * 是「连不上」（端口不通）
  * 是「连得上但缺钥匙」（data2=3 且没有 key）
  * 还是「被占用」（端口通、con_notify 无有效响应 → 手机 App 还连着）

用法::

    python main.py webrtc -i 192.168.2.111 -i 192.168.2.112,192.168.2.113
    python main.py webrtc --scan --subnet 192.168.2.0/24 --subnet 192.168.12.0/24
"""

from __future__ import annotations

import base64
import concurrent.futures
import json
import socket
import struct
import subprocess
import sys
import time
import urllib.error
import urllib.request
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple

# ---- 协议常量（与客户端保持一致）----
PORT_NEW = 9991     # con_notify 新流程
PORT_OLD = 8081     # legacy /offer
SN_QUERY_GROUP = "231.1.1.1"
SN_QUERY_PORT = 10131
SN_RECV_PORT = 10134
SN_QUERY_NAME = "unitree_dapengche"
AP_MODE_IP = "192.168.12.1"   # 机器狗 AP 模式下自身地址（客户端 id 必须传空串）

DEFAULT_DDS_PORTS = (7400, 7401, 7410, 7411, 7412, 7413)

# data2==2 分支用的内置静态 GCM key（Go2 < 1.1.15）
LEGACY_GCM_KEY = bytes([232, 86, 130, 189, 22, 84, 155, 0,
                        142, 4, 166, 104, 43, 179, 235, 227])


# --------------------------------------------------------------------- 工具


def tcp_open(ip: str, port: int, timeout: float = 0.7) -> Tuple[bool, float]:
    """TCP 端口探测，返回 (是否开放, 耗时毫秒)。优雅 shutdown，避免占住单线程信令服务。"""
    start = time.time()
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(timeout)
    try:
        sock.connect((ip, port))
        try:
            sock.shutdown(socket.SHUT_WR)   # 让服务端立刻收到 EOF
        except OSError:
            pass
        return True, (time.time() - start) * 1000
    except OSError:
        return False, (time.time() - start) * 1000
    finally:
        sock.close()


def http_get(ip: str, port: int, path: str, timeout: float = 3.0) -> Tuple[Optional[int], str, str]:
    """极简 HTTP GET，返回 (状态码, body, 错误)。

    注意：显式绕过系统代理。本机若配了 http_proxy（如 Clash），私有 IP 可能被代理
    转发，目标离线时返回代理的 502，看起来像"机器狗回了 502"——实测踩过。
    """
    url = f"http://{ip}:{port}{path}"
    req = urllib.request.Request(url, headers={"User-Agent": "go2-diag"})
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    try:
        with opener.open(req, timeout=timeout) as resp:
            return resp.status, resp.read().decode("utf-8", "replace"), ""
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", "replace"), str(e)
    except Exception as e:  # noqa: BLE001 - 网络异常种类多，统一回报
        return None, "", f"{type(e).__name__}: {e}"


def try_legacy_key(data1_b64: str) -> Tuple[bool, str]:
    """用内置静态 GCM key 试解 data1（data2=3 时也要试一次）。

    存在这样的固件：`data2` 报 3，但 data1 仍能用内置静态 key 解开。
    只按 data2 判断会给用户假故障（"缺钥匙"），所以这里实测一次。
    """
    try:
        from cryptography.hazmat.primitives.ciphers.aead import AESGCM
    except ImportError:
        return False, "未安装 cryptography，跳过静态钥匙试解"
    try:
        raw = base64.b64decode(data1_b64)
    except Exception:  # noqa: BLE001
        return False, "data1 base64 解码失败"
    if len(raw) < 28:
        return False, "data1 过短"
    tag, nonce, ct = raw[-16:], raw[-28:-16], raw[:-28]
    try:
        AESGCM(LEGACY_GCM_KEY).decrypt(nonce, ct + tag, None)
        return True, "内置静态 key 可解"
    except Exception as e:  # noqa: BLE001
        return False, f"内置静态 key 解不开（{type(e).__name__}）"


def ping(host: str, timeout: int = 1) -> Tuple[bool, Optional[float]]:
    """跨平台 ping（不依赖第三方库）。返回 (是否通, 平均延迟 ms)。"""
    if sys.platform.startswith("win"):
        cmd = ["ping", "-n", "1", "-w", str(timeout * 1000), host]
    elif sys.platform == "darwin":
        cmd = ["ping", "-c", "1", "-W", str(timeout * 1000), host]
    else:
        cmd = ["ping", "-c", "1", "-W", str(timeout), host]
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout + 3)
    except Exception:  # noqa: BLE001
        return False, None
    if out.returncode != 0:
        return False, None
    for token in out.stdout.replace("=", " ").split():
        try:
            val = float(token)
        except ValueError:
            continue
        if 0.0 < val < 10000.0:
            return True, val
    return True, None


# --------------------------------------------------------------------- 体检


@dataclass
class RobotCheck:
    ip: str
    sn: str = ""
    ping_ok: bool = False
    ping_ms: Optional[float] = None

    port_new_open: bool = False
    port_new_ms: float = 0.0
    port_old_open: bool = False
    port_old_ms: float = 0.0

    http_status: Optional[int] = None
    data2: Optional[int] = None
    path_ending: str = ""
    has_key: bool = False           # 是否已提供该台的钥匙（仅用于报告）
    legacy_key_ok: bool = False     # data2=3 时，内置静态 key 是否仍能解开 data1
    legacy_key_note: str = ""       # 上述试解的结果说明

    dds_ports_open: List[int] = field(default_factory=list)
    conclusion: str = ""
    actions: List[str] = field(default_factory=list)

    @property
    def signaling(self) -> str:
        if self.port_new_open:
            return f"{PORT_NEW} (con_notify 新流程)"
        if self.port_old_open:
            return f"{PORT_OLD} (legacy /offer 旧流程)"
        return "无（9991/8081 均不通）"


def _decode_con_notify(body: str) -> Optional[dict]:
    """con_notify 返回 base64(JSON)；少数固件可能直接返回明文，两种都试。"""
    for candidate in (body, None):
        text = candidate
        if text is None:
            try:
                text = base64.b64decode(body).decode("utf-8", "replace")
            except Exception:  # noqa: BLE001
                continue
        try:
            obj = json.loads(text)
        except Exception:  # noqa: BLE001
            continue
        if isinstance(obj, dict) and "data1" in obj:
            return obj
    return None


def check_robot(ip: str, has_key: bool = False, sn: str = "",
                dds_ports=DEFAULT_DDS_PORTS) -> RobotCheck:
    """对单台机器狗做一次控制链路体检。

    :param has_key: 调用方是否已持有该台的每设备 AES-128 钥匙（仅用于报告措辞）
    """
    r = RobotCheck(ip=ip, sn=sn)
    r.has_key = bool(has_key)

    ok, ms = ping(ip)
    r.ping_ok, r.ping_ms = ok, ms

    r.port_new_open, r.port_new_ms = tcp_open(ip, PORT_NEW)
    if not r.port_new_open:
        r.port_old_open, r.port_old_ms = tcp_open(ip, PORT_OLD)

    if r.port_new_open:
        status, body, err = http_get(ip, PORT_NEW, "/con_notify")
        r.http_status = status
        if status == 200 and body:
            obj = _decode_con_notify(body)
            if obj:
                r.data2 = obj.get("data2", 1)
                data1 = obj.get("data1", "")
                if data1:
                    r.legacy_key_ok, r.legacy_key_note = try_legacy_key(data1)
        elif err:
            r.actions.append(f"con_notify 请求异常（{err}），稍后重试")
    elif not r.port_old_open:
        for port in dds_ports:
            opened, _ = tcp_open(ip, port, timeout=0.3)
            if opened:
                r.dds_ports_open.append(port)

    # ---- 结论 ----
    if not r.port_new_open and not r.port_old_open:
        r.conclusion = "不可达：9991 与 8081 都不通"
        if r.ping_ok:
            r.actions.append(
                f"ICMP 通但两个信令端口都不通：确认 {ip} 是机器狗本身"
                "（不是路由器/其它设备），且它的 WebRTC 信令服务已启动")
        else:
            r.actions.append(f"先在电脑上 `ping {ip}`；不通说明不在同一网段或未开机")
        r.actions += [
            "确认机器狗 WiFi 已连到与电脑同一路由器（STA 模式），"
            "或在官方 App 里查它的当前 IP",
            f"若它是 AP 模式（自己发热点），地址固定为 {AP_MODE_IP}，"
            "此时信令 offer 的 id 必须是空串（本项目 RobotProfile.apMode=true）",
            "检查电脑防火墙是否拦截出站 TCP，以及路由器是否开了 AP 隔离",
        ]
    elif r.port_old_open and not r.port_new_open:
        r.conclusion = "旧固件（Go2 < 1.1.11）：只有 8081 明文 /offer"
        r.actions += [
            "不需要云账号钥匙；客户端会自动走 legacy /offer 流程",
            "建议把固件升级到最新，与其他机器狗保持一致（升级后改走 9991）",
        ]
    elif r.http_status != 200 or r.data2 is None:
        r.conclusion = "9991 端口通，但 con_notify 无有效响应（多半被占用）"
        r.actions += [
            "断开手机 App / 其它客户端，等 10~20 秒让旧会话释放后重试",
            "确认没有被另一个脚本/程序占着（同一时刻只允许一条 WebRTC 连接）",
        ]
    elif r.data2 == 3 and r.legacy_key_ok:
        r.conclusion = "data2=3，但内置静态 key 实测可解（固件上报与实现不一致）"
        r.actions += [
            "不需要云钥匙：客户端会用内置静态 key 直接解（已支持回退）",
            "若之前一直报「缺钥匙」，那是被 data2 的读数误导了",
        ]
    elif r.data2 == 3:
        if r.has_key:
            r.conclusion = "新固件（Go2 ≥ 1.1.15）：data2=3，且已提供钥匙"
            r.actions.append("若仍连不上，说明这把钥匙不属于这台机器狗（GCM 校验会失败）")
        else:
            r.conclusion = ("新固件（Go2 ≥ 1.1.15）：data2=3，缺少每设备 AES-128 钥匙"
                            f"（{r.legacy_key_note}）")
            r.actions += [
                "用官方 App 绑定的宇树账号登录客户端顶栏「云账号」拉取钥匙，"
                "或 `go2_remote --keys <32位hex>`",
                "每台狗一把钥匙，两台 Pro 若绑在不同账号需分别登录（钥匙会累加）",
                "钥匙与 IP 绑定后会写入 go2_keys_cache.json，下次自动复用",
            ]
    elif r.data2 == 2:
        r.conclusion = "固件 < 1.1.15：data2=2，内置静态 key 即可"
        r.actions.append("不需要云账号；直接连即可")
    else:
        r.conclusion = f"data2={r.data2}：未知握手分支（详见协议文档）"
        r.actions.append("抓包确认 con_notify 原始响应，并对照 docs/go2_webrtc_protocol.md")

    if r.port_new_open and not r.ping_ok:
        r.actions.append("ICMP 不通但 TCP 通：路由器可能禁 ICMP，不影响控制")
    return r


# --------------------------------------------------------------------- 发现


def scan_subnet(cidr: str, timeout: float = 0.35, workers: int = 64) -> List[str]:
    """扫描一个 /24 网段的 9991/8081（返回命中的 IP 列表）。"""
    prefix = cidr.rsplit(".", 1)[0]
    hits: List[str] = []

    def probe(n: int) -> Optional[str]:
        ip = f"{prefix}.{n}"
        if tcp_open(ip, PORT_NEW, timeout)[0] or tcp_open(ip, PORT_OLD, timeout)[0]:
            return ip
        return None

    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as ex:
        for res in ex.map(probe, range(1, 255)):
            if res:
                hits.append(res)
    return sorted(hits)


def discover_sn(timeout: float = 2.0) -> Dict[str, str]:
    """按 Go2 多播协议查询 SN -> IP。返回 {sn: ip}。

    注意：多播通常不跨路由器，若返回为空改用网段扫描。
    """
    result: Dict[str, str] = {}
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        sock.bind(("", SN_RECV_PORT))
    except OSError:
        sock.close()
        return result
    try:
        mreq = struct.pack("4sl", socket.inet_aton(SN_QUERY_GROUP), socket.INADDR_ANY)
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
    except OSError:
        sock.close()
        return result

    sock.settimeout(0.2)
    query = json.dumps({"name": SN_QUERY_NAME}).encode()
    for _ in range(3):
        try:
            sock.sendto(query, (SN_QUERY_GROUP, SN_QUERY_PORT))
        except OSError:
            break
        _collect_sn(sock, result)

    sock.settimeout(timeout)
    _collect_sn(sock, result)

    try:
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_DROP_MEMBERSHIP, mreq)
    except OSError:
        pass
    sock.close()
    return result


def _collect_sn(sock: socket.socket, out: Dict[str, str]) -> None:
    while True:
        try:
            data, addr = sock.recvfrom(1024)
        except OSError:
            return
        try:
            msg = json.loads(data.decode("utf-8", "replace"))
        except Exception:  # noqa: BLE001
            continue
        sn = msg.get("sn")
        if sn and sn not in out:
            out[sn] = msg.get("ip", addr[0])


def summarize(results: List[RobotCheck]) -> Tuple[int, List[str]]:
    """返回 (通过数, 全局建议列表)。"""
    ok = 0
    tips: List[str] = []
    for r in results:
        if r.port_new_open or r.port_old_open:
            ok += 1
    need_key = [r.ip for r in results if r.data2 == 3 and not r.has_key]
    if need_key:
        tips.append("以下机器狗需要每设备 AES-128 钥匙：" + ", ".join(need_key) +
                    "（客户端顶栏「云账号」登录后会自动重试）")
    both = [r.ip for r in results if r.port_new_open and r.data2 == 2]
    old = [r.ip for r in results if r.port_old_open and not r.port_new_open]
    if old:
        tips.append("旧固件机器狗（8081）：" + ", ".join(old) +
                    " —— 客户端走明文 /offer 流程，不需要钥匙")
    if both and len(results) > 1:
        tips.append("同一网络内存在新老固件混用：本项目已同时兼容两条信令流程，"
                    "但建议统一升级固件以便排查")
    return ok, tips
