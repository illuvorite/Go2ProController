#include "textures.hpp"

#include <cstdint>
#include <cstdio>

// stb_image：单头图片解码库（public domain），只在**这一个** TU 里实现一次
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#include <stb_image.h>

// ---- GL 头：安卓走 GLES3（go2_imgui 目标已定义 IMGUI_IMPL_OPENGL_ES3 并链接 GLESv3）；
// ---- 桌面走系统 GL（glTexImage2D 等 1.1 级函数，libGL 由 GLFW 的链接带入）
#if defined(__ANDROID__) || defined(IMGUI_IMPL_OPENGL_ES3)
#include <GLES3/gl3.h>
#else
#if defined(_WIN32)
#include <windows.h>
#endif
#include <GL/gl.h>
#endif

namespace go2 {
namespace {

/// 图片候选路径：桌面从 client/ 或 client/build/ 启动都能命中；
/// 安卓启动时已把 assets/web 导出到应用目录（cwd = 应用目录），第一个就能命中。
const char* kDogImageCandidates[] = {
    "assets/web/img/go2.jpg",
    "../assets/web/img/go2.jpg",
    "client/assets/web/img/go2.jpg",
    "../client/assets/web/img/go2.jpg",
};

ImTextureID uploadRgba(const unsigned char* pixels, int w, int h) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    if (tex == 0) return 0;
    ::glBindTexture(GL_TEXTURE_2D, tex);
    ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    ::glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    ::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    ::glBindTexture(GL_TEXTURE_2D, 0);
    // ImGui 1.92 的 ImTextureID 默认是 ImU64（不是 void*），按整型转
    return static_cast<ImTextureID>(static_cast<std::uintptr_t>(tex));
}

}  // namespace

ImTextureID dogCardImage() {
    static ImTextureID tex = 0;
    static bool tried = false;
    if (tried) return tex;  // 懒加载一次：无论成败都不再重试（失败则卡片退化为爪印图标）
    tried = true;

    for (const char* path : kDogImageCandidates) {
        FILE* probe = std::fopen(path, "rb");
        if (!probe) continue;
        std::fclose(probe);

        int w = 0, h = 0, comp = 0;
        unsigned char* pixels = ::stbi_load(path, &w, &h, &comp, 4);  // 强制 RGBA
        if (!pixels) continue;
        tex = uploadRgba(pixels, w, h);
        ::stbi_image_free(pixels);
        if (tex) {
            std::printf("[纹理] 狗卡片图片已加载 %s (%dx%d)\n", path, w, h);
            break;
        }
    }
    if (!tex) std::printf("[纹理] 未找到狗卡片图片（卡片退化为爪印图标）\n");
    return tex;
}

}  // namespace go2
