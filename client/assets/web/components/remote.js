// 遥控页：安全行（急停 + 强制阻尼）→ 参数 / 快捷（两栏）→ 当前指令
// 精简版：受控设备一览已删 —— 摇杆带中间的狗卡片就是它，没必要显示两遍（页面太空挤）
import { computed } from 'vue'
import { store, cmd, post, param, setParam } from '../store.js'
import { ICON } from '../icons.js'
import IosSlider from './slider.js'

export default {
  name: 'RemotePage',
  components: { IosSlider },
  setup() {
    const canMove = computed(() => store.st.selectedCount > 0)
    const vx = computed(() => (store.st.cmd ? store.st.cmd.vx : 0))
    const vy = computed(() => (store.st.cmd ? store.st.cmd.vy : 0))
    const vz = computed(() => (store.st.cmd ? store.st.cmd.vz : 0))

    // 拖动时只改本地值，松手才下发（后端 500ms 才回一次，拖动中会被旧值拽回去）
    function onSlide(name, v) { setParam(name, v) }
    function onCommit(name, v) { post({ cmd: 'param', name, value: v }) }

    return {
      ICON, store, canMove, vx, vy, vz, param, onSlide, onCommit,
      estop: () => cmd({ cmd: 'estop' }),
      unestop: () => cmd({ cmd: 'unestop' }),
      damp: () => { store.dampArmed = false; cmd({ cmd: 'damp' }) },
      quick: (dir) => post({ cmd: 'quick', dir }),
      num: (v, d = 2) => Number(v).toFixed(d),
    }
  },
  template: `
  <section class="page">
    <!-- 安全行：急停（大头）+ 强制阻尼（小头，两步确认）—— 同一行省高度，急停依然最显眼 -->
    <div class="safe-row">
      <button class="estop-big" :class="{armed: store.st.estop}" @click="estop()">
        <span class="ic">{{ ICON.estop }}</span>急停（全部停车）
      </button>
      <button v-if="!store.dampArmed" class="damp" @click="store.dampArmed = true">
        <span class="ic">{{ ICON.lock }}</span>强制阻尼…
      </button>
      <button v-else class="damp-go" @click="damp()">确认阻尼？</button>
    </div>

    <div v-if="store.st.estop" class="estop-box">
      <div class="estop-title">急停锁定中 —— 摇杆与快捷步已失效</div>
      <div class="estop-tip">
        一次性动作（舞蹈 / 空翻 / 拜年）固件不接受打断；狗仍在动就点「强制阻尼」立即停住。
      </div>
      <button class="unestop" :disabled="!store.stickIdle" @click="unestop()">
        解除急停{{ store.stickIdle ? '' : '（需双杆回中）' }}
      </button>
    </div>

    <div class="cols">
      <div class="card pad">
        <p class="title"><span class="ic">{{ ICON.sliders }}</span>参数</p>
        <IosSlider label="线速度上限" :value="param('maxLinSpeed')" :min="0.05" :max="1.5"
                   :step="0.01" unit=" m/s" :disabled="!canMove"
                   @update="v => onSlide('maxLinSpeed', v)" @commit="v => onCommit('maxLinSpeed', v)" />
        <IosSlider label="转向角速度" :value="param('yawRate')" :min="0.2" :max="2.0"
                   :step="0.01" unit=" rad/s" :disabled="!canMove"
                   @update="v => onSlide('yawRate', v)" @commit="v => onCommit('yawRate', v)" />
        <IosSlider label="快捷步速" :value="param('speedScale')" :min="0.05" :max="1.5"
                   :step="0.01" unit="" :disabled="!canMove"
                   @update="v => onSlide('speedScale', v)" @commit="v => onCommit('speedScale', v)" />
      </div>

      <div class="card pad">
        <p class="title"><span class="ic">{{ ICON.gamepad }}</span>快捷
          <span class="sub">一次下发，持续到下一条指令</span>
        </p>
        <div class="dpad">
          <button class="dp up" :disabled="!canMove || store.st.estop" @click="quick('fwd')">
            <span class="ic">{{ ICON.arrowUp }}</span>前进
          </button>
          <div class="dp-row">
            <button class="dp" :disabled="!canMove || store.st.estop" @click="quick('left')">◀ 左转</button>
            <button class="dp" :disabled="!canMove || store.st.estop" @click="quick('back')">▼ 后退</button>
            <button class="dp" :disabled="!canMove || store.st.estop" @click="quick('right')">右转 ▶</button>
          </div>
        </div>
        <div class="readout">
          <span :class="store.st.estop ? 'bad' : (vx || vy || vz ? 'ok' : 'idle')">
            {{ store.st.estop ? '急停锁定 · 未下发' : (vx || vy || vz ? '下发中 · 10Hz' : '松手即停') }}
          </span>
          <span class="mono">vx {{ num(vx, 2) }} · vy {{ num(vy, 2) }} · vz {{ num(vz, 2) }}</span>
        </div>
      </div>
    </div>
  </section>
  `,
}
