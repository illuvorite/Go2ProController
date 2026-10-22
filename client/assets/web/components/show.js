// 演出页（多机编队表演）：舞台图 + 拍格时间轴 + 开演控制。
//
// ============================================================================
// 这一页对标智身科技 SomaFlow 的「多机舞蹈表演系统」的**编导台**：
//   · 舞台图（俯视，单位米）—— 画舞台位、看队形、指派哪台狗站哪；
//   · 拍格时间轴 —— cue 落在第几拍，而不是"发完等多久"；
//   · 角色 —— 同一条 cue 可以只给队里的某几台；
//   · 开演 / 停止 + **同步度量**（最大错位、平均错位、迟到条数）。
//
// ============================================================================
// 为什么"拍"而不是"秒"：
//   演出的时间是跟着音乐走的，而音乐有 BPM。编导按拍编排（"第 8 拍全体敬礼"），
//   换一首歌只要改 BPM 与拍数，编舞不用动。后端把拍换算成绝对时刻
//   （t0 + beat × 一拍），所以误差不累积 —— 这是"同步"的实现前提。
//
// ★ 舞台图**不是实时定位图**：
//   Go2 的 sport 接口没有绝对定位（状态话题里只有电量/温度/姿态角），
//   所以队形是**开环**的：编导在图上定好位，狗按"前进几米/左移几米/转几度"走过去。
//   这张图的作用是**校验与沟通**：一眼看出队形对不对、位移量写没写错。
//   后端在开演前会用它做静态校验（位移量与舞台位对不上会直接报错）。
// ============================================================================
import { computed, ref, watch, onMounted, onUnmounted } from 'vue'
import { store, cmd, post, refresh } from '../store.js'
import { ICON } from '../icons.js'

const GROUPS = ['基础姿态', '表演动作', '跳跃特技', '其他 / 进阶']
const KIND = { ACTION: 0, MOVE: 1, LIGHT: 2 }
const KIND_NAME = { 0: '动作', 1: '位移', 2: '灯光' }

// 舞台图的显示范围（米）。超出这个范围就画到边上并标出来。
const STAGE_SPAN = 8

export default {
  name: 'ShowPage',
  setup() {
    const editing = ref(false)
    const form = ref(null)
    const tab = ref('cue')          // 'cue' | 'formation'
    const selCue = ref(-1)
    const selSlot = ref(-1)
    const canvasRef = ref(null)

    const shows = computed(() => store.st.shows || [])
    const engine = computed(() => store.st.showState || {})
    const running = computed(() => !!engine.value.running)
    const canPlay = computed(() => store.st.selectedCount > 0 && !store.st.estop && !running.value)

    // 可选动作：与舞蹈编排同一套过滤（跳过高危动作 —— 演出是连着播的）
    const actionChoices = computed(() =>
      (store.st.actions || []).filter((a) => a.group !== 1 && !a.risky).sort((a, b) => a.group - b.group)
    )
    const groupedChoices = computed(() => {
      const out = []
      for (const a of actionChoices.value) {
        const name = GROUPS[a.group] || '其他'
        let g = out.find((x) => x.name === name)
        if (!g) out.push((g = { name, items: [] }))
        g.items.push(a)
      }
      return out
    })
    const labelOf = (key) => {
      const a = (store.st.actions || []).find((x) => x.key === key)
      return a ? a.label : key
    }

    // 参与演出的狗（用 deviceKey —— 跨网络稳定；IP 会变）
    const robots = computed(() =>
      (store.st.robots || []).map((r) => ({
        key: r.deviceKey || ('ip:' + r.ip),
        ip: r.ip,
        label: r.label || r.ip,
        ready: !!r.ready,
        selected: !!r.selected,
      }))
    )

    // ---- 新建 / 编辑 ---------------------------------------------------
    function newShow() {
      form.value = {
        id: 0,
        name: '新演出',
        desc: '',
        bpm: 120,
        beats: 16,
        cues: [],
        formations: [{ id: 1, name: '初始队形', slots: [], assign: {} }],
      }
      selCue.value = -1
      selSlot.value = -1
      editing.value = true
    }
    function editShow(s) {
      form.value = JSON.parse(JSON.stringify(s))
      form.value.formations = form.value.formations || []
      if (!form.value.formations.length)
        form.value.formations = [{ id: 1, name: '初始队形', slots: [], assign: {} }]
      form.value.cues = form.value.cues || []
      selCue.value = -1
      selSlot.value = -1
      editing.value = true
    }
    function remove(s) {
      if (!confirm(`删除演出「${s.name}」？`)) return
      post({ cmd: 'show_del', id: s.id }).then(refresh)
    }

    // ---- 时间轴 --------------------------------------------------------
    const curFormation = computed(() => (form.value && form.value.formations[0]) || null)

    function addCue(kind, beat) {
      const c = {
        beat: beat ?? 0,
        kind: kind,
        action: kind === KIND.ACTION ? 'Hello' : '',
        light: '',
        roles: [],
        move: { forward: 0, left: 0, turn: 0, speed: 0.35, turnRate: 90 },
      }
      form.value.cues.push(c)
      selCue.value = form.value.cues.length - 1
    }
    function removeCue(i) {
      form.value.cues.splice(i, 1)
      if (selCue.value >= form.value.cues.length) selCue.value = form.value.cues.length - 1
    }
    function cueAt(beat) {
      return form.value.cues.map((c, i) => ({ c, i })).filter((x) => x.c.beat === beat)
    }
    // 点击拍格 → 在那一拍加一条动作 cue（已经有的就选中它）
    function onBeatClick(beat) {
      if (!form.value) return
      const hit = cueAt(beat)
      if (hit.length) {
        selCue.value = hit[0].i
        return
      }
      addCue(KIND.ACTION, beat)
    }

    // ---- 编队 ----------------------------------------------------------
    function addSlot() {
      const f = curFormation.value
      if (!f) return
      const id = 's' + (f.slots.length + 1) + '_' + Date.now().toString(36).slice(-4)
      f.slots.push({ id, name: '位 ' + (f.slots.length + 1), x: 0, y: 0, yaw: 0 })
      selSlot.value = f.slots.length - 1
    }
    function removeSlot(i) {
      const f = curFormation.value
      if (!f) return
      const id = f.slots[i].id
      f.slots.splice(i, 1)
      // 指派也要跟着删 —— 否则留下指向已删位的死指派
      for (const k of Object.keys(f.assign)) if (f.assign[k] === id) delete f.assign[k]
      selSlot.value = -1
    }
    function assignTo(slotId, robotKey) {
      const f = curFormation.value
      if (!f) return
      // 一台狗只能站一个位：先把别处对它的指派清掉
      for (const k of Object.keys(f.assign)) if (f.assign[k] !== slotId) delete f.assign[k]
      if (robotKey) f.assign[robotKey] = slotId
      else {
        for (const k of Object.keys(f.assign)) if (f.assign[k] === slotId) delete f.assign[k]
      }
    }
    function robotOfSlot(slotId) {
      const f = curFormation.value
      if (!f) return ''
      for (const k of Object.keys(f.assign)) if (f.assign[k] === slotId) return k
      return ''
    }

    // ---- 保存 ----------------------------------------------------------
    function save() {
      if (!form.value.name) { store.err = '演出必须有名字'; return }
      if (!form.value.cues.length) { store.err = '时间轴是空的'; return }
      post({
        cmd: 'show_save',
        show: {
          id: form.value.id,
          name: form.value.name,
          desc: form.value.desc,
          bpm: Number(form.value.bpm) || 120,
          beats: Number(form.value.beats) || 16,
          cues: form.value.cues,
          formations: form.value.formations,
        },
      }).then((r) => {
        store.err = ''
        editing.value = false
        refresh()
      })
    }

    function play(s) { cmd({ cmd: 'show_play', id: s.id }) }
    function stop() { cmd({ cmd: 'show_stop' }) }

    // ---- 舞台图 --------------------------------------------------------
    // 俯视图：x 向右、y 向前（北）。机器人朝向按 yaw 逆时针为正。
    let ro = null
    function drawStage() {
      const cv = canvasRef.value
      if (!cv || !form.value) return
      const dpr = window.devicePixelRatio || 1
      const w = cv.clientWidth
      const h = cv.clientHeight
      if (cv.width !== w * dpr || cv.height !== h * dpr) {
        cv.width = w * dpr
        cv.height = h * dpr
      }
      const g = cv.getContext('2d')
      g.setTransform(dpr, 0, 0, dpr, 0, 0)
      g.clearRect(0, 0, w, h)

      const cx = w / 2
      const cy = h / 2
      const scale = Math.min(w, h) / (STAGE_SPAN * 2) * 0.92
      // 舞台坐标 → 屏幕：x 右、y 上（屏幕 y 向下，所以取负）
      const px = (x) => cx + x * scale
      const py = (y) => cy - y * scale

      // 背景网格（每米一格）
      g.strokeStyle = 'rgba(255,255,255,.07)'
      g.lineWidth = 1
      for (let m = -STAGE_SPAN; m <= STAGE_SPAN; m++) {
        g.beginPath(); g.moveTo(px(m), py(-STAGE_SPAN)); g.lineTo(px(m), py(STAGE_SPAN)); g.stroke()
        g.beginPath(); g.moveTo(px(-STAGE_SPAN), py(m)); g.lineTo(px(STAGE_SPAN), py(m)); g.stroke()
      }
      // 中心十字
      g.strokeStyle = 'rgba(255,255,255,.16)'
      g.beginPath(); g.moveTo(cx - 8, cy); g.lineTo(cx + 8, cy); g.stroke()
      g.beginPath(); g.moveTo(cx, cy - 8); g.lineTo(cx, cy + 8); g.stroke()

      const f = curFormation.value
      if (!f) return
      f.slots.forEach((s, i) => {
        const x = px(s.x)
        const y = py(s.y)
        const selected = i === selSlot.value
        // 机器人朝向箭头（yaw 逆时针为正 → 屏幕上是逆时针）
        const a = (-s.yaw * Math.PI) / 180
        g.strokeStyle = selected ? '#7fd1ff' : 'rgba(255,255,255,.5)'
        g.lineWidth = selected ? 2.5 : 1.5
        g.beginPath()
        g.moveTo(x, y)
        g.lineTo(x + Math.sin(a) * 22, y - Math.cos(a) * 22)
        g.stroke()
        // 箭头
        const hx = x + Math.sin(a) * 22
        const hy = y - Math.cos(a) * 22
        g.beginPath()
        g.moveTo(hx, hy)
        g.lineTo(hx - Math.sin(a - 0.5) * 7, hy + Math.cos(a - 0.5) * 7)
        g.lineTo(hx - Math.sin(a + 0.5) * 7, hy + Math.cos(a + 0.5) * 7)
        g.closePath()
        g.fillStyle = selected ? '#7fd1ff' : 'rgba(255,255,255,.6)'
        g.fill()
        // 位点
        g.beginPath()
        g.arc(x, y, selected ? 7 : 5, 0, Math.PI * 2)
        g.fillStyle = selected ? '#7fd1ff' : 'rgba(255,255,255,.75)'
        g.fill()
        // 标签：位号 + 站在这的狗
        const who = robotOfSlot(s.id)
        const whoLabel = who ? (robots.value.find((r) => r.key === who) || {}).label || who : ''
        g.fillStyle = 'rgba(255,255,255,.72)'
        g.font = '11px system-ui, sans-serif'
        g.textAlign = 'center'
        g.fillText(s.name + (whoLabel ? ' · ' + whoLabel : ''), x, y + 20)
      })
    }

    let raf = null
    function loop() { drawStage(); raf = requestAnimationFrame(loop) }
    onMounted(() => { loop() })
    onUnmounted(() => { if (raf) cancelAnimationFrame(raf) })
    watch(() => form.value, () => drawStage(), { deep: true })
    watch(selSlot, () => drawStage())

    // 拍格 → 秒
    const beatSec = (b) => (b * 60) / (Number(form.value?.bpm) || 120)

    return {
      store, ICON, shows, engine, running, canPlay, editing, form, tab, selCue, selSlot,
      groupedChoices, robots, curFormation, canvasRef,
      KIND, KIND_NAME,
      labelOf, newShow, editShow, remove, addCue, removeCue, cueAt, onBeatClick,
      addSlot, removeSlot, assignTo, robotOfSlot, save, play, stop, beatSec,
      trackBy: (o, i) => i,
    }
  },
  template: `
  <section class="page show-page">
    <!-- 工具条 -->
    <div class="card pad show-bar">
      <button class="btn-primary" @click="newShow()"><span class="ic">{{ ICON.pencil }}</span>新建演出</button>
      <button v-if="running" class="btn-danger" @click="stop()">
        <span class="ic">{{ ICON.estop }}</span>停止演出
      </button>
      <span v-else-if="!canPlay" class="sub warn">
        未选择受控设备{{ store.st.estop ? ' / 急停锁定中' : '' }}，无法开演
      </span>

      <!-- 同步度量：「同步」必须可测，否则只是宣传语 -->
      <span v-if="engine.cuesTotal" class="chip" :class="{on: running}" title="同一拍内各台狗指令发出的最大错位">
        <span class="ic">{{ ICON.pulse }}</span>
        同步 {{ (engine.maxSkewMs || 0).toFixed(0) }} ms
        <template v-if="engine.lateCues"> · 迟到 {{ engine.lateCues }}</template>
      </span>
      <span class="sub">共 {{ shows.length }} 场演出</span>
    </div>

    <!-- 编辑器 -->
    <div v-if="editing && form" class="card pad show-edit">
      <p class="title">编辑演出</p>
      <div class="row-btns">
        <label class="fld">名称 <input v-model="form.name" maxlength="24" /></label>
        <label class="fld">说明 <input v-model="form.desc" maxlength="48" /></label>
        <label class="fld">BPM <input type="number" min="20" max="300" v-model.number="form.bpm" /></label>
        <label class="fld">总拍数 <input type="number" min="1" max="512" v-model.number="form.beats" /></label>
      </div>

      <div class="show-tabs">
        <button :class="{sel: tab === 'cue'}" @click="tab = 'cue'">时间轴</button>
        <button :class="{sel: tab === 'formation'}" @click="tab = 'formation'">队形</button>
      </div>

      <!-- 时间轴 -->
      <div v-if="tab === 'cue'" class="show-timeline">
        <div class="tl-grid">
          <button v-for="b in form.beats" :key="b" class="tl-beat"
                  :class="{has: cueAt(b - 1).length, sel: selCue >= 0 && form.cues[selCue] && form.cues[selCue].beat === b - 1}"
                  @click="onBeatClick(b - 1)"
                  :title="'第 ' + b + ' 拍 · ' + beatSec(b - 1).toFixed(1) + ' 秒'">
            <span class="n">{{ b }}</span>
          </button>
        </div>

        <div v-if="selCue >= 0 && form.cues[selCue]" class="cue-edit">
          <div class="cue-head">
            <span class="chip">第 {{ form.cues[selCue].beat }} 拍 · {{ beatSec(form.cues[selCue].beat).toFixed(1) }} 秒</span>
            <select v-model.number="form.cues[selCue].kind">
              <option :value="0">动作</option>
              <option :value="1">位移</option>
              <option :value="2">灯光</option>
            </select>
            <select v-model.number="form.cues[selCue].beat">
              <option v-for="b in form.beats" :key="b" :value="b - 1">第 {{ b }} 拍</option>
            </select>
            <button class="mini danger" @click="removeCue(selCue)">✕</button>
          </div>

          <!-- 动作 -->
          <div v-if="form.cues[selCue].kind === 0" class="cue-body">
            <select v-model="form.cues[selCue].action">
              <option v-for="a in actionChoices" :key="a.key" :value="a.key">{{ a.label }}</option>
            </select>
          </div>

          <!-- 位移（开环：前进 / 左移 / 原地转） -->
          <div v-else-if="form.cues[selCue].kind === 1" class="cue-body">
            <label class="fld">前进 m <input type="number" step="0.1" v-model.number="form.cues[selCue].move.forward" /></label>
            <label class="fld">左移 m <input type="number" step="0.1" v-model.number="form.cues[selCue].move.left" /></label>
            <label class="fld">左转 ° <input type="number" step="5" v-model.number="form.cues[selCue].move.turn" /></label>
            <label class="fld">速度 m/s <input type="number" step="0.05" v-model.number="form.cues[selCue].move.speed" /></label>
          </div>

          <!-- 灯光 -->
          <div v-else class="cue-body">
            <label class="fld">灯号 <input v-model="form.cues[selCue].light" placeholder="如 warm / spot-3" /></label>
          </div>

          <!-- 角色 -->
          <div class="cue-roles">
            <span class="sub">参与角色（不选 = 全体）</span>
            <label v-for="r in robots" :key="r.key" class="role-chip">
              <input type="checkbox" :value="r.key" v-model="form.cues[selCue].roles" /> {{ r.label }}
            </label>
          </div>
        </div>
      </div>

      <!-- 队形 -->
      <div v-else class="show-formation">
        <div class="stage-wrap">
          <canvas ref="canvasRef" class="stage-canvas"></canvas>
          <div class="sub stage-hint">俯视图 · 1 格 = 1 米 · 箭头是狗的朝向</div>
        </div>
        <div class="slot-list">
          <div class="row-btns">
            <button class="btn-mini" @click="addSlot()">＋ 加一个位</button>
          </div>
          <div v-for="(s, i) in (curFormation ? curFormation.slots : [])" :key="s.id" class="slot-row"
               :class="{sel: selSlot === i}" @click="selSlot = i">
            <span class="nm">{{ s.name }}</span>
            <label class="fld">x <input type="number" step="0.1" v-model.number="s.x" /></label>
            <label class="fld">y <input type="number" step="0.1" v-model.number="s.y" /></label>
            <label class="fld">朝向° <input type="number" step="15" v-model.number="s.yaw" /></label>
            <select :value="robotOfSlot(s.id)" @change="assignTo(s.id, $event.target.value)">
              <option value="">（空位）</option>
              <option v-for="r in robots" :key="r.key" :value="r.key">{{ r.label }}</option>
            </select>
            <button class="mini danger" @click="removeSlot(i)">✕</button>
          </div>
          <div v-if="!curFormation || !curFormation.slots.length" class="sub">
            还没有舞台位 —— 点「＋ 加一个位」摆出队形，再在上面指派机器狗。
          </div>
        </div>
      </div>

      <div class="row-btns right">
        <button class="btn-mini" @click="editing = false">取消</button>
        <button class="btn-primary" @click="save()">保存</button>
      </div>
    </div>

    <!-- 列表 -->
    <div v-for="s in shows" :key="s.id" class="card pad show-item">
      <div class="show-info">
        <div class="nm">{{ s.name }}</div>
        <div class="sub">{{ s.bpm }} BPM · {{ s.beats }} 拍 · {{ s.count || (s.cues || []).length }} 条 cue</div>
        <div v-if="(s.problems || []).length" class="sub warn">
          {{ s.problems.length }} 个问题：{{ s.problems[0] }}
        </div>
      </div>
      <div class="show-btns">
        <button class="btn-mini" :disabled="!canPlay" @click="play(s)">
          <span class="ic">{{ ICON.pulse }}</span>开演
        </button>
        <button class="btn-mini" @click="editShow(s)">编辑</button>
        <button class="btn-mini danger" @click="remove(s)">删除</button>
      </div>
    </div>
    <div v-if="!shows.length && !editing" class="card pad empty">
      还没有演出 —— 点「新建演出」排一条多机编队的时间轴。
    </div>
  </section>
  `,
}
