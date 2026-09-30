// Go2 控制台 · 网页界面（Vue 3，免构建：importmap 直接引 vendor/vue）
// 只做两件事：每 500ms 拉 /api/state，点按钮就 POST /api/command。
import { createApp, onMounted, onUnmounted, computed } from 'vue'
import { store, refresh, cmd } from './store.js'
import TopBar from './components/topbar.js'
import RemotePage from './components/remote.js'
import ActionsPage from './components/actions.js'
import JoystickBand from './components/band.js'
import DevicesModal from './components/devices.js'
import SettingsModal from './components/settings.js'
import LogModal from './components/log.js'

createApp({
  components: { TopBar, RemotePage, ActionsPage, JoystickBand, DevicesModal, SettingsModal, LogModal },
  setup() {
    let timer = null
    onMounted(() => {
      refresh()
      timer = setInterval(refresh, 500)
      // 空格 = 急停（与桌面端一致：两个页面都要能按）
      window.addEventListener('keydown', onKey)
    })
    onUnmounted(() => {
      if (timer) clearInterval(timer)
      window.removeEventListener('keydown', onKey)
    })

    function onKey(e) {
      if (e.code !== 'Space' || (e.target && e.target.tagName === 'INPUT')) return
      e.preventDefault()
      cmd({ cmd: 'estop' })
    }

    // 弹窗打开 → 摇杆带被盖住（与 ImGui 端 modalOpen 同语义：不画、不响应、数值清零）
    // 动作库页不放摇杆（用户要求）：那是浏览/触发动作的页面，控制回遥控页做
    const showBand = computed(() => store.page === 'remote' && store.modal === '')
    return { store, showBand }
  },
  template: `
  <div class="app">
    <TopBar />

    <main class="content" :class="{'with-band': showBand}">
      <RemotePage v-if="store.page === 'remote'" />
      <ActionsPage v-else />
    </main>

    <!-- 摇杆带：只在遥控页（弹窗打开时整条撤掉） -->
    <JoystickBand v-if="showBand" />

    <DevicesModal v-if="store.modal === 'devices'" />
    <SettingsModal v-if="store.modal === 'settings'" />
    <LogModal v-if="store.modal === 'log'" />
  </div>
  `,
}).mount('#app')
