// ============================================================================
// 断点布局的编译期单元测试
//
// 为什么用 static_assert 而不是运行时断言：
//   makeLayout 是 constexpr 纯函数，所以各机型档位的结果可以在**编译期**钉死。
//   以后谁改了 layout.hpp 里的断点常量、比例或上下限，而忘了同步预期值，编译直接失败 ——
//   比运行时测试更早、更硬。
//
// 这里只 #include 头文件（makeLayout 的实现在头文件里，constexpr 才能编译期求值），
// 由 CMake 把 ui/layout.cpp 一起编进这个目标 —— 于是它同时是一个**跨 TU 链接测试**：
// 早期版本 makeLayout 声明成 constexpr 却把定义放在 .cpp，单文件语法检查全过，
// 直到真正链接才暴露 `undefined symbol: go2::makeLayout`。所以这个目标必须真链接。
//
// 设计方向（2026-09-24 重做后）：主界面只有遥控 + 两下角悬浮摇杆，
// 设备/动作库/设置/日志 全是顶栏按钮 → 弹窗。断点只决定：
// 顶栏布局、内容最大宽度、摇杆半径、弹窗尺寸、动作库按钮列数、字号。
// ============================================================================

#include "../ui/layout.hpp"

#include <cstdio>

namespace {

constexpr bool near(float a, float b, float eps = 0.06f) {
    return (a > b ? a - b : b - a) <= eps;
}

// ---- 各平台的典型安全区 ----
constexpr go2::SafeArea kAndroidSafe{0.0f, 24.0f, 20.0f, 20.0f};  // 底部手势条 + 左右返回手势
constexpr go2::SafeArea kDesktopSafe{0.0f, 0.0f, 0.0f, 0.0f};

// ============================================================ 1. 手机竖屏 390×844
constexpr go2::LayoutSpec kPhonePortrait =
    go2::makeLayout(390.0f, 844.0f, true, kAndroidSafe, nullptr);

static_assert(kPhonePortrait.widthClass == go2::WidthClass::Compact, "350dp 可用宽应属 C 档");
static_assert(kPhonePortrait.heightClass == go2::HeightClass::Tall, "820dp 可用高应属 T 档");
static_assert(near(kPhonePortrait.topBarH, 58.0f), "顶栏 58dp（触摸 + 无顶部安全区）");
static_assert(!kPhonePortrait.showBrand, "太窄：品牌名让给四个入口按钮");
static_assert(kPhonePortrait.topBtnW >= 44.0f, "入口按钮不小于 44dp（触摸下限）");
static_assert(near(kPhonePortrait.contentMaxW, 322.0f), "内容宽 = 可用宽 - 左右内边距");
static_assert(near(kPhonePortrait.btnH, 50.0f), "触摸按钮最小高 50dp ≥ 48dp 下限");
static_assert(near(kPhonePortrait.estopH, 78.0f), "急停竖屏给 78dp（安全项要够大）");
static_assert(kPhonePortrait.paramInline, "竖屏内容区够高 → 参数常显");
static_assert(near(kPhonePortrait.fontTitle, 19.0f), "C 档字号 19/16/13");
// ★ 摇杆：永远悬浮在两下角，且必须完全落在屏幕内、底部让开手势条
static_assert(near(kPhonePortrait.joyRadius, 64.0f), "摇杆半径 = min(350,820)*0.18，触到 64 下限");
static_assert(near(kPhonePortrait.joyInsetX, 100.8f), "左内缩 = radius*0.95 + 安全区左 + 20");
static_assert(near(kPhonePortrait.joyCenterY, 734.0f), "圆心高 = 844-24-64-22");
static_assert(near(kPhonePortrait.joyReserve, 174.0f), "摇杆区高度 = 直径 + 上下留白");
static_assert(kPhonePortrait.joyCenterY + kPhonePortrait.joyRadius <=
                  kPhonePortrait.screenH - kPhonePortrait.safe.bottom,
              "★ 摇杆底边必须让开底部安全区（手势条）");
static_assert(kPhonePortrait.joyInsetX - kPhonePortrait.joyRadius >= kPhonePortrait.safe.left,
              "★ 摇杆左边必须让开左安全区");
// ★★ 关键：内容区（整屏 − 顶栏 − 摇杆区）必须留得下东西
static_assert(kPhonePortrait.screenH - kPhonePortrait.topBarH - kPhonePortrait.joyReserve >
                  300.0f,
              "★ 内容区高度要够（摇杆区不能把内容挤没）");
static_assert(kPhonePortrait.joyReserve > kPhonePortrait.joyRadius * 2.0f,
              "★ 摇杆区高度要盖住整个摇杆");
static_assert(kPhonePortrait.popupW <= kPhonePortrait.viewW, "弹窗宽不超可用宽");
static_assert(kPhonePortrait.popupH <= kPhonePortrait.viewH, "弹窗高不超可用高");

// ============================================================ 2. 手机横屏 844×390
constexpr go2::LayoutSpec kPhoneLandscape =
    go2::makeLayout(844.0f, 390.0f, true, kAndroidSafe, nullptr);

static_assert(kPhoneLandscape.widthClass == go2::WidthClass::Medium, "804dp 属 M 档");
static_assert(kPhoneLandscape.heightClass == go2::HeightClass::Short, "366dp 属 S 档");
static_assert(!kPhoneLandscape.paramInline, "★ 矮屏内容区只剩 ~154dp → 参数收进折叠区");
static_assert(near(kPhoneLandscape.estopH, 64.0f), "矮屏急停 64dp");
static_assert(near(kPhoneLandscape.joyRadius, 65.88f), "摇杆半径 = min(804,366)*0.18");
static_assert(near(kPhoneLandscape.joyReserve, 177.76f), "摇杆区高度");
static_assert(kPhoneLandscape.screenH - kPhoneLandscape.topBarH - kPhoneLandscape.joyReserve >
                  100.0f,
              "★ 矮屏也要留出可用的内容高度");
static_assert(kPhoneLandscape.contentMaxW <= kPhoneLandscape.viewW, "内容不超可用宽");

// ============================================================ 3. 平板竖屏 768×1024
constexpr go2::LayoutSpec kTabletPortrait =
    go2::makeLayout(768.0f, 1024.0f, true, kAndroidSafe, nullptr);

static_assert(kTabletPortrait.widthClass == go2::WidthClass::Medium, "728dp 属 M 档");
static_assert(kTabletPortrait.heightClass == go2::HeightClass::Tall, "1000dp 属 T 档");
static_assert(kTabletPortrait.showBrand, "平板够宽，品牌名保留");
static_assert(near(kTabletPortrait.topBtnW, 118.0f), "入口按钮触到 118dp 上限");
static_assert(near(kTabletPortrait.contentMaxW, 700.0f), "内容宽 = 728-28");
static_assert(kTabletPortrait.paramInline, "平板竖屏内容区够高 → 参数常显");
static_assert(near(kTabletPortrait.joyRadius, 112.0f), "摇杆半径触到 112dp 上限");
static_assert(near(kTabletPortrait.joyInsetX, 146.4f), "左内缩 = 112*0.95+20+20");
static_assert(near(kTabletPortrait.joyReserve, 270.0f), "摇杆区高度");
static_assert(kTabletPortrait.screenH - kTabletPortrait.topBarH - kTabletPortrait.joyReserve >
                  600.0f,
              "★ 平板竖屏内容区很充裕");

// ============================================================ 4. 平板横屏 1097×686
// ★ 这就是用户那台小米平板的真实值：1920x1200 物理像素 ÷ 密度 1.75
constexpr go2::LayoutSpec kTabletLandscape =
    go2::makeLayout(1097.0f, 686.0f, true, kAndroidSafe, nullptr);

static_assert(kTabletLandscape.widthClass == go2::WidthClass::Expanded, "1057dp 属 E 档");
static_assert(kTabletLandscape.heightClass == go2::HeightClass::Regular, "662dp 属 R 档");
static_assert(near(kTabletLandscape.fontTitle, 23.0f), "E 档字号 23/18/15（只跟宽度档走）");
static_assert(kTabletLandscape.showBrand, "平板横屏保留品牌名");
static_assert(near(kTabletLandscape.topBtnW, 118.0f), "入口按钮 118dp");
// 内容居中且有宽度上限 —— 免得 1097dp 宽把急停按钮拉成一条又长又扁的横条
static_assert(near(kTabletLandscape.contentMaxW, 760.0f), "★ 内容宽上限 760dp");
static_assert(kTabletLandscape.contentMaxW < kTabletLandscape.viewW,
              "★ 宽屏下内容宽度应小于可用宽（靠居中留白，而不是拉满）");
static_assert(near(kTabletLandscape.joyRadius, 112.0f), "摇杆半径 112dp 上限");
static_assert(near(kTabletLandscape.joyInsetX, 146.4f), "左内缩");
static_assert(near(kTabletLandscape.joyCenterY, 528.0f), "圆心高 = 686-24-112-22");
static_assert(near(kTabletLandscape.joyReserve, 270.0f), "摇杆区高度");
static_assert(kTabletLandscape.joyCenterY + kTabletLandscape.joyRadius <
                  kTabletLandscape.screenH,
              "★ 摇杆完全在屏幕内");
// ★★ 这条是上一版真机截图暴露的 bug：内容被摇杆压住。修法是内容区高度扣掉摇杆区，
//    所以必须保证扣完还剩得下东西。
static_assert(kTabletLandscape.screenH - kTabletLandscape.topBarH -
                      kTabletLandscape.joyReserve >
                  300.0f,
              "★ 平板横屏内容区必须留够（上一版这里内容被摇杆压住）");
static_assert(!kTabletLandscape.paramInline,
              "★ 平板横屏内容区只剩 ~358dp → 参数自动收进折叠区");
static_assert(near(kTabletLandscape.popupW, 930.16f), "弹窗宽 = 1057*0.88");
static_assert(near(kTabletLandscape.popupH, 542.8f), "弹窗高 = 662*0.82");
static_assert(kTabletLandscape.popupW <= kTabletLandscape.viewW, "弹窗不超可用宽");
static_assert(kTabletLandscape.popupH <= kTabletLandscape.viewH, "弹窗不超可用高");
static_assert(kTabletLandscape.actCols == 4, "动作库弹窗里排 4 列按钮");

// ============================================================ 5. 桌面 1440×880
constexpr go2::LayoutSpec kDesktop =
    go2::makeLayout(1440.0f, 880.0f, false, kDesktopSafe, nullptr);

static_assert(kDesktop.widthClass == go2::WidthClass::Large, "1440dp 属 L 档");
static_assert(near(kDesktop.topBarH, 46.0f), "桌面顶栏 46dp");
static_assert(near(kDesktop.topBtnH, 30.0f), "桌面按钮 30dp（鼠标更紧凑）");
static_assert(near(kDesktop.btnH, 34.0f), "鼠标：按钮最小高 34dp");
static_assert(near(kDesktop.styleScale, 1.0f), "鼠标：不放大控件间距");
static_assert(near(kDesktop.fontTitle, 23.0f), "桌面字号 23/18/15");
static_assert(near(kDesktop.contentMaxW, 760.0f), "★ 桌面内容同样限宽 760dp，居中显示");
static_assert(near(kDesktop.joyRadius, 112.0f), "桌面摇杆半径 112dp");
static_assert(near(kDesktop.joyInsetX, 126.4f), "左内缩 = 112*0.95+20");
static_assert(near(kDesktop.joyCenterY, 746.0f), "圆心高 = 880-112-22");
static_assert(kDesktop.paramInline, "桌面内容区够高 → 参数常显");
static_assert(near(kDesktop.popupW, 1000.0f), "弹窗宽触到 1000dp 上限");
static_assert(kDesktop.popupW < kDesktop.viewW, "弹窗不超可用宽");

// ============================================================ 6. 断点滞回
constexpr go2::LayoutSpec kPrevLarge =
    go2::makeLayout(1300.0f, 800.0f, false, kDesktopSafe, nullptr);
constexpr go2::LayoutSpec kPrevExpanded =
    go2::makeLayout(1100.0f, 800.0f, false, kDesktopSafe, nullptr);

static_assert(kPrevLarge.widthClass == go2::WidthClass::Large, "1300dp 属 L 档");
static_assert(kPrevExpanded.widthClass == go2::WidthClass::Expanded, "1100dp 属 E 档");
static_assert(go2::makeLayout(1275.0f, 800.0f, false, kDesktopSafe, &kPrevLarge).widthClass ==
                  go2::WidthClass::Large,
              "滞回：刚跌破边界不降级");
static_assert(go2::makeLayout(1285.0f, 800.0f, false, kDesktopSafe, &kPrevExpanded).widthClass ==
                  go2::WidthClass::Expanded,
              "滞回：刚涨过边界不升级");
static_assert(go2::makeLayout(1265.0f, 800.0f, false, kDesktopSafe, &kPrevLarge).widthClass ==
                  go2::WidthClass::Expanded,
              "滞回窗口外正常降级");

// ============================================================ 7. 触摸修正
constexpr go2::LayoutSpec kTouchTablet =
    go2::makeLayout(1097.0f, 686.0f, true, kAndroidSafe, nullptr);
constexpr go2::LayoutSpec kMouseTablet =
    go2::makeLayout(1097.0f, 686.0f, false, kDesktopSafe, nullptr);

static_assert(kTouchTablet.btnH >= 44.0f, "触摸：按钮最小高 ≥44dp");
static_assert(kMouseTablet.btnH == 34.0f, "鼠标：回落到 34dp（更紧凑）");
static_assert(kTouchTablet.styleScale > kMouseTablet.styleScale, "触摸：控件间距放大");
static_assert(kTouchTablet.fontTitle == kMouseTablet.fontTitle,
              "★ 字号只跟宽度档走 —— 同一块屏接鼠标或用手，字号不该变");
static_assert(kTouchTablet.fontTitle == 23.0f, "1097dp 属 E 档 → 23");

// ============================================================ 8. 安全区确实起作用
constexpr go2::LayoutSpec kNoSafe = go2::makeLayout(390.0f, 844.0f, true, kDesktopSafe, nullptr);
static_assert(near(kNoSafe.viewW - kPhonePortrait.viewW, 40.0f), "左右各扣 20dp");
static_assert(near(kNoSafe.viewH - kPhonePortrait.viewH, 24.0f), "底部扣 24dp");
static_assert(kNoSafe.joyCenterY > kPhonePortrait.joyCenterY, "没有底部安全区时摇杆可以更靠下");

// ============================================================ 9. 极端尺寸不崩
constexpr go2::LayoutSpec kTiny = go2::makeLayout(240.0f, 240.0f, true, kAndroidSafe, nullptr);
static_assert(kTiny.viewW >= 200.0f, "极小屏也要有正数可用宽");
static_assert(kTiny.joyRadius >= 64.0f, "摇杆半径有下限");
static_assert(kTiny.popupW >= 300.0f, "弹窗宽有下限");
static_assert(kTiny.actCols >= 1, "动作库至少 1 列");

}  // namespace

int main() {
    std::printf(
        "[layout_test] 断点布局编译期断言全部通过\n"
        "  手机竖屏 390x844  → 顶栏 %.0fdp / 内容 %.0fdp / 摇杆半径 %.1f / 摇杆区 %.0f\n"
        "  手机横屏 844x390  → 摇杆半径 %.1f / 摇杆区 %.1f / 参数常显 %s\n"
        "  平板竖屏 768x1024 → 内容 %.0fdp / 摇杆半径 %.0f / 弹窗 %.0fx%.0f\n"
        "  平板横屏 1097x686→ 内容 %.0fdp（居中）/ 摇杆半径 %.0f / 参数常显 %s\n"
        "  桌面   1440x880   → 顶栏 %.0fdp / 内容 %.0fdp / 弹窗 %.0fx%.0f\n",
        kPhonePortrait.topBarH, kPhonePortrait.contentMaxW, kPhonePortrait.joyRadius,
        kPhonePortrait.joyReserve, kPhoneLandscape.joyRadius, kPhoneLandscape.joyReserve,
        kPhoneLandscape.paramInline ? "是" : "否", kTabletPortrait.contentMaxW,
        kTabletPortrait.joyRadius, kTabletPortrait.popupW, kTabletPortrait.popupH,
        kTabletLandscape.contentMaxW, kTabletLandscape.joyRadius,
        kTabletLandscape.paramInline ? "是" : "否", kDesktop.topBarH, kDesktop.contentMaxW,
        kDesktop.popupW, kDesktop.popupH);
    return 0;
}
