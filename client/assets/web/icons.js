// 由 tools/icons/gen_icons.py 生成 —— 不要手改（改图标请改生成脚本再跑一次）。
//
// 两套图标，各司其职：
//   ICON.*     Phosphor Icons (MIT) —— **界面框架**（设备/设置/急停/方向键…）
//   ACTION_ICON 宇树「天树探界遥控」那套**人形动作剪影**（站立/坐下/拜年/翻滚…）
// 动作图标走 TianshuGo2 字体：人形剪影能直接读出在做什么动作，
// 而抽象符号（天平=平衡、床=趴下）读不出来 —— 这正是旧图标廉价感的根源。
//
// Phosphor Regular/Bold 码点完全一致，字重靠 CSS 的 .ic / .ic-b 切换。
// 字体：fonts/Phosphor.ttf、fonts/Phosphor-Bold.ttf、fonts/TianshuGo2.ttf

export const ICON = {
  arrowUp: '\uE08E',  // arrow-up
  arrowDown: '\uE03E',  // arrow-down
  arrowLeft: '\uE058',  // arrow-left
  arrowRight: '\uE06C',  // arrow-right
  caretLeft: '\uE138',  // caret-left
  caretDown: '\uE136',  // caret-down
  caretRight: '\uE13A',  // caret-right
  caretUp: '\uE13C',  // caret-up
  x: '\uE4F6',  // x
  check: '\uE182',  // check
  pencil: '\uE3B4',  // pencil-simple
  robot: '\uE762',  // robot
  dog: '\uE74A',  // dog
  paw: '\uE648',  // paw-print
  user: '\uE4C2',  // user
  users: '\uE68E',  // users-three
  gear: '\uE272',  // gear-six
  log: '\uE47E',  // terminal
  list: '\uE464',  // squares-four
  estop: '\uE9B8',  // siren
  stop: '\uE46C',  // stop
  lock: '\uE308',  // lock-simple
  lockOpen: '\uE30A',  // lock-simple-open
  joystick: '\uEA5E',  // joystick
  gamepad: '\uE26E',  // game-controller
  eye: '\uE220',  // eye
  eyeOff: '\uE224',  // eye-slash
  key: '\uE2D6',  // key
  refresh: '\uE094',  // arrows-clockwise
  fullscreen: '\uE0A6',  // arrows-out-simple
  dots: '\uE1FE',  // dots-three
  search: '\uE30C',  // magnifying-glass
  thermometer: '\uE5C6',  // thermometer
  wifi: '\uE4EA',  // wifi-high
  battery: '\uE0C0',  // battery-full
  charging: '\uE0BA',  // battery-charging
  link: '\uE2E2',  // link
  monitor: '\uE32E',  // monitor
  crosshair: '\uE1D6',  // crosshair
  warning: '\uE4E2',  // warning-circle
  wrench: '\uE5D4',  // wrench
  firstAid: '\uE56E',  // first-aid
  caretUpDown: '\uE140',  // caret-up-down
  scales: '\uE750',  // scales
  bed: '\uE0CC',  // bed
  chair: '\uE012',  // armchair
  chairSimple: '\uE950',  // chair
  person: '\uE3A8',  // person
  personSimple: '\uE72E',  // person-simple
  arrowUUpLeft: '\uE08A',  // arrow-u-up-left
  handPalm: '\uE57E',  // hand-palm
  arrowsIn: '\uE532',  // arrows-in-line-vertical
  compass: '\uE1C8',  // compass
  shuffle: '\uE422',  // shuffle
  sliders: '\uE434',  // sliders-horizontal
  arrowsOutLineV: '\uE536',  // arrows-out-line-vertical
  gauge: '\uE628',  // gauge
  speedometer: '\uEE74',  // speedometer
  infinity: '\uE634',  // infinity
  coins: '\uE78E',  // coins
  footprints: '\uEA88',  // footprints
  ruler: '\uE6B8',  // ruler
  arrowFatUp: '\uE52E',  // arrow-fat-up
  pulse: '\uE000',  // pulse
  path: '\uE39C',  // path
  wave: '\uE580',  // hand-waving
  stretch: '\uECFE',  // person-arms-spread
  smiley: '\uE436',  // smiley
  heartStraight: '\uE2AA',  // heart-straight
  musicNote: '\uE33C',  // music-note
  music: '\uE340',  // music-notes
  camera: '\uE10E',  // camera
  pray: '\uECC8',  // hands-praying
  waveSine: '\uE6DE',  // waves
  heartHand: '\uE810',  // hand-heart
  boot: '\uECCA',  // boot
  stairs: '\uE8EC',  // stairs
  sneaker: '\uED60',  // sneaker-move
  star: '\uE46A',  // star
  flag: '\uE244',  // flag
  walk: '\uE73A',  // person-simple-walk
  run: '\uE730',  // person-simple-run
  crown: '\uE614',  // crown
  arrowsLR: '\uE0A0',  // arrows-left-right
  arrowsOut: '\uE0A4',  // arrows-out-cardinal
  jump: '\uEAC2',  // rabbit
  throw: '\uE732',  // person-simple-throw
  arrowClockwise: '\uE036',  // arrow-clockwise
  bendDoubleUpLeft: '\uE03A',  // arrow-bend-double-up-left
  bendDoubleUpRight: '\uE03C',  // arrow-bend-double-up-right
  arrowCounter: '\uE038',  // arrow-counter-clockwise
  taiChi: '\uED5C',  // person-simple-tai-chi
  lightning: '\uE2DE',  // lightning
  fire: '\uE242',  // fire
  wind: '\uE5D2',  // wind
  shield: '\uE40C',  // shield-check
}

// 动作 → 图标：**与 C++ 端 ui_actions.cpp::iconGlyph() 同一张表**//（由本脚本从 ACTION_ICON 生成，改一边会自动同步另一边）
// 值形如 '\uE137' = 天树剪影（TianshuGo2 字体）；'\uE648' = Phosphor（.ic 类）
export const ACTION_ICON = {
  Damp: '\uE124',  // 阻尼：软腿瘫倒
  BalanceStand: '\uE135',  // 平衡站立：半蹲找平衡
  StopMove: '\uE57E',  // 停止移动：举手示意停（Phosphor 兜底：hand-palm）
  StandUp: '\uE137',  // 站立
  StandDown: '\uE10C',  // 趴下：卧倒剪影
  RecoveryStand: '\uE08A',  // 恢复站立：起身箭头（Phosphor 兜底：arrow-u-up-left）
  Sit: '\uE117',  // 坐下：坐姿剪影
  RiseSit: '\uE133',  // 起立(坐姿)：坐姿剪影
  Euler: '\uE12F',  // 姿态角：姿态剪影
  SwitchGait: '\uE139',  // 切换步态：换步
  BodyHeight: '\uE120',  // 机身高度：抬机身
  FootRaiseHeight: '\uE11C',  // 抬腿高度：A 字抬腿
  SpeedLevel: '\uE131',  // 速度档位：奔跑速度
  ContinuousGait: '\uE12A',  // 持续步态：持续移动
  EconomicGait: '\uE11F',  // 经济步态：省电（续航）
  StaticWalk: '\uE128',  // 静态行走：行走剪影
  TrotRun: '\uE12B',  // 小跑：奔跑剪影
  SwitchJoystick: '\uEA5E',  // 手柄接管：手柄（无对应剪影）（Phosphor 兜底：joystick）
  Trigger: '\uE1D6',  // 扳机：准星（无对应剪影）（Phosphor 兜底：crosshair）
  Hello: '\uE109',  // 打招呼：高挥手
  Stretch: '\uE118',  // 伸懒腰：伸展剪影
  Content: '\uE107',  // 满意：开心脸
  Wallow: '\uE10A',  // 撒娇打滚：拥抱（翻滚着黏人）
  Dance1: '\uE104',  // 舞蹈 1
  Dance2: '\uE105',  // 舞蹈 2
  Pose: '\uE11E',  // 摆姿势：展臂亮相
  Scrape: '\uE110',  // 拜年(作揖)：新年作揖
  WiggleHips: '\uE11A',  // 扭屁股：转身摇摆
  FingerHeart: '\uE10E',  // 比心：双手比心
  MoonWalk: '\uE13B',  // 太空步：滑步后仰
  OnesidedStep: '\uE134',  // 单边踏步：侧踏剪影
  CrossStep: '\uE123',  // 交叉步：交叉步剪影
  StandOut: '\uE138',  // 站立展示：站立激活
  LeadFollow: '\uE11D',  // 领航跟随：全地形跟随
  FreeWalk: '\uE127',  // 自由行走：自由行走
  FrontJump: '\uE10B',  // 前跳：向前跳
  FrontPounce: '\uE111',  // 前扑：向前扑
  FrontFlip: '\uE113',  // 前空翻：向前翻滚
  LeftFlip: '\uE119',  // 左空翻：侧身翻身
  RightFlip: '\uE122',  // 右空翻：出招式翻身
  BackFlip: '\uE12C',  // 后空翻：躺地翻起
  Handstand: '\uE129',  // 倒立：手倒立
  Bound: '\uE132',  // 跳跃奔跑：并腿跑
  FreeJump: '\uE136',  // 自由跳跃：蹲身起跳
  GetBodyHeight: '\uE6B8',  // 查机身高度：量尺寸（Phosphor 兜底：ruler）
  GetFootRaiseHeight: '\uE52E',  // 查抬腿高度：向上抬（Phosphor 兜底：arrow-fat-up）
  GetSpeedLevel: '\uEE74',  // 查速度档位：速度表（Phosphor 兜底：speedometer）
  GetState: '\uE000',  // 查运动状态：脉搏（Phosphor 兜底：pulse）
  GetAutoRecovery: '\uE56E',  // 查自动恢复：急救（Phosphor 兜底：first-aid）
  TrajectoryFollow: '\uE39C',  // 轨迹跟随：路径（Phosphor 兜底：path）
  CrossWalk: '\uE121',  // 横向行走：阶梯式横移
  Standup: '\uE13C',  // 起立(兼容)：从瘫软起身
  ClassicWalk: '\uE13B',  // 经典步态：行走
  BackStand: '\uE130',  // 后仰站立：准备姿势
  SetAutoRecovery: '\uE5D4',  // 设自动恢复：扳手（Phosphor 兜底：wrench）
  FreeAvoid: '\uE40C',  // 自由避障：盾牌（Phosphor 兜底：shield-check）
  SwitchAvoidMode: '\uE4E2',  // 避障模式：注意（Phosphor 兜底：warning-circle）
}

// 兜底：新指令还没进表时按中文关键词猜（与 C++ 端兜底顺序一致）
export function iconForAction(a) {
  if (ACTION_ICON[a.key]) return ACTION_ICON[a.key]
  const l = a.label || ''
  if (l.includes('空翻') || l.includes('翻')) return ICON.arrowCounter
  if (l.includes('跳')) return ICON.jump
  if (l.includes('扑')) return ICON.throw
  if (l.includes('舞')) return ICON.music
  if (l.includes('走') || l.includes('步')) return ICON.walk
  if (l.includes('查') || l.includes('状态')) return ICON.search
  return ICON.paw
}
