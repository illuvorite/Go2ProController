// 苹果风滑条：细轨道 + 已填充段 + 悬浮大旋钮（与 ImGui 端 iosSliderFloat 同一观感）
// 轨道/填充/旋钮全自己画（原生 input[type=range] 在深色玻璃风里怎么调都不对味）
import { ref, computed } from 'vue'

export default {
  name: 'IosSlider',
  props: {
    label: String,
    value: Number,
    min: { type: Number, default: 0 },
    max: { type: Number, default: 1 },
    step: { type: Number, default: 0.01 },
    unit: { type: String, default: '' },
    digits: { type: Number, default: 2 },
    disabled: Boolean,
  },
  emits: ['update', 'commit'],
  setup(props, { emit }) {
    const el = ref(null)
    const dragging = ref(false)

    const pct = computed(() => {
      const t = (props.value - props.min) / (props.max - props.min || 1)
      return Math.max(0, Math.min(1, t)) * 100
    })
    const text = computed(() => Number(props.value).toFixed(props.digits) + props.unit)

    function valueAt(clientX) {
      const r = el.value.getBoundingClientRect()
      const t = Math.max(0, Math.min(1, (clientX - r.left) / (r.width || 1)))
      const raw = props.min + t * (props.max - props.min)
      const snapped = Math.round(raw / props.step) * props.step
      // 浮点步长会拖出一串 0.30000000000000004 —— 按步长位数取整
      const fix = Math.max(0, Math.ceil(-Math.log10(props.step)))
      return Number(snapped.toFixed(fix))
    }
    function down(e) {
      if (props.disabled) return
      dragging.value = true
      el.value.setPointerCapture(e.pointerId)
      emit('update', valueAt(e.clientX))
    }
    function move(e) {
      if (!dragging.value) return
      emit('update', valueAt(e.clientX))
    }
    function up(e) {
      if (!dragging.value) return
      dragging.value = false
      if (el.value.hasPointerCapture(e.pointerId)) el.value.releasePointerCapture(e.pointerId)
      emit('commit', props.value)
    }

    return { el, dragging, pct, text, down, move, up }
  },
  template: `
  <div class="slider-row" :class="{off:disabled}">
    <div class="slider-label">{{ label }}</div>
    <div class="slider-track" ref="el"
         @pointerdown="down" @pointermove="move" @pointerup="up" @pointercancel="up">
      <div class="slider-fill" :style="{width: pct + '%'}"></div>
      <div class="slider-knob" :class="{grab:dragging}" :style="{left: pct + '%'}"></div>
    </div>
    <div class="slider-value">{{ text }}</div>
  </div>
  `,
}
