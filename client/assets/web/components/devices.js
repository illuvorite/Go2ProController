// 设备弹窗：改名 / 单控 / 连接 / 删除 / 手动添加 + 群控·取消受控
import { ref } from 'vue'
import { store, cmd, maskIp } from '../store.js'
import { ICON } from '../icons.js'

export default {
  name: 'DevicesModal',
  setup() {
    const newIp = ref('')
    const renaming = ref('')
    const newName = ref('')

    function doRename(ip) {
      cmd({ cmd: 'rename', ip, name: newName.value })
      renaming.value = ''
      newName.value = ''
    }
    function addIp() {
      if (!newIp.value.trim()) return
      cmd({ cmd: 'add', ip: newIp.value.trim() })
      newIp.value = ''
    }
    return {
      ICON, store, newIp, renaming, newName, doRename, addIp, maskIp,
      close: () => { store.modal = '' },
      one: (ip) => cmd({ cmd: 'select', mode: 'one', ip }),
      all: () => cmd({ cmd: 'select', mode: 'all' }),
      none: () => cmd({ cmd: 'select', mode: 'none' }),
      connect: (ip) => cmd({ cmd: 'connect', ip }),
      connectAll: () => cmd({ cmd: 'connectall' }),
      disconnectAll: () => cmd({ cmd: 'disconnectall' }),
      remove: (ip) => cmd({ cmd: 'remove', ip }),
      scan: () => cmd({ cmd: 'scan' }),
      batteryText: (v) => (v >= 0 ? Math.round(v) + '%' : '—'),
    }
  },
  template: `
  <div class="mask" @click.self="close()">
    <div class="modal">
      <div class="modal-head">
        <span class="ic">{{ ICON.robot }}</span>设备（{{ store.st.robots.length }}）
        <button class="ghost close" @click="close()">✕</button>
      </div>

      <div class="modal-body">
        <div class="scan-row">
          <button class="ghost sm" :disabled="store.st.scanning" @click="scan()">
            <span class="ic" :class="{spin: store.st.scanning}">{{ ICON.refresh }}</span>
            {{ store.st.scanning ? '扫描中…（几秒）' : '扫描局域网' }}
          </button>
          <span class="sub">探测 9991/8081 信令 + SN 多播，发现后自动加入并连接</span>
        </div>
        <div v-for="r in store.st.robots" :key="r.ip" class="dev-card" :class="{sel: r.selected}">
          <span class="dot" :class="{ready: r.ready}"></span>
          <div class="dev-main">
            <div v-if="renaming === r.ip">
              <input v-model="newName" :placeholder="r.ip" @keyup.enter="doRename(r.ip)"
                     @keyup.esc="renaming = ''" />
            </div>
            <div v-else>
              <div class="nm">{{ maskIp(r.label) }}</div>
              <div class="ip">{{ r.state }}<span v-if="r.battery >= 0"> · {{ batteryText(r.battery) }} · 模式 {{ r.mode }}</span></div>
            </div>
          </div>
          <div class="dev-btns">
            <button class="ghost sm" @click="one(r.ip)">单控</button>
            <button class="ghost sm" v-if="!r.ready" @click="connect(r.ip)">连接</button>
            <button class="ghost sm" @click="renaming === r.ip ? doRename(r.ip) : (renaming = r.ip, newName = r.label)">
              {{ renaming === r.ip ? '保存' : '改名' }}
            </button>
            <button class="ghost sm danger-text" @click="remove(r.ip)">删除</button>
          </div>
        </div>
        <div v-if="!store.st.robots.length" class="empty">
          还没有设备 —— 在下面手动添加 IP（或到桌面端点「扫描局域网」）
        </div>
      </div>

      <div class="modal-foot">
        <div class="row">
          <button class="ghost" @click="connectAll()"><span class="ic">{{ ICON.link }}</span>全部连接</button>
          <button class="ghost" @click="disconnectAll()">全部断开</button>
        </div>
        <div class="row">
          <button class="ghost" @click="all()"><span class="ic">{{ ICON.users }}</span>群控（全选）</button>
          <button class="ghost" @click="none()">取消受控</button>
        </div>
        <div class="row">
          <input v-model="newIp" placeholder="手动添加 IP（如 192.168.123.161）" @keyup.enter="addIp()" />
          <button @click="addIp()"><span class="ic">{{ ICON.link }}</span>添加并连接</button>
        </div>
      </div>
    </div>
  </div>
  `,
}
