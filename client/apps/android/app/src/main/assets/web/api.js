// 与 C++ 后端（ui/web_bridge.cpp）通信：只有两个口子 —— GET /api/state、POST /api/command。
// 协议 / 加密 / 钥匙库 / 运动指令表全在 C++ 那边，前端一行都不碰。

export async function getState() {
  const r = await fetch('/api/state', { cache: 'no-store' })
  if (!r.ok) throw new Error('HTTP ' + r.status)
  return await r.json()
}

export async function send(body) {
  const r = await fetch('/api/command', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  })
  const j = await r.json()
  if (j.ok === false) throw new Error(j.error || '指令失败')
  return j
}
