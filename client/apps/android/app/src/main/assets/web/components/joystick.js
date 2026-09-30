// 悬浮摇杆：只用 pointer 事件（鼠标 / 触屏 / 触控笔通吃），两杆各持有自己的指针 → **可以同时拖**
// （这也是当初把界面往 WebView 搬的原因之一：ImGui 只有一个指针，双杆要靠平台层自己接管手指）
import { ref, computed } from 'vue'

export default {
  name: 'Joystick',
  props: {
    label: String,
    radius: { type: Number, default: 72 },
    axis: { type: String, default: 'xy' },  // 保留但不再锁方向：两个杆都是 360° 全向
  },
  emits: ['change', 'release'],
  setup(props, { emit }) {
    const el = ref(null)
    // 旋钮行程： knobR = 0.31r，行程上限留 6px 边 → **推到头时旋钮边缘几乎贴到杆的外缘**
    const knobR = computed(() => props.radius * 0.31)
    const maxOff = computed(() => props.radius - knobR.value - 6)
    const x = ref(0)
    const y = ref(0)
    const active = ref(false)

    function track(e) {
      const r = el.value.getBoundingClientRect()
      let nx = (e.clientX - (r.left + r.width / 2)) / (r.width / 2)
      let ny = (e.clientY - (r.top + r.height / 2)) / (r.height / 2)
      const m = Math.hypot(nx, ny)
      if (m > 1) { nx /= m; ny /= m }   // 超出圆 → 按方向压回圆周（全向，不锁轴）
      if (Math.abs(nx) < 0.02) nx = 0   // 死区：不然手指一碰就飘
      if (Math.abs(ny) < 0.02) ny = 0
      x.value = nx
      y.value = ny
      emit('change', { x: nx, y: ny })
    }
    function down(e) {
      active.value = true
      el.value.setPointerCapture(e.pointerId)
      track(e)
    }
    function move(e) { if (active.value) track(e) }
    function up(e) {
      if (!active.value) return
      if (el.value.hasPointerCapture(e.pointerId)) el.value.releasePointerCapture(e.pointerId)
      active.value = false
      x.value = 0
      y.value = 0
      emit('change', { x: 0, y: 0 })
      emit('release')
    }

    return { el, x, y, active, down, move, up, knobR, maxOff }
  },
  template: `
  <div class="joy" :class="{active:active}" ref="el"
       :style="{width: radius*2 + 'px', height: radius*2 + 'px'}"
       @pointerdown="down" @pointermove="move" @pointerup="up" @pointercancel="up">
    <div class="joy-ring"></div>
    <div class="joy-cross"></div>
    <div class="joy-knob" :style="{
      width: knobR*2 + 'px', height: knobR*2 + 'px',
      left: '50%', top: '50%',
      marginLeft: (-knobR) + 'px', marginTop: (-knobR) + 'px',
      transform: 'translate(' + (x*maxOff) + 'px,' + (y*maxOff) + 'px)'
    }"></div>
    <div class="joy-label">{{ label }}</div>
  </div>
  `,
}
