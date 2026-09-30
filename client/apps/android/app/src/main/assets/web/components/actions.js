// 动作库页：常驻整屏页面（不是弹窗），**只有瓷砖网格**
// 组顺序与 C++ 端一致，且同样跳过 SportGroup::Gait（=1，步态/速度/身高那组，用户已要求撤下）
import { computed } from 'vue'
import { store, cmd } from '../store.js'
import { iconForAction } from '../icons.js'

const GROUPS = ['基础姿态', '步态 / 参数', '表演动作', '跳跃特技', '状态查询', '其他 / 进阶']
const SKIP = 1  // Gait

export default {
  name: 'ActionsPage',
  setup() {
    const groups = computed(() => {
      const out = []
      for (const a of store.st.actions || []) {
        if (a.group === SKIP) continue
        const name = GROUPS[a.group] || '其他'
        let g = out.find((x) => x.name === name)
        if (!g) out.push((g = { name, items: [] }))
        g.items.push(a)
      }
      return out
    })
    const isOn = (a) => !!(store.st.toggles && store.st.toggles[a.key])

    return { store, groups, iconForAction, isOn, run: (a) => cmd({ cmd: 'action', key: a.key }) }
  },
  template: `
  <section class="page">
    <div v-for="g in groups" :key="g.name" class="card pad group">
      <p class="title">{{ g.name }} <span class="sub">({{ g.items.length }})</span></p>
      <div class="tiles">
        <button v-for="a in g.items" :key="a.key" class="tile"
                :class="{risky: a.risky, on: isOn(a)}" @click="run(a)">
          <span class="ico">{{ iconForAction(a) }}</span>
          <span class="nm">{{ a.label }}</span>
          <span v-if="a.toggle" class="tag">{{ isOn(a) ? '开' : '关' }}</span>
        </button>
      </div>
    </div>
    <div v-if="!groups.length" class="card pad empty">动作表还没拿到 —— 本机服务连上了吗？</div>
  </section>
  `,
}
