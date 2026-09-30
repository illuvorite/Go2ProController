#include "theme.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
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

// ---------------------------------------------------------------- 图标字体
// 用 Phosphor（MIT，细线圆润）跟中文字体**合并**成一份 ImFont：汉字和图标能混排，
// 断点字号一变两者一起缩，不用分别 PushFont。字形清单见 ui/icons.hpp。
bool g_iconFont = false;

/// 图标字体候选路径。桌面是从 client/ 目录启动的，所以第一个就能命中；
/// 移动端（安卓）启动时会 chdir 到应用目录，把同一份字体拷到那儿即可；
/// 也可以用环境变量 GO2_ICON_FONT 直接指定绝对路径。
const char* kIconFontCandidates[] = {
    "assets/fonts/Phosphor.ttf",
    "../assets/fonts/Phosphor.ttf",
    "client/assets/fonts/Phosphor.ttf",
    "../client/assets/fonts/Phosphor.ttf",
    "fonts/Phosphor.ttf",
};

bool mergeIconFont(float baseSize) {
    ImGuiIO& io = ImGui::GetIO();
    const char* env = std::getenv("GO2_ICON_FONT");
    std::vector<const char*> cands;
    if (env && *env) cands.push_back(env);
    for (const char* p : kIconFontCandidates) cands.push_back(p);

    for (const char* p : cands) {
        FILE* probe = std::fopen(p, "rb");
        if (!probe) continue;
        std::fclose(probe);

        ImFontConfig cfg;
        cfg.MergeMode = true;  // 合并进**上一个**字体：图标与汉字共享一份 ImFont
        cfg.OversampleH = 1;   // 图标是纯色线稿，1 倍采样就够，省图集
        cfg.OversampleV = 1;
        // ImGui 1.92 动态字形：不用给范围，用到哪个图标才栅格化哪个
        if (io.Fonts->AddFontFromFileTTF(p, baseSize, &cfg)) {
            g_iconFont = true;
            std::printf("[字体] 图标字体已合并 %s\n", p);
            return true;
        }
    }
    std::printf("[字体] 未找到图标字体（图标回退为手绘形状）。可从仓库取一份放到 %s\n",
                kIconFontCandidates[0]);
    return false;
}

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
            mergeIconFont(kBaseBody);  // 图标字体并进同一份字体（找不到就退回手绘图标）
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
    if (g_fonts.font) mergeIconFont(kBaseBody * k);  // 移动端：把 Phosphor.ttf 放到应用目录即可
    std::printf("[字体] 已从内存加载（%d 字节，动态字号）\n", dataSize);
    return g_fonts.font != nullptr;
}

bool iconFontLoaded() { return g_iconFont; }

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
    // ★ 圆角统一放大（8~12dp）：圆角越大，边缘的"反光"越明显，质感越强
    s.WindowRounding = 0.0f;      // 主窗口铺满，不需要圆角
    s.ChildRounding = 12.0f;
    s.FrameRounding = 8.0f;
    s.PopupRounding = 12.0f;
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
    s.ScrollbarSize = 8.0f;       // 细一点：粗滚动条很"工具软件"，细的更像 App
    s.GrabMinSize = 11.0f;
    s.SeparatorTextBorderSize = 1.0f;

    ImVec4* c = s.Colors;
    // ★★★ 质感的关键一步：面板 / 控件全部改成**半透明白**，不再用实色灰。
    // 这样底下那层背景渐变能透上来 —— 同一个按钮在屏幕上方就偏亮、下方就偏暗，
    // 于是界面有了"环境光"和层次；全用实色灰的话，屏幕上就是一片死板的同色方块。
    const ImVec4 bg0 = rgba(0.055f, 0.067f, 0.086f, 1.00f);  // 最底（渐变会盖在上面）
    const ImVec4 bg1 = rgba(1.0f, 1.0f, 1.0f, 0.045f);       // 面板
    const ImVec4 bg2 = rgba(1.0f, 1.0f, 1.0f, 0.062f);       // 控件（按钮 / 输入框）
    const ImVec4 bg3 = rgba(1.0f, 1.0f, 1.0f, 0.105f);       // 悬停
    const ImVec4 bg4 = rgba(1.0f, 1.0f, 1.0f, 0.150f);       // 按下
    // 玻璃边：淡白描边（比灰边通透，深色背景上立刻有"玻璃感"）
    const ImVec4 line = rgba(1.0f, 1.0f, 1.0f, 0.10f);

    c[ImGuiCol_WindowBg] = bg0;
    c[ImGuiCol_ChildBg] = bg1;
    // 弹窗要盖住底下 → 用接近实色的深灰（半透明会看穿到主界面，读不清）
    c[ImGuiCol_PopupBg] = rgba(0.082f, 0.098f, 0.125f, 0.985f);
    c[ImGuiCol_Border] = line;
    c[ImGuiCol_BorderShadow] = rgba(0, 0, 0, 0.32f);  // 面板下方一点暗边 = 轻微"离地"

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
