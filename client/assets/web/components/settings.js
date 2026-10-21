// 设置弹窗：指令集 / 隐私模式 / 钥匙库（数量只读）/ 从官方 App 导入 / 从机器狗找钥匙
import { ref, watch } from 'vue'
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
      // 手动/探测候选可以绑定到探测用的 IP；官方导入的不知道对应哪台 → 不绑
      cmd({ cmd: 'keyapply', key: k, ip: probeIp.value.trim() })
      if (k === manualKey.value) manualKey.value = ''
    }
    function adopt(k) { cmd({ cmd: 'keyapply', key: k }) }
    function applyManual() { if (manualKey.value.trim()) apply(manualKey.value.trim()) }
    function importKeys() { post({ cmd: 'keyimport' }) }

    const scan = () => store.st.keyScan || {}
    const official = () => store.st.officialKeys || {}
    const officialHits = () => official().hits || []
    const cloud = () => store.st.cloud || {}

    // 云账号：密码只在本次请求里发给本机后端（后端也不落盘）
    const cloudAcc = ref('')
    const cloudPwd = ref('')
    const cnRegion = ref(true)
    function cloudLogin() {
      // 账号密码留空 = 用后端保存的那份（登录成功后自动保存）
      post({ cmd: 'cloudimport', account: cloudAcc.value.trim(), password: cloudPwd.value,
             regionCn: cnRegion.value })
      cloudPwd.value = ''
    }
    function cloudClear() { post({ cmd: 'cloudclear' }) }

    // 自动填入：后端扫描到钥匙后给一次 autoFill，只填一次（用户清空后不反复填）
    let autoFilled = false
    watch(() => official().autoFill, (v) => {
      if (!autoFilled && v) { manualKey.value = v; autoFilled = true }
    }, { immediate: true })

    return {
      ICON, store, param, toggle, probe, apply, applyManual, probeIp, manualKey, scan,
      adopt, importKeys, official, officialHits,
      cloud, cloudAcc, cloudPwd, cnRegion, cloudLogin, cloudClear,
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

        <!-- 云账号：一次登录拉全部钥匙（官方 App 的 key 本来就来自云端） -->
        <div class="opt static">
          <span class="opt-main">
            <span class="nm"><span class="ic">{{ ICON.key }}</span>云账号拉钥匙</span>
            <span class="ip">登录宇树账号，一次拿账号下所有设备的 AES key。登录成功后记住账号，
              以后启动自动拉取，不用再填</span>
          </span>
        </div>
        <div v-if="cloud().saved" class="keyscan-row">
          <span class="mono" style="flex:1">已保存账号 {{ cloud().savedAccount }} · 启动自动登录</span>
          <button class="ghost sm" @click="cloudClear()">清除</button>
        </div>
        <div class="keyscan-row">
          <input v-model="cloudAcc" :placeholder="cloud().saved ? '换账号（可留空）' : '手机号 / 邮箱'"
                 autocomplete="username" />
        </div>
        <div class="keyscan-row">
          <input v-model="cloudPwd" type="password"
                 :placeholder="cloud().saved ? '密码（留空 = 用已保存的）' : '密码'"
                 autocomplete="current-password" @keyup.enter="cloudLogin()" />
          <button class="sm" :disabled="cloud().busy || (!cloudAcc.trim() && !cloud().saved) || (!cloudPwd && !cloud().saved)"
                  @click="cloudLogin()">
            <span class="ic" :class="{spin: cloud().busy}">{{ ICON.refresh }}</span>
            {{ cloud().busy ? '登录中…' : (cloud().saved ? '重新拉取' : '登录并拉取') }}
          </button>
        </div>
        <label class="opt" @click="cnRegion = !cnRegion">
          <span class="opt-main">
            <span class="nm">国内账号</span>
            <span class="ip">国内注册的账号走 robot-api；海外账号关掉（手机号账号一般开着）</span>
          </span>
          <span class="switch" :class="{on: cnRegion}"><i></i></span>
        </label>
        <div v-if="cloud().note" class="keyscan-note">{{ cloud().note }}</div>

        <!-- 从官方 App 导入：官方 App 连过新狗后，钥匙就在本机 -->
        <div class="opt static">
          <span class="opt-main">
            <span class="nm"><span class="ic">{{ ICON.key }}</span>从官方 App 导入</span>
            <span class="ip">官方 App 连过新狗后，钥匙就保存在本机。找到后自动填入下面的输入框并保存</span>
          </span>
        </div>
        <div class="keyscan-row">
          <input :value="official().note || '尚未扫描'" disabled />
          <button class="sm" :disabled="official().scanning" @click="importKeys()">
            <span class="ic" :class="{spin: official().scanning}">{{ ICON.refresh }}</span>
            {{ official().scanning ? '扫描中…' : '立即扫描' }}
          </button>
        </div>
        <div v-for="h in officialHits()" :key="h.key" class="keyscan-cand">
          <span class="mono">{{ h.masked }}<template v-if="h.sn"> · {{ h.sn }}</template></span>
          <button class="ghost sm" @click="adopt(h.key)">采用</button>
        </div>

        <!-- 从机器狗内网找钥匙（不连电脑）：扫端口 + Web 服务抓 32 位 hex -->
        <div class="opt static">
          <span class="opt-main">
            <span class="nm"><span class="ic">{{ ICON.search }}</span>从机器狗找钥匙</span>
            <span class="ip">扫狗的 Web 端口找候选。多数固件只暴露加密握手、不含明文 key，
              所以不一定扫得到。SSH / ADB / NFS 这几条路仍需要电脑（App 里没有那些工具）。</span>
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
