#!/usr/bin/env python3
# 网页界面预览服务器（**只用于看界面**，不连机器狗）
#
# 为什么需要它：C++ 端那个 WebUI 服务跑在 WSL 里，实测 Windows 浏览器连不上
# （即便 networkingMode=mirrored，回环地址也不跨 Windows/WSL）。想在本机浏览器里
# 看/改前端，就在 Windows 上跑这个脚本：它用假数据顶掉 /api/state 与 /api/command。
#
#   python tools/web_preview.py            # 默认 8123
#   python tools/web_preview.py 9000       # 换端口
#   → 浏览器打开 http://127.0.0.1:8123
#
# 静态文件来自 client/assets/web（与 C++ 端 set_base_dir("assets/web") 同一份）。
import json
import os
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'client', 'assets', 'web')
ROOT = os.path.normpath(ROOT)

MIME = {
    '.html': 'text/html; charset=utf-8',
    '.js': 'text/javascript; charset=utf-8',
    '.mjs': 'text/javascript; charset=utf-8',
    '.css': 'text/css; charset=utf-8',
    '.json': 'application/json; charset=utf-8',
    '.ttf': 'font/ttf',
    '.svg': 'image/svg+xml',
    '.png': 'image/png',
}

# 假数据：两台狗 + 一小撮动作（够看布局；真实动作表由 C++ 端下发）
STATE = {
    'estop': False,
    'mcf': False,
    'selectedCount': 1,
    'target': '单控 · 小黑',
    'batteryMin': 78,
    'problemCount': 3,
    'problemUnread': 2,
    'scanning': False,
    'keyCount': 4,
    'params': {'maxLinSpeed': 0.60, 'yawRate': 1.20, 'speedScale': 0.50,
               'bodyHeight': 0.28, 'footRaise': 0.06, 'speedLevel': 1,
               'gaitType': 1, 'mcf': False, 'privacy': False},
    'cmd': {'vx': 0.0, 'vy': 0.0, 'vz': 0.0},
    'toggles': {'FreeWalk': True},
    'robots': [
        {'ip': '192.168.123.161', 'name': '小黑', 'label': '小黑', 'selected': True,
         'battery': 78.0, 'mode': 'pro', 'state': '就绪', 'ready': True},
        {'ip': '192.168.123.18', 'name': '', 'label': '192.168.123.18', 'selected': False,
         'battery': 42.0, 'mode': 'air', 'state': '未连接', 'ready': False},
    ],
    'actions': [
        {'key': 'Damp', 'label': '阻尼', 'group': 0, 'risky': False, 'toggle': False},
        {'key': 'BalanceStand', 'label': '平衡站立', 'group': 0, 'risky': False, 'toggle': False},
        {'key': 'StopMove', 'label': '停止移动', 'group': 0, 'risky': False, 'toggle': False},
        {'key': 'StandUp', 'label': '站立', 'group': 0, 'risky': False, 'toggle': False},
        {'key': 'StandDown', 'label': '趴下', 'group': 0, 'risky': False, 'toggle': False},
        {'key': 'RecoveryStand', 'label': '恢复站立', 'group': 0, 'risky': False, 'toggle': False},
        {'key': 'Sit', 'label': '坐下', 'group': 0, 'risky': False, 'toggle': False},
        {'key': 'Hello', 'label': '打招呼', 'group': 2, 'risky': False, 'toggle': False},
        {'key': 'Stretch', 'label': '伸懒腰', 'group': 2, 'risky': False, 'toggle': False},
        {'key': 'WiggleHips', 'label': '扭屁股', 'group': 2, 'risky': False, 'toggle': False},
        {'key': 'FingerHeart', 'label': '比心', 'group': 2, 'risky': False, 'toggle': False},
        {'key': 'Dance1', 'label': '舞蹈 1', 'group': 2, 'risky': False, 'toggle': False},
        {'key': 'Dance2', 'label': '舞蹈 2', 'group': 2, 'risky': False, 'toggle': False},
        {'key': 'MoonWalk', 'label': '太空步', 'group': 2, 'risky': False, 'toggle': False},
        {'key': 'FreeWalk', 'label': '自由行走', 'group': 2, 'risky': False, 'toggle': True},
        {'key': 'FrontJump', 'label': '前跳', 'group': 3, 'risky': True, 'toggle': False},
        {'key': 'BackFlip', 'label': '后空翻', 'group': 3, 'risky': True, 'toggle': False},
        {'key': 'LeftFlip', 'label': '左空翻', 'group': 3, 'risky': True, 'toggle': False},
        {'key': 'RightFlip', 'label': '右空翻', 'group': 3, 'risky': True, 'toggle': False},
        {'key': 'Handstand', 'label': '倒立', 'group': 3, 'risky': True, 'toggle': False},
        {'key': 'GetState', 'label': '查运动状态', 'group': 4, 'risky': False, 'toggle': False},
        {'key': 'GetBodyHeight', 'label': '查机身高度', 'group': 4, 'risky': False, 'toggle': False},
        {'key': 'ClassicWalk', 'label': '经典步态', 'group': 5, 'risky': False, 'toggle': False},
        {'key': 'FreeAvoid', 'label': '自由避障', 'group': 5, 'risky': False, 'toggle': False},
    ],
    'log': ['[预览] 这是假数据 —— 真机界面请跑 C++ 端 go2_remote',
            '[预览] 12:00:01 开始连接 sn: B42D2000P6CDL807',
            '[预览] 12:00:03 就绪 (data2=3)',
            '[预览] 12:00:11 动作 Hello 回执 code=0',
            '[预览] 12:00:20 失败：api 2041 不在本固件指令表 (3203)'],
}


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):  # 精简控制台输出
        sys.stderr.write('%s %s\n' % (self.command, self.path))

    def _send(self, code, body, ctype):
        self.send_response(code)
        self.send_header('Content-Type', ctype)
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-store')
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = self.path.split('?')[0]
        if path == '/api/state':
            # 预览模式下让前端的本地改动（参数 / 单控）也能反映出来：只回静态假数据
            self._send(200, json.dumps(STATE).encode('utf-8'), 'application/json; charset=utf-8')
            return
        if path == '/':
            path = '/index.html'
        full = os.path.normpath(os.path.join(ROOT, path.lstrip('/')))
        if not full.startswith(ROOT) or not os.path.isfile(full):
            self._send(404, b'404', 'text/plain')
            return
        with open(full, 'rb') as f:
            self._send(200, f.read(), MIME.get(os.path.splitext(full)[1], 'application/octet-stream'))

    def do_POST(self):
        n = int(self.headers.get('Content-Length') or 0)
        raw = self.rfile.read(n) if n else b''
        try:
            body = json.loads(raw.decode('utf-8'))
        except Exception:
            body = {}
        cmd = body.get('cmd')
        # 预览：只把指令打在控制台（顺便让 state 里的开关能翻，看着像真的）
        if cmd == 'action' and body.get('key') in STATE['toggles']:
            STATE['toggles'][body['key']] = not STATE['toggles'][body['key']]
        if cmd == 'select':
            mode = body.get('mode')
            ips = body.get('ips') or []
            for r in STATE['robots']:
                if mode == 'all':
                    r['selected'] = True
                elif mode == 'none':
                    r['selected'] = False
                elif mode == 'multi':   # 卡片勾选：精确这个集合
                    r['selected'] = r['ip'] in ips
                else:                   # one：只控这一台
                    r['selected'] = (r['ip'] == body.get('ip'))
            cnt = sum(1 for r in STATE['robots'] if r['selected'])
            STATE['selectedCount'] = cnt
            if cnt == 0:
                STATE['target'] = '未选择受控'
            elif cnt == 1:
                STATE['target'] = '单控 · ' + [r for r in STATE['robots'] if r['selected']][0]['label']
            else:
                STATE['target'] = '群控 · %d 台' % cnt
        if cmd == 'estop':
            STATE['estop'] = True
        if cmd == 'unestop':
            STATE['estop'] = False
        if cmd == 'param':
            STATE['params'][body.get('name')] = body.get('value')
        if cmd == 'logseen':
            STATE['problemUnread'] = 0
        if cmd == 'connect':
            for r in STATE['robots']:
                if r['ip'] == body.get('ip'):
                    r['state'], r['ready'] = '信令中', False

            def _ready(ip=body.get('ip')):
                for r in STATE['robots']:
                    if r['ip'] == ip:
                        r['state'], r['ready'] = '就绪', True
            threading.Timer(2.0, _ready).start()
        if cmd == 'remove':
            STATE['robots'] = [r for r in STATE['robots'] if r['ip'] != body.get('ip')]
        if cmd == 'rename':
            for r in STATE['robots']:
                if r['ip'] == body.get('ip'):
                    r['name'] = body.get('name', '')
                    r['label'] = body.get('name') or r['ip']
        if cmd == 'scan':
            # 预览模式没有真扫描：**绝不伪造设备**（用户要自己加 IP 就走「添加」）
            STATE['scanning'] = True

            def _scan_done():
                STATE['scanning'] = False
                print('[预览] 扫描在预览模式下不生效 —— 真扫描请跑 go2_remote')
            threading.Timer(1.0, _scan_done).start()
        print('[预览] 收到指令：%s' % json.dumps(body, ensure_ascii=False))
        self._send(200, json.dumps({'ok': True}).encode('utf-8'), 'application/json; charset=utf-8')


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8123
    print('[预览] 静态目录：%s' % ROOT)
    print('[预览] 浏览器打开 http://127.0.0.1:%d' % port)
    ThreadingHTTPServer(('127.0.0.1', port), Handler).serve_forever()


if __name__ == '__main__':
    main()
