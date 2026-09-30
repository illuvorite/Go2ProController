// 设置弹窗：指令集 / 隐私模式 / 钥匙库（数量只读）/ 从机器狗找钥匙（不连电脑）
import { ref } from 'vue'
import { store, cmd, post, param, setParam } from '../store.js'
import { ICON } from '../icons.js'

export default {
  name: 'SettingsModal',
  setup() {
    const probeIp = ref('')     // 空 = 后端自动取设备列表第一台
    const manualKey = ref('')   // 手动粘贴 32 位 hex（与 ImGui 设置页同一功能）

    function toggle(name) {
      const v = !param(name)
      setParam(name, v)
      post({ cmd: 'param', name, value: v })
    }
    function probe() { post({ cmd: 'keyprobe', ip: probeIp.value.trim() }) }
    function apply(k) {
      cmd({ cmd: 'keyapply', key: k, ip: probeIp.value.trim() })
      if (k === manualKey.value) manualKey.value = ''
    }
    function applyManual() { if (manualKey.value.trim()) apply(manualKey.value.trim()) }

    const scan = () => store.st.keyScan || {}
    return {
      ICON, store, param, toggle, probe, apply, applyManual, probeIp, manualKey, scan,
      running: () => scan().running === true,
      note: () => scan().note || '',
      candidates: () => scan().candidates || [],
      close: () => { store.modal = '' },
    }
  },
  template: `
  <div class="mask" @click.self="close()">
    <div class="modal narrow">
      <div class="modal-head">
        <span class="ic">{{ ICON.gear }}</span>设置
        <button class="ghost close" @click="close()"><span class="ic">{{ ICON.x }}</span></button>
      </div>
      <div class="modal-body">
        <label class="opt" @click="toggle('mcf')">
          <span class="opt-main">
            <span class="nm">MCF 指令集</span>
            <span class="ip">Go2 Pro / 新固件用这套 api_id；动作被拒（3203）时可以试着切一下</span>
          </span>
          <span class="switch" :class="{on: param('mcf')}"><i></i></span>
        </label>
        <label class="opt" @click="toggle('privacy')">
          <span class="opt-main">
            <span class="nm">隐私模式</span>
            <span class="ip">界面把 IP 等敏感信息打码（截图 / 演示时用）</span>
          </span>
          <span class="switch" :class="{on: param('privacy')}"><i></i></span>
        </label>
        <div class="opt static">
          <span class="opt-main">
            <span class="nm"><span class="ic">{{ ICON.key }}</span>本地钥匙库</span>
            <span class="ip">已加载 {{ store.st.keyCount || 0 }} 把 AES 钥匙（data2=3 新固件要用）</span>
          </span>
        </div>

        <!-- 从机器狗内网找钥匙（不连电脑）：扫端口 + Web 服务抓 32 位 hex -->
        <div class="opt static">
          <span class="opt-main">
            <span class="nm"><span class="ic">{{ ICON.search }}</span>从机器狗找钥匙</span>
            <span class="ip">狗的固件 ≥1.1.15 时，钥匙明文就在狗的内网服务里 —— 扫端口抓出来。
              注意：SSH / ADB / NFS 这几条路仍需要电脑（App 里没有那些工具）。</span>
          </span>
        </div>
        <div class="keyscan-row">
          <input v-model="probeIp" placeholder="狗的 IP（留空 = 设备列表第一台）" />
          <button class="sm" :disabled="running()" @click="probe()">
            <span class="ic" :class="{spin: running()}">{{ ICON.refresh }}</span>
            {{ running() ? '探测中…' : '开始探测' }}
          </button>
        </div>
        <div v-if="note()" class="keyscan-note">{{ note() }}</div>
        <div v-for="k in candidates()" :key="k" class="keyscan-cand">
          <span class="mono">{{ k }}</span>
          <button class="ghost sm" @click="apply(k)">采用</button>
        </div>
        <div class="keyscan-row" style="margin-top:12px">
          <input v-model="manualKey" placeholder="手动粘贴 32 位 hex 钥匙" @keyup.enter="applyManual()" />
          <button class="sm" :disabled="!manualKey.trim()" @click="applyManual()">
            <span class="ic">{{ ICON.key }}</span>保存
          </button>
        </div>
        <p class="tip">
          采用后写入本地 <span class="mono">keys.txt</span> 并绑定到该 IP，下次连接自动生效；
          WebRTC 握手能过（就绪）即为正确钥匙。
        </p>
      </div>
      <div class="modal-foot">
        <div class="row"><button class="ghost" @click="close()">关闭</button></div>
      </div>
    </div>
  </div>
  `,
}
