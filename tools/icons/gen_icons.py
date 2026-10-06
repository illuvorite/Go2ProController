#!/usr/bin/env python3
"""生成图标相关的**单一来源**产物：

    1. client/ui/icons.hpp          C++ 端字形常量（Phosphor + 天树两套）
    2. client/assets/web/icons.js   网页端码点 + 动作→图标表（与 C++ 同一张语义表）
    3. client/assets/fonts/         图标字体（Phosphor Regular/Bold + TianshuGo2）

图标集分两套，各司其职：

  A. **Phosphor Icons**（MIT，https://phosphoricons.com）—— **界面框架图标**
     设备/设置/日志/急停/摇杆/电池/方向键…… 细线圆润，不跟内容抢视觉。

  B. **TianshuGo2**（自有字体，由 tools/icons/svg2font.py 从 SVG 转出）—— **动作图标**
     宇树官方 App"天树探界遥控"那套**人形动作剪影**（站立/坐下/伸懒腰/拜年/翻滚/倒立…）。
     这套图标的价值在于"见名知形"：一眼能读出在做什么动作，而抽象符号（天平=平衡、
     床=趴下）读不出来。Phosphor 之类开源库里**没有**这类人形动作剪影。
     ⚠ 授权：天树的 SVG 来自第三方 App，属其专有美术资源，适合本地自用 / 内部部署；
       若本项目对外开源分发，须替换为自绘或已授权图标（见 TIANSHU_LICENSE_NOTE）。

★ 关于字重
    Phosphor 的 Regular 与 Bold **码点完全一致**（1530 个图标逐一核对，零错位），
    所以语义只维护一份（下面的 TABLE），切字重只是换一份字体，字形串不变。
    TianshuGo2 只有单字重（剪影本身就是实心块，加粗无意义）。

用法（仓库根目录下）：
    python tools/icons/gen_icons.py

换/加图标：改下面的 TABLE / ACTION_ICON，再跑一次。**不要手改生成出来的 icons.hpp / icons.js。**
图标名可以在这份脚本旁边的 phosphor.css / phosphor-bold.css 里 `grep '\\.ph-'` 查，
或去官网 https://phosphoricons.com 搜。脚本会校验每个名字在两套 CSS 里都存在。
"""
import os
import re
import shutil
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))          # tools/icons → 仓库根
CLIENT = os.path.join(ROOT, "client")
CSS_REG = os.path.join(HERE, "phosphor.css")
CSS_BOLD = os.path.join(HERE, "phosphor-bold.css")
FONT_DIR = os.path.join(CLIENT, "assets", "fonts")
FONT_REG_SRC = os.path.join(HERE, "Phosphor.ttf")
FONT_BOLD_SRC = os.path.join(HERE, "Phosphor-Bold.ttf")
HPP = os.path.join(CLIENT, "ui", "icons.hpp")
JS = os.path.join(CLIENT, "assets", "web", "icons.js")
CDN = "https://cdn.jsdelivr.net/npm/@phosphor-icons/web@2.1.1/src/%s/%s"

# ---------------------------------------------------------------- 天树动作图标
# TianshuGo2.ttf：宇树官方 App「天树探界遥控」那套**人形动作剪影**，
# 由 tools/icons/svg2font.py 从其 APK 里的 SVG 转出（私有码位 U+E100 起）。
#
# 为什么单独一套：动作图标要"见名知形" —— 站立/坐下/伸懒腰/拜年/翻滚/倒立…
# 这些**人形剪影**一眼能读出在做什么，而抽象符号（天平=平衡、床=趴下、相机=摆姿势）
# 读不出来 —— 这正是旧图标"廉价感"的根源。Phosphor 等开源库**没有**这类动作剪影。
#
# ⚠ TIANSHU_LICENSE_NOTE：这些 SVG 来自第三方 App，属其专有美术资源。
#    适合本地自用 / 内部部署；**对外开源分发前必须替换**为自绘或已授权图标。
TIANSHU_TSV = os.path.join(CLIENT, "assets", "fonts", "TianshuGo2.codepoints.tsv")
TIANSHU_TTF = os.path.join(CLIENT, "assets", "fonts", "TianshuGo2.ttf")


def load_tianshu_codepoints():
    """读 svg2font.py 生成的码位对照表。

    返回 **(SVG 文件名 → 码位)**，与 Phosphor 那边 `load_codepoints()` 的方向一致
    （都是"图标名 → 码点"），这样 resolve_action_icon 查起来才对称。
    """
    if not os.path.exists(TIANSHU_TSV):
        return {}
    cp = {}
    for line in open(TIANSHU_TSV, encoding="utf-8"):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        code, name = line.split("\t", 1)
        cp[name] = int(code[2:], 16)
    return cp


def tianshu_codepoint(cp_table, svg_name):
    """按 SVG 文件名取码位，缺字体/缺图标时回退到狗爪。"""
    return cp_table.get("icon_active_paw", cp_table.get(svg_name, 0xE648))


# ---------------------------------------------------------------- 天树名 → SVG 文件名
# ACTION_ICON 里用 `ts_xxx` 引用天树图标，这里做一层**语义名 → SVG 文件名**的解析。
# 天树那 61 个 SVG 分两类：`icon_active_*`（表演动作 28 个）/ `icon_model_*`（模式步态 33 个）。
# 找不到对应剪影的（"停止移动""救生圈"这类非肢体动作）回退到 Phosphor。
TS_PREFIX_MAP = {
    # ---- icon_model_*（模式 / 步态）----
    "ts_damping":     "icon_model_damping",
    "ts_stand":       "icon_model_stand",
    "ts_seating":     "icon_model_seating",
    "ts_pose":        "icon_model_pose",
    "ts_step":        "icon_model_step",
    "ts_preparation": "icon_model_preparation",
    "ts_climb":       "icon_model_climb",
    "ts_run":         "icon_model_run",
    "ts_keepMoving":  "icon_model_keepMoving",
    "ts_batteryLife": "icon_model_batteryLife",
    "ts_g1_walk":     "icon_model_g1_walk",
    "ts_keepRunning": "icon_model_keepRunning",
    "ts_hand_stand":  "icon_model_hand_stand",
    "ts_sideStep":    "icon_model_sideStep",
    "ts_crossStep":   "icon_model_crossStep",
    "ts_standActive": "icon_model_standActive",
    "ts_freeWalk":    "icon_model_freeWalk",
    "ts_walk":        "icon_model_walk",
    "ts_follow":      "icon_model_g1_walk",
    "ts_moonwalk":    "icon_model_walk",
    # ---- icon_active_*（表演动作）----
    "ts_lieDown":            "icon_active_lieDown",
    "ts_sitDown":            "icon_active_sitDown",
    "ts_hightWave":          "icon_active_hightWave",
    "ts_stretch":            "icon_active_stretch",
    "ts_happy":              "icon_active_happy",
    "ts_showHeart":          "icon_active_showHeart",
    "ts_dance1":             "icon_active_dance1",
    "ts_dance2":             "icon_active_dance2",
    "ts_newYear":            "icon_active_newYear",
    "ts_turnWave":           "icon_active_turnWave",
    "ts_makeHeartBothHands": "icon_active_makeHeartBothHands",
    "ts_jumpForward":        "icon_active_jumpForward",
    "ts_pounceForward":      "icon_active_pounceForward",
    "ts_rollOver":           "icon_active_rollOver",
    "ts_turnOver":           "icon_active_turnOver",
    # ★ 这几个之前漏了：天树**有**对应剪影，Go2 动作能对上，不该退回 Phosphor：
    #   ShakeHands（握手）→ shakeHands      Hug/撒娇打滚 → hug（拥抱）
    #   Bound 跳跃奔跑 → runSideBySide（并腿跑）  Handstand 附近 → a2_stand（A 字站立）
    "ts_shakeHands":         "icon_active_shakeHands",
    "ts_hug":                "icon_active_hug",
    "ts_boundRun":           "icon_model_runSideBySide",
    "ts_a2stand":            "icon_model_a2_stand",
    "ts_allTerrain":         "icon_model_b2w_all_terrain",
    "ts_special":            "icon_model_b2w_special",
    "ts_squat":              "icon_model_squat",
    "ts_shuffleStep":        "icon_model_step",
    "ts_squatUp":            "icon_model_squatUp",
    "ts_lieUp":              "icon_model_lieUp",
    "ts_combat":             "icon_model_combat",
    "ts_climbStairs":        "icon_model_climbingStairs",
    "ts_zeroTorque":         "icon_model_zeroTorque",
    # 下面这些天树里**没有**人形剪影（属于"设备状态"而非"肢体动作"）→ 走 Phosphor
    # 下面这些天树里**没有**人形剪影（属于"设备状态"而非"肢体动作"）→ 走 Phosphor
    # ★ StopMove / RecoveryStand 原来用的是 paw-print(狗爪) / lifebuoy(救生圈)，
    #   都名不副实（狗爪看不出"停"，救生圈看不出"站起来"）。已按实际字形换成更贴的：
    #   停止移动 → hand-palm（举手示意停）；恢复站立 → arrow-u-up-left（起身箭头）。
    "ts_paw": None,
    "ts_lifebuoy": None,
    "ts_joystick": None,
    "ts_crosshair": None,
    "ts_ruler": None,
    "ts_arrowFatUp": None,
    "ts_speedometer": None,
    "ts_pulse": None,
    "ts_firstAid": None,
    "ts_path": None,
    "ts_wrench": None,
    "ts_shield": None,
    "ts_warning": None,
}
# ts_xxx = None 时的 Phosphor 兜底
TS_PHOSPHOR_FALLBACK = {
    # 停止移动：原 paw-print(狗爪) 看不出"停" → hand-palm（举手示意停）
    "ts_paw": "hand-palm",
    # 恢复站立：原 lifebuoy(救生圈) 看不出"站起来" → arrow-u-up-left（起身箭头）
    "ts_lifebuoy": "arrow-u-up-left",
    "ts_joystick": "joystick",
    "ts_crosshair": "crosshair",
    "ts_ruler": "ruler",
    "ts_arrowFatUp": "arrow-fat-up",
    "ts_speedometer": "speedometer",
    "ts_pulse": "pulse",
    "ts_firstAid": "first-aid",
    "ts_path": "path",
    "ts_wrench": "wrench",
    "ts_shield": "shield-check",
    "ts_warning": "warning-circle",
}


def resolve_action_icon(ref, tianshu_cp):
    """把 ACTION_ICON 里的 `ts_xxx` 解析成 (来源, 值)。

    返回 ('ts', codepoint) 表示用天树剪影；('ph', phosphor_name) 表示退回 Phosphor。
    """
    if not ref.startswith("ts_"):
        return ("ph", ref)
    svg_name = TS_PREFIX_MAP.get(ref)
    if svg_name and svg_name in tianshu_cp:
        return ("ts", tianshu_cp[svg_name])
    return ("ph", TS_PHOSPHOR_FALLBACK.get(ref, "paw-print"))


# ---------------------------------------------------------------- 语义表
# (C++ 常量名, Phosphor 图标名, 中文注释)
#
# ★ 关于"字重"：**字重不是图标的属性，是渲染上下文的属性**。
#   同一个字形在动作库瓷砖里要加粗（小尺寸才立得住），在顶栏要细线（不跟内容抢注意力）。
#   因为 Regular / Bold 码点完全一致，切字重只是换字体，字形字符串不变 ——
#   所以**不需要**在表里记字重：
#     · 网页端：CSS 的 .ic（Regular）/ .ic-b（Bold）两个 class 切换
#     · C++ 端：动作瓷砖用 drawTileIconAuto()（走加粗字体），界面框架用 ImGui::Text()（常规字体）
#
# 选图原则（这轮重做的重点 —— 原来最大的问题是"字形跟动作含义不沾边"）：
#   1. 同一个动作家族内**互不重样**，四个空翻必须四个不同字形；
#   2. 字面能对上动作本义就别用"抽象代用品"；
#   3. 少用带方向语义的箭头去做无方向的动作，反之亦然。
#
# ★★★ 最重要的一条纪律：**换图标之前必须先渲染出来看一眼**。
#   本脚本能校验的只是"这个名字在 Phosphor 的 CSS 里存在、且两套字重码点一致"，
#   **但名字存在 ≠ 字形好看、≠ 语义对得上**。
#   反面教材（2026-10-06 踩过，已全部回退）：
#     · 前空翻 arrow-clockwise → flip-vertical      （渲染出来像个"Λ"，根本不是空翻）
#     · 后空翻 arrow-counter-clockwise → flip-horizontal（同上）
#     · 左/右空翻 arrow-bend-double-up-* → arrow-circle-down-left/right（像时钟/刷新）
#     · 扭屁股 wave-sine → waves                    （变成"≈"，像数学符号）
#     · 摆姿势 camera → confetti                    （散落的光点，看不出是亮相）
#   验完记得 `python tools/web_preview.py` 打开动作库页**用眼睛确认**再收工。
TABLE = [
    # ---- 界面框架 ----
    # ★ 方向类图标**统一用箭头家族**（arrow-*），不要混用 caret-*（尖括号）。
    #   原来 D-pad 前进用 arrow-up、其余三个用 caret-left/down/right ——
    #   四个并排的按钮里混着两种字形family，粗细和视觉重量都不一样，看着就不成套。
    #   caret-* 只留给"展开/收起"这类真正是尖括号的场合。
    ("ArrowUp",       "arrow-up",               "前进（D-pad 向上）"),
    ("ArrowDown",     "arrow-down",             "后退（D-pad 向下）"),
    ("ArrowLeft",     "arrow-left",             "左转（D-pad 向左）"),
    ("ArrowRight",    "arrow-right",            "右转（D-pad 向右）"),
    ("CaretLeft",     "caret-left",             "返回"),
    ("CaretDown",     "caret-down",             "下移 / 收起"),
    ("CaretRight",    "caret-right",            "向前"),
    ("CaretUp",       "caret-up",               "上移 / 展开"),
    ("X",             "x",                      "关闭"),
    ("Check",         "check",                  "勾选"),
    ("Pencil",        "pencil-simple",          "编辑"),
    ("Robot",         "robot",                  "设备 / 机器狗"),
    ("Dog",           "dog",                    "机器狗"),
    ("Paw",           "paw-print",              "脚印 / 狗爪"),
    ("User",          "user",                   "单控"),
    ("Users",         "users-three",            "群控"),
    ("Gear",          "gear-six",               "设置"),
    # ★ 日志原来用 list-dashes，和"动作库"的 list 都是横线，16px 下几乎分不清。
    #   换 terminal（终端/输出流）—— 与 list 的方块点阵在轮廓上就分得开。
    ("Log",           "terminal",               "日志（终端输出）"),
    # ★ "动作库"图标全站统一用 squares-four（4 宫格 = 一堆动作卡片），
    #   顶栏、更多菜单、动作库页标题**必须一致** —— 以前顶栏用 tai-chi(人形)、
    #   菜单用 list，同一个功能两个图标。
    ("List",          "squares-four",           "动作库（动作卡片网格）"),
    ("Estop",         "siren",                  "急停（警报，最强信号）"),
    ("Stop",          "stop",                   "停止"),
    ("Lock",          "lock-simple",            "锁定 / 阻尼"),
    ("LockOpen",      "lock-simple-open",       "解锁"),
    ("Joystick",      "joystick",               "遥控"),
    ("Gamepad",       "game-controller",        "手柄"),
    ("Eye",           "eye",                    "可见"),
    ("EyeOff",        "eye-slash",              "隐藏 IP"),
    ("Key",           "key",                    "钥匙库"),
    ("Refresh",       "arrows-clockwise",       "刷新 / 重扫"),
    ("Fullscreen",    "arrows-out-simple",      "全屏"),
    ("Dots",          "dots-three",             "更多"),
    ("Search",        "magnifying-glass",       "查询 / 搜索"),
    ("Thermometer",   "thermometer",            "温度"),
    ("Wifi",          "wifi-high",              "网络"),
    ("Battery",       "battery-full",           "电量"),
    ("Charging",      "battery-charging",       "充电"),
    ("Link",          "link",                   "连接"),
    ("Monitor",       "monitor",                "图传"),
    ("Crosshair",     "crosshair",              "定位 / 瞄准"),
    ("Warning",       "warning-circle",         "注意 / 避障"),
    ("Wrench",        "wrench",                 "设置 / 修复"),
    ("FirstAid",      "first-aid",              "急救 / 恢复"),
    ("CaretUpDown",   "caret-up-down",          "上下切换"),

    # ---- 姿态 / 平衡（基础动作）----
    ("Scales",        "scales",                 "平衡站立（天平 = 平衡）"),
    ("Bed",           "bed",                    "趴下（躺下）"),
    ("Chair",         "armchair",               "起立（扶手椅 = 从坐姿起身）"),
    ("ChairSimple",   "chair",                  "坐下（椅子）"),
    ("Person",        "person",                 "站立（人形）"),
    ("PersonSimple",  "person-simple",          "常规姿态"),
    ("ArrowUUpLeft",   "arrow-u-up-left",     "恢复站立（起身箭头）"),
    ("HandPalm",      "hand-palm",              "停止移动（举手示意停）"),
    ("ArrowsIn",      "arrows-in-line-vertical", "阻尼（向内收 / 软腿）"),

    # ---- 步态 / 参数 ----
    ("Compass",       "compass",                "姿态角（罗盘）"),
    ("Shuffle",       "shuffle",                "切换步态"),
    ("Sliders",       "sliders-horizontal",     "机身高度（滑杆）"),
    ("ArrowsOutLineV", "arrows-out-line-vertical", "抬腿高度（上下撑开）"),
    ("Gauge",         "gauge",                  "档位 / 仪表"),
    ("Speedometer",   "speedometer",            "速度档位（速度表）"),
    ("Infinity",      "infinity",               "持续步态（∞ 持续）"),
    ("Coins",         "coins",                  "经济步态（省钱）"),
    ("Footprints",    "footprints",             "静态行走（脚印）"),
    ("Ruler",         "ruler",                  "查机身高度"),
    ("ArrowFatUp",    "arrow-fat-up",           "查抬腿高度（向上抬）"),
    ("Pulse",         "pulse",                 "查运动状态（脉搏）"),
    ("Path",          "path",                   "轨迹跟随（路径）"),

    # ---- 表演动作 ----
    ("Wave",          "hand-waving",            "打招呼（挥手）"),
    ("Stretch",       "person-arms-spread",     "伸懒腰（张开双臂）"),
    ("Smiley",        "smiley",                 "满意（笑脸）"),
    ("HeartStraight", "heart-straight",         "撒娇（卖萌）"),
    ("MusicNote",     "music-note",             "舞蹈 1（单音符）"),
    ("Music",         "music-notes",            "舞蹈 2（双音符）"),
    ("Camera",        "camera",                 "相机 / 摆姿势"),
    ("Pray",          "hands-praying",          "拜年（作揖）"),
    ("WaveSine",      "waves",                  "扭屁股（多波峰 = 摆动的体感；wave-sine 单线太弱看不出在扭）"),
    ("HeartHand",     "hand-heart",             "比心（手比心）"),
    ("Boot",          "boot",                   "太空步（靴子后滑）"),
    ("Stairs",        "stairs",                 "单边踏步（台阶）"),
    ("Sneaker",       "sneaker-move",           "交叉步（交叉的脚）"),
    ("Star",          "star",                   "站立展示（亮相星标）"),
    ("Flag",          "flag",                   "领航跟随（旗）"),
    ("Walk",          "person-simple-walk",     "自由行走"),
    ("Run",           "person-simple-run",      "小跑"),
    ("Crown",         "crown",                  "经典步态"),
    ("ArrowsLR",      "arrows-left-right",      "横向行走（左右）"),
    ("ArrowsOut",     "arrows-out-cardinal",    "自由跳跃（不限定方向）"),

    # ---- 跳跃特技（四个空翻必须四个不同字形）----
    ("Jump",          "rabbit",                 "前跳（蹦跳）"),
    ("Throw",         "person-simple-throw",    "前扑（身体前扑）"),
    # ★ 四个空翻保持原有映射。**不要再改成 flip-* / arrow-circle-***：
    #   实测那两组字形渲染出来很难看（flip-horizontal 像"Λ"，arrow-circle-* 像时钟）。
    #   如果以后要换，务必先渲染出来看一眼再定 —— 光验证名字存在于 CSS 里没有意义。
    ("ArrowClockwise", "arrow-clockwise",       "前空翻（向前滚翻）"),
    ("BendDoubleUpLeft", "arrow-bend-double-up-left", "左空翻（翻向左侧）"),
    ("BendDoubleUpRight", "arrow-bend-double-up-right", "右空翻（翻向右侧）"),
    ("ArrowCounter",  "arrow-counter-clockwise", "后空翻（向后滚翻）"),
    ("TaiChi",        "person-simple-tai-chi",  "倒立（倒立人形）"),
    ("Lightning",     "lightning",              "跳跃奔跑（迅捷）"),
    ("Fire",          "fire",                   "高能 / 爆发"),
    ("Wind",          "wind",                   "风速"),

    # ---- 避障 / 防护 ----
    ("Shield",        "shield-check",           "自由避障（防护）"),
]

# ---------------------------------------------------------------- 动作 → 图标
# 动作 key 必须与 core/sport_library.cpp 的 key 精确一致。
# ★ 动作图标走 **TianshuGo2 人形剪影**（天树探界那套），见 TS_ACTION。
#   只有天树里**没有对应剪影**的动作（参数/状态查询/避障这类"非肢体动作"）
#   才退回 Phosphor —— 那是"设备状态"，用人形反而不合适。
ACTION_ICON = [
    # ---- 基础姿态（全部有人形剪影）----
    ("Damp",             "ts_damping",     "阻尼：软腿瘫倒"),
    ("BalanceStand",     "ts_squat",       "平衡站立：半蹲找平衡"),
    ("StopMove",         "ts_paw",         "停止移动：举手示意停"),
    ("StandUp",          "ts_stand",       "站立"),
    ("StandDown",        "ts_lieDown",     "趴下：卧倒剪影"),
    ("RecoveryStand",    "ts_lifebuoy",    "恢复站立：起身箭头"),
    ("Sit",              "ts_sitDown",     "坐下：坐姿剪影"),
    ("RiseSit",          "ts_seating",     "起立(坐姿)：坐姿剪影"),
    # ---- 步态 / 参数（混合：动作类用剪影，参数类用 Phosphor）----
    ("Euler",            "ts_pose",        "姿态角：姿态剪影"),
    ("SwitchGait",       "ts_shuffleStep", "切换步态：换步"),
    ("BodyHeight",       "ts_climb",       "机身高度：抬机身"),
    ("FootRaiseHeight",  "ts_a2stand",     "抬腿高度：A 字抬腿"),
    ("SpeedLevel",       "ts_run",         "速度档位：奔跑速度"),
    ("ContinuousGait",   "ts_keepMoving",  "持续步态：持续移动"),
    ("EconomicGait",     "ts_batteryLife", "经济步态：省电（续航）"),
    ("StaticWalk",       "ts_g1_walk",     "静态行走：行走剪影"),
    ("TrotRun",          "ts_keepRunning", "小跑：奔跑剪影"),
    ("SwitchJoystick",   "ts_joystick",    "手柄接管：手柄（无对应剪影）"),
    ("Trigger",          "ts_crosshair",   "扳机：准星（无对应剪影）"),
    # ---- 表演动作（全部有人形剪影）----
    ("Hello",            "ts_hightWave",   "打招呼：高挥手"),
    ("Stretch",          "ts_stretch",     "伸懒腰：伸展剪影"),
    ("Content",          "ts_happy",       "满意：开心脸"),
    ("Wallow",           "ts_hug",          "撒娇打滚：拥抱（翻滚着黏人）"),
    ("Dance1",           "ts_dance1",      "舞蹈 1"),
    ("Dance2",           "ts_dance2",      "舞蹈 2"),
    ("Pose",             "ts_special",     "摆姿势：展臂亮相"),
    ("Scrape",           "ts_newYear",     "拜年(作揖)：新年作揖"),
    ("WiggleHips",       "ts_turnWave",    "扭屁股：转身摇摆"),
    ("FingerHeart",      "ts_makeHeartBothHands", "比心：双手比心"),
    ("MoonWalk",         "ts_moonwalk",    "太空步：滑步后仰"),
    ("OnesidedStep",     "ts_sideStep",    "单边踏步：侧踏剪影"),
    ("CrossStep",        "ts_crossStep",   "交叉步：交叉步剪影"),
    ("StandOut",         "ts_standActive", "站立展示：站立激活"),
    ("LeadFollow",       "ts_allTerrain",  "领航跟随：全地形跟随"),
    ("FreeWalk",         "ts_freeWalk",    "自由行走：自由行走"),
    # ---- 跳跃特技 ----
    ("FrontJump",        "ts_jumpForward", "前跳：向前跳"),
    ("FrontPounce",      "ts_pounceForward", "前扑：向前扑"),
    ("FrontFlip",        "ts_rollOver",    "前空翻：向前翻滚"),
    ("LeftFlip",         "ts_turnOver",    "左空翻：侧身翻身"),
    ("RightFlip",        "ts_combat",      "右空翻：出招式翻身"),
    ("BackFlip",         "ts_lieUp",       "后空翻：躺地翻起"),
    ("Handstand",        "ts_hand_stand",  "倒立：手倒立"),
    ("Bound",            "ts_boundRun",    "跳跃奔跑：并腿跑"),
    ("FreeJump",         "ts_squatUp",     "自由跳跃：蹲身起跳"),
    # ---- 状态查询（非肢体，用 Phosphor 设备类图标）----
    ("GetBodyHeight",    "ts_ruler",       "查机身高度：量尺寸"),
    ("GetFootRaiseHeight", "ts_arrowFatUp", "查抬腿高度：向上抬"),
    ("GetSpeedLevel",    "ts_speedometer", "查速度档位：速度表"),
    ("GetState",         "ts_pulse",       "查运动状态：脉搏"),
    ("GetAutoRecovery",  "ts_firstAid",    "查自动恢复：急救"),
    # ---- 其他 / 进阶（混合）----
    ("TrajectoryFollow", "ts_path",        "轨迹跟随：路径"),
    ("CrossWalk",        "ts_climbStairs", "横向行走：阶梯式横移"),
    ("Standup",          "ts_zeroTorque",  "起立(兼容)：从瘫软起身"),
    ("ClassicWalk",      "ts_walk",        "经典步态：行走"),
    ("BackStand",        "ts_preparation", "后仰站立：准备姿势"),
    ("SetAutoRecovery",  "ts_wrench",      "设自动恢复：扳手"),
    ("FreeAvoid",        "ts_shield",      "自由避障：盾牌"),
    ("SwitchAvoidMode",  "ts_warning",     "避障模式：注意"),
]


def load_codepoints(css_path, extra_class):
    """图标名 → 码点。extra_class: '' (regular) / '-bold'。"""
    if not os.path.exists(css_path):
        raise SystemExit("缺少 %s（图标名 → 码点的来源；可从 npm "
                         "@phosphor-icons/web 的 src/regular|src/bold/style.css 取）"
                         % css_path)
    css = open(css_path, encoding="utf-8-sig").read()
    head = (r"\.ph%s\.ph-" % extra_class) if extra_class else r"\.ph-"
    pat = head + r"([a-z0-9-]+):before\s*\{\s*content:\s*\"\\([0-9a-fA-F]{4})\""
    cp = dict(re.findall(pat, css))
    if not cp:
        raise SystemExit("%s 解析不到任何码点 —— CSS 格式变了？" % css_path)
    return cp


def ensure_fonts():
    """字体缺了就下（CSS 是码点来源，随仓库一起带）。"""
    jobs = [
        (FONT_REG_SRC, CDN % ("regular", "Phosphor.ttf"), FONT_REG_SRC),
        (FONT_BOLD_SRC, CDN % ("bold", "Phosphor-Bold.ttf"), FONT_BOLD_SRC),
    ]
    for src, url, name in jobs:
        if not os.path.exists(src) or os.path.getsize(src) < 10000:
            print("下载字体", url)
            urllib.request.urlretrieve(url, src)


def utf8_escape(ch):
    """字形以 UTF-8 字节转义写死 —— 不依赖编译器对 \\uXXXX 的编码实现。"""
    return "".join("\\x%02X" % b for b in ch.encode("utf-8"))


def gen_hpp(entries, tianshu_cp):
    """生成 client/ui/icons.hpp：Phosphor 字形常量 + 天树动作字形 + 两者的对应表。"""
    out = [
        "// 由 tools/icons/gen_icons.py 生成 —— 不要手改（改图标请改生成脚本再跑一次）。\n",
        "//\n",
        "// 两套图标，各司其职：\n",
        "//   Phosphor Icons (MIT, https://phosphoricons.com) —— **界面框架**\n",
        "//     client/assets/fonts/Phosphor.ttf (Regular) / Phosphor-Bold.ttf (Bold)\n",
        "//   TianshuGo2 —— **动作图标**（宇树官方 App「天树探界遥控」的人形动作剪影）\n",
        "//     client/assets/fonts/TianshuGo2.ttf\n",
        "//\n",
        "// ★ 动作图标为什么要单独一套：人形剪影能**一眼读出在做什么动作**\n",
        "//   （站立/坐下/拜年/翻滚/倒立…），而抽象符号（天平=平衡、床=趴下、相机=摆姿势）\n",
        "//   读不出来 —— 那正是旧动作图标\"廉价感\"的根源。Phosphor 之类开源库没有这类剪影。\n",
        "//\n",
        "// ★ Phosphor Regular 与 Bold 的码点完全一致（1530 个逐一核对，零错位），\n",
        "//   所以切字重只是换一份 ImFont，字形字符串不变 —— 表里不必记字重。\n",
        "//   TianshuGo2 只有单字重（剪影本身是实心块，加粗无意义）。\n",
        "//\n",
        "// 用法：\n",
        "//   界面框架：ImGui::Text(\"%s 设备\", go2::icon::Robot);\n",
        "//   动作图标：drawActionIcon(dl, a, c, s, col);  // 见 ui_actions.cpp\n",
        "// 说明：字形以 UTF-8 字节转义写死 —— 不依赖编译器对 \\\\uXXXX 的编码实现。\n",
        "#pragma once\n\n",
        "namespace go2 {\nnamespace icon {\n\n",
    ]
    width = max(len(name) for name, _, _ in entries) + 2
    for name, phos, note in entries:
        code = phos_cp[phos]
        out.append('inline constexpr const char* %-*s = "%s";  // %s  (%s U+%s)\n'
                   % (width, name, utf8_escape(chr(int(code, 16))),
                      note, phos, code.upper()))

    # ---- 天树动作字形（私有码位 U+E100 起，只有一份）----
    out.append("\n// ---- 天树动作图标（TianshuGo2 字体，U+E100 起；无字重概念）----\n")
    out.append("// 码点由 tools/icons/svg2font.py 从 SVG 转字体时写死，顺序即 SVG 文件名排序。\n")
    out.append("namespace ts {\n")
    if tianshu_cp:
        w2 = max(len(k) for k in tianshu_cp) + 2
        for nm in sorted(tianshu_cp):
            cp = tianshu_cp[nm]
            short = nm.replace("icon_active_", "a_").replace("icon_model_", "m_")
            out.append('inline constexpr const char* %-*s = "%s";  // %s  (U+%04X)\n'
                       % (w2, short, utf8_escape(chr(cp)), nm, cp))
    out.append("}  // namespace ts\n")
    out.append("\n}  // namespace icon\n}  // namespace go2\n")
    return "".join(out)


def gen_js(codepoints_reg, codepoints_bold, tianshu_cp):
    """生成 client/assets/web/icons.js：两套码点 + 动作→图标表（与 C++ 同一张语义表）。"""
    name2js = {}
    for name, _, _ in TABLE:
        # 网页端 key 用 camelCase，和既有 ICON.xxx 引用保持一致
        name2js[name] = name[0].lower() + name[1:]

    lines = [
        "// 由 tools/icons/gen_icons.py 生成 —— 不要手改（改图标请改生成脚本再跑一次）。\n",
        "//\n",
        "// 两套图标，各司其职：\n",
        "//   ICON.*     Phosphor Icons (MIT) —— **界面框架**（设备/设置/急停/方向键…）\n",
        "//   ACTION_ICON 宇树「天树探界遥控」那套**人形动作剪影**（站立/坐下/拜年/翻滚…）\n",
        "// 动作图标走 TianshuGo2 字体：人形剪影能直接读出在做什么动作，\n",
        "// 而抽象符号（天平=平衡、床=趴下）读不出来 —— 这正是旧图标廉价感的根源。\n",
        "//\n",
        "// Phosphor Regular/Bold 码点完全一致，字重靠 CSS 的 .ic / .ic-b 切换。\n",
        "// 字体：fonts/Phosphor.ttf、fonts/Phosphor-Bold.ttf、fonts/TianshuGo2.ttf\n\n",
        "export const ICON = {\n",
    ]
    for name, phos, _ in TABLE:
        code = codepoints_reg[phos]
        lines.append("  %s: '\\u%s',  // %s\n" % (name2js[name], code.upper(), phos))
    lines.append("}\n\n")

    # 动作 → 图标：key 精确对应，**不要退回关键词匹配**（会把前跳/跳跃奔跑/自由跳跃撞成同一个）
    lines.append("// 动作 → 图标：**与 C++ 端 ui_actions.cpp::iconGlyph() 同一张表**")
    lines.append("//（由本脚本从 ACTION_ICON 生成，改一边会自动同步另一边）\n")
    lines.append("// 值形如 '\\uE137' = 天树剪影（TianshuGo2 字体）；'\\uE648' = Phosphor（.ic 类）\n")
    lines.append("export const ACTION_ICON = {\n")
    for key, ref, note in ACTION_ICON:
        kind, val = resolve_action_icon(ref, tianshu_cp)
        # ★ 必须写成字面量反斜杠 + u（"\\uXXXX"），**不能**让 Python 把 \u 解释成真字符：
        #   私有区字符（U+E1xx）写进 JS 是不可见字符，diff 看不出、grep 搜不到、
        #   某些编辑器/工具链还会当非法字符丢掉。这里强制输出 6 字符的转义序列。
        esc = ("\\u%04X" % val) if kind == "ts" else ("\\u%s" % codepoints_reg[val].upper())
        if kind == "ts":
            lines.append("  %s: '%s',  // %s\n" % (key, esc, note))
        else:
            lines.append("  %s: '%s',  // %s（Phosphor 兜底：%s）\n"
                         % (key, esc, note, val))
    lines.append("}\n\n")

    lines.append(
        "// 兜底：新指令还没进表时按中文关键词猜（与 C++ 端兜底顺序一致）\n"
        "export function iconForAction(a) {\n"
        "  if (ACTION_ICON[a.key]) return ACTION_ICON[a.key]\n"
        "  const l = a.label || ''\n"
        "  if (l.includes('空翻') || l.includes('翻')) return ICON.arrowCounter\n"
        "  if (l.includes('跳')) return ICON.jump\n"
        "  if (l.includes('扑')) return ICON.throw\n"
        "  if (l.includes('舞')) return ICON.music\n"
        "  if (l.includes('走') || l.includes('步')) return ICON.walk\n"
        "  if (l.includes('查') || l.includes('状态')) return ICON.search\n"
        "  return ICON.paw\n"
        "}\n")
    return "".join(lines)


def main():
    ensure_fonts()
    codepoints_reg = load_codepoints(CSS_REG, "")
    codepoints_bold = load_codepoints(CSS_BOLD, "-bold")
    global phos_cp

    # ---- 校验：每个图标名在两套 CSS 里都必须存在 ----
    missing_reg = [p for _, p, _ in TABLE if p not in codepoints_reg]
    missing_bold = [p for _, p, _ in TABLE if p not in codepoints_bold]
    if missing_reg or missing_bold:
        if missing_reg:
            print("!! 这些名字在 Phosphor Regular 里不存在:", ", ".join(sorted(set(missing_reg))))
        if missing_bold:
            print("!! 这些名字在 Phosphor Bold 里不存在:", ", ".join(sorted(set(missing_bold))))
        raise SystemExit(1)

    # ---- 码点一致性抽查（防止上游改版导致两套字重错位）----
    mismatch = [p for p in codepoints_reg
                if p in codepoints_bold and codepoints_reg[p].lower() != codepoints_bold[p].lower()]
    if mismatch:
        print("!! Regular/Bold 码点不一致（%d 个）: %s" % (len(mismatch), ", ".join(mismatch[:8])))
        raise SystemExit(1)

    # 动作表引用的必须能解析（ts_xxx 要么命中天树、要么有 Phosphor 兜底）
    for k, ref, _ in ACTION_ICON:
        if ref.startswith("ts_") and ref not in TS_PREFIX_MAP:
            print("!! ACTION_ICON.%s 引用了未登记的 %s" % (k, ref))
            raise SystemExit(1)
        if not ref.startswith("ts_"):
            known = {n for n, _, _ in TABLE}
            if ref not in known:
                print("!! ACTION_ICON.%s 引用了不存在的常量 %s" % (k, ref))
                raise SystemExit(1)

    # ★ 同一图标不能被两个动作复用 —— 项目要求"每个按钮的图标别重样"，
    #   两个动作撞图标就等于没换。同一 SVG 只在一个动作里用（toggles 允许重复）。
    dup = {}
    for k, ref, _ in ACTION_ICON:
        dup.setdefault(ref, []).append(k)
    collisions = {r: ks for r, ks in dup.items() if len(ks) > 1}
    if collisions:
        print("!! 这些动作共用同一个图标（必须互不相同）:")
        for r, ks in sorted(collisions.items()):
            print("     %-14s ← %s" % (r, ", ".join(ks)))
        raise SystemExit(1)

    tianshu_cp = load_tianshu_codepoints()
    if not tianshu_cp:
        print("!! 找不到 %s —— 先跑 python tools/icons/svg2font.py 生成天树动作字体" % TIANSHU_TSV)
        raise SystemExit(1)
    # 校验每个 ts_ 引用要么命中天树、要么有 Phosphor 兜底
    unresolved = [ref for _, ref, _ in ACTION_ICON
                  if ref.startswith("ts_") and ref not in TS_PREFIX_MAP]
    if unresolved:
        print("!! ACTION_ICON 引用了未登记的 ts_ 图标:", sorted(set(unresolved)))
        raise SystemExit(1)
    # 兜底图标必须在 Phosphor 里真实存在
    for ref, phos in TS_PHOSPHOR_FALLBACK.items():
        if phos not in codepoints_reg:
            print("!! 兜底 %s -> %s 在 Phosphor 里不存在" % (ref, phos))
            raise SystemExit(1)

    phos_cp = codepoints_reg

    os.makedirs(os.path.dirname(HPP), exist_ok=True)
    open(HPP, "w", encoding="utf-8", newline="\n").write(gen_hpp(TABLE, tianshu_cp))

    open(JS, "w", encoding="utf-8", newline="\n").write(
        gen_js(codepoints_reg, codepoints_bold, tianshu_cp))

    os.makedirs(FONT_DIR, exist_ok=True)
    shutil.copyfile(FONT_REG_SRC, os.path.join(FONT_DIR, "Phosphor.ttf"))
    shutil.copyfile(FONT_BOLD_SRC, os.path.join(FONT_DIR, "Phosphor-Bold.ttf"))

    # 网页端有**独立的一份**字体目录（style.css 的 @font-face 从这里读）。
    # 都要同步 —— 漏了网页端就会静默退回细线/缺字方框（web_check.mjs 会报 FAIL）。
    web_fonts = os.path.join(CLIENT, "assets", "web", "fonts")
    os.makedirs(web_fonts, exist_ok=True)
    shutil.copyfile(FONT_REG_SRC, os.path.join(web_fonts, "Phosphor.ttf"))
    shutil.copyfile(FONT_BOLD_SRC, os.path.join(web_fonts, "Phosphor-Bold.ttf"))
    # 天树动作字体：两端都要（网页 @font-face + C++ 合并进图集）
    if os.path.exists(TIANSHU_TTF):
        shutil.copyfile(TIANSHU_TTF, os.path.join(web_fonts, "TianshuGo2.ttf"))
    else:
        print("!! 找不到 %s，网页端动作图标会变缺字方框" % TIANSHU_TTF)

    n_ts = sum(1 for _, ref, _ in ACTION_ICON
               if resolve_action_icon(ref, tianshu_cp)[0] == "ts")
    print("已生成 %s（%d 个 Phosphor 图标 + %d 个天树动作字形）"
          % (HPP, len(TABLE), len(tianshu_cp)))
    print("已生成 %s（%d 条动作映射：%d 走天树剪影，%d 走 Phosphor 兜底）"
          % (JS, len(ACTION_ICON), n_ts, len(ACTION_ICON) - n_ts))
    print("已同步字体到 %s 与 %s" % (FONT_DIR, web_fonts))


if __name__ == "__main__":
    main()
