// 图标：Phosphor Icons (Regular, MIT) —— 与 C++ 端 client/ui/icons.hpp **同一套字体、同一批码点**。
// 改图标请改 tools/icons/gen_icons.py 再重跑（C++ 与网页两边一起更新）。
// 字体文件：fonts/Phosphor.ttf（由 client/assets/fonts/Phosphor.ttf 复制而来，见 style.css 的 @font-face）

// 界面图标
export const ICON = {
  robot: '\uE762', dog: '\uE74A', paw: '\uE648',
  user: '\uE4C2', users: '\uE68E',
  gear: '\uE272', log: '\uE2F4', list: '\uE2F0', dots: '\uE1FE',
  estop: '\uE38E', stop: '\uE46C', lock: '\uE308',
  joystick: '\uEA5E', gamepad: '\uE26E',
  battery: '\uE0C0', charging: '\uE0BA',
  refresh: '\uE094', search: '\uE30C', key: '\uE2D6', eye: '\uE220', eyeOff: '\uE224',
  link: '\uE2E2', wifi: '\uE4EA', monitor: '\uE32E',
  crosshair: '\uE1D6', flag: '\uE244', sparkle: '\uE6A2',
  wave: '\uE580', handshake: '\uE582', pray: '\uECC8', clap: '\uE6A0',
  music: '\uE340', musicNote: '\uE33C', confetti: '\uE81A',
  jump: '\uEAC2', flip: '\uED6A', throw: '\uE732',
  heartHand: '\uE810', heartStraight: '\uE2AA',
  dog2: '\uE74A', bed: '\uE0CC', chair: '\uE012', chairSimple: '\uE950',
  person: '\uE3A8', personSimple: '\uE72E', walk: '\uE73A', run: '\uE730',
  scales: '\uE750', lifebuoy: '\uE63A', handPalm: '\uE57E',
  boot: '\uECCA', sneaker: '\uED60', footprints: '\uEA88', stairs: '\uE8EC',
  camera: '\uE10E', stretch: '\uECFE', waveSine: '\uEA9A', smiley: '\uE436',
  star: '\uE46A', crown: '\uE614', shield: '\uE40C', lightning: '\uE2DE',
  arrowUp: '\uE08E', arrowFatUp: '\uE52E', arrowClockwise: '\uE036', arrowCounter: '\uE038',
  bendLeft: '\uE03A', bendRight: '\uE03C', arrowUUpLeft: '\uE08A',
  arrowsLR: '\uE0A0', arrowsOut: '\uE0A4', arrowsIn: '\uE532', arrowsOutV: '\uE536',
  compass: '\uE1C8', shuffle: '\uE422', sliders: '\uE434', gauge: '\uE628',
  infinity: '\uE634', coins: '\uE78E', ruler: '\uE6B8', speedometer: '\uEE74',
  pulse: '\uE000', firstAid: '\uE56E', path: '\uE39C', wrench: '\uE5D4',
  warning: '\uE4E2', taiChi: '\uED5C', fire: '\uE242', wind: '\uE5D2',
}

// 动作 → 图标：**与 ui.cpp 的 iconGlyph() 同一张表**（按英文 key 精确对应，别退回关键词匹配）
const ACTION_ICON = {
  Damp: ICON.arrowsIn, BalanceStand: ICON.scales, StopMove: ICON.handPalm,
  StandUp: ICON.person, StandDown: ICON.bed, RecoveryStand: ICON.lifebuoy,
  Sit: ICON.chairSimple, RiseSit: ICON.chair,
  Euler: ICON.compass, SwitchGait: ICON.shuffle, BodyHeight: ICON.sliders,
  FootRaiseHeight: ICON.arrowsOutV, SpeedLevel: ICON.gauge,
  ContinuousGait: ICON.infinity, EconomicGait: ICON.coins, StaticWalk: ICON.footprints,
  TrotRun: ICON.run, SwitchJoystick: ICON.gamepad, Trigger: ICON.crosshair,
  Hello: ICON.wave, Stretch: ICON.stretch, Content: ICON.smiley, Wallow: ICON.heartStraight,
  Dance1: ICON.musicNote, Dance2: ICON.music, Pose: ICON.camera, Scrape: ICON.pray,
  WiggleHips: ICON.waveSine, FingerHeart: ICON.heartHand, MoonWalk: ICON.boot,
  OnesidedStep: ICON.stairs, CrossStep: ICON.sneaker, StandOut: ICON.star,
  LeadFollow: ICON.flag, FreeWalk: ICON.walk,
  FrontJump: ICON.jump, FrontPounce: ICON.throw, FrontFlip: ICON.arrowClockwise,
  LeftFlip: ICON.bendLeft, RightFlip: ICON.bendRight, BackFlip: ICON.arrowCounter,
  Handstand: ICON.taiChi, Bound: ICON.lightning, FreeJump: ICON.arrowsOut,
  GetBodyHeight: ICON.ruler, GetFootRaiseHeight: ICON.arrowFatUp,
  GetSpeedLevel: ICON.speedometer, GetState: ICON.pulse, GetAutoRecovery: ICON.firstAid,
  TrajectoryFollow: ICON.path, Standup: ICON.arrowUp, CrossWalk: ICON.arrowsLR,
  ClassicWalk: ICON.crown, BackStand: ICON.arrowUUpLeft,
  SetAutoRecovery: ICON.wrench, FreeAvoid: ICON.shield, SwitchAvoidMode: ICON.warning,
}

// 兜底：新指令还没进表时按中文关键词猜（与 C++ 端兜底顺序一致）
export function iconForAction(a) {
  if (ACTION_ICON[a.key]) return ACTION_ICON[a.key]
  const l = a.label || ''
  if (l.includes('空翻') || l.includes('翻')) return ICON.arrowClockwise
  if (l.includes('跳')) return ICON.jump
  if (l.includes('扑')) return ICON.throw
  if (l.includes('舞')) return ICON.music
  if (l.includes('走') || l.includes('步')) return ICON.walk
  if (l.includes('查') || l.includes('状态')) return ICON.search
  return ICON.paw
}
