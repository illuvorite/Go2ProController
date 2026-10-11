// 舞蹈编排页：把一串动作按顺序播放（参考实现「天树探界遥控」的「舞蹈编排」页）。
//
// 参考实现把编排存在**云端**（按企业 ID 区分），我们没有那套服务，
// 所以改成存本地 `robot_dances.json` —— 数据模型（名称/说明/一串{动作,延时}）与
// 播放语义都一致，只是"存在哪"不同：离线可用、不依赖账号。
//
// 播放是后端的一个**后台脚本**：按顺序下发、按每步的延时等待，
// 期间可以急停（dance_stop：中止脚本 + 停车）。
import { computed, ref } from 'vue'
import { store, cmd, post, refresh } from '../store.js'
import { iconForAction, ICON } from '../icons.js'

// 可选动作：直接用「动作执行」页那张表（服务端给的 actions），按组显示中文名
const GROUPS = ['基础姿态', '表演动作', '跳跃特技', '其他 / 进阶']

export default {
  name: 'DancePage',
  setup() {
    const editing = ref(false)      // 编辑面板是否打开
    const form = ref({ id: 0, name: '', desc: '', steps: [] })
    const pickerFor = ref(-1)       // 正在给第几步选动作（-1 = 没在选）

    // 播放/编辑/删除都作用于受控设备；没有受控设备时播放会被后端拒绝
    const dances = computed(() => store.st.dances || [])
    const playing = computed(() => !!store.st.scriptRunning)
    const canPlay = computed(() => store.st.selectedCount > 0 && !store.st.estop)

    // 可选择的标准动作（跳过 Gait 组；那组是参数/步态，不适合编排）
    const actionChoices = computed(() =>
      (store.st.actions || []).filter((a) => a.group !== 1).sort((a, b) => a.group - b.group)
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
      if (key === 'wait') return '等待'
      const a = (store.st.actions || []).find((x) => x.key === key)
      return a ? a.label : key
    }
    const iconOf = (key) =>
      key === 'wait' ? ICON.pulse : iconForAction({ key, label: labelOf(key) })

    function newDance() {
      form.value = { id: 0, name: '新编排', desc: '', steps: [{ key: 'StandUp', delay: 2500 }] }
      editing.value = true
    }
    function editDance(d) {
      form.value = {
        id: d.id,
        name: d.name,
        desc: d.desc || '',
        steps: (d.steps || []).map((s) => ({ key: s.key, delay: s.delay })),
      }
      editing.value = true
    }
    function addStep(key) {
      // 延时用后端给的默认值（与参考实现的 durationMap 同口径）
      const def = key === 'wait' ? 1000 : defaultDelay(key)
      form.value.steps.push({ key, delay: def })
      pickerFor.value = -1
    }
    function removeStep(i) { form.value.steps.splice(i, 1) }
    function moveStep(i, d) {
      const j = i + d
      if (j < 0 || j >= form.value.steps.length) return
      const a = form.value.steps
      ;[a[i], a[j]] = [a[j], a[i]]
    }
    function save() {
      if (!form.value.steps.length) { store.err = '编排里至少要有一步'; return }
      post({
        cmd: 'dance_save',
        dance: { id: form.value.id, name: form.value.name, desc: form.value.desc, steps: form.value.steps },
      }).then(refresh)
      editing.value = false
    }
    function remove(d) {
      post({ cmd: 'dance_del', id: d.id }).then(refresh)
    }
    function play(d) { cmd({ cmd: 'dance_play', id: d.id }) }
    function stop() { cmd({ cmd: 'dance_stop' }) }

    // 每步默认延时（与 C++ 端 defaultDanceDelayMs 同口径）—— 仅供新加入的步用
    function defaultDelay(key) {
      const map = { Dance1: 5000, Dance2: 5000, FrontFlip: 6000, BackFlip: 6000, LeftFlip: 6000,
                    RightFlip: 6000, Handstand: 4500, StandUp: 2500, StandDown: 2500, Sit: 2500,
                    BalanceStand: 2500, RecoveryStand: 2500, Stretch: 3000, Hello: 3500 }
      return map[key] || 3500
    }

    return { store, ICON, dances, playing, canPlay, editing, form, pickerFor, groupedChoices,
             labelOf, iconOf, newDance, editDance, addStep, removeStep, moveStep, save, remove,
             play, stop }
  },
  template: `
  <section class="page">
    <div class="card pad dance-bar">
      <button class="btn-primary" @click="newDance()"><span class="ic">{{ ICON.pencil }}</span>新建编排</button>
      <button v-if="playing" class="btn-danger" @click="stop()">
        <span class="ic">{{ ICON.estop }}</span>急停（{{ store.st.scriptName || '播放中' }}）
      </button>
      <span v-else-if="!canPlay" class="sub warn">未选择受控设备{{ store.st.estop ? ' / 急停锁定中' : '' }}，无法播放</span>
      <span v-else class="sub">共 {{ dances.length }} 套编排（存本机 robot_dances.json）</span>
    </div>

    <!-- 编辑面板 -->
    <div v-if="editing" class="card pad dance-edit">
      <p class="title">编辑编排</p>
      <label class="fld">名称 <input v-model="form.name" maxlength="24" /></label>
      <label class="fld">说明 <input v-model="form.desc" maxlength="48" /></label>

      <div class="steps">
        <div v-for="(s, i) in form.steps" :key="i" class="step">
          <span class="idx">{{ i + 1 }}</span>
          <span class="ic step-ico">{{ iconOf(s.key) }}</span>
          <button class="step-name" @click="pickerFor = pickerFor === i ? -1 : i">{{ labelOf(s.key) }}</button>
          <label class="delay">延时 <input type="number" min="0" step="100" v-model.number="s.delay" /> ms</label>
          <button class="mini" @click="moveStep(i, -1)">↑</button>
          <button class="mini" @click="moveStep(i, 1)">↓</button>
          <button class="mini danger" @click="removeStep(i)">✕</button>
        </div>
      </div>

      <div class="row-btns">
        <button class="btn-mini" @click="pickerFor = form.steps.length">＋ 添加动作</button>
        <button class="btn-mini" @click="addStep('wait')">＋ 添加等待</button>
      </div>

      <!-- 选动作：**底部抽屉**，不是页面内联展开。
           内联展开时它夹在编辑卡片中间，平板这种小屏上会被顶栏和上方卡片压住，
           三十多个动作只能露出尾部几块 —— 看着就像"被遮挡"。

           ★ 必须 Teleport 到 body：.card 上有 backdrop-filter: blur(8px)，
             而 backdrop-filter 会给 position:fixed 的后代**建立包含块** ——
             不 Teleport 的话抽屉会相对「卡片」定位、而不是相对视口
             （2026-10-11 实机截图：抽屉卡在卡片中间）。
             ⚠ 这段注释写在 Vue 的模板字符串里，里面**不能用反引号**，
               否则会把模板提前截断（js_syntax_check 会报 Unexpected identifier）。 -->
      <Teleport to="body">
      <div v-if="pickerFor >= 0" class="sheet-mask" @click="pickerFor = -1"></div>
      <div v-if="pickerFor >= 0" class="sheet">
        <div class="sheet-head">
          <span>给第 {{ pickerFor + 1 }} 步选动作</span>
          <button class="btn-mini" @click="pickerFor = -1">关闭</button>
        </div>
        <div class="sheet-body">
          <div class="picker-group">
            <span class="sub">其它</span>
            <div class="picker-tiles">
              <button class="picker-tile" @click="addStep('wait')">
                <span class="ic">{{ iconOf('wait') }}</span>等待
              </button>
            </div>
          </div>
          <div v-for="g in groupedChoices" :key="g.name" class="picker-group">
            <span class="sub">{{ g.name }}</span>
            <div class="picker-tiles">
              <button v-for="a in g.items" :key="a.key" class="picker-tile" @click="addStep(a.key)">
                <span class="ic">{{ iconOf(a.key) }}</span>{{ a.label }}
              </button>
            </div>
          </div>
        </div>
      </div>
      </Teleport>

      <div class="row-btns right">
        <button class="btn-mini" @click="editing = false">取消</button>
        <button class="btn-primary" @click="save()">保存</button>
      </div>
    </div>

    <!-- 列表 -->
    <div v-for="d in dances" :key="d.id" class="card pad dance-item">
      <div class="dance-info">
        <div class="nm">{{ d.name }}</div>
        <div class="sub">{{ d.desc || '无说明' }} · {{ d.count || (d.steps || []).length }} 个动作</div>
      </div>
      <div class="dance-btns">
        <button class="btn-mini" :disabled="!canPlay" @click="play(d)">
          <span class="ic">{{ ICON.pulse }}</span>播放
        </button>
        <button class="btn-mini" @click="editDance(d)">编辑</button>
        <button class="btn-mini danger" @click="remove(d)">删除</button>
      </div>
    </div>
    <div v-if="!dances.length && !editing" class="card pad empty">
      还没有编排 —— 点「新建编排」把一串动作串起来一次播放。
    </div>
  </section>
  `,
}
