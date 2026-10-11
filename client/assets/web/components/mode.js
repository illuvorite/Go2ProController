// 模式设置页：普通 / AI / MCF 三选一（参考实现「天树探界遥控」的「模式设置」页）。
//
// 它走的是 **motion_switcher**（`rt/api/motion_switcher/request`，api_id 1002），
// 跟运动指令（`rt/api/sport/request`）是两套东西 —— 所以不能当成一条普通动作下发。
// 后端 `cmd::setMotionMode` 负责逐台分发；机器人自己上报的 mode_name 会覆盖这个显示。
import { computed } from 'vue'
import { store, cmd } from '../store.js'
import { ICON } from '../icons.js'

const MODES = [
  { key: 'normal', label: '普通模式', icon: 'paw', desc: '默认运动控制：手动遥控、动作库、舞蹈编排全功能可用。' },
  { key: 'ai', label: 'AI 模式', icon: 'robot', desc: '启用机器人自身的智能功能（跟随 / 避障等），部分手动动作会被限制。' },
  { key: 'mcf', label: 'MCF 模式', icon: 'shield', desc: '运动控制框架模式：使用另一套 api_id 指令集。动作库页的「MCF 指令集」勾选项要与它一致，否则会大量动作被拒。' },
]

export default {
  name: 'ModePage',
  setup() {
    // 后端给的 mode 可能是 "normal" / "ai" / "mcf"；也接受固件上报的其它名字
    const current = computed(() => (store.st.motionMode || '').toLowerCase())
    const isOn = (m) => current.value === m.key
    const canSend = computed(() => store.st.selectedCount > 0 && !store.st.estop)
    return {
      store, ICON, MODES, isOn, canSend,
      pick: (m) => cmd({ cmd: 'mode', mode: m.key }),
      // 固件没上报过模式名时不装作知道
      unknown: computed(() => !current.value),
    }
  },
  template: `
  <section class="page">
    <div class="card pad mode-header">
      <div class="sub">
        当前：<b>{{ store.st.motionMode || '未知' }}</b>
        <span v-if="!canSend" class="warn"> · {{ store.st.estop ? '急停锁定中，无法切换' : '未选择受控设备' }}</span>
      </div>
      <div class="sub">模式切换走的是运动切换服务（motion_switcher），不是运动指令；切换后请确认机器人已接受。</div>
    </div>

    <div class="mode-grid">
      <button v-for="m in MODES" :key="m.key" class="mode-card"
              :class="{on: isOn(m), disabled: !canSend}" @click="canSend && pick(m)">
        <span class="mode-ico ic">{{ ICON[m.icon] }}</span>
        <span class="mode-name">{{ m.label }}</span>
        <span class="mode-desc">{{ m.desc }}</span>
        <span v-if="isOn(m)" class="mode-badge">当前</span>
      </button>
    </div>

    <div v-if="unknown" class="card pad empty">机器人还没上报过当前模式 —— 连上后会自动显示。</div>
  </section>
  `,
}
