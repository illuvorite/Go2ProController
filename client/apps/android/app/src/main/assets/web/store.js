// 全局状态（Vue3 响应式单例）：页面 / 弹窗 / 机器狗快照 / 参数，都在这一份里。
// 组件直接 import 它 —— 单页应用没那么多层级，provide/inject 是多余的仪式。
import { reactive } from 'vue'
import { getState, send } from './api.js'

const DEFAULT_PARAMS = {
  maxLinSpeed: 0.60,  // 摇杆满偏线速度 m/s
  yawRate: 1.20,      // 转向角速度 rad/s
  speedScale: 0.50,   // 快捷步速
  bodyHeight: 0.28,
  footRaise: 0.06,
  speedLevel: 1,
  gaitType: 1,
  mcf: false,
  privacy: false,
}

export const store = reactive({
  st: {
    robots: [], actions: [], log: [],
    estop: false, selectedCount: 0, target: '未选择受控',
    params: { ...DEFAULT_PARAMS },
    cmd: { vx: 0, vy: 0, vz: 0 },
    batteryMin: -1,
    problemCount: 0,
    keyCount: 0,
  },
  page: 'remote',   // remote | actions
  modal: '',        // '' | devices | settings | log
  menu: false,      // 顶栏「⋯更多」菜单
  picker: false,    // 摇杆带中间的「单控」选择器（向上弹）
  dampArmed: false, // 强制阻尼二次确认
  stickIdle: true,  // 双杆是否回中（解除急停的前置条件，由摇杆带写入）
  err: '',
  online: false,
})

export function param(name) {
  const v = store.st.params ? store.st.params[name] : undefined
  return v === undefined ? DEFAULT_PARAMS[name] : v
}

export function setParam(name, v) {
  if (!store.st.params) store.st.params = { ...DEFAULT_PARAMS }
  store.st.params[name] = v
}

export async function refresh() {
  try {
    const j = await getState()
    // 保留本地正在拖动的参数值：后端 500ms 才回一次，拖动中会被旧值拽回去
    const keep = store.st.params
    store.st = j
    if (j.params) store.st.params = { ...DEFAULT_PARAMS, ...j.params, ...keep }
    store.online = true
    store.err = ''
  } catch (e) {
    store.online = false
    store.err = '连不上本机服务：' + e.message
  }
}

export async function cmd(body) {
  try {
    await send(body)
    store.err = ''
    await refresh()
  } catch (e) {
    store.err = e.message
  }
}

/// 高频指令（摇杆 10Hz）用这个：**不要**每次都回拉一次 state，交给 500ms 轮询
export async function post(body) {
  try {
    await send(body)
    store.err = ''
  } catch (e) {
    store.err = e.message
  }
}

/// 已经在受控的设备（遥控页「受控设备」与单控选择器共用）
export function selectedRobots() {
  return (store.st.robots || []).filter((r) => r.selected)
}

export function batteryText(v) {
  return v >= 0 ? Math.round(v) + '%' : '—'
}
