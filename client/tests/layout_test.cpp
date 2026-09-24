// ============================================================================
// 断点布局的编译期单元测试
//
// 为什么用 static_assert 而不是运行时断言：
//   makeLayout 是 constexpr 纯函数，所以各机型档位的结果可以在**编译期**钉死。
//   以后谁改了 layout.cpp 里的断点常量、比例或上下限，而忘了同步预期值，编译直接失败 ——
//   比运行时测试更早、更硬。
//
// 注意：这里 #include 的是 .cpp 而不是 .hpp —— constexpr 函数必须在当前 TU 内可见定义
// 才能参与常量求值，只包含头文件拿不到函数体。
// 因此这个目标**不要**再链 go2_ui，否则符号重复。
// ============================================================================

#include "../ui/layout.cpp"

#include <cstdio>

namespace {

/// 浮点近似比较（避免依赖编译器的最后一位差异）
constexpr bool near(float a, float b, float eps = 0.06f) {
    return (a > b ? a - b : b - a) <= eps;
}

// ---- 各平台的典型安全区 ----
constexpr go2::SafeArea kAndroidSafe{0.0f, 24.0f, 20.0f, 20.0f};  // 底部手势条 + 左右返回手势
constexpr go2::SafeArea kDesktopSafe{0.0f, 0.0f, 0.0f, 0.0f};

// ============================================================ 1. 手机竖屏 390×844
// 改造前：左栏写死 452dp > 屏宽 390dp → 整栏溢出屏幕，竖屏完全不可用
// 改造后：C 档 → 单栏 + 底部页签 + 悬浮双摇杆
constexpr go2::LayoutSpec kPhonePortrait =
    go2::makeLayout(390.0f, 844.0f, true, kAndroidSafe, nullptr);

static_assert(kPhonePortrait.mode == go2::LayoutMode::SingleColumn, "手机竖屏应走单栏");
static_assert(kPhonePortrait.widthClass == go2::WidthClass::Compact, "350dp 可用宽应属 C 档");
static_assert(kPhonePortrait.heightClass == go2::HeightClass::Tall, "820dp 可用高应属 T 档");
static_assert(kPhonePortrait.nav == go2::NavKind::BottomTab, "竖屏有纵向空间，用底部页签");
static_assert(kPhonePortrait.topRows == 1, "顶栏压成 1 行");
static_assert(near(kPhonePortrait.topBarH, 56.0f), "顶栏 56dp");
static_assert(kPhonePortrait.leftW == 0.0f, "左栏降级为抽屉，不占宽度");
static_assert(near(kPhonePortrait.ctrlW, 350.0f), "遥控栏占满可用宽");
static_assert(!kPhonePortrait.logInline && kPhonePortrait.logDrawer, "日志改为独立一页/浮层");
static_assert(!kPhonePortrait.joyInline, "单栏模式摇杆必须悬浮");
static_assert(near(kPhonePortrait.joyRadius, 70.0f), "摇杆半径 = min(350,820)*0.20");
static_assert(near(kPhonePortrait.estopH, 88.0f), "急停竖屏给 88dp（安全项要够大）");
static_assert(near(kPhonePortrait.btnH, 52.0f), "触摸按钮最小高 52dp ≥ 48dp 下限");
static_assert(near(kPhonePortrait.fontTitle, 19.0f), "手机字号 19/16/13");
static_assert(kPhonePortrait.actCols == 1, "手机抽屉太窄 → 动作库单列");
// 摇杆与内容预留高度必须同源：摇杆顶边之上才是可用内容区
static_assert(near(kPhonePortrait.joyInsetX, 102.5f), "左内缩 = radius*0.95 + 安全区左 + 16");
static_assert(near(kPhonePortrait.joyCenterY, 670.0f), "圆心高 = 844-24-70-24-56（让开页签）");
static_assert(near(kPhonePortrait.joyReserve, 244.0f), "内容预留 = 摇杆直径 + 上下留白 + 页签");
// 摇杆整体必须落在屏幕内，且不与底部页签重叠
static_assert(kPhonePortrait.joyCenterY - kPhonePortrait.joyRadius >
                  kPhonePortrait.topBarH,
              "摇杆顶部应在顶栏之下");

// ============================================================ 2. 手机横屏 844×390
// 纵向只有约 366dp 可用：摇杆占底部 ~150dp，剩下的才给急停/阻尼
constexpr go2::LayoutSpec kPhoneLandscape =
    go2::makeLayout(844.0f, 390.0f, true, kAndroidSafe, nullptr);

static_assert(kPhoneLandscape.mode == go2::LayoutMode::SingleColumn, "M 宽 × S 高 → 单栏");
static_assert(kPhoneLandscape.widthClass == go2::WidthClass::Medium, "804dp 属 M 档");
static_assert(kPhoneLandscape.heightClass == go2::HeightClass::Short, "366dp 属 S 档");
static_assert(kPhoneLandscape.nav == go2::NavKind::Drawer, "矮屏用侧滑抽屉，把纵向空间全留给遥控");
static_assert(near(kPhoneLandscape.topBarH, 52.0f), "矮屏顶栏 52dp");
static_assert(near(kPhoneLandscape.estopH, 72.0f), "矮屏急停 72dp");
static_assert(near(kPhoneLandscape.joyRadius, 73.2f), "摇杆半径 = min(804,366)*0.20");
static_assert(near(kPhoneLandscape.joyReserve, 194.4f), "内容预留高度");
static_assert(!kPhoneLandscape.paramInline, "矮屏参数收进折叠区");
static_assert(near(kPhoneLandscape.drawerW, 420.0f), "抽屉宽 = min(420, 804*0.62)");
// 摇杆顶边必须低于「顶栏 + 急停」，否则会压住急停按钮
static_assert(kPhoneLandscape.joyCenterY - kPhoneLandscape.joyRadius >
                  kPhoneLandscape.topBarH + kPhoneLandscape.estopH,
              "★ 摇杆不得压住急停按钮（改造前矮屏就会压住快捷按钮）");

// ============================================================ 3. 平板竖屏 768×1024
// 同样是 600–899dp 宽，但纵向够高 → 走双栏（这就是二维断点的价值）
constexpr go2::LayoutSpec kTabletPortrait =
    go2::makeLayout(768.0f, 1024.0f, true, kAndroidSafe, nullptr);

static_assert(kTabletPortrait.mode == go2::LayoutMode::TwoColumn, "平板竖屏应走双栏");
static_assert(kTabletPortrait.heightClass == go2::HeightClass::Tall, "1000dp 属 T 档");
static_assert(near(kTabletPortrait.leftW, 291.0f), "左栏 = 728*0.40");
static_assert(near(kTabletPortrait.ctrlW, 421.0f), "右栏 = 728-291-16");
static_assert(kTabletPortrait.logInline, "平板竖屏日志常驻在右栏下方");
static_assert(kTabletPortrait.joyInline, "平板竖屏摇杆内嵌在遥控栏");
static_assert(near(kTabletPortrait.joyRadius, 71.57f), "内嵌摇杆半径 = 421*0.17");
static_assert(kTabletPortrait.actCols == 2, "平板竖屏动作库 2 列");
static_assert(near(kTabletPortrait.fontTitle, 21.0f), "平板字号 21/17/14");

// ============================================================ 4. 平板横屏 1024×768
// 关键：可用宽只有 984dp（扣掉左右各 20dp 安全区），仍必须凑出三栏、日志不折叠
constexpr go2::LayoutSpec kTabletLandscape =
    go2::makeLayout(1024.0f, 768.0f, true, kAndroidSafe, nullptr);

static_assert(kTabletLandscape.mode == go2::LayoutMode::ThreeColumn, "平板横屏应走三栏");
static_assert(kTabletLandscape.widthClass == go2::WidthClass::Expanded, "984dp 属 E 档");
static_assert(kTabletLandscape.topRows == 2, "E 档顶栏压成 2 行");
static_assert(near(kTabletLandscape.topBarH, 92.0f), "顶栏 92dp");
static_assert(near(kTabletLandscape.leftW, 280.0f), "左栏触到 280dp 下限");
static_assert(near(kTabletLandscape.ctrlW, 360.0f), "遥控栏触到 360dp 下限");
static_assert(kTabletLandscape.logInline, "★ 日志必须常驻（不然就白瞎了 1024 的屏宽）");
static_assert(near(kTabletLandscape.logW, 312.0f), "日志栏 312dp");
static_assert(kTabletLandscape.logW >= 280.0f, "日志栏不低于 280dp 下限");
static_assert(kTabletLandscape.actCols == 2, "280dp 左栏仍排得下 2 列动作按钮");

// ============================================================ 5. 桌面 1440×880（改造前的默认窗口）
// 桌面**零回归**：栏宽与改造前写死的 392 / 468 基本一致
constexpr go2::LayoutSpec kDesktop =
    go2::makeLayout(1440.0f, 880.0f, false, kDesktopSafe, nullptr);

static_assert(kDesktop.mode == go2::LayoutMode::ThreeColumn, "桌面三栏");
static_assert(kDesktop.widthClass == go2::WidthClass::Large, "1440dp 属 L 档");
static_assert(kDesktop.heightClass == go2::HeightClass::Tall, "880dp 属 T 档");
static_assert(kDesktop.topRows == 3, "桌面顶栏保持 3 行");
static_assert(near(kDesktop.topBarH, 124.0f), "顶栏 124dp（与改造前一致）");
static_assert(near(kDesktop.leftW, 389.0f), "左栏 389dp（改造前写死 392）");
static_assert(near(kDesktop.ctrlW, 475.0f), "遥控栏 475dp（改造前写死 468）");
static_assert(near(kDesktop.logW, 544.0f), "日志栏 544dp（改造前约 580）");
static_assert(near(kDesktop.actW1, 150.0f), "★ 桌面动作按钮仍是 150dp（改造前写死 150）");
static_assert(kDesktop.actCols == 2, "桌面 2 列");
static_assert(near(kDesktop.styleScale, 1.0f), "鼠标优先：不放大控件间距");
static_assert(kDesktop.btnH == 0.0f, "鼠标优先：沿用主题默认按钮高");

// ============================================================ 6. 大屏平板横屏 1920×1200
// 改造前：代码走"手机那套"，把日志整栏砍掉、摇杆浮到两角（截图实测就是这个状态）
constexpr go2::LayoutSpec kBigScreen =
    go2::makeLayout(1920.0f, 1200.0f, false, kDesktopSafe, nullptr);

static_assert(kBigScreen.mode == go2::LayoutMode::ThreeColumn, "大屏必须三栏");
static_assert(kBigScreen.logInline, "★ 1920dp 宽的大屏日志必须常驻");
static_assert(near(kBigScreen.logW, 948.0f), "日志栏 948dp");
static_assert(near(kBigScreen.leftW, 420.0f), "左栏触到 420dp 上限");
static_assert(kBigScreen.joyInline, "大屏摇杆内嵌，不悬浮");

// ============================================================ 7. 桌面窄窗 900×600（窗口被拖窄）
constexpr go2::LayoutSpec kNarrow =
    go2::makeLayout(900.0f, 600.0f, false, kDesktopSafe, nullptr);

static_assert(kNarrow.mode == go2::LayoutMode::ThreeColumn, "900dp 仍算 E 档");
static_assert(!kNarrow.logInline, "★ 剩余宽度不足 280dp → 日志自动折叠，而不是留一条鸡肋窄栏");
static_assert(kNarrow.logDrawer, "折叠后走浮层");
static_assert(near(kNarrow.leftW + kNarrow.ctrlW, 640.0f), "280 + 360");

// ============================================================ 8. 断点滞回（防止拖窗口时来回抖动）
constexpr go2::LayoutSpec kPrevLarge =
    go2::makeLayout(1300.0f, 800.0f, false, kDesktopSafe, nullptr);
constexpr go2::LayoutSpec kPrevExpanded =
    go2::makeLayout(1100.0f, 800.0f, false, kDesktopSafe, nullptr);

static_assert(kPrevLarge.widthClass == go2::WidthClass::Large, "1300dp 属 L 档");
static_assert(kPrevExpanded.widthClass == go2::WidthClass::Expanded, "1100dp 属 E 档");
// 从 L 档缩到 1275（已低于 1280 边界）：8dp 滞回内保持 L，不立刻降级
static_assert(go2::makeLayout(1275.0f, 800.0f, false, kDesktopSafe, &kPrevLarge).widthClass ==
                  go2::WidthClass::Large,
              "滞回：刚跌破边界不降级");
// 从 E 档涨到 1285（已超过 1280 边界）：8dp 滞回内保持 E，不立刻升级
static_assert(go2::makeLayout(1285.0f, 800.0f, false, kDesktopSafe, &kPrevExpanded).widthClass ==
                  go2::WidthClass::Expanded,
              "滞回：刚涨过边界不升级");
// 超过滞回窗口就该真的切档
static_assert(go2::makeLayout(1265.0f, 800.0f, false, kDesktopSafe, &kPrevLarge).widthClass ==
                  go2::WidthClass::Expanded,
              "滞回窗口外正常降级");
// 同一尺寸重复计算必须稳定（drawUi 与安卓入口各算一次，结果必须一致）
static_assert(go2::makeLayout(1275.0f, 800.0f, false, kDesktopSafe, &kPrevLarge).leftW ==
                  go2::makeLayout(1275.0f, 800.0f, false, kDesktopSafe, &kPrevLarge).leftW,
              "幂等");

// ============================================================ 9. 触摸修正
// 同一块屏、同一种布局，只是输入方式不同 → 按钮尺寸应该跟着变
constexpr go2::LayoutSpec kTouchTablet =
    go2::makeLayout(1024.0f, 768.0f, true, kAndroidSafe, nullptr);
constexpr go2::LayoutSpec kMouseTablet =
    go2::makeLayout(1024.0f, 768.0f, false, kDesktopSafe, nullptr);

static_assert(kTouchTablet.btnH >= 44.0f, "触摸：按钮最小高 ≥44dp");
static_assert(kMouseTablet.btnH == 0.0f, "鼠标：回落到主题默认（更紧凑）");
static_assert(kTouchTablet.styleScale > kMouseTablet.styleScale, "触摸：控件间距放大");
static_assert(kTouchTablet.fontTitle == kMouseTablet.fontTitle, "字号只跟断点走，不跟输入方式走");

// ============================================================ 10. 安全区确实起作用
constexpr go2::LayoutSpec kNoSafe = go2::makeLayout(390.0f, 844.0f, true, kDesktopSafe, nullptr);
static_assert(kNoSafe.viewW > kPhonePortrait.viewW, "安全区应从可用宽里扣掉");
static_assert(near(kNoSafe.viewW - kPhonePortrait.viewW, 40.0f), "左右各扣 20dp");
static_assert(near(kNoSafe.viewH - kPhonePortrait.viewH, 24.0f), "底部扣 24dp");

}  // namespace

int main() {
    std::printf(
        "[layout_test] 断点布局编译期断言全部通过\n"
        "  手机竖屏 390x844  → %s / 摇杆半径 %.1f / 急停 %.0fdp / 字号 %.0f\n"
        "  手机横屏 844x390  → %s / 内容预留 %.1fdp\n"
        "  平板竖屏 768x1024 → %s / 左栏 %.0f + 右栏 %.0f\n"
        "  平板横屏 1024x768 → %s / %.0f + %.0f + 日志 %.0f\n"
        "  桌面   1440x880   → %s / %.0f + %.0f + 日志 %.0f\n",
        go2::layoutModeName(kPhonePortrait.mode), kPhonePortrait.joyRadius,
        kPhonePortrait.estopH, kPhonePortrait.fontTitle,
        go2::layoutModeName(kPhoneLandscape.mode), kPhoneLandscape.joyReserve,
        go2::layoutModeName(kTabletPortrait.mode), kTabletPortrait.leftW,
        kTabletPortrait.ctrlW, go2::layoutModeName(kTabletLandscape.mode),
        kTabletLandscape.leftW, kTabletLandscape.ctrlW, kTabletLandscape.logW,
        go2::layoutModeName(kDesktop.mode), kDesktop.leftW, kDesktop.ctrlW, kDesktop.logW);
    return 0;
}
