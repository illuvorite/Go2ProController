#include "textures.hpp"

#include <cstddef>
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

/// 品牌字标（与狗卡片同一份资源目录，所以三个平台的路径规则完全一致）
const char* kBrandLogoCandidates[] = {
    "assets/web/img/logo-word.png",
    "../assets/web/img/logo-word.png",
    "client/assets/web/img/logo-word.png",
    "../client/assets/web/img/logo-word.png",
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

/// 依次尝试候选路径，第一个能打开且能解码的生效。
template <size_t N>
LoadedImage loadFirst(const char* const (&candidates)[N], const char* label) {
    LoadedImage out;
    for (const char* path : candidates) {
        FILE* probe = std::fopen(path, "rb");
        if (!probe) continue;
        std::fclose(probe);

        int w = 0, h = 0, comp = 0;
        unsigned char* pixels = ::stbi_load(path, &w, &h, &comp, 4);  // 强制 RGBA
        if (!pixels) continue;
        const ImTextureID tex = uploadRgba(pixels, w, h);
        ::stbi_image_free(pixels);
        if (tex) {
            out.tex = tex;
            out.w = w;
            out.h = h;
            std::printf("[纹理] %s已加载 %s (%dx%d)\n", label, path, w, h);
            return out;
        }
    }
    return out;
}

}  // namespace

ImTextureID dogCardImage() {
    static ImTextureID tex = 0;
    static bool tried = false;
    if (tried) return tex;  // 懒加载一次：无论成败都不再重试（失败则卡片退化为爪印图标）
    tried = true;
    tex = loadFirst(kDogImageCandidates, "狗卡片图片").tex;
    if (!tex) std::printf("[纹理] 未找到狗卡片图片（卡片退化为爪印图标）\n");
    return tex;
}

LoadedImage brandLogo() {
    // 懒加载一次：纹理上传是一次性的，之后每帧只返回缓存（不再碰文件系统）
    static LoadedImage img;
    static bool tried = false;
    if (tried) return img;
    tried = true;
    img = loadFirst(kBrandLogoCandidates, "品牌字标");
    if (!img.tex) std::printf("[纹理] 未找到品牌字标（顶栏回退到文字品牌）\n");
    return img;
}

}  // namespace go2
