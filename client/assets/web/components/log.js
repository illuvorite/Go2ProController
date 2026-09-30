// 运行日志弹窗：自动滚底 / 只看异常 / 清空（与 ImGui 端 drawLogPanel 同一功能集）
import { ref, computed, watch, nextTick, onMounted } from 'vue'
import { store, cmd, post } from '../store.js'
import { ICON } from '../icons.js'

export default {
  name: 'LogModal',
  setup() {
    const box = ref(null)
    const errorsOnly = ref(false)  // 只看异常（与 ImGui 的「只看异常」同一判据）
    // 打开日志 = 看过了 → 角标立刻清掉（不等下一次 500ms 轮询回来说了算）
    if (store.st) store.st.problemUnread = 0
    post({ cmd: 'logseen' })

    const toBottom = () => nextTick(() => {
      if (box.value) box.value.scrollTop = box.value.scrollHeight
    })
    onMounted(toBottom)
    watch([() => (store.st.log || []).length, errorsOnly], toBottom)

    const lines = computed(() => {
      const all = store.st.log || []
      return errorsOnly.value
        ? all.filter((l) => /失败|错误|急停|警告|reject|error|超时/i.test(l))
        : all
    })
    const clear = () => cmd({ cmd: 'clearlog' })

    return { ICON, store, box, lines, errorsOnly, clear, close: () => { store.modal = '' } }
  },
  template: `
  <div class="mask" @click.self="close()">
    <div class="modal">
      <div class="modal-head">
        <span class="ic">{{ ICON.log }}</span>运行日志
        <button class="ghost close" @click="close()"><span class="ic">{{ ICON.x }}</span></button>
      </div>
      <div class="modal-body">
        <div class="log-tools">
          <label class="log-opt"><input type="checkbox" v-model="errorsOnly" /> 只看异常</label>
          <button class="ghost sm" @click="clear()">清空</button>
        </div>
        <div class="log" ref="box">
          <div v-for="(l, i) in lines" :key="i"
               :class="{warn: /失败|错误|急停|警告|reject|error/i.test(l)}">{{ l }}</div>
          <div v-if="!lines.length" class="empty">{{ errorsOnly ? '没有异常日志' : '日志为空' }}</div>
        </div>
      </div>
    </div>
  </div>
  `,
}
