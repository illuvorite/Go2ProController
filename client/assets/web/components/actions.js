// 动作库页：常驻整屏页面（不是弹窗），**只有瓷砖网格**
// 组顺序与 C++ 端一致，且同样跳过 SportGroup::Gait（=1，步态/速度/身高那组，用户已要求撤下）
// 页眉：MCF 指令集开关 + 隐藏不支持的（与 ImGui 端 drawActionPageHeader 同一功能集）
// 瓷砖带可用性标注：上次回执 ✓ / ✗（后端按当前指令集解析 api_id 后回传 ack code）
// 页首另有「快捷动作」组：参考实现（天树探界）里那批"一次点击发一串指令"的按钮
import { computed } from 'vue'
import { store, cmd, post, param, setParam } from '../store.js'
import { iconForAction } from '../icons.js'

const GROUPS = ['基础姿态', '步态 / 参数', '表演动作', '跳跃特技', '状态查询', '其他 / 进阶']
const SKIP = 1  // Gait
const QUICK_GROUPS = ['姿态微调', '机身高度', '高级技能']

export default {
  name: 'ActionsPage',
  setup() {
    function toggleParam(name) {
      const v = !param(name)
      setParam(name, v)
      post({ cmd: 'param', name, value: v })
    }
    const mcf = () => !!param('mcf')
    const hideUnsupported = () => !!param('hideUnsupported')

    const groups = computed(() => {
      const out = []
      for (const a of store.st.actions || []) {
        if (a.group === SKIP) continue
        // ★ 只隐藏"**每台受控机型都被证明不支持**"的动作（服务端给 support === 'none'）。
        //   群控混机型（Air + Pro）时给的是 'partial' → 绝不隐藏：只要还有一台能做，
        //   藏掉它就会让用户以为"这个动作不存在"，而那台其实做得了。
        if (hideUnsupported() && a.support === 'none') continue
        const name = GROUPS[a.group] || '其他'
        let g = out.find((x) => x.name === name)
        if (!g) out.push((g = { name, items: [] }))
        g.items.push(a)
      }
      return out
    })

    // 快捷动作（复合指令）：后端按 kind 给了 group 序号，前端只负责分组显示
    const quickGroups = computed(() => {
      const out = []
      for (const q of store.st.quickActions || []) {
        const name = QUICK_GROUPS[q.group] || q.groupName || '快捷动作'
        let g = out.find((x) => x.name === name)
        if (!g) out.push((g = { name, items: [] }))
        g.items.push(q)
      }
      return out
    })

    // 机型标签：机型未知时不显示、也不做任何过滤；混机型时明说"只标注、不隐藏"
    const ctrlModel = computed(() => store.st.controlModel || '')
    const ctrlModels = computed(() => store.st.controlModels || [])
    const mixedModels = computed(() => ctrlModels.value.length > 1)
    const isOn = (a) => !!(store.st.toggles && store.st.toggles[a.key])
    // 可用性标注：ack=0 全都成功 ✓；ack≠0 有失败 ✗；未试过不标注
    const ackText = (a) => (a.ack === 0 ? '✓' : (a.ack > 0 ? '✗' : ''))
    const ackClass = (a) => (a.ack === 0 ? 'ok' : (a.ack > 0 ? 'bad' : ''))
    // 「哪台不支持」—— 混机型时贴在瓷砖上，比一个孤零零的 ✗ 有用得多
    const badModels = (a) => (a && a.rejectModels ? a.rejectModels.join('、') : '')

    return { store, groups, quickGroups, iconForAction, isOn, mcf, hideUnsupported, toggleParam,
             ackText, ackClass, ctrlModel, ctrlModels, mixedModels, badModels,
             run: (a) => cmd({ cmd: 'action', key: a.key }),
             runQuick: (q) => cmd({ cmd: 'quick', key: q.key }) }
  },
  template: `
  <section class="page">
    <div class="card pad act-header">
      <div class="sub act-who">
        指令发给 {{ store.st.target
        }}<span v-if="ctrlModel"> · Go2 {{ ctrlModel }}</span>
        <span v-else-if="mixedModels"> · Go2 {{ ctrlModels.join(' + ') }}（混机型：只标注不支持，不隐藏）</span>
      </div>
      <label class="act-opt" @click="toggleParam('mcf')">
        <input type="checkbox" class="ck" :checked="mcf()" /> MCF 指令集
        <span class="sub">Go2 Pro 等新固件用这套 api_id；选错会自动用另一套重试</span>
      </label>
      <label class="act-opt" @click="toggleParam('hideUnsupported')">
        <input type="checkbox" class="ck" :checked="hideUnsupported()" /> 隐藏机型不支持的
        <span class="sub">只隐藏"每台受控机型都做不到"的；混机型时保留并标 ✗</span>
      </label>
    </div>

    <!-- 快捷动作（复合指令）：姿态预设 / 平移 / 机身高度 / 方向回正 / 原地踏步 / 恢复移动 -->
    <div v-for="g in quickGroups" :key="'q-' + g.name" class="card pad group">
      <p class="title">{{ g.name }} <span class="sub">({{ g.items.length }})</span></p>
      <div class="tiles">
        <button v-for="q in g.items" :key="q.key" class="tile quick" :title="q.hint"
                :class="{script: q.script}" @click="runQuick(q)">
          <span class="ico">{{ iconForAction({ key: q.key, label: q.label }) }}</span>
          <span class="nm">{{ q.label }}</span>
          <span v-if="q.script" class="tag">脚本</span>
        </button>
      </div>
    </div>

    <div v-for="g in groups" :key="g.name" class="card pad group">
      <p class="title">{{ g.name }} <span class="sub">({{ g.items.length }})</span></p>
      <div class="tiles">
        <button v-for="a in g.items" :key="a.key" class="tile"
                :class="{risky: a.risky, on: isOn(a)}" @click="run(a)">
          <span class="ico">{{ iconForAction(a) }}</span>
          <span class="nm">{{ a.label }}</span>
          <span v-if="a.toggle" class="tag">{{ isOn(a) ? '开' : '关' }}</span>
          <span v-if="ackText(a)" class="tag ack" :class="ackClass(a)">{{ ackText(a) }}</span>
          <span v-if="badModels(a)" class="tag ack bad" :title="badModels(a) + ' 不支持这条'">{{ badModels(a) }} 不支持</span>
        </button>
      </div>
    </div>
    <div v-if="!groups.length && !quickGroups.length" class="card pad empty">动作表还没拿到 —— 本机服务连上了吗？</div>
  </section>
  `,
}
