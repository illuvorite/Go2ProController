#!/usr/bin/env python3
"""读取机器狗 :9991 /con_notify 的完整明文（SN / 固件版本 / data2 等）。

用法: python3 _con_notify.py <IP> [端口]
"""
import base64
import socket
import sys

IP = sys.argv[1] if len(sys.argv) > 1 else "192.168.0.169"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 9991

s = socket.socket()
s.settimeout(4)
s.connect((IP, PORT))
s.sendall(f"GET /con_notify HTTP/1.1\r\nHost: {IP}\r\nConnection: close\r\n\r\n".encode())
data = b""
while True:
    try:
        chunk = s.recv(4096)
    except socket.timeout:
        break
    if not chunk:
        break
    data += chunk
s.close()

head, _, body = data.partition(b"\r\n\r\n")
print("HTTP:", head.split(b"\r\n")[0].decode("latin1"))
raw = base64.b64decode(body.strip())
print(f"明文长度 {len(raw)} 字节\n")
print(raw.decode("utf-8", "replace"))
