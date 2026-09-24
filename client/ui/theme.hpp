#pragma once

#include <imgui.h>

namespace go2 {

/// 界面字体：**只加载一份字体**，三档字号在运行时给。
///
/// ImGui 1.92 起支持动态字体（本项目用的 imgui_impl_opengl3 已声明
/// ImGuiBackendFlags_RendererHasTextures），所以 `PushFont(font, 字号)` 就能换字号，
/// 字形按需栅格化 —— 换断点、转屏幕都不用重载字体图集，字还更清晰。
/// 改造前是把字号**烘焙**进图集（桌面 23/18/15，手机再乘 1.55 烘一份），
/// 加载后就改不了，横竖屏切换得重来 —— 那是响应式布局的硬障碍。
struct UiFonts {
    ImFont* font = nullptr;  ///< 唯一字体；中文缺失时为 nullptr，界面回到默认字体
    float title = 23.0f;     ///< 三档字号，随断点变化（由 layout.hpp 的 LayoutSpec 给）
    float body = 18.0f;
    float small = 15.0f;
};

/// 加载中文字体（系统字体候选）
bool loadUiFonts();

/// 从内存加载（移动端：把字体打进 assets 再传进来）
/// @param fontScale 基准字号的整体倍率，一般传 1.0（字号改由断点控制）
bool loadUiFontsFromMemory(const void* data, int dataSize, float fontScale = 1.0f);

const UiFonts& uiFonts();

/// 按断点设置三档字号（运行时可变）
void setUiFontSizes(float title, float body, float small);

/// 应用现代化深色主题：配色 / 圆角 / 间距 / 滚动条（只需在启动时调一次，建立基线）
void applyTheme();

/// 按断点缩放控件间距 / 圆角 / 滚动条。
///
/// ⚠ ImGuiStyle::ScaleAllSizes 是**就地相乘**（imgui.cpp 里各字段直接 `*=`，
/// 只额外记了个 `_MainScale`），反复调用会累乘（1.35 → 1.82 → 2.46…）。
/// 所以这里每次都从 applyTheme() 建立的基线快照重算，而不是在当前样式上再乘一次。
void applyUiScale(float scale);

/// 作用域字体：字体为空时自动跳过，避免 PushFont(nullptr)。
/// @param size 字号；传 0 表示沿用当前字号（只换字体）
struct FontScope {
    explicit FontScope(ImFont* f, float size = 0.0f) : pushed_(false) {
        if (!f) return;
        ImGui::PushFont(f, size);
        pushed_ = true;
    }
    ~FontScope() {
        if (pushed_) ImGui::PopFont();
    }
    FontScope(const FontScope&) = delete;
    FontScope& operator=(const FontScope&) = delete;

   private:
    bool pushed_;
};

/// 按档位取字体作用域（字号跟随断点）。用法：`FontScope fs = fontSmall();`
FontScope fontTitle();
FontScope fontBody();
FontScope fontSmall();

/// 语义色（与主题一致，供各处直接引用）
namespace col {
constexpr ImVec4 kAccent{0.29f, 0.56f, 0.99f, 1.00f};   // 主色（蓝）
constexpr ImVec4 kOk{0.24f, 0.72f, 0.35f, 1.00f};       // 成功（绿）
constexpr ImVec4 kWarn{0.85f, 0.62f, 0.16f, 1.00f};     // 警告（黄）
constexpr ImVec4 kErr{0.96f, 0.32f, 0.29f, 1.00f};      // 危险（红）
constexpr ImVec4 kIdle{0.45f, 0.50f, 0.56f, 1.00f};     // 空闲（灰）
constexpr ImVec4 kText{0.90f, 0.93f, 0.96f, 1.00f};     // 主文字
constexpr ImVec4 kDim{0.52f, 0.57f, 0.63f, 1.00f};      // 次要文字
}  // namespace col

}  // namespace go2
