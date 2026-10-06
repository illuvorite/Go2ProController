#pragma once

// ============================================================================
// 界面内部的共享面（ui.cpp 拆分后各 ui_*.cpp 的"内部头"）。
//
// 为什么单独有个头，而不放进 ui.hpp：
//   ui.hpp 是**对外** API（main.cpp / web_bridge.cpp / main_android.cpp 看的那份），
//   里面只有 drawUi / drawJoystickAt / startScan 这类真正的入口。而这一份装的是
//   界面自己的小控件（chip / sectionTitle / iosSlider…）与各页面绘制函数 ——
//   外部根本用不着，堆进 ui.hpp 只会让对外 API 越来越糊。
//
// 拆法（2026-10-06，对应 docs/optimization_plan.md M3-2）：
//   ui.cpp        小控件实现 + drawUi（主循环）
//   ui_chrome.cpp 顶栏 + 急停
//   ui_actions.cpp 动作库页（含图标表）
//   ui_remote.cpp 遥控页
//   ui_popups.cpp 设备 / 设置 / 日志 三个弹窗
//   ui_joystick.cpp 悬浮双摇杆（绘制 + 命中区）
//
// ★ 各 TU 的写法：`#include "ui_internal.hpp"` 后把代码放进 `namespace go2::uix`，
//   函数体**一个字都不用改** —— 因为在 uix 内部，原本的 `chip(...)`、`sectionTitle(...)`
//   这些非限定调用会自动解析到这里声明的函数。
//
// ⚠ 默认参数只写在**这里**（定义处不再重复），否则同一 TU 里重复声明默认值会报错。
// ============================================================================

#include "key_probe.hpp"     // runKeyProbe / applyKeyCandidate（设置页）
#include "layout.hpp"        // LayoutSpec（各 draw* 的入参）
#include "local_keys.hpp"    // defaultKeysTxtPath / ensureParentDir（钥匙落盘位置）
#include "robot_client.hpp"  // ConnState（stateText/stateColor 的入参）
#include "robot_manager.hpp" // RobotManager / RobotClient（各页面要查设备状态）
#include "sport_library.hpp" // SportAction（iconGlyph）
#include "ui.hpp"            // UiState / drawUi 的对外声明

#include <imgui.h>

#include <fstream>
#include <string>

namespace go2 {
namespace uix {

// ---------------------------------------------------------------- 语义色
// 放在头里（而不是某个 .cpp 的匿名命名空间）：多个 TU 都要用。
// constexpr 在头文件里 → 每个 TU 各有一份，无 ODR 问题。
constexpr ImVec4 kGreen{0.35f, 0.85f, 0.45f, 1.0f};
constexpr ImVec4 kRed{0.95f, 0.40f, 0.40f, 1.0f};
constexpr ImVec4 kYellow{0.95f, 0.80f, 0.35f, 1.0f};
constexpr ImVec4 kGray{0.55f, 0.55f, 0.55f, 1.0f};

// ---------------------------------------------------------------- 弹窗 ID
// drawUi 里要用它们 OpenPopup，ui_popups.cpp 里用它们 BeginPopupModal —— 必须同一份，
// 否则界面点"设备"打不开（ID 对不上，而且这种错不报错、只是点了没反应）。
constexpr const char* kIdDevices = "设备##dlg";
constexpr const char* kIdSettings = "设置##dlg";
constexpr const char* kIdLog = "日志##dlg";

// ---------------------------------------------------------------- 小控件
const char* stateText(ConnState s);
ImVec4 stateColor(ConnState s);
bool bigButton(const char* label, const ImVec2& size = ImVec2(0, 34));
bool accentButton(const char* label, const ImVec2& size = ImVec2(0, 32));
void sectionTitle(const char* text);
void statusDot(ImVec4 color, float r = 4.5f);
void chip(const char* text, ImVec4 color);
void batteryBar(float pct, float width = 64.0f, float height = 6.0f);
ImVec4 logColor(const std::string& s);
void readout(const char* label, const char* value, ImVec4 valueColor);
bool iosSliderFloat(const char* label, float* v, float lo, float hi, const char* fmt,
                    float width = 0.0f);
bool iosSliderInt(const char* label, int* v, int lo, int hi, float width = 0.0f);
float iosSliderHeight();
void paramRowF(const char* label, float* v, float lo, float hi, const char* fmt, float def,
               float labelW = 108.0f);
void touchDragScroll(const LayoutSpec& L);
void rollDragFlags();

// ---------------------------------------------------------------- 长按看说明
// 这三个状态被"控件实现"与"页面代码"共享，所以定义在 ui.cpp、这里只声明。
extern bool g_touchUi;          ///< 本帧的输入方式，由 drawUi 每帧写入
extern double g_tipHoldStart;   ///< 本次按住的起点
extern bool g_tipShownInPress;  ///< 本次按住期间是否弹过说明
bool takeTipShown();
void showTipNearItem(const char* text);
void helpTip(const char* text);

// ---------------------------------------------------------------- 小工具
void reloadKeysAndRetry(RobotManager& mgr, UiState& ui,
                        const std::string& path = std::string());
bool isHex32(const std::string& s);
std::string maskIps(const std::string& text, bool on);

// ---------------------------------------------------------------- 设备勾选
// 顶栏、设备弹窗、摇杆带都用这几个；放在这里免得各写一份。
void ensureNamesLoaded(UiState& ui);
void selectGroupAll(UiState& ui);
void selectOne(UiState& ui, const std::string& ip);
void clearSelection(UiState& ui);

// ---------------------------------------------------------------- 动作 → 图标
/// 动作 → Phosphor 字形。动作库瓷砖与底部快捷栏**共用同一份表**（见 ui_actions.cpp）。
const char* iconGlyph(const SportAction& a);

// ---------------------------------------------------------------- 安全动作
/// 急停：停全部就绪设备 + 关掉已开启的持续模式（语义在 cmd::estop）。
void triggerEstop(RobotManager& mgr, UiState& ui);

// ---------------------------------------------------------------- 页面 / 面板
void drawTopBar(RobotManager& mgr, UiState& ui, const LayoutSpec& L);
void drawDeviceList(RobotManager& mgr, UiState& ui);
void drawDevicePanel(RobotManager& mgr, UiState& ui, const LayoutSpec& L);
void drawActionPage(RobotManager& mgr, UiState& ui, const LayoutSpec& L);
void drawSettingsPanel(RobotManager& mgr, UiState& ui, const LayoutSpec& L);
void drawRemotePanel(RobotManager& mgr, UiState& ui, const LayoutSpec& L);
void drawLogPanel(UiState& ui, const LayoutSpec& L);
void drawPopups(RobotManager& mgr, UiState& ui, const LayoutSpec& L);
bool joystickHitArea(const char* id, ImVec2 center, float radius, float* x, float* y);
void drawJoysticks(RobotManager& mgr, UiState& ui, const LayoutSpec& L);

}  // namespace uix
}  // namespace go2
