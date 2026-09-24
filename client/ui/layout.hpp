#pragma once

// ============================================================================
// 断点布局：把「可用尺寸 + 输入方式 + 安全区」算成一套布局参数
//
// ★ 设计方向（2026-09-24 按用户反馈重做）：
//   主界面**只有遥控**，双摇杆永远悬浮在屏幕两个下角（画在最上层，
//   任何面板/弹窗都不能遮挡或抢走它们）；设备 / 动作库 / 设置 / 日志
//   全部改成顶栏按钮 → 弹窗。
//   所以这里不再有「三栏 / 双栏」那套列布局 —— 断点只决定：
//     顶栏布局、遥控内容的最大宽度、摇杆半径、弹窗尺寸、动作库按钮列数、字号。
//
// 单位约定：**dp**（逻辑像素）。各入口必须先按设备密度归一 ImGui 坐标
// （见 apps/android/native/main_android.cpp 里的 detectPixelScale），
// 否则本文件里的断点数值在 3x 屏上会全部失效 —— 真机实测：小米平板
// SDL 报 1920x1200 物理像素、displayDPI=280 → 密度 1.75，逻辑尺寸 1097x686dp。
//
// 实现为什么全在头文件里：makeLayout 是 **constexpr**，而 constexpr 函数隐含 inline
// —— 定义必须对所有使用它的 TU 可见。曾经把定义放在 layout.cpp、头文件只留声明，
// 结果 ui.cpp / main_android.cpp 发出 -Wundefined-inline、链接报 undefined symbol。
// ============================================================================

#include <algorithm>

namespace go2 {

/// 设备安全区（刘海 / 圆角 / 手势条），单位 dp
struct SafeArea {
    float top = 0.0f;
    float bottom = 0.0f;
    float left = 0.0f;
    float right = 0.0f;
};

/// 宽度档：<600 / 600–899 / 900–1279 / >=1280 dp
enum class WidthClass { Compact, Medium, Expanded, Large };
/// 高度档：<480 / 480–799 / >=800 dp
enum class HeightClass { Short, Regular, Tall };

/// 一帧要用的全部布局参数。字段都是 dp。
struct LayoutSpec {
    WidthClass widthClass = WidthClass::Large;
    HeightClass heightClass = HeightClass::Regular;

    float screenW = 0.0f;  ///< 整屏宽（含安全区）—— 摇杆落点用屏幕坐标
    float screenH = 0.0f;  ///< 整屏高（含安全区）
    float viewW = 0.0f;    ///< 可用宽 = screenW − 安全区左右
    float viewH = 0.0f;    ///< 可用高 = screenH − 安全区上下
    bool touch = false;    ///< 手指优先（决定按钮最小尺寸）；按输入方式判定，不按平台
    SafeArea safe{};

    // ---- 顶栏（品牌 + 状态 + 四个入口按钮）----
    float topBarH = 56.0f;
    float topBtnH = 40.0f;    ///< 顶栏按钮高
    float topBtnW = 88.0f;    ///< 顶栏按钮宽（窄屏会算小）
    bool showBrand = true;    ///< 太窄时把品牌名让给按钮

    // ---- 遥控主体 ----
    float contentMaxW = 720.0f;  ///< 内容最大宽（居中，避免宽屏把按钮拉得又长又扁）
    float contentPad = 16.0f;    ///< 内容左右内边距

    // ---- 控件尺寸 ----
    float btnH = 48.0f;       ///< 通用按钮高
    float estopH = 76.0f;     ///< 急停按钮高（安全关键：永远全宽、永远可见）
    float dampH = 52.0f;      ///< 阻尼按钮高
    float sliderW = 260.0f;   ///< 参数滑条宽
    bool paramInline = true;  ///< 参数常显（否则收进「参数」折叠区）

    // ---- 双摇杆：永远悬浮在屏幕两个下角 ----
    float joyRadius = 96.0f;
    float joyInsetX = 0.0f;   ///< 圆心距屏幕左/右边缘
    float joyCenterY = 0.0f;  ///< 圆心距屏幕顶部
    float joyReserve = 0.0f;  ///< 内容底部要预留的高度（与摇杆同源，避免压住按钮）

    // ---- 弹窗（设备 / 动作库 / 设置 / 日志）----
    float popupW = 640.0f;
    float popupH = 480.0f;

    // ---- 动作库弹窗里的按钮网格（一排排平铺，不折叠）----
    int actCols = 3;
    float actW1 = 160.0f;
    float actW2 = 130.0f;
    float actW3 = 120.0f;
    float actH = 48.0f;

    // ---- 字体与样式 ----
    float fontTitle = 23.0f, fontBody = 18.0f, fontSmall = 15.0f;
    float styleScale = 1.0f;  ///< ImGuiStyle 缩放倍率（从快照重算，不累乘）
};

namespace layout_detail {

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

/// 动作库弹窗里的按钮网格：按弹窗宽度算列数与单格宽度（一排排平铺，不用折叠面板）
constexpr void computeActionGrid(LayoutSpec& s) {
    const float gap = 8.0f;
    const float avail = std::max(160.0f, s.popupW - 48.0f);  // 扣掉弹窗内边距
    const float minW = s.touch ? 118.0f : 108.0f;
    const float idealW = s.touch ? 200.0f : 170.0f;

    int cols = static_cast<int>((avail + gap) / (minW + gap));
    if (cols < 1) cols = 1;
    if (cols > 4) cols = 4;
    float w = (avail - gap * static_cast<float>(cols - 1)) / static_cast<float>(cols);
    if (w > idealW) {  // 太宽就加一列（最多 4 列），否则按钮会又长又扁
        const int more = static_cast<int>(avail / (idealW + gap));
        if (more > cols) {
            cols = std::min(more, 4);
            w = (avail - gap * static_cast<float>(cols - 1)) / static_cast<float>(cols);
        }
    }
    s.actCols = cols;
    s.actW1 = w;
    s.actW2 = std::max(96.0f, w - 24.0f);
    s.actW3 = std::max(88.0f, w - 36.0f);
}

}  // namespace layout_detail

/// 由 screenW/screenH/safe/tabBarH 推导悬浮摇杆的圆心与内容预留高度。
/// 摇杆位置与预留高度必须同源 —— 改造前一个算 h-radius-90、一个写死 250，屏幕一变就错位。
constexpr void computeFloatingJoysticks(LayoutSpec& s) {
    // 注意坐标系：算出来的是**屏幕坐标**（ImGui 原点是窗口左上角），所以用 screenH / safe.*
    // 左右往里收：贴边时拇指够着别扭，还会撞上系统手势区（安卓边缘约 20dp 是返回手势）
    s.joyInsetX = s.joyRadius * 0.95f + s.safe.left + 20.0f;
    // 底部往上收：避开手势条
    s.joyCenterY = s.screenH - s.safe.bottom - s.joyRadius - 22.0f;
    // ★ 内容预留高度与摇杆位置**同源**：改造前一个算 h-radius-90、一个写死 250，屏幕一变就错位
    s.joyReserve = s.screenH - (s.joyCenterY - s.joyRadius);
}

/// 由「可用尺寸 + 输入方式 + 安全区」算出布局参数（纯函数，无副作用）。
///
/// **constexpr**：所以 tests/layout_test.cpp 能用 static_assert 在编译期把各机型档位的
/// 结果钉死 —— 改了断点常量而忘了同步预期值，编译就会失败。
/// @param prev 上一帧的 spec，用于断点滞回（避免在断点附近来回抖动）；可为 nullptr
constexpr LayoutSpec makeLayout(float w, float h, bool touch, const SafeArea& safe,
                                const LayoutSpec* prev = nullptr) {
    using namespace layout_detail;
    LayoutSpec s;
    s.touch = touch;
    s.safe = safe;
    s.screenW = std::max(240.0f, w);
    s.screenH = std::max(240.0f, h);
    // 可用尺寸扣掉安全区（刘海 / 圆角 / 手势条）
    s.viewW = std::max(240.0f, s.screenW - safe.left - safe.right);
    s.viewH = std::max(240.0f, s.screenH - safe.top - safe.bottom);

    const bool hasPrev = prev != nullptr && prev->viewW > 0.0f;
    s.widthClass = widthClassOf(s.viewW, hasPrev ? prev->widthClass : WidthClass::Compact, hasPrev);
    s.heightClass =
        heightClassOf(s.viewH, hasPrev ? prev->heightClass : HeightClass::Regular, hasPrev);

    const bool shortScreen = (s.heightClass == HeightClass::Short);
    const bool narrow = (s.widthClass == WidthClass::Compact);

    // ======================= 字号与样式 =======================
    // 字号只跟**宽度档**走（同一块屏上，接鼠标还是用手指不该改变字号）；
    // 触摸只放大控件间距/圆角/滚动条（styleScale）。
    if (s.widthClass == WidthClass::Compact) {
        s.fontTitle = 19.0f; s.fontBody = 16.0f; s.fontSmall = 13.0f;
    } else if (s.widthClass == WidthClass::Medium) {
        s.fontTitle = 21.0f; s.fontBody = 17.0f; s.fontSmall = 14.0f;
    } else {
        s.fontTitle = 23.0f; s.fontBody = 18.0f; s.fontSmall = 15.0f;
    }
    s.styleScale = touch ? 1.15f : 1.0f;  // 触摸：内边距/圆角/滚动条跟着放大

    // ======================= 顶栏 =======================
    // 只有一行：品牌 + 受控状态 + 四个入口按钮（设备/动作库/设置/日志）
    s.topBarH = (touch ? 58.0f : 46.0f) + safe.top;
    s.topBtnH = touch ? (shortScreen ? 40.0f : 44.0f) : 30.0f;

    // 按钮宽：给品牌与状态 chip 留出位置，剩下的四等分；太窄就收起品牌名
    const float barAvail = s.viewW - 32.0f;                       // 左右内边距
    const float brandNeed = touch ? 132.0f : 210.0f;              // 品牌 + chip
    const float gaps = 8.0f * 5.0f;                               // 品牌/4 按钮之间的间隙
    float btnW = (barAvail - brandNeed - gaps) / 4.0f;
    s.showBrand = btnW >= 56.0f;
    if (!s.showBrand) btnW = (barAvail - 96.0f - gaps) / 4.0f;    // 只留 chip
    s.topBtnW = clampf(btnW, 44.0f, 118.0f);

    // ======================= 遥控主体 =======================
    // 宽屏不把按钮拉得又长又扁 → 内容居中 + 宽度上限
    const float pad = (touch ? 14.0f : 18.0f);
    s.contentPad = pad;
    s.contentMaxW = clampf(s.viewW - pad * 2.0f, 260.0f, 760.0f);

    // ======================= 双摇杆（永远悬浮两下角）=======================
    // 半径跟着短边走，但**上限压到 112dp**：摇杆太大就会把整条底边都占掉，
    // 内容区会被挤得只剩一小条（真机截图里就出现过内容被摇杆压住）。
    s.joyRadius = clampf(std::min(s.viewW, s.viewH) * 0.18f, 64.0f, 112.0f);
    computeFloatingJoysticks(s);

    // ---- 控件尺寸 ----
    s.btnH = touch ? (shortScreen ? 44.0f : 50.0f) : 34.0f;
    s.estopH = touch ? (shortScreen ? 64.0f : 78.0f) : 52.0f;
    s.dampH = touch ? (shortScreen ? 46.0f : 54.0f) : 0.0f;
    s.sliderW = clampf(s.contentMaxW - 150.0f, 150.0f, 300.0f);
    // 内容区真实可用高度 = 整屏 − 顶栏 − 摇杆区。
    // 矮屏纵向紧张 → 参数收进折叠区，把空间让给急停/阻尼/快捷。
    const float contentAvailH = s.screenH - s.topBarH - s.joyReserve;
    s.paramInline = (contentAvailH > 420.0f);

    // ======================= 弹窗 =======================
    s.popupW = clampf(s.viewW * 0.88f, 300.0f, 1000.0f);
    s.popupH = clampf(s.viewH * 0.82f, 260.0f, 780.0f);
    s.actH = touch ? 52.0f : 34.0f;
    computeActionGrid(s);

    (void)narrow;
    return s;
}

const char* widthClassName(WidthClass c);
const char* heightClassName(HeightClass c);

}  // namespace go2
