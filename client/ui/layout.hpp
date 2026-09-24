#pragma once

// ============================================================================
// 断点布局：把「可用尺寸 + 输入方式 + 安全区」算成一套布局参数
//
// ★ 设计方向（2026-09-24 第三轮，按用户反馈再改）：
//   主界面分**上下两块**：
//     · 上 = 页面区：遥控 / 动作库 两个页面（顶栏页签切换）。
//       **两个页面都铺满整屏宽**（不再有 760dp 居中留白）；设备 / 设置 / 日志
//       仍是弹窗 —— 弹窗打开时摇杆带被它盖住（摇杆那一帧不画）。
//     · 下 = 摇杆带：双摇杆悬浮在屏幕两个下角，任何页面都盖不住它；
//       **两杆中间是「单控 / 群控」面板**（按钮 + 当前受控对象）。
//   摇杆半径比第一版**缩小**（短边 *0.15、上限 88dp）—— 把纵向空间让给页面区。
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

    // ---- 顶栏（品牌 + 状态 + 页签 + 模式/工具/急停按钮）----
    float topBarH = 56.0f;
    float topBtnH = 40.0f;    ///< 顶栏按钮高
    float topBtnW = 88.0f;    ///< 顶栏按钮宽（窄屏会算小）
    bool showBrand = true;    ///< 太窄时把品牌名让给按钮
    bool topTwoRows = false;  ///< 极窄屏：顶栏拆两行（一行放不下 8 个按钮）

    // ---- 页面区（遥控 / 动作库）----
    float pagePad = 16.0f;      ///< 页面左右内边距
    float pageW = 0.0f;         ///< 页面内容宽（两个页面都按它铺满整屏宽）
    float pageH = 0.0f;         ///< 页面内容高 = screenH − 顶栏 − 摇杆带
    /// 页面内容宽（历史名：曾经是用来居中限宽的"最大宽"）。
    /// ★ 用户明确要求"铺满" → 现在直接等于 pageW，不再居中留白。
    float contentMaxW = 720.0f;
    float contentPad = 16.0f;   ///< 页面内边距

    // ---- 控件尺寸 ----
    float btnH = 48.0f;       ///< 通用按钮高
    float estopH = 76.0f;     ///< 遥控页急停按钮高（安全关键：永远全宽、永远可见）
    float dampH = 52.0f;      ///< 阻尼按钮高
    float sliderW = 260.0f;   ///< 参数滑条宽
    bool paramInline = true;  ///< 参数常显（否则收进「参数」折叠区）

    // ---- 摇杆带：双摇杆悬浮在屏幕两个下角，中间是「单控 / 群控」面板 ----
    float joyRadius = 88.0f;
    float joyInsetX = 0.0f;   ///< 圆心距屏幕左/右边缘
    float joyCenterY = 0.0f;  ///< 圆心距屏幕顶部
    float joyReserve = 0.0f;  ///< 页面区底部要预留的高度（与摇杆同源，避免压住内容）
    float joyGapMinX = 0.0f;  ///< 两杆之间的空档（面板放在这里，绝不压到摇杆）
    float joyGapMaxX = 0.0f;
    float joyPanelW = 0.0f;   ///< 空档里「单控 / 群控」面板的尺寸
    float joyPanelH = 0.0f;
    bool joyPanelStack = false;  ///< 空档太窄 → 两个按钮竖排（窄屏手机）

    // ---- 弹窗（设备 / 设置 / 日志）：打开时**盖住**摇杆带 ----
    float popupW = 640.0f;
    float popupH = 480.0f;

    // ---- 动作库页里的按钮网格（一排排平铺，不折叠）----
    float actAreaW = 0.0f;  ///< 网格可用宽（= 页面宽 − 内边距）
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

/// 摇杆下方给标签留的高度：标签本身约 16dp + 与圆之间的 8dp 间隔 + 一点余量。
/// 不给它留位置，真机上（底部安全区为 0 时）标签会被屏幕下边缘裁掉。
constexpr float kJoyLabelSpace = 30.0f;

/// 顶栏按钮个数（决定单行模式下的按钮宽）：
/// 页签 2（遥控 / 动作库）+ 工具 3（设备 / 设置 / 日志）+ 急停 1
/// ★「单控 / 群控」已挪到两个摇杆中间（摇杆带面板），不再占顶栏位置。
constexpr int kTopBtnsSingleRow = 6;
/// 两行模式下第二行的按钮个数：页签 2 + 工具 3
constexpr int kTopBtnsRow2 = 5;

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

/// 动作库页里的按钮网格：按**页面可用宽**算列数与单格宽度
/// （动作库是常驻整屏页面，所以用整屏宽而不是弹窗宽 —— 宽屏能排 8 列）。
constexpr void computeActionGrid(LayoutSpec& s) {
    const float gap = 8.0f;
    const float avail = std::max(160.0f, s.actAreaW);
    const float minW = s.touch ? 118.0f : 108.0f;
    const float idealW = s.touch ? 190.0f : 168.0f;

    int cols = static_cast<int>((avail + gap) / (minW + gap));
    if (cols < 1) cols = 1;
    if (cols > 8) cols = 8;
    float w = (avail - gap * static_cast<float>(cols - 1)) / static_cast<float>(cols);
    if (w > idealW) {  // 太宽就加列（最多 8 列），否则按钮会又长又扁
        const int more = static_cast<int>(avail / (idealW + gap));
        if (more > cols) {
            cols = std::min(more, 8);
            w = (avail - gap * static_cast<float>(cols - 1)) / static_cast<float>(cols);
        }
    }
    s.actCols = cols;
    s.actW1 = w;
    s.actW2 = std::max(96.0f, w - 24.0f);
    s.actW3 = std::max(88.0f, w - 36.0f);
}

}  // namespace layout_detail

/// 由 screenW/screenH/safe 推导悬浮摇杆的圆心、内容预留高度与两杆之间的空档。
/// 摇杆位置与预留高度必须同源 —— 改造前一个算 h-radius-90、一个写死 250，屏幕一变就错位。
constexpr void computeFloatingJoysticks(LayoutSpec& s) {
    // 注意坐标系：算出来的是**屏幕坐标**（ImGui 原点是窗口左上角），所以用 screenH / safe.*
    // 左右往里收：贴边时拇指够着别扭，还会撞上系统手势区（安卓边缘约 20dp 是返回手势）
    s.joyInsetX = s.joyRadius * 0.95f + s.safe.left + 20.0f;
    // 底部往上收：既要避开手势条，也要给摇杆下方的标签（"左 · 移动"）留出位置 ——
    // 真机上底部安全区可能是 0（沉浸式下系统把手势条藏了），不给标签留就会把字裁掉。
    s.joyCenterY = s.screenH - s.safe.bottom - s.joyRadius - layout_detail::kJoyLabelSpace;
    // ★ 页面区预留高度与摇杆位置**同源**：改造前一个算 h-radius-90、一个写死 250，屏幕一变就错位
    s.joyReserve = s.screenH - (s.joyCenterY - s.joyRadius);
    // 两杆之间的空档（极窄屏可能为负 → 交给绘制侧判断，不画状态文字）
    s.joyGapMinX = s.joyInsetX + s.joyRadius;
    s.joyGapMaxX = s.screenW - s.joyInsetX - s.joyRadius;
    if (s.joyGapMaxX < s.joyGapMinX) {
        // 两杆几乎贴在一起：收缩成一个"以屏幕中心为准"的空点（绘制侧会跳过文字）
        const float c = s.screenW * 0.5f;
        s.joyGapMinX = c;
        s.joyGapMaxX = c;
    }
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
    s.topBtnH = touch ? (shortScreen ? 40.0f : 44.0f) : 30.0f;

    // 按钮宽：给品牌与状态 chip 留出位置，剩下等分。
    // 极窄屏（C 档）一行塞不下 8 个按钮 → 拆两行：
    //   第一行：状态 chip + 单控 / 群控 / 急停
    //   第二行：页签（遥控 / 动作库）+ 工具（设备 / 设置 / 日志）
    constexpr float kPadX = 16.0f;  // 顶栏左右内边距（单边）
    const float barAvail = std::max(120.0f, s.viewW - kPadX * 2.0f);
    s.topTwoRows = (s.widthClass == WidthClass::Compact);
    if (s.topTwoRows) {
        s.showBrand = false;  // 位置上让给按钮；品牌改在遥控页标题里出现
        // 第二行 5 个按钮：4 个间隙 + 两端留白
        const float gaps = 8.0f * static_cast<float>(kTopBtnsRow2);
        s.topBtnW = clampf((barAvail - gaps) / static_cast<float>(kTopBtnsRow2), 44.0f, 118.0f);
        s.topBarH = 2.0f * s.topBtnH + (touch ? 26.0f : 20.0f) + safe.top;
    } else {
        const float brandNeed = touch ? 140.0f : 210.0f;  // 品牌 + chip
        const float gaps = 8.0f * static_cast<float>(kTopBtnsSingleRow + 1);
        float btnW = (barAvail - brandNeed - gaps) / static_cast<float>(kTopBtnsSingleRow);
        s.showBrand = btnW >= 56.0f;
        if (!s.showBrand)  // 只留 chip
            btnW = (barAvail - 96.0f - gaps) / static_cast<float>(kTopBtnsSingleRow);
        s.topBtnW = clampf(btnW, 44.0f, 118.0f);
        s.topBarH = (touch ? 58.0f : 46.0f) + safe.top;
    }

    // ======================= 双摇杆（悬浮两下角）=======================
    // 半径跟着短边走，但**上限压到 88dp**、下限 52dp：
    // 页面区吃掉的高度要尽量小（这一版比第一版小了约 1/4）。
    s.joyRadius = clampf(std::min(s.viewW, s.viewH) * 0.15f, 52.0f, 88.0f);
    computeFloatingJoysticks(s);

    // ---- 摇杆带中间的「单控 / 群控」面板（用户要求放两个摇杆中间）----
    {
        const float gapW = s.joyGapMaxX - s.joyGapMinX;
        s.joyPanelW = clampf(gapW - 24.0f, 84.0f, 320.0f);
        s.joyPanelStack = (s.joyPanelW < 190.0f);  // 空档太窄 → 两个按钮竖排
        s.joyPanelH = (s.joyPanelStack ? 2.0f * s.topBtnH + 6.0f : s.topBtnH) +
                      (touch ? 46.0f : 38.0f);     // 再加一行"当前受控对象"文字
    }

    // ======================= 页面区 =======================
    const float pad = (touch ? 14.0f : 18.0f);
    s.pagePad = pad;
    s.contentPad = pad;
    s.pageW = std::max(240.0f, s.viewW - pad * 2.0f);
    s.pageH = std::max(160.0f, s.screenH - s.topBarH - s.joyReserve);
    // ★ 铺满：两个页面都用满 pageW（用户明确否定"居中限宽留白"的观感）
    s.contentMaxW = s.pageW;

    // ---- 控件尺寸 ----
    s.btnH = touch ? (shortScreen ? 44.0f : 50.0f) : 34.0f;
    s.estopH = touch ? (shortScreen ? 64.0f : 78.0f) : 52.0f;
    s.dampH = touch ? (shortScreen ? 46.0f : 54.0f) : 0.0f;
    // 参数行左边是标签、右边滑条撑满（见 ui.cpp 的 paramRow）—— 这里给个上限免得宽屏滑条太长
    s.sliderW = clampf(s.pageW * 0.5f, 150.0f, 560.0f);
    // 矮屏纵向紧张 → 参数收进折叠区，把空间让给急停/阻尼/快捷。
    s.paramInline = (s.pageH > 420.0f);

    // ======================= 弹窗（设备 / 设置 / 日志）=======================
    // ★ 弹窗要**盖住摇杆带**（用户要求："其余弹窗统一覆盖在摇杆区域上方"）：
    //   所以按整屏比例取高，且居中于整屏 —— 打开时摇杆那一帧不画（见 ui.cpp）。
    s.popupW = clampf(s.viewW * 0.88f, 300.0f, 1000.0f);
    s.popupH = clampf(s.viewH * 0.90f, 260.0f, 820.0f);
    s.actH = touch ? 52.0f : 34.0f;
    s.actAreaW = std::max(160.0f, s.pageW - 24.0f);
    computeActionGrid(s);

    return s;
}

const char* widthClassName(WidthClass c);
const char* heightClassName(HeightClass c);

}  // namespace go2
