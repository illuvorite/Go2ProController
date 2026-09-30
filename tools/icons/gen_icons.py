#!/usr/bin/env python3
"""生成 client/ui/icons.hpp（图标字形常量）并把图标字体放进 client/assets/fonts/。

图标集：**Phosphor Icons (Regular)** —— MIT，https://phosphoricons.com
细线圆润，与宇树官方 App 的风格同类（官方素材在加密的 Web bundle 里取不到，见 docs 里的记录）。

用法（WSL，仓库根目录下）：
    python3 tools/icons/gen_icons.py

换/加图标：改下面的 TABLE（左 = C++ 常量名，中 = Phosphor 图标名，右 = 中文注释），再跑一次。
图标名可以在这份脚本旁边的 phosphor.css 里 `grep '\.ph-'` 查，或去官网搜。
"""
import os
import re
import shutil
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))          # tools/icons → 仓库根
CLIENT = os.path.join(ROOT, "client")
CSS = os.path.join(HERE, "phosphor.css")
FONT_SRC = os.path.join(HERE, "Phosphor.ttf")
FONT_DST = os.path.join(CLIENT, "assets", "fonts", "Phosphor.ttf")
HPP = os.path.join(CLIENT, "ui", "icons.hpp")
CDN = ("https://cdn.jsdelivr.net/npm/@phosphor-icons/web@2.1.1/src/regular/"
       "Phosphor.ttf")

# (C++ 常量名, Phosphor 图标名, 注释)
TABLE = [
    ("CaretLeft",   "caret-left",             "返回 / 左转"),
    ("CaretDown",   "caret-down",             "后退（向下）"),
    ("CaretRight",  "caret-right",            "右转"),
    ("X",           "x",                      "关闭"),
    ("Pencil",      "pencil-simple",          "编辑"),
    ("Robot",       "robot",                  "设备 / 机器狗"),
    ("Dog",         "dog",                    "机器狗"),
    ("Paw",         "paw-print",              "脚印 / 狗爪"),
    ("User",        "user",                   "单控"),
    ("Users",       "users-three",            "群控"),
    ("Gear",        "gear-six",               "设置"),
    ("Log",         "list-dashes",            "日志"),
    ("List",        "list",                   "列表"),
    ("Estop",       "octagon",                "急停"),
    ("Siren",       "siren",                  "急停（备用）"),
    ("Joystick",    "joystick",               "遥控"),
    ("Gamepad",     "game-controller",        "手柄"),
    ("TaiChi",      "person-simple-tai-chi",  "倒立 / 摆姿势"),
    ("Eye",         "eye",                    "隐私 / 可见"),
    ("EyeOff",      "eye-slash",              "隐藏 IP"),
    ("Key",         "key",                    "钥匙库"),
    ("Refresh",     "arrows-clockwise",       "刷新 / 重扫"),
    ("Fullscreen",  "arrows-out-simple",      "全屏"),
    ("Dots",        "dots-three",             "更多"),
    ("Search",      "magnifying-glass",       "查询 / 状态"),
    ("Thermometer", "thermometer",            "温度"),
    ("Wifi",        "wifi-high",              "网络"),
    ("Battery",     "battery-full",           "电量 / 续航"),
    ("Charging",    "battery-charging",       "充电"),
    ("Camera",      "camera",                 "相机"),
    ("Video",       "video-camera",           "录像"),
    ("Record",      "record",                 "录制"),
    ("Stop",        "stop",                   "停止"),
    ("Speaker",     "speaker-high",           "音量"),
    ("Lamp",        "lamp",                   "灯光"),
    ("Broadcast",   "broadcast",              "雷达 / 广播"),
    ("Monitor",     "monitor",                "图传"),
    ("Crosshair",   "crosshair",              "定位"),
    ("Flag",        "flag",                   "标记"),
    # ---- 动作 / 模式 ----
    ("Jump",        "rabbit",                 "跳 / 跳跃跑"),
    ("Flip",        "flip-horizontal",        "翻身 / 空翻"),
    ("Throw",       "person-simple-throw",    "扑人"),
    ("HeartHand",   "hand-heart",             "比心"),
    ("Handshake",   "handshake",              "握手"),
    ("Wave",        "hand-waving",            "打招呼"),
    ("Pray",        "hands-praying",          "拜年"),
    ("Clap",        "hands-clapping",         "鼓掌"),
    ("Music",       "music-notes",            "舞蹈"),
    ("Confetti",    "confetti",               "庆祝"),
    ("Boot",        "boot",                   "太空步"),
    ("Sneaker",     "sneaker-move",           "交叉步"),
    ("Footprints",  "footprints",             "并腿跑"),
    ("Chair",       "armchair",               "坐下"),
    ("Stretch",     "person-arms-spread",     "伸懒腰"),
    ("Person",      "person",                 "直立 / 站立"),
    ("PersonSimple", "person-simple",         "常规"),
    ("Walk",        "person-simple-walk",     "行走 / 跟随"),
    ("Run",         "person-simple-run",      "跑步 / 小跑"),
    ("Hike",        "person-simple-hike",     "前跳（备用）"),
    ("Lock",        "lock-simple",            "阻尼 / 锁定"),
    ("LockOpen",    "lock-simple-open",       "解锁"),
    ("Gauge",       "gauge",                  "档位 / 阻尼"),
    ("Sliders",     "sliders-horizontal",     "高度 / 步态"),
    ("ArrowsLR",    "arrows-left-right",      "左右"),
    ("ArrowsOut",   "arrows-out-cardinal",    "全向"),
    ("ArrowsIn",    "arrows-in-line-vertical", "压缩 / 阻尼"),
    ("Lasso",       "lasso",                  "牵引"),
    ("Link",        "link",                   "连接"),
    ("Sparkle",     "sparkle",                "灵动"),
    ("Lightning",   "lightning",              "暴走 / 高能"),
    ("Fire",        "fire",                   "加速"),
    ("Wind",        "wind",                   "风速"),
    ("Shuffle",     "shuffle",                "闪避"),
    ("Crown",       "crown",                  "经典"),
    ("Medal",       "medal",                  "奖章"),
    ("Star",        "star",                   "收藏 / 星标"),
    ("Shield",      "shield-check",           "安全 / 防护"),
    ("Sun",         "sun-horizon",            "日光"),
    ("Stairs",      "stairs",                 "台阶"),
    ("Cube",        "cube",                   "模型"),
    # ---- 第二批（2026-09-28）：动作库要**逐个区分**、见名知意 ----
    ("Scales",       "scales",                 "平衡站立（天平=平衡）"),
    ("Bed",          "bed",                    "趴下（躺下）"),
    ("ChairSimple",  "chair",                  "坐下（椅子）"),
    ("Compass",      "compass",                "姿态角（方向/角度）"),
    ("Infinity",     "infinity",               "持续步态（∞ 持续）"),
    ("Coins",        "coins",                  "经济步态（省钱）"),
    ("Smiley",       "smiley",                 "满意（笑脸）"),
    ("MusicNote",    "music-note",             "舞蹈 1（单音符）"),
    ("WaveSine",     "wave-sine",              "扭屁股（扭动）"),
    ("ArrowClockwise", "arrow-clockwise",      "前空翻（向前滚翻）"),
    ("ArrowCounter", "arrow-counter-clockwise", "后空翻（向后滚翻）"),
    ("BendDoubleUpLeft", "arrow-bend-double-up-left", "左空翻"),
    ("BendDoubleUpRight", "arrow-bend-double-up-right", "右空翻"),
    ("ArrowFatUp",   "arrow-fat-up",           "抬腿高度（向上抬）"),
    ("ArrowUp",      "arrow-up",               "起立（兼容）"),
    ("ArrowUUpLeft", "arrow-u-up-left",        "后仰站立（向后仰）"),
    ("ArrowsOutLineV", "arrows-out-line-vertical", "机身高度（上下撑开）"),
    ("Ruler",        "ruler",                  "量高度（查尺寸）"),
    ("Speedometer",  "speedometer",            "速度档位（速度表）"),
    ("Pulse",        "pulse",                  "运动状态（脉搏）"),
    ("FirstAid",     "first-aid",              "自动恢复（急救）"),
    ("Path",         "path",                   "轨迹跟随（路径）"),
    ("Wrench",       "wrench",                 "设置自动恢复（扳手）"),
    ("WarningCircle", "warning-circle",        "避障模式（注意障碍）"),
    ("Lifebuoy",     "lifebuoy",               "恢复站立（救生圈）"),
    ("HandPalm",     "hand-palm",              "停止移动（手势停）"),
    ("HeartStraight", "heart-straight",         "撒娇（卖萌）"),
]


def ensure_inputs():
    """字体缺了就下（CSS 是码点来源，随仓库一起带）。"""
    if not os.path.exists(CSS):
        raise SystemExit("缺少 %s（图标名 → 码点的来源；可从 npm "
                         "@phosphor-icons/web 的 src/regular/style.css 取）" % CSS)
    if not os.path.exists(FONT_SRC) or os.path.getsize(FONT_SRC) < 10000:
        print("下载字体", CDN)
        urllib.request.urlretrieve(CDN, FONT_SRC)


def main():
    ensure_inputs()
    css = open(CSS, encoding="utf-8").read()
    cp = dict(re.findall(
        r"\.ph-([a-z0-9-]+):before\s*\{\s*content:\s*\"\\([0-9a-f]{4})\"", css, re.I))

    missing = []
    out = [
        "// 由 tools/icons/gen_icons.py 生成 —— 不要手改（改图标请改生成脚本再跑一次）。\n",
        "//\n",
        "// 图标字体：Phosphor Icons (Regular) —— MIT License, https://phosphoricons.com\n",
        "// 字体文件：client/assets/fonts/Phosphor.ttf（原样分发）\n",
        "//\n",
        "// 用法：ImGui::Text(\"%s 设备\", go2::icon::Robot);\n",
        "// 说明：字形以 UTF-8 字节转义写死 —— 不依赖编译器对 \\\\uXXXX 的编码实现。\n",
        "#pragma once\n\n",
        "namespace go2 {\nnamespace icon {\n\n",
    ]
    for cpp, phos, note in TABLE:
        code = cp.get(phos)
        if code is None:
            missing.append(phos)
            continue
        esc = "".join("\\x%02X" % b for b in chr(int(code, 16)).encode("utf-8"))
        out.append('inline constexpr const char* %-13s = "%s";  // %s  (%s U+%s)\n'
                   % (cpp, esc, note, phos, code.upper()))
    out.append("\n}  // namespace icon\n}  // namespace go2\n")

    os.makedirs(os.path.dirname(HPP), exist_ok=True)
    open(HPP, "w", encoding="utf-8", newline="\n").write("".join(out))

    os.makedirs(os.path.dirname(FONT_DST), exist_ok=True)
    shutil.copyfile(FONT_SRC, FONT_DST)

    print("已生成 %s（%d 个图标）" % (HPP, len(TABLE) - len(missing)))
    print("已同步字体 %s (%.0f KB)" % (FONT_DST, os.path.getsize(FONT_DST) / 1024.0))
    if missing:
        print("!! 这些名字在 Phosphor 里不存在，已跳过:", ", ".join(missing))


if __name__ == "__main__":
    main()
