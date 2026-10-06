// 与 C++ 后端（ui/web_bridge.cpp）通信：只有两个口子 —— GET /api/state、POST /api/command。
// 协议 / 加密 / 钥匙库 / 运动指令表全在 C++ 那边，前端一行都不碰。
//
// 访问令牌：后端默认只监听 127.0.0.1（不需要令牌）。当它被放在别的机器/容器上时，
// 会要求 `X-Go2-Token` 头或 `?token=` 参数。用户只要带 `?token=xxx` 打开一次页面，
// 这里就会记住它（localStorage），之后每次请求自动带上。

const TOKEN_KEY = 'go2_token'

/** 取当前令牌：URL 上的 ?token= 优先（首次访问），否则用之前记住的 */
export function resolveToken() {
  try {
    const fromUrl = new URLSearchParams(location.search).get('token')
    if (fromUrl) {
      localStorage.setItem(TOKEN_KEY, fromUrl)
      return fromUrl
    }
    return localStorage.getItem(TOKEN_KEY) || ''
  } catch {
    return ''
  }
}

function authHeaders(extra) {
  const t = resolveToken()
  return t ? { ...extra, 'X-Go2-Token': t } : { ...extra }
}

export async function getState() {
  const r = await fetch('/api/state', { cache: 'no-store', headers: authHeaders({}) })
  if (!r.ok) throw new Error('HTTP ' + r.status)
  return await r.json()
}

export async function send(body) {
  const r = await fetch('/api/command', {
    method: 'POST',
    headers: authHeaders({ 'Content-Type': 'application/json' }),
    body: JSON.stringify(body),
  })
  const j = await r.json()
  if (j.ok === false) throw new Error(j.error || '指令失败')
  return j
}
