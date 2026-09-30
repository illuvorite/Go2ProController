// 摇杆带：双摇杆恒悬浮两下角 + 中间「单控 / 群控」面板
// 对应 ImGui 端的 drawJoysticks：摇杆半径 clamp(min(viewW,viewH)*0.15, 52, 88)，矮屏（<760）上限 70
import { computed, ref, onMounted, onUnmounted, watchEffect } from 'vue'
import { store, cmd, post, param, maskIp } from '../store.js'
import { planMotion } from '../motion.js'
import { ICON } from '../icons.js'
import Joystick from './joystick.js'

export default {
  name: 'JoystickBand',
  components: { Joystick },
  setup() {
    const vw = ref(window.innerWidth)
    const vh = ref(window.innerHeight)
    const onResize = () => { vw.value = window.innerWidth; vh.value = window.innerHeight }
    onMounted(() => window.addEventListener('resize', onResize))
    onUnmounted(() => window.removeEventListener('resize', onResize))

    const radius = computed(() => {
      const cap = vh.value < 760 ? 70 : 88
      return Math.round(Math.max(52, Math.min(Math.min(vw.value, vh.value) * 0.15, cap)))
    })

    // 摇杆带高度告诉页面区：内容底部要留白，别被摇杆压住（ImGui 端对应 L.joyReserve）
    watchEffect(() => {
      // +56：卡片行 + 单控/群控按钮 + 受控对象文字，小屏也不会把摇杆挤扁
      document.documentElement.style.setProperty('--band-h', (radius.value * 2 + 56) + 'px')
    })

    const lx = ref(0), ly = ref(0), rx = ref(0)
    let movingSent = false
    let timer = null

    function stopTimer() {
      if (timer) { clearInterval(timer); timer = null }
    }

    function tick() {
      const p = planMotion(lx.value, ly.value, rx.value, store.st.estop,
                           param('maxLinSpeed'), param('yawRate'), movingSent)
      store.st.cmd = { vx: p.vx, vy: p.vy, vz: p.vz }
      if (p.send) {
        post({ cmd: 'move', x: p.vx, y: p.vy, z: p.vz })
        movingSent = true
      } else {
        // 松手 / 急停 → 补发一次停车（不重复刷）
        if (p.stop) { post({ cmd: 'move', stop: true }); }
        movingSent = false
        stopTimer()
      }
    }

    function onChange(which, v) {
      if (which === 'L') { lx.value = v.x; ly.value = v.y } else { rx.value = v.x }
      const idle = lx.value === 0 && ly.value === 0 && rx.value === 0
      store.stickIdle = idle
      if (idle) { tick(); return }
      if (!timer) { tick(); timer = setInterval(tick, 100) }  // 10Hz 持续下发
    }
    // 弹窗打开 / 组件卸载：数值清零并停车（别让"看着弹窗、手指还在推"继续下发）
    onUnmounted(() => { stopTimer(); if (movingSent) post({ cmd: 'move', stop: true }) })

    // 勾选式受控：点一台勾一台（可多选），勾选集合 = 精确发给后端的 ips。
    // ★ 勾上 = 真的能控：还没连上的顺手发起连接（不然勾了也没反应，看着像无效）
    async function toggleSel(r) {
      let ips = store.st.robots.filter((x) => x.selected).map((x) => x.ip)
      const turningOn = !r.selected
      if (turningOn) ips.push(r.ip)
      else ips = ips.filter((ip) => ip !== r.ip)
      await cmd({ cmd: 'select', mode: 'multi', ips })
      if (turningOn && !r.ready) await post({ cmd: 'connect', ip: r.ip })
    }
    // 群控 = 开关：没选任何台 → 全选；已选了台数 → 再点一次全取消
    function toggleAll() {
      cmd({ cmd: 'select', mode: store.st.selectedCount > 0 ? 'none' : 'all' })
    }
    // 单控（保留）：向上弹的选择器里点一台 = **只控它**；另有「取消单控」
    function pick(ip) { store.picker = false; cmd({ cmd: 'select', mode: 'one', ip }) }
    function pickNone() { store.picker = false; cmd({ cmd: 'select', mode: 'none' }) }

    return { ICON, store, radius, lx, ly, rx, onChange, toggleSel, toggleAll, pick, pickNone, maskIp,
             batteryText: (v) => (v >= 0 ? Math.round(v) + '%' : '—') }
  },
  template: `
  <div class="band">
    <Joystick class="joy-left" label="移动" :radius="radius" axis="xy"
              @change="v => onChange('L', v)" />

    <div class="band-panel">
      <!-- 受控机械狗：**单独一行卡片**（狗图标 + 名字 + 电量），点一张勾/取消，狗多了横着滑 -->
      <div class="dog-row">
        <!-- 用 label 包 card：点卡片任意处 = 点右上角的选择框（一次 change，不会双触发） -->
        <label v-for="r in store.st.robots" :key="r.ip" class="dog-card" :class="{sel: r.selected}"
               :title="maskIp(r.ip) + ' · ' + r.state">
          <input type="checkbox" class="dog-ck" :checked="r.selected" @change="toggleSel(r)" />
          <span class="dog-dot" :class="{ready: r.ready}"></span>
          <!-- 宇树 Go2 实拍图（Wikimedia Commons，CC BY 3.0，署名见 img/CREDITS.txt） -->
          <img class="dog-img" src="./img/go2.jpg" alt="Unitree Go2"
               srcset="./img/go2.jpg 1x, ./img/go2@2x.jpg 2x" />
          <span class="dog-nm">{{ maskIp(r.label) }}</span>
          <span class="dog-bt">
            <span class="ic">{{ ICON.battery }}</span>{{ batteryText(r.battery) }}
            <span class="dog-st" :class="{ok: r.ready}">· {{ r.state }}</span>
          </span>
        </label>
        <div v-if="!store.st.robots.length" class="dog-empty">还没有设备 —— 去顶栏「设备」添加</div>
      </div>

      <div class="panel-btns">
        <button class="panel-btn" :class="{on: store.picker}" @click="store.picker = !store.picker">
          <span class="ic">{{ ICON.user }}</span>单控
        </button>
        <button class="panel-btn" :class="{on: store.st.selectedCount > 0}" @click="toggleAll()">
          <span class="ic">{{ ICON.users }}</span>群控
        </button>
      </div>
      <span class="panel-target">
        <span class="dot" :class="{ready: store.st.selectedCount > 0}"></span>
        {{ store.st.target || '未选择受控' }}
      </span>

      <!-- 单控选择器：**向上弹**（底边贴着按钮顶边），点一台就只控它 -->
      <div v-if="store.picker" class="picker-backdrop" @click="store.picker = false"></div>
      <div v-if="store.picker" class="picker">
        <div class="picker-title">单控：只控制这一台</div>
        <button v-for="r in store.st.robots" :key="r.ip" class="picker-item"
                :class="{sel: r.selected}" @click="pick(r.ip)">
          <span class="dot" :class="{ready: r.ready}"></span>
          <span class="nm">{{ maskIp(r.label) }}</span>
          <span class="st">{{ r.state }}</span>
          <span class="bt">{{ batteryText(r.battery) }}</span>
        </button>
        <div v-if="!store.st.robots.length" class="picker-empty">还没有设备 —— 去顶栏「设备」添加</div>
        <button class="picker-item none" @click="pickNone()">取消单控（不控制任何设备）</button>
      </div>
    </div>

    <!-- 两个杆都是 360° 全向（不锁方向）：旋钮跟着手指走满一圈 -->
    <Joystick class="joy-right" label="转向" :radius="radius" axis="xy"
              @change="v => onChange('R', v)" />
  </div>
  `,
}
