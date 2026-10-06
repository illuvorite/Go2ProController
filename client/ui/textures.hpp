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

/// 一张已上传到 GPU 的图片。`w/h` 是像素尺寸（画的时候要靠它算宽高比，
/// 否则图会被拉成正方形 —— 尤其中文 logo 这种横长条）。
struct LoadedImage {
    ImTextureID tex = 0;  ///< 0 = 没加载成功（调用方负责回退到文字/图标）
    int w = 0;
    int h = 0;
};

/// 狗卡片用的 Go2 实拍图（懒加载，进程内只加载一次；失败返回 nullptr，卡片退化为爪印图标）
ImTextureID dogCardImage();

/// 顶栏品牌字标（`assets/web/img/logo-word.png`，透明背景）。
/// 用字标而不是完整 lockup：原图是"图形+中文 / 分隔线 / H-bbot"三合一，
/// 缩到顶栏高度后那行中文只剩几像素，等于糊掉。
/// 懒加载；失败返回空（调用方回退到文字品牌）。
LoadedImage brandLogo();

}  // namespace go2
