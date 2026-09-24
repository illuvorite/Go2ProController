#pragma once

// ============================================================================
// 断点布局：把「可用尺寸 + 输入方式 + 安全区」算成一套布局参数
//
// 背景：改造前界面只有「桌面 / 手机」一个布尔开关（UiState::mobileLayout），
// 面板宽度全是写死的像素（左栏 392/452、遥控栏 468、动作按钮 150/214…），
// 于是平板被当手机用、手机竖屏直接溢出屏幕。这里改成：
//
//   宽度 4 档 × 高度 3 档  →  3 种布局模式（三栏 / 双栏 / 单栏）
//   + 两条叠加修正：高度 <480dp 的「矮屏修正」、手指输入的「触摸修正」
//
// 单位约定：**dp**（逻辑像素）。各入口必须先按设备密度归一 ImGui 坐标
// （见 apps/android/native/main_android.cpp 里的 applyDisplayScale），
// 否则本文件里的断点数值在 3x 屏上会全部失效。
// ============================================================================

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
/// 导航方式（只有单栏模式用得到）
enum class NavKind { BottomTab, Drawer };

/// 布局模式
enum class LayoutMode {
    ThreeColumn,   ///< A：左栏 + 遥控 + 日志（平板横屏 / 桌面）
    TwoColumn,     ///< B：左栏常驻 + 右栏（遥控上 / 日志下）（平板竖屏）
    SingleColumn,  ///< C：单栏 + 悬浮双摇杆（手机竖屏 / 横屏矮屏）
};

/// 一帧要用的全部布局参数。字段都是 dp。
struct LayoutSpec {
    LayoutMode mode = LayoutMode::ThreeColumn;
    WidthClass widthClass = WidthClass::Large;
    HeightClass heightClass = HeightClass::Regular;
    NavKind nav = NavKind::BottomTab;

    float screenW = 0.0f;  ///< 整屏宽（含安全区）—— 摇杆落点要用屏幕坐标，不能用"可用宽"
    float screenH = 0.0f;  ///< 整屏高（含安全区）
    float viewW = 0.0f;    ///< 可用宽 = screenW − 安全区左右
    float viewH = 0.0f;    ///< 可用高 = screenH − 安全区上下
    bool touch = false;   ///< 手指优先（决定按钮最小尺寸）；按输入方式判定，不按平台
    SafeArea safe{};

    // ---- 顶栏 ----
    float topBarH = 124.0f;
    int topRows = 3;      ///< 1 / 2 / 3 行；窄屏把次要按钮收进「⋮」

    // ---- 三栏 / 双栏 ----
    float leftW = 392.0f;
    float ctrlW = 468.0f;
    float logW = 0.0f;
    bool logInline = true;   ///< 日志是否常驻占宽度
    bool logDrawer = false;  ///< 日志是否改为浮层（矮屏 / 单栏）

    // ---- 控件尺寸 ----
    float btnH = 0.0f;                       ///< 0 = 沿用主题默认高度
    float actW1 = 150.0f, actW2 = 120.0f, actW3 = 110.0f;  ///< 动作库按钮宽
    float actH = 0.0f;                       ///< 动作库按钮高（0 = 默认）
    int actCols = 2;                         ///< 动作库列数（按栏宽算）
    float sliderW = 250.0f;                  ///< 遥控参数滑条宽
    bool paramInline = true;                 ///< 参数是否常显（否则收进「参数」折叠区）
    float estopH = 52.0f;                    ///< 急停按钮高（安全关键：永远全宽、永远可见）
    float dampH = 0.0f;                      ///< 阻尼按钮高（0 = 默认）

    // ---- 双摇杆 ----
    bool joyInline = true;    ///< true = 内嵌在遥控栏；false = 悬浮在屏幕两下角
    float joyRadius = 74.0f;
    float joyInsetX = 0.0f;   ///< 悬浮时：圆心距屏幕左/右边缘
    float joyCenterY = 0.0f;  ///< 悬浮时：圆心距屏幕顶部
    float joyReserve = 0.0f;  ///< 悬浮时：内容底部要预留的高度（与摇杆同源，避免压住按钮）

    // ---- 字体与样式 ----
    float fontTitle = 23.0f, fontBody = 18.0f, fontSmall = 15.0f;
    float styleScale = 1.0f;  ///< ImGuiStyle 缩放倍率（会从快照重算，不累乘）

    // ---- 单栏模式的导航 ----
    float tabBarH = 0.0f;     ///< 底部页签本体高（不含安全区）
    float tabH = 0.0f;        ///< 底部页签总高（含底部安全区）
    float drawerW = 0.0f;     ///< 侧滑抽屉宽
};

/// 由「可用尺寸 + 输入方式 + 安全区」算出布局参数（纯函数，无副作用）。
///
/// **constexpr**：所以 tests/layout_test.cpp 能用 static_assert 在编译期把各机型档位的
/// 结果钉死 —— 改了断点常量而忘了同步预期值，编译就会失败。
/// @param prev 上一帧的 spec，用于断点滞回（避免在断点附近来回抖动）；可为 nullptr
constexpr LayoutSpec makeLayout(float w, float h, bool touch, const SafeArea& safe,
                                const LayoutSpec* prev = nullptr);

/// 由 screenW/screenH/safe/tabBarH 推导悬浮摇杆的圆心与内容预留高度。
/// 摇杆位置与预留高度必须同源 —— 改造前一个算 h-radius-90、一个写死 250，屏幕一变就错位。
constexpr void computeFloatingJoysticks(LayoutSpec& s);

const char* layoutModeName(LayoutMode m);
const char* widthClassName(WidthClass c);
const char* heightClassName(HeightClass c);

}  // namespace go2
