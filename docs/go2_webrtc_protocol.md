# Unitree Go2 WebRTC 控制协议（实测验证版）

> 本协议由 `legion1581/go2_webrtc_connect` 提取，并已在**实体机器狗上实测跑通**（2026-09-22）。
> 适用机型：Go2 AIR / PRO / EDU（Air 无网口，只能走此通道）。
> 测试环境：Windows 11 + WSL2 镜像网络模式，机器狗 `192.168.2.111`，固件返回 `data2 = 2`。

---

## 0. 前置事实

| 项 | 值 |
|---|---|
| 信令端口 | **9991**（TCP/HTTP，`Server: Boost.Beast/1.0`）**注意：不是网上老教程说的 8081** |
| 视频流 | RTSP 8551（GStreamer RTSP Server） |
| HTTP 服务 | 80（返回 502 网关） |
| DataChannel 标签 | **`"data"`** |
| DTLS 角色 | 机器狗回 `a=setup:active`（**机器狗是 DTLS client**） |
| ICE | 实测走 **IPv6**（`240e:...`），双向可达 |
| 并发 | 不带 token 时，**同一时刻只允许一条连接** |

---

## 1. 信令握手（三步）

### 步骤 1：取公钥

```
GET http://<ip>:9991/con_notify
→ 200 OK，响应体是 base64
```

base64 解码后得到 JSON：

```json
{ "data1": "<base64>", "data2": 2 }
```

`data2` 含义：
- `1` 或缺省 → `data1` 已是明文
- `2` → 用**静态 AES-GCM key** 解密（Go2 < 1.1.15）
- `3` → 用**每台设备的 AES-128 key** 解密（Go2 ≥ 1.1.15，需从云端 `device/bind/list` 取 `dev.key`）

`data2 == 2` 时的解密（已实测）：

```python
raw        = base64.b64decode(data1)
tag        = raw[-16:]
nonce      = raw[-28:-16]
ciphertext = raw[:-28]
plaintext  = AESGCM(LEGACY_GCM_KEY).decrypt(nonce, ciphertext + tag, None)

LEGACY_GCM_KEY = bytes([232, 86, 130, 189, 22, 84, 155, 0,
                        142, 4, 166, 104, 43, 179, 235, 227])
```

### 步骤 2：从明文里取出两个东西

```python
public_key_b64 = plaintext[10 : len(plaintext) - 10]   # 前后各去 10 字符
public_key     = RSA.import_key(base64.b64decode(public_key_b64))   # 2048 位，DER
```

路径后缀（复刻 APK 的算法）：

```python
table  = list("ABCDEFGHIJ")
last10 = plaintext[-10:]
# 两两分组，取每组的第二个字符在 table 中的下标，拼成字符串
path_ending = "".join(
    str(table.index(last10[i+1]))
    for i in range(0, len(last10), 2) if i + 1 < len(last10)
)
# 实测得到类似 "41170" / "74600"
```

### 步骤 3：加密并提交 SDP offer

```python
aes_key = uuid.uuid4().bytes.hex()      # 32 个 hex 字符 = AES-256 密钥

offer_body = {
    "id":    "STA_localNetwork",        # 局域网 STA 模式固定值
    "sdp":   <本地 SDP offer>,
    "type":  "offer",
    "token": "",                        # 无 token 时传空串
}

body = {
    "data1": aes_encrypt(json.dumps(offer_body), aes_key),  # AES-256-ECB + PKCS7，再 base64
    "data2": rsa_encrypt(aes_key, public_key),              # RSA PKCS#1 v1.5，再 base64
}

POST http://<ip>:9991/con_ing_<path_ending>
Content-Type: application/x-www-form-urlencoded
Body: <上面的 JSON 字符串>

→ 响应体是 AES 密文，用同一个 aes_key 解密 → 得到 SDP answer 的 JSON
```

加密细节：
- **AES-256-ECB + PKCS7**（padding 按字符个数补 `chr(n)*n` 后 UTF-8 编码）
- **RSA PKCS#1 v1.5**，分块大小为 `key_size - 11`

answer 处理：

```python
answer = json.loads(aes_decrypt(resp.text, aes_key))
if answer["sdp"] == "reject":
    # 机器人忙（通常被手机 App 占用）
setRemoteDescription(answer["sdp"], answer["type"])
```

---

## 2. 关键的 SDP 要求（踩坑记录）

### ⚠️ 坑 1：offer 必须包含音频和视频 m-line

只给 `m=application` 一条会被机器狗拒绝在 SCTP 层协商。实测需要在 offer 里加入：

- `m=audio`，direction `sendrecv`
- `m=video`，direction `recvonly`

机器狗会据此回 `m=video ... sendonly`（它推视频）。

### ⚠️ 坑 2：`a=fingerprint` 只能有一条（sha-256）

**这是本次最大的坑。** 默认的 WebRTC 库可能生成三条指纹：

```
a=fingerprint:sha-256 ...
a=fingerprint:sha-384 ...
a=fingerprint:sha-512 ...
```

**机器狗处理不了，会导致它侧 DTLS 静默失败 → 我们发的 SCTP INIT 永远收不到 INIT-ACK → 通道卡在 connecting。**

现象对比：

| | SCTP 日志 |
|---|---|
| 三条指纹（坏） | `> InitChunk` → `x T1 expired` → 无限重传 |
| 一条 sha-256（好） | `> InitChunk` → `< InitAckChunk` → `CookieEcho` → `< CookieAck` → `ESTABLISHED` |

**修复：发送 SDP 前剔除 `a=fingerprint:sha-384` 和 `a=fingerprint:sha-512` 行。**

### ⚠️ 坑 3：不要配置 STUN

构造 PeerConnection 时传**空的 iceServers**。若默认带 STUN，offer 会多出 `typ srflx` 的公网候选，机器狗可能选错路径。

```python
pc = RTCPeerConnection(RTCConfiguration(iceServers=[]))
```

---

## 3. DataChannel 消息格式

所有消息都是 **JSON 文本**，信封格式：

```json
{
  "type":  "<消息类型>",
  "topic": "<主题，心跳/校验时为空串>",
  "data":  <对象 | 字符串>
}
```

消息类型（`type`）：

| 值 | 方向 | 说明 |
|---|---|---|
| `validation` | 双向 | 校验握手 |
| `heartbeat` | 双向 | 心跳 |
| `msg` | 双向 | 普通消息 |
| `req` / `res` | 请求/响应 | API 调用 |
| `subscribe` / `unsubscribe` | 上行 | 订阅/退订主题 |
| `rtc_inner_req` / `rtc_report` | 双向 | 内部请求 |
| `vid` / `aud` | 上行 | 开关视频/音频通道 |
| `err` / `errors` / `add_error` / `rm_error` | 下行 | 错误上报 |

---

## 4. 校验握手（必须先完成）

连接建立后，机器狗会主动发来校验消息：

```json
{"type": "validation", "data": "<key>"}
```

客户端回复：

```python
md5_hex = md5(("UnitreeGo2_" + key).encode()).hexdigest()
reply   = base64.b64encode(bytes.fromhex(md5_hex)).decode()
```

```json
{"type": "validation", "topic": "", "data": "<reply>"}
```

机器狗回 `{"type":"validation","data":"Validation Ok."}` 即校验成功。

> 若收到 `{"type":"err","info":"Validation Needed."}`，需重发上面的 reply。

---

## 5. 心跳（每 2 秒）

```json
{
  "type": "heartbeat",
  "topic": "",
  "data": { "timeInStr": "2026-09-22 11:41:09", "timeInNum": 1790048469 }
}
```

机器狗回 `{"type":"heartbeat"}`。**不发心跳连接会被断开。**

---

## 6. 发送控制指令

```json
{
  "type": "req",
  "topic": "rt/api/sport/request",
  "data": {
    "header": {
      "identity": { "id": <随机 32 位整数>, "api_id": <指令号> }
    },
    "parameter": "<JSON 字符串>"     // 注意是字符串，不是对象；无参数时用 ""
  }
}
```

响应：

```json
{
  "type": "res",
  "topic": "rt/api/sport/response",
  "data": { "header": { "identity": {"api_id":1003,"id":...}, "status": {"code": 0} }, "data": "" }
}
```

`status.code == 0` 表示执行成功。

### 常用指令（topic = `rt/api/sport/request`）

| 名称 | api_id | parameter |
|---|---|---|
| Damp | 1001 | — |
| BalanceStand | 1002 | — |
| **StopMove** | **1003** | — |
| StandUp | 1004 | — |
| StandDown | 1005 | — |
| RecoveryStand | 1006 | — |
| **Move** | **1008** | `{"x":0.5,"y":0,"z":0}` |
| Sit | 1009 | — |
| RiseSit | 1010 | — |
| BodyHeight | 1013 | `{"data":<高度>}` |
| Hello | 1016 | — |
| Stretch | 1017 | — |
| Dance1 / Dance2 | 1022 / 1023 | — |

`Move` 的 x/y/z 单位：x 前后、y 左右（米/秒），z 转向（弧度/秒）。

### 运动模式切换（topic = `rt/api/motion_switcher/request`）

```json
{"api_id": 1001}                              // 查询当前模式
{"api_id": 1002, "parameter": "{\"name\":\"normal\"}"}   // 切到 normal / ai
```

---

## 7. 常用订阅主题（`type: "subscribe"`）

| 主题 | 说明 |
|---|---|
| `rt/sportmodestate` | 运动状态（姿态、速度、位置） |
| `rt/lf/lowstate` | 关节、IMU、电池 |
| `rt/utlidar/robot_pose` | 里程计位姿 |
| `rt/servicestate` | 服务状态 |
| `rt/utlidar/voxel_map_compressed` | 点云（二进制，需解码） |

---

## 8. 实测环境备注

- 本次测试机器狗上报了一个常驻错误：`{"type":"errors","data":[[<ts>,309,4]]}`（错误源 309）——排查时注意。
- 本机机器狗固件返回 `data2 = 2`，走 legacy 静态 GCM key 分支。
