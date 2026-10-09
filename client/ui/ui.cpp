#include "ui.hpp"
#include "ui_internal.hpp"

#include "command_service.hpp"
#include "discovery.hpp"
#include "icons.hpp"
#include "textures.hpp"
#include "key_probe.hpp"
#include "robot_client.hpp"
#include "robot_manager.hpp"
#include "sport_library.hpp"
#include "local_keys.hpp"
#include "theme.hpp"

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <regex>
#include <cstring>
#include <fstream>
#include <functional>
#include <thread>

namespace go2 {


namespace uix {


const char* stateText(ConnState s) {
    switch (s) {
        case ConnState::Disconnected: return "未连接";
        case ConnState::Signaling:    return "信令中";
        case ConnState::Connecting:   return "连接中";
        case ConnState::Validating:   return "校验中";
        case ConnState::Ready:        return "就绪";
        case ConnState::Failed:       return "失败";
    }
    return "?";
}

ImVec4 stateColor(ConnState s) {
    switch (s) {
        case ConnState::Ready:  return kGreen;
        case ConnState::Failed: return kRed;
        case ConnState::Disconnected: return kGray;
        default:                return kYellow;
    }
}

/// 大按钮：竖向渐变 + 顶边高光 + 底部投影（ImGui 原生 Button 是一块死平色，毫无质感）。
/// 做法：先 ImGui::Button（拿到底色/悬停/按下的配色），再用 draw list **叠画**
/// "上白下黑"的竖向渐变 + 顶边 1px 高光；渐变压得很淡，按钮上原有的文字不受影响。
bool bigButton(const char* label, const ImVec2& size) {
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::Button(label, size);
    const ImVec2 p1 = ImGui::GetItemRectMax();
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rounding = ImGui::GetStyle().FrameRounding;
    const ImVec2 c0(p0.x + 2.0f, p0.y + 2.0f), c1(p1.x - 2.0f, p1.y - 2.0f);
    // 顶亮底暗的竖向渐变（叠在按钮底色上 → 立体感）
    dl->AddRectFilledMultiColor(c0, c1, IM_COL32(255, 255, 255, 24), IM_COL32(255, 255, 255, 24),
                                IM_COL32(0, 0, 0, 40), IM_COL32(0, 0, 0, 40));
    // 顶边高光 1px（悬停更亮）
    dl->AddLine(ImVec2(c0.x + 6.0f, c0.y + 0.5f), ImVec2(c1.x - 6.0f, c0.y + 0.5f),
                ImGui::GetColorU32(ImVec4(1, 1, 1, hovered ? 0.34f : 0.16f)), 1.0f);
    // 投影：只画按钮**下方**（画在上方会盖住按钮本体；按下时不画 → "按进去了"）
    if (!pressed) {
        for (int i = 3; i >= 1; --i) {
            const float o = static_cast<float>(i) * 1.6f;
            dl->AddRectFilled(ImVec2(p0.x + o * 0.5f, p1.y - 1.0f),
                              ImVec2(p1.x - o * 0.5f, p1.y + o),
                              ImGui::GetColorU32(ImVec4(0, 0, 0, 0.075f)), rounding);
        }
    }
    return pressed;
}

/// 主色按钮（强调操作用）
bool accentButton(const char* label, const ImVec2& size) {
    ImGui::PushStyleColor(ImGuiCol_Button,
                          ImVec4(col::kAccent.x, col::kAccent.y, col::kAccent.z, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.40f, 0.65f, 1.00f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.55f, 0.75f, 1.00f, 1.00f));
    const bool r = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return r;
}

/// 段落标题：左侧主色竖条 + 标题字号（small=true 用正文字号）
void sectionTitle(const char* text) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float lh = ImGui::GetTextLineHeight();
    dl->AddRectFilled(ImVec2(p.x, p.y + 2.0f), ImVec2(p.x + 3.0f, p.y + lh - 2.0f),
                      ImGui::GetColorU32(col::kAccent), 2.0f);
    ImGui::Dummy(ImVec2(3.0f, 0.0f));
    ImGui::SameLine(0.0f, 9.0f);
    FontScope fs = fontTitle();
    ImGui::TextUnformatted(text);
}

/// 圆形状态灯（带柔和光晕）
void statusDot(ImVec4 color, float r) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 c(p.x + r + 2.0f, p.y + ImGui::GetTextLineHeight() * 0.5f);
    dl->AddCircleFilled(c, r + 2.5f,
                        ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, 0.20f)));
    dl->AddCircleFilled(c, r, ImGui::GetColorU32(color));
    ImGui::Dummy(ImVec2(r * 2.0f + 5.0f, ImGui::GetTextLineHeight()));
}

/// 圆角胶囊标签
void chip(const char* text, ImVec4 color) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    FontScope fs = fontSmall();
    const ImVec2 ts = ImGui::CalcTextSize(text);
    const ImVec2 pad(9.0f, 3.0f);
    const ImVec2 sz(ts.x + pad.x * 2.0f, ts.y + pad.y * 2.0f);
    const ImVec2 b(p.x + sz.x, p.y + sz.y);
    dl->AddRectFilled(p, b, ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, 0.16f)),
                      sz.y * 0.5f);
    dl->AddRect(p, b, ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, 0.50f)), sz.y * 0.5f,
                0, 1.0f);
    dl->AddText(ImVec2(p.x + pad.x, p.y + pad.y), ImGui::GetColorU32(color), text);
    ImGui::Dummy(sz);
}

/// 电量条（pct < 0 表示未知）
void batteryBar(float pct, float width, float height) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float y = p.y + (ImGui::GetTextLineHeight() - height) * 0.5f;
    const ImVec2 a(p.x, y), b(p.x + width, y + height);
    dl->AddRectFilled(a, b, ImGui::GetColorU32(ImVec4(1, 1, 1, 0.10f)), height * 0.5f);
    if (pct >= 0.0f) {
        const float w = width * (pct > 100.0f ? 1.0f : pct / 100.0f);
        const ImVec4 cc = pct < 20.0f ? col::kErr : (pct < 40.0f ? col::kWarn : col::kOk);
        dl->AddRectFilled(a, ImVec2(a.x + w, b.y), ImGui::GetColorU32(cc), height * 0.5f);
    }
    ImGui::Dummy(ImVec2(width, ImGui::GetTextLineHeight()));
}

/// 日志分级着色：一眼区分"成功 / 失败 / 指令 / 提示"
ImVec4 logColor(const std::string& s) {
    if (s.find("失败") != std::string::npos || s.find("[错误]") != std::string::npos ||
        s.find("WARN") != std::string::npos || s.find("异常") != std::string::npos ||
        s.find("超时") != std::string::npos || s.find("掉线") != std::string::npos)
        return col::kErr;
    if (s.find("[急停]") != std::string::npos) return ImVec4(1.00f, 0.57f, 0.25f, 1.00f);
    if (s.find("成功") != std::string::npos || s.find("就绪") != std::string::npos ||
        s.find("已打开") != std::string::npos)
        return col::kOk;
    if (s.find("[回执]") != std::string::npos) return ImVec4(0.62f, 0.78f, 0.98f, 1.00f);
    if (s.find("[指令]") != std::string::npos) return ImVec4(0.74f, 0.78f, 0.86f, 1.00f);
    if (s.find("[UI]") != std::string::npos || s.find("[动作库]") != std::string::npos ||
        s.find("[App]") != std::string::npos || s.find("[扫描]") != std::string::npos)
        return col::kDim;
    return col::kText;
}

/// 指令读数框（深色内嵌面板，用于显示当前速度指令等）
void readout(const char* label, const char* value, ImVec4 valueColor) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    const float h = ImGui::GetTextLineHeight() + 14.0f;
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(ImVec4(0, 0, 0, 0.28f)),
                      7.0f);
    dl->AddRect(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(ImVec4(1, 1, 1, 0.07f)), 7.0f, 0,
                1.0f);
    {
        FontScope fs = fontSmall();
        dl->AddText(ImVec2(p.x + 11.0f, p.y + 7.0f), ImGui::GetColorU32(col::kDim), label);
    }
    {
        const ImVec2 ts = ImGui::CalcTextSize(value);
        dl->AddText(ImVec2(p.x + w - ts.x - 11.0f, p.y + 7.0f), ImGui::GetColorU32(valueColor),
                    value);
    }
    ImGui::Dummy(ImVec2(w, h));
}

/// 拖动型控件（滑条）是否正被拖动：是 → 手指拖动用于调值，**不滚屏**。
/// 用"上一帧的标记"：touchDragScroll 在窗口开头调用，而滑条是在之后才画的。
/// （声明必须在 touchDragScroll / iosSliderFloat 之前）
bool g_valueDragPrev = false;
bool g_valueDragCur = false;
/// 由 drawUi 每帧开头调用：把本帧的标记挪到"上一帧"、清空本帧的
void rollDragFlags() {
    g_valueDragPrev = g_valueDragCur;
    g_valueDragCur = false;
}

// ---------------------------------------------------------------- 苹果风格控件
// 用户要"苹果风格"的滑条 —— iOS 那条滑条的**标志性观感**是三件事：
//   ① 轨道**细**（约字号的一半高），两端半圆
//   ② 旋钮**比轨道大一圈**、像一颗悬浮在轨道上的白色圆点（带一点柔和投影）
//   ③ 左侧已填充段与轨道**同高同圆角**，右端停在旋钮圆心（被旋钮压住，看不出接缝）
// 旧版把轨道做成"和旋钮一样粗的一条"（灰条/蓝条里塞个球），看起来像进度条不像滑条 → 现在改掉。
//
// ⚠ 为什么轨道/填充/投影都要自己画：ImGui 原生滑条只画一个"旋钮"矩形 + 整块 FrameBg，
//   并不画左边已填充的部分（见 imgui_widgets.cpp::SliderBehaviorT 的 out_grab_bb）。
//   所以：FrameBg 全设成**透明**（让 ImGui 那根粗条消失），滑条本体只贡献"白色圆旋钮 + 拖动交互"。
bool iosSliderFloat(const char* label, float* v, float lo, float hi, const char* fmt,
                    float width) {
    const float fontSize = ImGui::GetFontSize();
    const float knob = fontSize * 1.5f;                     // 旋钮直径：比轨道大一圈
    const float trackH = std::max(8.0f, fontSize * 0.52f);  // 轨道高：细
    // ImGui 的旋钮跨轴尺寸 = 帧高 − 4（grab_padding 固定 2）→ 用 FramePadding 把帧高撑到"旋钮+4"
    const float padY = std::max(2.0f, (knob + 4.0f - fontSize) * 0.5f);
    const float frameH = fontSize + padY * 2.0f;
    const float frameW = (width > 0.0f) ? width : ImGui::CalcItemWidth();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1(p0.x + frameW, p0.y + frameH);
    const bool hovering = ImGui::IsMouseHoveringRect(p0, p1);

    float t = (hi > lo) ? ((*v - lo) / (hi - lo)) : 0.0f;
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    const float cy = p0.y + frameH * 0.5f;               // 轨道与旋钮同一水平中心
    const float xMin = p0.x + 2.0f + knob * 0.5f;        // 旋钮圆心行程（与 ImGui 内部一致）
    const float xMax = p1.x - 2.0f - knob * 0.5f;
    const float kx = xMin + t * std::max(0.0f, xMax - xMin);
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float ty0 = cy - trackH * 0.5f;
        const float ty1 = cy + trackH * 0.5f;
        // 轨道（细胶囊；悬停时略亮一点，给一点反馈）
        dl->AddRectFilled(ImVec2(p0.x, ty0), ImVec2(p1.x, ty1),
                          ImGui::GetColorU32(ImVec4(1, 1, 1, hovering ? 0.17f : 0.11f)),
                          trackH * 0.5f);
        // 已填充段（主色，与轨道等高同圆角，右端停在旋钮圆心）
        if (kx > p0.x + 1.0f)
            dl->AddRectFilled(ImVec2(p0.x, ty0), ImVec2(kx, ty1),
                              ImGui::GetColorU32(ImVec4(col::kAccent.x, col::kAccent.y,
                                                        col::kAccent.z, 0.95f)),
                              trackH * 0.5f);
        // 旋钮投影：ImGui 没有模糊，用几层低透明度同心圆近似 iOS 那种柔和阴影
        for (int i = 3; i >= 1; --i)
            dl->AddCircleFilled(ImVec2(kx, cy), knob * 0.5f + static_cast<float>(i) * 1.7f,
                                ImGui::GetColorU32(ImVec4(0, 0, 0, 0.05f)), 28);
    }

    // ---- 滑条本体：只留白色圆旋钮 ----
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, padY));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 999.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, knob);  // ≥ 半径 → 正圆
    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, knob);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));  // 轨道自己画了 → 透明
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(1.0f, 1.0f, 1.0f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::SetNextItemWidth(frameW);
    // 格式给一个空格：ImGui 自己的居中数值不要（数值由调用方画在轨道外侧，见 paramRowF）。
    // NoInput：Ctrl+点 的临时输入框会以"空"起步（回车会把值压到下限）→ 关掉。
    const bool changed = ImGui::SliderFloat(label, v, lo, hi, " ", ImGuiSliderFlags_NoInput);
    // 记下"正在拖滑条"：这一下的手指拖动不该变成页面滚动（见 touchDragScroll）
    if (ImGui::IsItemActive()) g_valueDragCur = true;
    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar(5);

    (void)fmt;  // 数值文本由调用方画（轨道变细之后，字压在轨道里就没法看了）
    return changed;
}

/// 苹果滑条占的高度（同一行里给数值文字 / 按钮做垂直居中对齐用）。
/// 与 iosSliderFloat 里的帧高算法保持一致：帧高 = 字号 + 2*padY，padY 由"旋钮+4"反推。
float iosSliderHeight() {
    const float fs = ImGui::GetFontSize();
    const float knob = fs * 1.5f;
    const float padY = std::max(2.0f, (knob + 4.0f - fs) * 0.5f);
    return fs + padY * 2.0f;
}

/// 整数滑条：内部仍走**浮点**滑条（ImGui 的整数滑条会把旋钮拉成一个长条：
/// grab_sz = 行程/(取值范围+1)，见 SliderBehaviorT）—— 这里只把结果四舍五入回整数。
bool iosSliderInt(const char* label, int* v, int lo, int hi, float width) {
    float f = static_cast<float>(*v);
    const bool changed =
        iosSliderFloat(label, &f, static_cast<float>(lo), static_cast<float>(hi), "%.0f", width);
    if (changed) {
        const int nv = static_cast<int>(f + 0.5f);
        *v = nv < lo ? lo : (nv > hi ? hi : nv);
    }
    return changed;
}

/// 参数行（苹果风格）：左边标签，右边滑条撑满剩余宽度（宽屏不再留一大片空白）。
/// ⚠ 滑条的 ID 由**标签派生**（`##线速度上限` 之类）—— 一开始这里图省事三个滑条都用 "##p"，
///   结果 ImGui 报 "3 visible items with conflicting ID" 并在界面上弹红框
///   （io.ConfigDebugHighlightIdConflicts 在这个版本的 ImGui 里**默认开着**，Release 也照报）。
///   凡是同一窗口里重复使用的控件，标签必须带上区分度，或用 PushID 包一层。
void paramRowF(const char* label, float* v, float lo, float hi, const char* fmt, float def,
               float labelW) {
    ImGui::TextUnformatted(label);
    ImGui::SameLine(labelW);
    const float resetW = ImGui::CalcTextSize("重置").x + 22.0f;
    // 数值单独占一栏（固定宽度，居中）→ 拖动时数字不会左右跳，滑条右端也照样对齐
    char buf[64];
    std::snprintf(buf, sizeof(buf), fmt, *v);
    const float valW = std::max(74.0f, ImGui::CalcTextSize(buf).x + 10.0f);
    const std::string id = std::string("##") + label;
    iosSliderFloat(id.c_str(), v, lo, hi, fmt,
                   ImGui::GetContentRegionAvail().x - resetW - valW - 18.0f);
    ImGui::SameLine();
    {
        FontScope fs = fontBody();
        const ImVec2 ts = ImGui::CalcTextSize(buf);
        const ImVec2 cur = ImGui::GetCursorPos();
        // 与滑条垂直居中（滑条比文字高，不对齐会显得"数字浮在上面"）
        ImGui::SetCursorPos(ImVec2(cur.x + std::max(0.0f, (valW - ts.x) * 0.5f),
                                   cur.y + (iosSliderHeight() - ts.y) * 0.5f));
        ImGui::TextUnformatted(buf);
    }
    ImGui::SameLine();
    if (ImGui::Button((std::string("重置##") + label).c_str(), ImVec2(resetW, 0.0f)))
        *v = def;  // 恢复默认值（helpTip 定义在后面，这里不调）
}

/// ★ 手指按住拖动 = 滚动当前窗口。
/// 用户实测反馈：平板上只有按住最右边的滚动条才能往下滑，太别扭；
/// 应该是"在任意位置按住屏幕就能滑"。ImGui 自己没有拽动滚动（只支持滚轮），
/// 所以这里补上：在**每个可滚动窗口的开头**调一次。
/// 只在触摸输入下生效（鼠标拖拽另有含义，别抢）。
/// ⚠ 这里**不能用 `IsAnyItemActive()` 当守卫**：手指按到按钮上按钮就会 Active，
///   而整页都是按钮 → 实测"按住屏幕滑不动"（第一次真机验证就是这么失败的）。
///   改成只看"拖动型控件（滑条）是否在拖"—— 用上一帧的标记（滑条在本函数之后才画）。
void touchDragScroll(const LayoutSpec& L) {
    if (!L.touch) return;
    if (g_valueDragPrev) return;  // 正在拖滑条 → 这一下是要调值，不是滚屏
    // ChildWindows：设备卡片那种"不滚动的小子窗"上按住也要能滚**本窗口**
    if (!ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) return;
    if (!ImGui::IsMouseDragging(ImGuiMouseButton_Left)) return;
    const float dy = ImGui::GetIO().MouseDelta.y;
    if (dy == 0.0f) return;
    ImGui::SetScrollY(ImGui::GetScrollY() - dy);
}

// ---------------------------------------------------------------- 操作提示
// 鼠标：悬停即弹 tooltip（和原来一样）。
// 触摸：手指按住控件时正好把 tooltip 挡住（真机上基本看不到），所以改成
//      **按住 0.6 秒**才弹，并且弹在控件上方。
// ★ 关键：看过说明的那一下**不再算点击** —— 否则"想看说明"就变成"执行操作"了
//   （对急停/阻尼这类安全项尤其不能忍）。用法：
//       const bool hit = ImGui::Button(...);
//       helpTip("说明");
//       if (hit && !takeTipShown()) { ...真正执行... }
bool g_touchUi = false;          ///< 本帧的输入方式，由 drawUi 每帧写入
double g_tipHoldStart = 0.0;     ///< 本次按住的起点
bool g_tipShownInPress = false;  ///< 本次按住期间是否弹过说明

/// 取走"本次按住弹过说明"的标记（取走后清空，每次只生效一次）
bool takeTipShown() {
    const bool v = g_tipShownInPress;
    g_tipShownInPress = false;
    return v;
}

/// 把提示画在控件**上方**（手指在控件上，上方才看得见）；上方不够就改画下方
void showTipNearItem(const char* text) {
    const ImVec2 mn = ImGui::GetItemRectMin();
    const ImVec2 mx = ImGui::GetItemRectMax();
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float fs = ImGui::GetFontSize();
    float y = mn.y - 10.0f;
    ImVec2 pivot(0.0f, 1.0f);                  // pivot 在左下 → 窗口长在控件上方
    if (y < fs * 5.0f) {                       // 上方放不下 → 改画到控件下方
        y = mx.y + 10.0f;
        pivot = ImVec2(0.0f, 0.0f);
    }
    const float maxW = fs * 26.0f;
    const float x = std::min(mn.x, std::max(0.0f, ds.x - maxW - fs * 2.0f));
    ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always, pivot);
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(maxW);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

/// 统一的操作提示：鼠标悬停显示；触摸下长按 0.6 秒显示
void helpTip(const char* text) {
    if (!ImGui::IsItemHovered()) {
        g_tipHoldStart = 0.0;
        g_tipShownInPress = false;  // 指针移开 → 标记作废，避免影响下一次点击
        return;
    }
    if (!g_touchUi) {
        ImGui::SetTooltip("%s", text);
        return;
    }
    if (ImGui::IsItemActive()) {
        const double now = ImGui::GetTime();
        if (g_tipHoldStart <= 0.0) {  // 新一次按住：重新计时并清掉上次的标记
            g_tipHoldStart = now;
            g_tipShownInPress = false;
        }
        if (now - g_tipHoldStart >= 0.6) {
            showTipNearItem(text);
            g_tipShownInPress = true;
        }
    } else {
        g_tipHoldStart = 0.0;
    }
}

// 注：群控循环（原 forEachSelected）、急停、动作分发都搬到了 ui/command_service.*，
//     桌面端与网页端共用同一份语义 —— 这里不再有第二份实现。

/// 重载本地钥匙库并对处于失败状态的机器狗自动重连。
///
/// 主位置是**应用数据目录**的 `keys.txt`（`~/.go2/keys.txt`）—— 敏感文件不进工程目录；
/// 同时兼容读一份老的 `./keys.txt`（老版本写在当前工作目录里），读一次即可。
void reloadKeysAndRetry(RobotManager& mgr, UiState& ui, const std::string& path) {
    const std::string main = path.empty() ? defaultKeysTxtPath() : path;
    std::vector<std::string> keys = loadLocalKeysFile(main);
    if (path.empty()) {
        // 兼容旧位置：工作目录里的 keys.txt（去重合并）
        for (const auto& k : loadLocalKeysFile("keys.txt"))
            if (std::find(keys.begin(), keys.end(), k) == keys.end()) keys.push_back(k);
    }
    mgr.addAesKeys(keys);  // 与云账号钥匙合并
    ui.addLog("[钥匙] 本地钥匙库已装载 " + std::to_string(keys.size()) + " 把（累计 " +
              std::to_string(mgr.aesKeys().size()) + " 把）");
    mgr.reconnectFailed();
}

/// 校验 32 位 hex
bool isHex32(const std::string& s) {
    if (s.size() != 32) return false;
    for (char c : s) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F')))
            return false;
    }
    return true;
}


/// 隐私模式：把 IPv4 的中间两段打码（192.168.0.169 -> 192.168.*.***）
std::string maskIps(const std::string& text, bool on) {
    if (!on || text.empty()) return text;
    static const std::regex ipRe(R"((\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3}))");
    return std::regex_replace(text, ipRe, "$1.$2.*.***");
}

// ============================================================================
// 界面：上下两块 —— 上「页面区」（遥控 / 动作库）+ 下「摇杆带」
//
// ★ 设计要点（2026-09-24 第二轮，按用户反馈重做）：
//   · 动作库改成**常驻整屏页面**（不再弹窗）：顶栏页签切换，按 pageW 铺满，
//     一排排按钮平铺（不折叠）；高度扣掉摇杆带 → 与摇杆区**明确分上下**
//   · 双摇杆**永远悬浮在屏幕两个下角**，半径比上一版缩小（短边 0.15、上限 88dp），
//     把纵向空间让给动作库；两杆中间的空档显示"指令发给谁"（单控 / 群控）
//   · 设备 / 设置 / 日志 仍是弹窗，打开时**盖住摇杆带**（摇杆那一帧不画）
//   · 顶栏：页签（遥控 / 动作库）+ 单控 / 群控 + 设备 / 设置 / 日志 + ■ 急停
// ============================================================================

// ---------------------------------------------------------------- 顶栏与全局操作
/// 持久化的界面状态（名称 + 设备列表）只在第一次画界面时读一次
///（桌面与安卓共用这条路径，入口不用各自记得初始化）
void ensureNamesLoaded(UiState& ui) {
    static bool loaded = false;
    if (loaded) return;
    loaded = true;
    ui.loadNames();     // robot_names.json  —— 设备名
    ui.loadDevices();   // robot_devices.json —— 设备列表（否则重启/重建后列表清空）
}

/// 群控：全选（指令只发给已就绪的）
void selectGroupAll(UiState& ui) {
    const int n = ui.selectAll();
    ui.addLog("[群控] 全选 " + std::to_string(n) + " 台（指令只发给已就绪的）");
}

/// 单控：只控制这一台（其余全部取消勾选）
void selectOne(RobotManager& mgr, UiState& ui, const std::string& ip) {
    const std::string nm = ui.nameOf(ip);
    // ★ 先记下"现在受控的是谁"：Go2 的速度是保持型的，光改 selected 不会让狗停下，
    //   而 stopSelected 只遍历**新的**集合 → 被移出去那台会一直走、且再也停不到。
    const std::vector<std::string> before = ui.selectedIps();
    ui.selectOnly(ip);
    ManagerSink sink(mgr, ui);
    const int stopped = cmd::stopDeselected(sink, before);
    ui.addLog("[单控] 只控制 " + ip + (nm.empty() ? "" : "（" + nm + "）"));
    if (stopped > 0)
        ui.addLog("[单控] 移出受控的 " + std::to_string(stopped) + " 台已停车");
}

/// 取消单控：谁都不控制（摇杆 / 动作 / 快捷都不再发给任何设备）
void clearSelection(RobotManager& mgr, UiState& ui) {
    if (ui.selectedCount() == 0) return;
    const std::vector<std::string> before = ui.selectedIps();
    ui.selectOnly("");  // 没有哪台的 ip 是空串 → 等于全部取消勾选
    ManagerSink sink(mgr, ui);
    const int stopped = cmd::stopDeselected(sink, before);
    ui.addLog("[单控] 已取消：当前不控制任何设备（再点「单控」挑一台即可恢复）");
    if (stopped > 0)
        ui.addLog("[单控] 移出受控的 " + std::to_string(stopped) + " 台已停车");
}
}  // namespace uix

using namespace uix;

// ============================================================ 主界面
void drawUi(RobotManager& mgr, UiState& ui) {
    ui.safetyRectCount = 0;   // 安全区矩形每帧重登（急停/阻尼/摇杆带面板，见 addSafetyRect）
    rollDragFlags();          // 拖动型控件标记翻帧（touchDragScroll 要用上一帧的）
    ensureNamesLoaded(ui);    // 名字文件只读一次（桌面 / 安卓共用这条路径）

    // ---- 姿态角到点自动归零（安全项）----
    // ★ 为什么必须放在 drawUi 顶部、而不是动作库那一页里：
    //   用户点完"姿态角"往往就切回遥控页了（狗歪着走路，得盯着）。计时器要是跟着页面走，
    //   页面一关就再没人归零 —— 恰恰是最需要它的时候失效。
    if (ui.eulerResetAt >= 0.0 && ImGui::GetTime() >= ui.eulerResetAt) {
        ManagerSink sink(mgr, ui);
        const int n = cmd::resetEuler(sink, ui);   // 内部会清掉 eulerResetAt 与三个轴
        ui.addLog("[姿态角] 已自动归零 → " + std::to_string(n) + " 台");
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("Go2 控制管理台", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

    // ---- 背景：顶部略亮 → 底部近黑的**竖向渐变** + 顶部一点氛围光 ----
    // ★ 这是"有没有质感"的第一根分水岭：全屏一个纯色，所有半透明面板都贴在死灰上；
    //   铺一层渐变之后，控件上下位置自带明暗差，立刻有空间感（见 theme.cpp 里"半透明白"的说明）。
    {
        const ImVec2 wp = ImGui::GetWindowPos();
        const ImVec2 ws = ImGui::GetWindowSize();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilledMultiColor(wp, ImVec2(wp.x + ws.x, wp.y + ws.y),
                                    IM_COL32(28, 33, 43, 255), IM_COL32(28, 33, 43, 255),
                                    IM_COL32(10, 12, 16, 255), IM_COL32(10, 12, 16, 255));
        // 顶部中央的氛围光（非常淡 —— 深色背景上只留一点点"打光"的感觉）
        for (int i = 4; i >= 1; --i)
            dl->AddCircleFilled(ImVec2(wp.x + ws.x * 0.5f, wp.y + ws.y * 0.10f),
                                ws.y * (0.20f + static_cast<float>(i) * 0.13f),
                                IM_COL32(80, 124, 205, 6));
    }

    // ---- 断点布局：每帧按「视口 + 输入方式 + 安全区」重算（纯函数，带 8dp 滞回，幂等）----
    // 注意：DisplaySize 必须是**已按设备密度归一**的逻辑尺寸（dp），
    // 否则 3x 屏上按钮实际只有 18dp —— 归一由平台入口负责。
    {
        const ImVec2 ds = ImGui::GetIO().DisplaySize;
        ui.layout = makeLayout(ds.x, ds.y, ui.touchInput, ui.safe,
                               ui.layout.viewW > 0.0f ? &ui.layout : nullptr);
        setUiFontSizes(ui.layout.fontTitle, ui.layout.fontBody, ui.layout.fontSmall);
        applyUiScale(ui.layout.styleScale);  // 内部从基线重算，不会累乘
    }
    const LayoutSpec& L = ui.layout;
    g_touchUi = L.touch;  // 提示文案：鼠标悬停显示 / 触摸长按显示

    // 空格 = 急停：**两个页面都要能按** —— 动作库里全是危险动作，
    // 急停不能只活在遥控页（正在输入框里打字时不触发）。
    if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Space, false))
        triggerEstop(mgr, ui);

    drawTopBar(mgr, ui, L);  // 顶栏：页签 + 单控/群控 + 设备/设置/日志 + ■ 急停
    if (ui.page == UiPage::Actions)
        drawActionPage(mgr, ui, L);  // 动作库：**常驻整屏页面**（与摇杆带分上下）
    else
        drawRemotePanel(mgr, ui, L);  // 遥控：居中 + 宽度上限

    // 弹窗要在**主窗口层级**打开：顶栏按钮画在子窗口里，而 ImGui 的弹窗 ID 会带上
    // 子窗口的 ID 栈前缀，直接在子窗口里 OpenPopup 会与这里的 BeginPopupModal 对不上
    //（症状：点按钮没反应，弹窗永远不出现 —— 真机上踩过一次）。
    ui.modalOpen = false;  // 由 drawPopups 置位：有弹窗时摇杆带被盖住
    switch (ui.popupRequest) {
        case 1: ImGui::OpenPopup(kIdDevices);  ui.showDevices = true;  break;
        case 2: ImGui::OpenPopup(kIdSettings); ui.showSettings = true; break;
        case 3: ImGui::OpenPopup(kIdLog);      ui.showLog = true;      break;
        default: break;
    }
    ui.popupRequest = 0;
    drawPopups(mgr, ui, L);  // 设备 / 设置 / 日志

    ImGui::End();

    // ★ 摇杆在最后画、且画到前景层：页面内容盖不住它；弹窗打开时整条带子不画。
    // 两杆中间的「快捷动作 + 单控 / 群控」面板也在这里（要 mgr 读设备状态）。
    drawJoysticks(mgr, ui, L);
}

}  // namespace go2
