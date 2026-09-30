#pragma once

// ============================================================================
// 界面图片纹理：把 PNG/JPG 文件加载成 ImGui 可用的 GL 纹理。
//
// 为什么存在：网页界面的受控狗卡片用了一张 Go2 实拍图（assets/web/img/go2.jpg），
// ImGui 端对齐后也需要它 —— ImGui 不自带图片加载，这里用 stb_image（单头库，
// third_party/stb_image.h）解码 + OpenGL 上传。
//
// ⚠ 必须在 **GL 上下文就绪之后**调用（drawUi 主循环内首次用到时懒加载即可）。
// ============================================================================

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui.h>

namespace go2 {

/// 狗卡片用的 Go2 实拍图（懒加载，进程内只加载一次；失败返回 nullptr，卡片退化为爪印图标）
ImTextureID dogCardImage();

}  // namespace go2
