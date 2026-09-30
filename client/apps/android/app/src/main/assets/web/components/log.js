// 运行日志弹窗（自动滚到底部）
import { ref, watch, nextTick, onMounted } from 'vue'
import { store, post } from '../store.js'
import { ICON } from '../icons.js'

export default {
  name: 'LogModal',
  setup() {
    const box = ref(null)
    // 打开日志 = 看过了 → 角标立刻清掉（不等下一次 500ms 轮询回来说了算）
    if (store.st) store.st.problemUnread = 0
    post({ cmd: 'logseen' })
    const toBottom = () => nextTick(() => {
      if (box.value) box.value.scrollTop = box.value.scrollHeight
    })
    onMounted(toBottom)
    watch(() => (store.st.log || []).length, toBottom)
    return { ICON, store, box, close: () => { store.modal = '' } }
  },
  template: `
  <div class="mask" @click.self="close()">
    <div class="modal">
      <div class="modal-head">
        <span class="ic">{{ ICON.log }}</span>运行日志
        <button class="ghost close" @click="close()">✕</button>
      </div>
      <div class="modal-body">
        <div class="log" ref="box">
          <div v-for="(l, i) in store.st.log" :key="i" :class="{warn: /失败|错误|急停|警告|reject|error/i.test(l)}">{{ l }}</div>
        </div>
      </div>
    </div>
  </div>
  `,
}
