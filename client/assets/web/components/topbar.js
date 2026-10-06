// 顶栏：左侧品牌 + 受控胶囊 + 电量胶囊（取受控设备里最低的）；右侧 **4 个按钮**：
// 设备 / 页面切换（遥控⇄动作库）/ ⋯更多（设置·日志）/ ■急停
// ⚠ 与 ImGui 端保持一致：不要返回键，「设备」必须在顶栏（不埋进菜单）
import { computed } from 'vue'
import { store, cmd, maskIp } from '../store.js'
import { ICON } from '../icons.js'

export default {
  name: 'TopBar',
  setup() {
    const isRemote = computed(() => store.page === 'remote')
    const unread = computed(() => store.st.problemUnread || 0)
    const battery = computed(() => store.st.batteryMin)

    function togglePage() { store.page = isRemote.value ? 'actions' : 'remote'; store.menu = false }
    function toPage(p) { store.page = p; store.menu = false }
    function open(m) { store.modal = m; store.menu = false }

    return {
      ICON, store, isRemote, unread, battery, togglePage, toPage, open, maskIp,
      estop: () => cmd({ cmd: 'estop' }),
      batteryText: (v) => (v >= 0 ? Math.round(v) + '%' : '—'),
    }
  },
  template: `
  <header class="topbar">
    <div class="tb-left">
      <span class="brand" title="H-bbot · 幻核睛山"><img class="brand-logo" src="img/logo-word.png" alt="H-bbot" /></span>
      <span class="chip" :class="{on: store.st.selectedCount > 0}">
        <span class="ic">{{ store.st.selectedCount > 1 ? ICON.users : ICON.user }}</span>
        {{ maskIp(store.st.target) || '未选择受控' }}
      </span>
      <span class="chip">
        <span class="ic">{{ ICON.battery }}</span>{{ batteryText(battery) }}
      </span>
      <span v-if="store.err" class="chip err">{{ store.err }}</span>
    </div>

    <div class="tb-right">
      <button @click="open('devices')">
        <span class="ic">{{ ICON.robot }}</span>设备
      </button>
      <button @click="togglePage()">
        <span class="ic">{{ isRemote ? ICON.taiChi : ICON.joystick }}</span>{{ isRemote ? '动作库' : '遥控' }}
      </button>
      <button class="icon-btn" @click="store.menu = !store.menu">
        <span class="ic">{{ ICON.dots }}</span>
        <i v-if="unread > 0" class="badge">{{ unread > 99 ? '99+' : unread }}</i>
      </button>
      <button class="danger" @click="estop()">
        <span class="ic">{{ ICON.estop }}</span>急停
      </button>
    </div>

    <div v-if="store.menu" class="menu-mask" @click="store.menu = false"></div>
    <div v-if="store.menu" class="menu">
      <button @click="open('settings')"><span class="ic">{{ ICON.gear }}</span>设置</button>
      <button @click="open('log')">
        <span class="ic">{{ ICON.log }}</span>日志
        <i v-if="unread > 0" class="badge">{{ unread > 99 ? '99+' : unread }}</i>
      </button>
      <div class="menu-sep"></div>
      <button @click="toPage('actions')"><span class="ic">{{ ICON.list }}</span>动作库</button>
      <button @click="toPage('remote')"><span class="ic">{{ ICON.joystick }}</span>遥控</button>
    </div>
  </header>
  `,
}
