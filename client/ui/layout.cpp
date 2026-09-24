#include "layout.hpp"

#include <algorithm>
#include <cmath>

namespace go2 {
namespace {

constexpr float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

/// 四舍五入到整数（constexpr 版：std::round 不是 constexpr，
/// 而 makeLayout 需要能在编译期求值，供 tests/layout_test.cpp 的 static_assert 使用）
constexpr float roundi(float v) {
    return static_cast<float>(static_cast<int>(v + (v >= 0.0f ? 0.5f : -0.5f)));
}

// ---- 断点常量（dp）----
constexpr float kWMedium = 600.0f;    // C → M
constexpr float kWExpanded = 900.0f;  // M → E
constexpr float kWLarge = 1280.0f;    // E → L

constexpr float kHRegular = 480.0f;  // S → R
constexpr float kHTall = 800.0f;     // R → T

/// 断点滞回：升级要超出边界 8dp、降级要低于边界 8dp。
/// 不加这个，把窗口拖到 900dp 附近会反复横跳。
constexpr float kHysteresis = 8.0f;

constexpr WidthClass rawWidthClass(float w) {
    if (w >= kWLarge) return WidthClass::Large;
    if (w >= kWExpanded) return WidthClass::Expanded;
    if (w >= kWMedium) return WidthClass::Medium;
    return WidthClass::Compact;
}

constexpr HeightClass rawHeightClass(float h) {
    if (h >= kHTall) return HeightClass::Tall;
    if (h >= kHRegular) return HeightClass::Regular;
    return HeightClass::Short;
}

constexpr WidthClass widthClassOf(float w, WidthClass prev, bool hasPrev) {
    const WidthClass raw = rawWidthClass(w);
    if (!hasPrev || raw == prev) return raw;
    const int rawR = static_cast<int>(raw);
    const int prevR = static_cast<int>(prev);
    if (rawR > prevR) {
        // 正在变宽：必须超出边界 8dp 才升级
        if (static_cast<int>(rawWidthClass(w - kHysteresis)) <= prevR) return prev;
    } else {
        // 正在变窄：必须低于边界 8dp 才降级
        if (static_cast<int>(rawWidthClass(w + kHysteresis)) >= prevR) return prev;
    }
    return raw;
}

constexpr HeightClass heightClassOf(float h, HeightClass prev, bool hasPrev) {
    const HeightClass raw = rawHeightClass(h);
    if (!hasPrev || raw == prev) return raw;
    const int rawR = static_cast<int>(raw);
    const int prevR = static_cast<int>(prev);
    if (rawR > prevR) {
        if (static_cast<int>(rawHeightClass(h - kHysteresis)) <= prevR) return prev;
    } else {
        if (static_cast<int>(rawHeightClass(h + kHysteresis)) >= prevR) return prev;
    }
    return raw;
}

/// 动作库按钮：按栏宽自适应分列（改造前写死 150/214，栏一窄就溢出）。
/// idealW 与改造前的观感对齐（桌面 150、触摸 190），只在放不下时才收窄、必要时退成单列。
constexpr void computeActionButtons(LayoutSpec& s, float columnW) {
    const float gap = 8.0f;
    const float avail = std::max(120.0f, columnW - 24.0f);  // 扣掉 child 内边距
    const float idealW = s.touch ? 190.0f : 150.0f;
    const float minW = s.touch ? 124.0f : 104.0f;

    float twoCol = (avail - gap) * 0.5f;
    if (twoCol >= minW) {
        s.actCols = 2;
        s.actW1 = std::min(twoCol, idealW);
    } else {
        s.actCols = 1;
        s.actW1 = std::min(avail, idealW);
    }
    // 滑条 + 按钮同一行、以及输入框 + 按钮同一行时用的窄档
    s.actW2 = std::max(96.0f, s.actW1 - 30.0f);
    s.actW3 = std::max(88.0f, s.actW1 - 40.0f);
}

}  // namespace

constexpr LayoutSpec makeLayout(float w, float h, bool touch, const SafeArea& safe,
                      const LayoutSpec* prev) {
    LayoutSpec s;
    s.touch = touch;
    s.safe = safe;
    // 整屏尺寸（含安全区）：摇杆落点用屏幕坐标
    s.screenW = std::max(240.0f, w);
    s.screenH = std::max(240.0f, h);
    // 可用尺寸扣掉安全区（刘海 / 圆角 / 手势条）—— 栏宽、顶栏、日志宽度都按可用宽算
    s.viewW = std::max(240.0f, s.screenW - safe.left - safe.right);
    s.viewH = std::max(240.0f, s.screenH - safe.top - safe.bottom);

    const bool hasPrev = prev != nullptr && prev->viewW > 0.0f;
    s.widthClass = widthClassOf(s.viewW, hasPrev ? prev->widthClass : WidthClass::Compact, hasPrev);
    s.heightClass =
        heightClassOf(s.viewH, hasPrev ? prev->heightClass : HeightClass::Regular, hasPrev);

    const bool wide = (s.widthClass == WidthClass::Large || s.widthClass == WidthClass::Expanded);
    const bool shortScreen = (s.heightClass == HeightClass::Short);

    if (wide) {
        // ================= A · 三栏工作台（平板横屏 / 桌面）=================
        s.mode = LayoutMode::ThreeColumn;
        s.topRows = (s.widthClass == WidthClass::Large) ? 3 : 2;
        s.topBarH = (s.topRows == 3) ? 124.0f : 92.0f;

        // 栏宽按比例算 + 上下限夹取。1440dp 时约 389/475/544，
        // 与改造前的 392/468/580 基本一致 —— 桌面不回归。
        s.leftW = clampf(roundi(s.viewW * 0.27f), 280.0f, 420.0f);
        s.ctrlW = clampf(roundi(s.viewW * 0.33f), 360.0f, 520.0f);
        const float rest = s.viewW - s.leftW - s.ctrlW - 32.0f;  // 两处 16dp 间隙
        // 日志算下来不到 280dp 就不常驻 —— 免得出现一条 150px 宽的鸡肋日志栏
        s.logInline = rest >= 280.0f;
        s.logW = s.logInline ? rest : 0.0f;
        s.logDrawer = !s.logInline;

        s.fontTitle = 23.0f;
        s.fontBody = 18.0f;
        s.fontSmall = 15.0f;
        s.styleScale = touch ? 1.15f : 1.0f;

        s.btnH = touch ? 44.0f : 0.0f;
        s.actH = touch ? 46.0f : 0.0f;
        s.estopH = shortScreen ? 56.0f : 64.0f;
        s.dampH = touch ? 48.0f : 0.0f;
        s.sliderW = 250.0f;
        s.paramInline = !shortScreen;

        s.joyInline = true;
        s.joyRadius = 74.0f;
        computeActionButtons(s, s.leftW);
    } else if (s.widthClass == WidthClass::Medium && !shortScreen) {
        // ================= B · 双栏（平板竖屏 / 小平板横屏）=================
        // 同样 600–899dp 宽，矮屏走单栏、高屏走双栏 —— 这就是二维断点的价值
        s.mode = LayoutMode::TwoColumn;
        s.topRows = 1;
        s.topBarH = 56.0f;

        s.leftW = clampf(roundi(s.viewW * 0.40f), 280.0f, 340.0f);
        s.ctrlW = s.viewW - s.leftW - 16.0f;
        s.logInline = true;
        s.logW = s.ctrlW;
        s.logDrawer = false;

        s.fontTitle = 21.0f;
        s.fontBody = 17.0f;
        s.fontSmall = 14.0f;
        s.styleScale = 1.15f;

        s.btnH = 48.0f;
        s.actH = 48.0f;
        s.estopH = 64.0f;
        s.dampH = 50.0f;
        s.sliderW = clampf(s.ctrlW - 130.0f, 150.0f, 220.0f);
        s.paramInline = true;

        s.joyInline = true;
        s.joyRadius = clampf(s.ctrlW * 0.17f, 56.0f, 84.0f);
        computeActionButtons(s, s.leftW);
    } else {
        // ================= C · 单栏 + 悬浮双摇杆（手机横竖屏）=================
        s.mode = LayoutMode::SingleColumn;
        s.topRows = 1;
        s.topBarH = shortScreen ? 52.0f : 56.0f;

        s.leftW = 0.0f;  // 左栏降级为侧滑抽屉
        s.ctrlW = s.viewW;
        s.logInline = false;
        s.logW = 0.0f;
        s.logDrawer = true;
        s.drawerW = std::min(420.0f, s.viewW * 0.62f);

        // 竖屏有纵向空间 → 底部页签；横屏矮屏 → 侧滑抽屉，把纵向空间全留给遥控
        s.nav = shortScreen ? NavKind::Drawer : NavKind::BottomTab;
        s.tabBarH = (s.nav == NavKind::BottomTab) ? 56.0f : 0.0f;
        s.tabH = s.tabBarH + safe.bottom;

        s.fontTitle = 19.0f;
        s.fontBody = 16.0f;
        s.fontSmall = 13.0f;
        s.styleScale = 1.15f;

        s.btnH = 52.0f;
        s.actH = 52.0f;
        s.estopH = shortScreen ? 72.0f : 88.0f;
        s.dampH = 50.0f;
        s.sliderW = clampf(s.viewW - 130.0f, 140.0f, 260.0f);
        // 横屏矮屏纵向只有约 338dp，塞不下全部内容 → 快捷与参数收进「更多」浮层
        s.paramInline = false;

        s.joyInline = false;
        s.joyRadius = clampf(std::min(s.viewW, s.viewH) * 0.20f, 60.0f, 128.0f);
        computeActionButtons(s, s.drawerW);
    }

    // 顶栏高度含顶部安全区（刘海 / 状态栏）
    s.topBarH += safe.top;

    if (!s.joyInline) computeFloatingJoysticks(s);
    return s;
}

constexpr void computeFloatingJoysticks(LayoutSpec& s) {
    // 注意坐标系：这里算出来的是**屏幕坐标**（ImGui 的原点在窗口左上角），
    // 所以要用 screenH / safe.* 而不是 viewH。
    // 左右往里收：贴边时拇指够着别扭，还会撞上系统手势区（安卓边缘约 20dp 是返回手势）
    s.joyInsetX = s.joyRadius * 0.95f + s.safe.left + 16.0f;
    // 底部往上收：避开手势条；竖屏还要再让开底部页签
    s.joyCenterY = s.screenH - s.safe.bottom - s.joyRadius - 24.0f - s.tabBarH;
    // ★ 内容预留高度与摇杆位置**同源**：改造前一个算 h-radius-90、一个写死 250，屏幕一变就错位
    s.joyReserve = s.screenH - (s.joyCenterY - s.joyRadius);
}

const char* layoutModeName(LayoutMode m) {
    switch (m) {
        case LayoutMode::ThreeColumn: return "A · 三栏工作台";
        case LayoutMode::TwoColumn: return "B · 双栏";
        case LayoutMode::SingleColumn: return "C · 单栏 + 悬浮摇杆";
    }
    return "?";
}

const char* widthClassName(WidthClass c) {
    switch (c) {
        case WidthClass::Compact: return "C <600dp";
        case WidthClass::Medium: return "M 600–899dp";
        case WidthClass::Expanded: return "E 900–1279dp";
        case WidthClass::Large: return "L >=1280dp";
    }
    return "?";
}

const char* heightClassName(HeightClass c) {
    switch (c) {
        case HeightClass::Short: return "S <480dp";
        case HeightClass::Regular: return "R 480–799dp";
        case HeightClass::Tall: return "T >=800dp";
    }
    return "?";
}

}  // namespace go2
