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
// 设计方向（2026-09-24 第二轮）：界面分上下两块 ——
//   · 上 = 页面区（遥控 / 动作库；动作库是常驻整屏页面，按 pageW 铺满）
//   · 下 = 摇杆带（双摇杆悬浮两下角 + 中间"单控/群控"状态）
// 断点决定：顶栏布局、页面尺寸、动作库列数、摇杆半径与摇杆带高度、弹窗尺寸、字号。
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
// ★ 极窄屏：一行塞不下 8 个按钮 → 顶栏拆两行
static_assert(kPhonePortrait.topTwoRows, "★ C 档顶栏拆两行");
static_assert(!kPhonePortrait.showBrand, "太窄：品牌名让给按钮");
static_assert(near(kPhonePortrait.topBtnH, 44.0f), "触摸按钮高 44dp");
static_assert(near(kPhonePortrait.topBarH, 114.0f), "★ 两行顶栏 = 2*44 + 26 行距");
static_assert(near(kPhonePortrait.topBtnW, 55.6f), "第二行 5 个按钮均分（(318-40)/5）");
static_assert(kPhonePortrait.topBtnW >= 44.0f, "入口按钮不小于 44dp（触摸下限）");
static_assert(near(kPhonePortrait.contentMaxW, 322.0f), "★ 铺满：内容宽 = 可用宽 - 左右内边距");
static_assert(near(kPhonePortrait.btnH, 50.0f), "触摸按钮最小高 50dp ≥ 48dp 下限");
static_assert(near(kPhonePortrait.estopH, 78.0f), "急停竖屏给 78dp（安全项要够大）");
static_assert(kPhonePortrait.paramInline, "竖屏页面区够高 → 参数常显");
static_assert(near(kPhonePortrait.fontTitle, 19.0f), "C 档字号 19/16/13");
// ★ 摇杆：永远悬浮在两下角，且必须完全落在屏幕内、底部让开手势条
static_assert(near(kPhonePortrait.joyRadius, 52.5f), "★ 摇杆半径 = min(350,820)*0.15（比第一版小）");
static_assert(near(kPhonePortrait.joyInsetX, 89.88f), "左内缩 = radius*0.95 + 安全区左 + 20");
static_assert(near(kPhonePortrait.joyCenterY, 737.5f), "圆心高 = 844-24-52.5-30（底部留出标签位置）");
static_assert(near(kPhonePortrait.joyReserve, 159.0f), "摇杆带高 = 2*radius + 标签位 + 底部安全区");
static_assert(kPhonePortrait.joyCenterY + kPhonePortrait.joyRadius <=
                  kPhonePortrait.screenH - kPhonePortrait.safe.bottom,
              "★ 摇杆底边必须让开底部安全区（手势条）");
static_assert(kPhonePortrait.joyInsetX - kPhonePortrait.joyRadius >= kPhonePortrait.safe.left,
              "★ 摇杆左边必须让开左安全区");
// ★★ 关键：页面区（整屏 − 顶栏 − 摇杆带）必须留得下东西
static_assert(kPhonePortrait.pageH > 300.0f, "★ 页面区高度要够（摇杆带不能把内容挤没）");
static_assert(near(kPhonePortrait.pageH, 571.0f), "页面区高 = 844-114-159");
static_assert(kPhonePortrait.joyReserve > kPhonePortrait.joyRadius * 2.0f,
              "★ 摇杆带要盖住整个摇杆");
static_assert(kPhonePortrait.joyCenterY + kPhonePortrait.joyRadius + 30.0f <=
                  kPhonePortrait.screenH - kPhonePortrait.safe.bottom + 0.1f,
              "★ 摇杆下方要留得下标签（真机上底部安全区为 0 时标签曾被裁掉）");
// 两杆之间的空档：状态文字（单控/群控）画在这里，绝不能压到摇杆
static_assert(kPhonePortrait.joyGapMaxX > kPhonePortrait.joyGapMinX,
              "★ 手机竖屏两杆之间还要有空档");
static_assert(near(kPhonePortrait.joyGapMinX, 142.38f), "空档左边界 = 圆心 + 半径");
static_assert(near(kPhonePortrait.joyGapMaxX, 247.62f), "空档右边界");
// ★ 单控 / 群控面板就在这个空档里（窄屏竖排，绝不压到摇杆）
static_assert(near(kPhonePortrait.joyPanelW, 84.0f), "空档太窄 → 面板宽取下限 84dp");
static_assert(kPhonePortrait.joyPanelStack, "★ 手机竖屏：单控/群控 竖排");
static_assert(near(kPhonePortrait.joyPanelH, 140.0f), "竖排 = 2*44 + 6 + 46");
static_assert(kPhonePortrait.joyGapMinX + kPhonePortrait.joyPanelW <=
                  kPhonePortrait.joyGapMaxX,
              "★ 面板不能超出空档（否则会压到摇杆）");
static_assert(kPhonePortrait.popupW <= kPhonePortrait.viewW, "弹窗宽不超可用宽");
static_assert(kPhonePortrait.popupH <= kPhonePortrait.viewH, "弹窗高不超可用高");

// ============================================================ 2. 手机横屏 844×390
constexpr go2::LayoutSpec kPhoneLandscape =
    go2::makeLayout(844.0f, 390.0f, true, kAndroidSafe, nullptr);

static_assert(kPhoneLandscape.widthClass == go2::WidthClass::Medium, "804dp 属 M 档");
static_assert(kPhoneLandscape.heightClass == go2::HeightClass::Short, "366dp 属 S 档");
static_assert(!kPhoneLandscape.topTwoRows, "M 档顶栏一行 6 个按钮");
static_assert(near(kPhoneLandscape.topBtnW, 96.0f), "一行 6 个按钮均分（(772-140-56)/6）");
static_assert(!kPhoneLandscape.paramInline, "★ 矮屏页面区只剩 ~168dp → 参数收进折叠区");
static_assert(near(kPhoneLandscape.estopH, 64.0f), "矮屏急停 64dp");
static_assert(near(kPhoneLandscape.joyRadius, 54.9f), "摇杆半径 = min(804,366)*0.15");
static_assert(near(kPhoneLandscape.joyReserve, 163.8f), "摇杆带高度");
static_assert(kPhoneLandscape.pageH > 160.0f, "★ 矮屏也要留出可用的页面高度");
static_assert(near(kPhoneLandscape.contentMaxW, 776.0f), "★ 铺满：= pageW（804-28）");
static_assert(kPhoneLandscape.contentMaxW <= kPhoneLandscape.viewW, "内容不超可用宽");
static_assert(near(kPhoneLandscape.joyPanelW, 320.0f), "空档够宽 → 面板触到 320dp 上限");
static_assert(!kPhoneLandscape.joyPanelStack, "横屏空档够宽 → 两个按钮并排");
static_assert(near(kPhoneLandscape.joyPanelH, 86.0f), "面板高 = 40 + 46");
static_assert(kPhoneLandscape.popupH <= kPhoneLandscape.viewH, "弹窗高不超可用高");

// ============================================================ 3. 平板竖屏 768×1024
constexpr go2::LayoutSpec kTabletPortrait =
    go2::makeLayout(768.0f, 1024.0f, true, kAndroidSafe, nullptr);

static_assert(kTabletPortrait.widthClass == go2::WidthClass::Medium, "728dp 属 M 档");
static_assert(kTabletPortrait.heightClass == go2::HeightClass::Tall, "1000dp 属 T 档");
static_assert(kTabletPortrait.showBrand, "平板够宽，品牌名保留");
static_assert(near(kTabletPortrait.topBtnW, 83.33f), "6 个按钮均分（(696-140-56)/6）");
static_assert(near(kTabletPortrait.contentMaxW, 700.0f), "★ 铺满：内容宽 = 728-28");
static_assert(near(kTabletPortrait.joyPanelW, 320.0f), "面板宽上限 320dp");
static_assert(near(kTabletPortrait.joyPanelH, 90.0f), "面板高 = 44 + 46");
static_assert(kTabletPortrait.paramInline, "平板竖屏页面区够高 → 参数常显");
static_assert(near(kTabletPortrait.joyRadius, 88.0f), "★ 摇杆半径触到 88dp 上限（第一版是 112）");
static_assert(near(kTabletPortrait.joyInsetX, 123.6f), "左内缩 = 88*0.95+20+20");
static_assert(near(kTabletPortrait.joyReserve, 230.0f), "摇杆带高度");
static_assert(near(kTabletPortrait.pageH, 736.0f), "页面区高 = 1024-58-230");
static_assert(kTabletPortrait.pageH > 600.0f, "★ 平板竖屏页面区很充裕");

// ============================================================ 4. 平板横屏 1097×686
// ★ 这就是用户那台小米平板的真实值：1920x1200 物理像素 ÷ 密度 1.75
constexpr go2::LayoutSpec kTabletLandscape =
    go2::makeLayout(1097.0f, 686.0f, true, kAndroidSafe, nullptr);

static_assert(kTabletLandscape.widthClass == go2::WidthClass::Expanded, "1057dp 属 E 档");
static_assert(kTabletLandscape.heightClass == go2::HeightClass::Regular, "662dp 属 R 档");
static_assert(near(kTabletLandscape.fontTitle, 23.0f), "E 档字号 23/18/15（只跟宽度档走）");
static_assert(kTabletLandscape.showBrand, "平板横屏保留品牌名");
static_assert(near(kTabletLandscape.topBtnW, 118.0f), "顶栏 6 个按钮均分 → 触到 118dp 上限");
// ★ 铺满：遥控页与动作库页**同宽**（都不再居中限宽）
static_assert(near(kTabletLandscape.pageW, 1029.0f), "★ 页面宽 = 可用宽 - 左右内边距");
static_assert(near(kTabletLandscape.contentMaxW, 1029.0f), "★ 铺满：遥控页内容也用满 pageW");
static_assert(near(kTabletLandscape.actAreaW, 1005.0f), "动作库网格可用宽");
static_assert(near(kTabletLandscape.joyPanelW, 320.0f), "单控/群控面板宽 320dp");
static_assert(near(kTabletLandscape.joyPanelH, 90.0f), "面板高 = 44 + 46");
static_assert(near(kTabletLandscape.joyRadius, 88.0f), "摇杆半径 88dp 上限");
static_assert(near(kTabletLandscape.joyInsetX, 123.6f), "左内缩");
static_assert(near(kTabletLandscape.joyCenterY, 544.0f), "圆心高 = 686-24-88-30");
static_assert(near(kTabletLandscape.joyReserve, 230.0f), "摇杆带高度（第一版 278 → 现在 230）");
static_assert(kTabletLandscape.joyCenterY + kTabletLandscape.joyRadius <
                  kTabletLandscape.screenH,
              "★ 摇杆完全在屏幕内");
// ★★ 内容不能被摇杆压住：页面区高度扣掉摇杆带，所以必须保证扣完还剩得下东西。
static_assert(near(kTabletLandscape.pageH, 398.0f), "页面区高 = 686-58-230");
static_assert(kTabletLandscape.pageH > 300.0f, "★ 页面区必须留够");
static_assert(!kTabletLandscape.paramInline, "★ 页面区只剩 ~398dp → 参数自动收进折叠区");
static_assert(near(kTabletLandscape.popupW, 930.16f), "弹窗宽 = 1057*0.88");
static_assert(near(kTabletLandscape.popupH, 595.8f), "★ 弹窗高 = 662*0.90（要盖住摇杆带）");
static_assert(kTabletLandscape.popupW <= kTabletLandscape.viewW, "弹窗不超可用宽");
static_assert(kTabletLandscape.popupH <= kTabletLandscape.viewH, "弹窗不超可用高");
static_assert(kTabletLandscape.actCols == 8, "★ 动作库铺满整屏 → 排 8 列按钮");
static_assert(near(kTabletLandscape.actW1, 118.63f), "列宽 = (1005 - 7*8)/8");
static_assert(kTabletLandscape.actW1 >= 118.0f, "触摸列宽不低于 118dp 下限");

// ============================================================ 5. 桌面 1440×880
constexpr go2::LayoutSpec kDesktop =
    go2::makeLayout(1440.0f, 880.0f, false, kDesktopSafe, nullptr);

static_assert(kDesktop.widthClass == go2::WidthClass::Large, "1440dp 属 L 档");
static_assert(near(kDesktop.topBarH, 46.0f), "桌面顶栏 46dp（一行 8 个按钮）");
static_assert(near(kDesktop.topBtnH, 30.0f), "桌面按钮 30dp（鼠标更紧凑）");
static_assert(near(kDesktop.topBtnW, 118.0f), "桌面按钮触到 118dp 上限");
static_assert(near(kDesktop.btnH, 34.0f), "鼠标：按钮最小高 34dp");
static_assert(near(kDesktop.styleScale, 1.0f), "鼠标：不放大控件间距");
static_assert(near(kDesktop.fontTitle, 23.0f), "桌面字号 23/18/15");
static_assert(near(kDesktop.contentMaxW, 1404.0f), "★ 铺满：桌面遥控页也用满（1440-36）");
static_assert(near(kDesktop.pageW, 1404.0f), "★ 页面铺满（1440-36）");
static_assert(near(kDesktop.sliderW, 560.0f), "滑条宽上限 560dp（铺满后别拉太长）");
static_assert(near(kDesktop.joyPanelW, 320.0f), "单控/群控面板宽 320dp");
static_assert(near(kDesktop.joyPanelH, 68.0f), "面板高 = 30 + 38");
static_assert(near(kDesktop.joyRadius, 88.0f), "★ 桌面摇杆半径 88dp（第一版 112）");
static_assert(near(kDesktop.joyInsetX, 103.6f), "左内缩 = 88*0.95+20");
static_assert(near(kDesktop.joyCenterY, 762.0f), "圆心高 = 880-88-30");
static_assert(near(kDesktop.joyReserve, 206.0f), "摇杆带高 = 2*88+30");
static_assert(kDesktop.paramInline, "桌面页面区够高 → 参数常显");
static_assert(near(kDesktop.popupW, 1000.0f), "弹窗宽触到 1000dp 上限");
static_assert(near(kDesktop.popupH, 792.0f), "弹窗高 = 880*0.90");
static_assert(kDesktop.popupW < kDesktop.viewW, "弹窗不超可用宽");
static_assert(kDesktop.actCols == 8, "桌面动作库也排 8 列");

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
static_assert(kTiny.joyRadius >= 52.0f, "摇杆半径有下限");
static_assert(kTiny.joyGapMaxX >= kTiny.joyGapMinX, "★ 空档边界不许反号");
static_assert(kTiny.popupW >= 300.0f, "弹窗宽有下限");
static_assert(kTiny.pageH >= 160.0f, "★ 页面区有下限（宁可挤，不能为负）");
static_assert(kTiny.actCols >= 1, "动作库至少 1 列");

}  // namespace

int main() {
    std::printf(
        "[layout_test] 断点布局编译期断言全部通过\n"
        "  手机竖屏 390x844  → 顶栏 %.0fdp(两行) / 页面 %.0fdp / 摇杆半径 %.1f / 摇杆带 %.0f\n"
        "  手机横屏 844x390  → 摇杆半径 %.1f / 摇杆带 %.1f / 参数常显 %s\n"
        "  平板竖屏 768x1024 → 页面 %.0fdp / 摇杆半径 %.0f / 弹窗 %.0fx%.0f\n"
        "  平板横屏 1097x686→ 页面 %.0fdp（铺满）/ 摇杆半径 %.0f / 动作库 %d 列 / 参数常显 %s\n"
        "  桌面   1440x880   → 顶栏 %.0fdp / 页面 %.0fdp（铺满） / 单控群控面板 %.0fx%.0f / 动作库 %d 列\n",
        kPhonePortrait.topBarH, kPhonePortrait.pageH, kPhonePortrait.joyRadius,
        kPhonePortrait.joyReserve, kPhoneLandscape.joyRadius, kPhoneLandscape.joyReserve,
        kPhoneLandscape.paramInline ? "是" : "否", kTabletPortrait.pageH,
        kTabletPortrait.joyRadius, kTabletPortrait.popupW, kTabletPortrait.popupH,
        kTabletLandscape.pageW, kTabletLandscape.joyRadius, kTabletLandscape.actCols,
        kTabletLandscape.paramInline ? "是" : "否", kDesktop.topBarH, kDesktop.pageW,
        kDesktop.joyPanelW, kDesktop.joyPanelH, kDesktop.actCols);
    return 0;
}
