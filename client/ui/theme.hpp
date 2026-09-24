#pragma once

#include <imgui.h>

namespace go2 {

/// 三档字号（标题 / 正文 / 小字提示）。中文字体缺失时全部为 nullptr，界面回到默认字体。
struct UiFonts {
    ImFont* title = nullptr;
    ImFont* body = nullptr;
    ImFont* small = nullptr;
};

/// 加载中文字体（三档字号）。@return 是否找到可用中文字体
bool loadUiFonts();
const UiFonts& uiFonts();

/// 从内存加载中文字体（三档字号）——供移动端等没有系统字体的平台使用：
/// 把字体文件打进 assets 再传进来即可（字体授权需自行确认，推荐 OFL 的思源黑体/Noto Sans SC）
/// @param fontScale 字号倍率：手机/平板离眼睛远且靠手指操作，一般放大到 1.4~1.6
///                  （字体在 ImGui 里是**烘焙进图集**的，加载后改不了，必须加载时定好）
bool loadUiFontsFromMemory(const void* data, int dataSize, float fontScale = 1.0f);

/// 应用现代化深色主题：配色 / 圆角 / 间距 / 滚动条
void applyTheme();

/// 作用域字体：字体为空时自动跳过，避免 PushFont(nullptr)
struct FontScope {
    explicit FontScope(ImFont* f) : f_(f) {
        if (f_) ImGui::PushFont(f_);
    }
    ~FontScope() {
        if (f_) ImGui::PopFont();
    }
    FontScope(const FontScope&) = delete;
    FontScope& operator=(const FontScope&) = delete;
    ImFont* f_;
};

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
