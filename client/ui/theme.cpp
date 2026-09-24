#include "theme.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

namespace go2 {
namespace {

UiFonts g_fonts;

/// applyTheme() 建立的**基线样式**。applyUiScale() 每次都从这里重算，
/// 因为 ScaleAllSizes 是就地相乘、反复调用会累乘。
ImGuiStyle g_baseStyle;
bool g_hasBaseStyle = false;
float g_appliedScale = 1.0f;

/// 中文字体候选（Linux 优先，其次 Windows 字体）
const char* kFontCandidates[] = {
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
    "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
    "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
    "C:/Windows/Fonts/msyh.ttc",
    "C:/Windows/Fonts/simhei.ttf",
};

constexpr ImVec4 rgba(float r, float g, float b, float a) { return ImVec4(r, g, b, a); }

/// 三档字号的基准值（实际用哪档由断点决定，见 setUiFontSizes）
constexpr float kBaseTitle = 23.0f;
constexpr float kBaseBody = 18.0f;
constexpr float kBaseSmall = 15.0f;

}  // namespace

bool loadUiFonts() {
    ImGuiIO& io = ImGui::GetIO();
    for (const char* path : kFontCandidates) {
        FILE* probe = std::fopen(path, "rb");
        if (!probe) continue;
        std::fclose(probe);

        ImFontConfig cfg;
        cfg.OversampleH = 2;
        cfg.OversampleV = 2;
        // ImGui 1.92+ 按需动态加载字形，无需预先指定中文字形范围；
        // 也只加载**一份**字体 —— 字号靠 PushFont(font, size) 在运行时给。
        g_fonts.font = io.Fonts->AddFontFromFileTTF(path, kBaseBody, &cfg);
        g_fonts.title = kBaseTitle;
        g_fonts.body = kBaseBody;
        g_fonts.small = kBaseSmall;
        if (g_fonts.font) {
            io.FontDefault = g_fonts.font;
            std::printf("[字体] 已加载 %s（动态字号：标题/正文/小字 由断点决定）\n", path);
        }
        return g_fonts.font != nullptr;
    }
    g_fonts.font = io.Fonts->AddFontDefault();
    io.FontDefault = g_fonts.font;
    std::printf("[字体] 未找到中文字体，中文可能显示为方块\n");
    return false;
}

bool loadUiFontsFromMemory(const void* data, int dataSize, float fontScale) {
    if (!data || dataSize <= 0) return false;
    ImGuiIO& io = ImGui::GetIO();

    // AddFontFromMemoryTTF 会接管内存（要求可写且生命周期覆盖整个进程），这里复制一份
    static std::vector<unsigned char> owned;
    owned.assign(static_cast<const unsigned char*>(data),
                 static_cast<const unsigned char*>(data) + dataSize);

    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;  // 由上面的 owned 持有
    cfg.OversampleH = 2;
    cfg.OversampleV = 2;
    const void* buf = owned.data();
    const float k = fontScale > 0.1f ? fontScale : 1.0f;
    // 只加载一份（图集默认字号 = 正文）。字号不再"烘焙"，
    // 而是运行时按断点 PushFont(font, size) —— 换断点/转屏幕都不用重载图集。
    g_fonts.font = io.Fonts->AddFontFromMemoryTTF(const_cast<void*>(buf), dataSize,
                                                  kBaseBody * k, &cfg);
    g_fonts.title = kBaseTitle * k;
    g_fonts.body = kBaseBody * k;
    g_fonts.small = kBaseSmall * k;
    if (g_fonts.font) io.FontDefault = g_fonts.font;
    std::printf("[字体] 已从内存加载（%d 字节，动态字号）\n", dataSize);
    return g_fonts.font != nullptr;
}

const UiFonts& uiFonts() { return g_fonts; }

void setUiFontSizes(float title, float body, float small) {
    g_fonts.title = title > 6.0f ? title : kBaseTitle;
    g_fonts.body = body > 6.0f ? body : kBaseBody;
    g_fonts.small = small > 6.0f ? small : kBaseSmall;
}

FontScope fontTitle() { return FontScope(g_fonts.font, g_fonts.title); }
FontScope fontBody() { return FontScope(g_fonts.font, g_fonts.body); }
FontScope fontSmall() { return FontScope(g_fonts.font, g_fonts.small); }

void applyTheme() {
    ImGuiStyle& s = ImGui::GetStyle();
    ImGui::StyleColorsDark();

    // ---- 圆角与间距：现代控制台的"卡片感" ----
    s.WindowRounding = 0.0f;      // 主窗口铺满，不需要圆角
    s.ChildRounding = 10.0f;
    s.FrameRounding = 7.0f;
    s.PopupRounding = 8.0f;
    s.ScrollbarRounding = 8.0f;
    s.GrabRounding = 6.0f;
    s.TabRounding = 7.0f;
    s.WindowBorderSize = 0.0f;
    s.ChildBorderSize = 1.0f;
    s.FrameBorderSize = 1.0f;
    s.WindowPadding = ImVec2(14, 12);
    s.FramePadding = ImVec2(11, 6);
    s.ItemSpacing = ImVec2(9, 7);
    s.ItemInnerSpacing = ImVec2(7, 5);
    s.CellPadding = ImVec2(8, 5);
    s.IndentSpacing = 18.0f;
    s.ScrollbarSize = 13.0f;
    s.GrabMinSize = 11.0f;
    s.SeparatorTextBorderSize = 1.0f;

    ImVec4* c = s.Colors;
    const ImVec4 bg0 = rgba(0.055f, 0.067f, 0.086f, 1.00f);  // 最底
    const ImVec4 bg1 = rgba(0.078f, 0.094f, 0.118f, 1.00f);  // 面板
    const ImVec4 bg2 = rgba(0.106f, 0.125f, 0.153f, 1.00f);  // 控件
    const ImVec4 bg3 = rgba(0.137f, 0.161f, 0.196f, 1.00f);  // 悬停
    const ImVec4 bg4 = rgba(0.169f, 0.198f, 0.239f, 1.00f);  // 按下
    const ImVec4 line = rgba(0.169f, 0.196f, 0.235f, 1.00f);

    c[ImGuiCol_WindowBg] = bg0;
    c[ImGuiCol_ChildBg] = bg1;
    c[ImGuiCol_PopupBg] = bg1;
    c[ImGuiCol_Border] = line;
    c[ImGuiCol_BorderShadow] = rgba(0, 0, 0, 0);

    c[ImGuiCol_Text] = col::kText;
    c[ImGuiCol_TextDisabled] = col::kDim;

    c[ImGuiCol_FrameBg] = bg2;
    c[ImGuiCol_FrameBgHovered] = bg3;
    c[ImGuiCol_FrameBgActive] = bg4;

    c[ImGuiCol_TitleBg] = bg0;
    c[ImGuiCol_TitleBgActive] = bg1;
    c[ImGuiCol_MenuBarBg] = bg1;

    c[ImGuiCol_ScrollbarBg] = rgba(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = bg3;
    c[ImGuiCol_ScrollbarGrabHovered] = bg4;
    c[ImGuiCol_ScrollbarGrabActive] = col::kAccent;

    c[ImGuiCol_CheckMark] = col::kAccent;
    c[ImGuiCol_SliderGrab] = col::kAccent;
    c[ImGuiCol_SliderGrabActive] = rgba(0.45f, 0.68f, 1.00f, 1.0f);

    c[ImGuiCol_Button] = bg2;
    c[ImGuiCol_ButtonHovered] = bg3;
    c[ImGuiCol_ButtonActive] = bg4;

    c[ImGuiCol_Header] = rgba(0.29f, 0.56f, 0.99f, 0.22f);
    c[ImGuiCol_HeaderHovered] = rgba(0.29f, 0.56f, 0.99f, 0.34f);
    c[ImGuiCol_HeaderActive] = rgba(0.29f, 0.56f, 0.99f, 0.46f);

    c[ImGuiCol_Separator] = line;
    c[ImGuiCol_SeparatorHovered] = col::kAccent;
    c[ImGuiCol_SeparatorActive] = col::kAccent;

    c[ImGuiCol_ResizeGrip] = rgba(0, 0, 0, 0);
    c[ImGuiCol_Tab] = bg2;
    c[ImGuiCol_TabHovered] = bg3;
    c[ImGuiCol_TabSelected] = rgba(0.29f, 0.56f, 0.99f, 0.35f);
    c[ImGuiCol_TableHeaderBg] = bg2;
    c[ImGuiCol_TableBorderStrong] = line;
    c[ImGuiCol_TableBorderLight] = rgba(0.169f, 0.196f, 0.235f, 0.6f);
    c[ImGuiCol_TableRowBg] = rgba(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = rgba(1, 1, 1, 0.025f);
    c[ImGuiCol_TextSelectedBg] = rgba(0.29f, 0.56f, 0.99f, 0.35f);
    c[ImGuiCol_NavCursor] = col::kAccent;

    // ★ 存基线：applyUiScale 每次都从这里重算
    g_baseStyle = s;
    g_hasBaseStyle = true;
    g_appliedScale = 1.0f;
}

void applyUiScale(float scale) {
    if (!g_hasBaseStyle) {
        g_baseStyle = ImGui::GetStyle();
        g_hasBaseStyle = true;
    }
    if (!(scale > 0.01f)) scale = 1.0f;
    if (std::fabs(scale - g_appliedScale) < 0.001f) return;  // 没变就别动，省得每帧重算

    ImGuiStyle& s = ImGui::GetStyle();
    s = g_baseStyle;  // ★ 先还原到基线，再乘 —— 否则会累乘（1.15 → 1.32 → 1.52…）
    s.ScaleAllSizes(scale);
    g_appliedScale = scale;
}

}  // namespace go2
