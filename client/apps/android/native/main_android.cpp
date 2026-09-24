// ============================================================================
// Android 入口：SDL2 + OpenGL ES 3 + Dear ImGui
//
// 与桌面入口（apps/desktop/main.cpp）共用同一套 core/ 与 ui/ 代码：
//   core/  协议、加密、发现、多机管理（纯逻辑，Android 上原样复用）
//   ui/    drawUi() / theme —— 只依赖 ImGui，不依赖 GLFW
// 差异只在"窗口、渲染后端、输入、生命周期"：
//   GLFW+OpenGL3.3  →  SDL2 + GLES3
//   鼠标键盘        →  触摸（ImGui 的 SDL2 后端把触摸映射成鼠标；急停做成常驻大按钮）
//   关窗即退出      →  Android 生命周期（切后台必须停车断开，回前台重连）
// ============================================================================

#include "local_keys.hpp"
#include "robot_manager.hpp"
#include "theme.hpp"
#include "ui.hpp"

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"

#include <SDL.h>
#include <SDL_opengles2.h>
#include <android/asset_manager.h>
#include <android/log.h>
#include <jni.h>

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <string>
#include <thread>
#include <unistd.h>  // chdir：把工作目录切到应用可写目录
#include <vector>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "go2", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "go2", __VA_ARGS__)

namespace {

std::atomic<bool> g_inBackground{false};

// ---- 坐标单位归一（dp）------------------------------------------------------
// SDL2 在 Android 上返回的是**物理分辨率**，所以 ImGui 的坐标单位默认就是物理像素：
// 3x 屏上 54 单位的按钮实际只有约 18dp，远低于 48dp 的触摸下限。
//（改造前用 kMobileFontScale = 1.55 整体放大字号来"糊着补偿"，既不准、也没法随断点变。）
// 现在改成：把 io.DisplaySize 折成逻辑尺寸、io.DisplayFramebufferScale 设成像素密度，
// 于是 ImGui 单位 = dp —— 断点数值、按钮高度、摇杆半径才有一致含义。
// 密度探测见 detectPixelScale()，实际值会打到日志里，便于在真机上核对。
static float g_pixelScale = 1.0f;

/// 探测"物理像素 → 逻辑 dp"的换算比。
/// 分两种情况：SDL 已经给了逻辑尺寸（drawable > window）就直接用比值；
/// 否则说明 window 就是物理像素，此时用 DPI 反推密度（Android 基准密度 = 160dpi）。
static float detectPixelScale(SDL_Window* window) {
    int ww = 0, hh = 0, dw = 0, dh = 0;
    SDL_GetWindowSize(window, &ww, &hh);
    SDL_GL_GetDrawableSize(window, &dw, &dh);
    if (ww > 0 && dw > ww) {
        const float s = static_cast<float>(dw) / static_cast<float>(ww);
        LOGI("像素密度：SDL 已给逻辑尺寸 %dx%d，drawable %dx%d → scale=%.2f", ww, hh, dw, dh, s);
        return s;
    }
    float ddpi = 0.0f, hdpi = 0.0f, vdpi = 0.0f;
    const int display = SDL_GetWindowDisplayIndex(window);
    if (SDL_GetDisplayDPI(display, &ddpi, &hdpi, &vdpi) == 0 && ddpi > 1.0f) {
        const float s = std::max(1.0f, std::min(4.0f, ddpi / 160.0f));
        LOGI("像素密度：window %dx%d（物理像素），displayDPI=%.0f → scale=%.2f", ww, hh,
             ddpi, s);
        return s;
    }
    LOGI("像素密度：探测失败，按 1.0 处理（window %dx%d）—— 若界面偏小请核对这里", ww, hh);
    return 1.0f;
}

// ---------------------------------------------------------------- 触屏双摇杆（多点触控）
// ImGui 只有一个"指针"（触摸被映射成鼠标），两个摇杆没法同时拖动 ——
// 手游布局下由我们自己接管手指事件：按下点落在某个摇杆的作用圈里就被它"吃掉"，
// 这样左右两根手指可以**同时**操作（一边走一边转），也不会误触下面的界面控件。
struct TouchSticks {
    ImVec2 leftC{0.0f, 0.0f}, rightC{0.0f, 0.0f};
    float radius = 84.0f;
    bool leftUsed = false, rightUsed = false;
    SDL_FingerID leftId = -1, rightId = -1;
    float lx = 0.0f, ly = 0.0f, rx = 0.0f, ry = 0.0f;

    /// 安全操作区（急停 / 阻尼按钮的屏幕矩形）：落在这里的手指**不归摇杆**，
    /// 直接放给 ImGui。改造前抓取半径是 radius*1.9，覆盖范围很大，
    /// 落在急停上的手指会被摇杆吃掉 —— 这是安全项，必须优先。
    float blockMinX = 0.0f, blockMinY = 0.0f, blockMaxX = 0.0f, blockMaxY = 0.0f;

    /// 几何完全来自断点布局（ui/layout.cpp::computeFloatingJoysticks）。
    /// 不再在这里自己算 radius/inset/cy 那套魔法数 —— 否则摇杆位置与内容预留高度
    /// 会各算一套（改造前一个算 h-radius-90、一个写死 250），屏幕一变就错位。
    void applyLayout(const go2::LayoutSpec& L, float viewW) {
        radius = L.joyRadius;
        leftC = ImVec2(L.joyInsetX, L.joyCenterY);
        rightC = ImVec2(viewW - L.joyInsetX, L.joyCenterY);
    }

    bool inSafetyZone(float x, float y) const {
        if (blockMaxX <= blockMinX) return false;
        return x >= blockMinX && x <= blockMaxX && y >= blockMinY && y <= blockMaxY;
    }

    /// 手指按下：落在哪个杆的作用圈里就归它（返回 true = 这个事件被摇杆消费掉）
    bool down(SDL_FingerID id, float x, float y) {
        // ★ 安全优先：急停 / 阻尼按钮上的手指放给 ImGui
        if (inSafetyZone(x, y)) return false;
        const float grab = radius * 1.9f;  // 抓取范围放宽，手感更宽容
        const auto near = [&](const ImVec2& c) {
            const float dx = x - c.x, dy = y - c.y;
            return dx * dx + dy * dy <= grab * grab;
        };
        if (!leftUsed && near(leftC)) {
            leftUsed = true;
            leftId = id;
            move(id, x, y);
            return true;
        }
        if (!rightUsed && near(rightC)) {
            rightUsed = true;
            rightId = id;
            move(id, x, y);
            return true;
        }
        return false;
    }
    void move(SDL_FingerID id, float x, float y) {
        const auto calc = [&](const ImVec2& c, float* ox, float* oy) {
            const float limit = radius - 18.0f;
            float dx = (x - c.x) / limit, dy = (y - c.y) / limit;
            const float len = std::sqrt(dx * dx + dy * dy);
            if (len > 1.0f) { dx /= len; dy /= len; }    // 限制在圆内
            if (len < 0.08f) { dx = 0.0f; dy = 0.0f; }   // 死区（与桌面一致）
            *ox = dx;
            *oy = dy;
        };
        if (leftUsed && id == leftId) calc(leftC, &lx, &ly);
        if (rightUsed && id == rightId) calc(rightC, &rx, &ry);
    }
    void up(SDL_FingerID id) {
        if (leftUsed && id == leftId) { leftUsed = false; leftId = -1; lx = ly = 0.0f; }
        if (rightUsed && id == rightId) { rightUsed = false; rightId = -1; rx = ry = 0.0f; }
    }
    bool owns(SDL_FingerID id) const {
        return (leftUsed && id == leftId) || (rightUsed && id == rightId);
    }
};

// ---------------------------------------------------------------- 资源（内嵌字体）
/// 从 APK 的 assets/ 读整个文件（用于内嵌中文字体：assets/fonts/*.ttf|otf）
/// SDL 的 SDL_RWFromFile 在 Android 上会自动落到 assets（无需 AAssetManager 手写 JNI）
std::vector<unsigned char> readAsset(const char* path) {
    std::vector<unsigned char> out;
    SDL_RWops* rw = SDL_RWFromFile(path, "rb");
    if (!rw) return out;
    const Sint64 size = SDL_RWsize(rw);
    if (size > 0) {
        out.resize(static_cast<size_t>(size));
        const size_t got = SDL_RWread(rw, out.data(), 1, out.size());
        out.resize(got);
    }
    SDL_RWclose(rw);
    return out;
}

/// 尝试按顺序加载内嵌中文字体（体积考虑：只放一个即可）
void loadFontsFromAssets() {
    static const char* kCandidates[] = {
        "fonts/NotoSansSC-Regular.otf",
        "fonts/NotoSansSC-Regular.ttf",
        "fonts/SourceHanSansSC-Regular.otf",
        "fonts/wqy-microhei.ttc",
    };
    for (const char* path : kCandidates) {
        auto data = readAsset(path);
        if (data.empty()) continue;
        // 只加载**一份**字体：三档字号改由断点在运行时给（ImGui 1.92 动态字号，
        // 见 ui/theme.cpp 的 setUiFontSizes + PushFont(font, size)），
        // 所以这里不再传字号倍率 —— 换断点/转屏幕都不用重载字体图集。
        if (go2::loadUiFontsFromMemory(data.data(), static_cast<int>(data.size()), 1.0f)) {
            LOGI("内嵌字体加载成功: %s (%zu 字节)", path, data.size());
            return;
        }
    }
    LOGI("未找到内嵌字体（assets/fonts/*），中文可能显示为方块");
    go2::loadUiFonts();  // 退回系统字体（Android 上通常只有拉丁字形）
}

// ---------------------------------------------------------------- Android 特有：多播锁
/// Android 默认过滤组播 → 必须在 Java 侧持有 WifiManager.MulticastLock，
/// 否则 SN 多播发现（231.1.1.1:10131）收不到任何回包。
/// MainActivity 暴露 acquireMulticastLock()/releaseMulticastLock() 供这里调用。
bool callActivityVoid(const char* method) {
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    if (!env) return false;
    jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (!activity) return false;
    jclass cls = env->GetObjectClass(activity);
    if (!cls) return false;
    jmethodID mid = env->GetMethodID(cls, method, "()V");
    if (!mid) {
        // ★ 查找失败同样会留下 pending 异常：不清掉的话，SDL/ART 的下一次 JNI 调用
        //   会命中 "JNI DETECTED ERROR ... called with pending exception" 直接 abort 进程
        //   （踩过：清单里跑的是 SDLActivity 而不是 MainActivity 时必崩）。
        env->ExceptionClear();
        SDL_Log("JNI: 找不到方法 %s()（清单里的 activity 必须是 org.libsdl.app.MainActivity）", method);
    }
    bool ok = false;
    if (mid) {
        env->CallVoidMethod(activity, mid);
        ok = !env->ExceptionCheck();
        if (!ok) env->ExceptionClear();
    }
    env->DeleteLocalRef(cls);
    return ok;
}

// ---------------------------------------------------------------- 前后台切换
/// 切后台：**先停车再断开**（安全 + 机器狗同一时刻只允许一条连接），回前台自动重连
void onEnterBackground(go2::RobotManager& mgr, go2::UiState& ui) {
    g_inBackground = true;
    ui.addLog("[Android] 切到后台：停车并断开（回前台自动重连）");
    std::thread([&mgr] {
        for (const auto& s : mgr.snapshot())
            if (auto* c = mgr.find(s.ip); c && c->isReady()) c->stopMove();
        mgr.disconnectAll();
    }).detach();
}

void onEnterForeground(go2::RobotManager& mgr, go2::UiState& ui) {
    g_inBackground = false;
    ui.addLog("[Android] 回到前台：重连设备");
    for (const auto& ip : mgr.desiredIps()) mgr.connect(ip);
}

// ---------------------------------------------------------------- 回调接线（与桌面入口一致）
void managerWireUp(go2::RobotManager& mgr, go2::UiState& ui) {
    mgr.onCreated = [&ui](go2::RobotClient& c) {
        go2::RobotClient* pc = &c;  // ip() 在 connect 之后才有值
        c.onLog = [&ui, pc](const std::string& line) { ui.addLog("[" + pc->ip() + "] " + line); };
        c.onStateChanged = [&ui, pc](go2::ConnState s) {
            if (s != go2::ConnState::Ready) return;
            ui.addLog("[" + pc->ip() + "] 就绪，订阅状态主题");
            pc->subscribe("rt/sportmodestate");
            pc->subscribe("rt/lf/sportmodestate");
            pc->subscribe("rt/lf/lowstate");
        };
        // 动作回执 → 动作库界面的 ✓/✗ 标注
        c.onSportAck = [&ui](int apiId, int code, const std::string& note) {
            ui.noteApiResult(apiId, code, note);
        };
        c.onTopicData = [&ui, pc](const std::string& topic, const nlohmann::json& msg) {
            float battery = -1.0f;
            std::string mode;
            try {
                const auto& d = msg.contains("data") ? msg["data"] : nlohmann::json();
                if (topic.find("sportmodestate") != std::string::npos) {
                    if (d.contains("mode_name")) mode = d["mode_name"].get<std::string>();
                    if (d.contains("battery_level")) battery = d["battery_level"].get<float>();
                } else if (topic.find("lowstate") != std::string::npos) {
                    if (d.contains("bms_state") && d["bms_state"].contains("soc"))
                        battery = d["bms_state"]["soc"].get<float>();
                }
            } catch (...) {
                return;  // 字段随固件变化，解析失败忽略
            }
            ui.updateStatus(pc->ip(), battery, mode);
        };
    };

    // 每设备 AES 钥匙（data2=3 新固件）：与桌面同一套本地安全存储加载逻辑
    // 规矩：只读本应用专属目录里的钥匙文件，**不碰官方宇树 App 的任何东西**
    // （不读它的日志/私有数据，连路径都不再探测），也不扫描平板存储。
    const auto lk = go2::loadLocalAesKeys();
    ui.localKeyCount = static_cast<int>(lk.keys.size());
    if (lk.keys.empty()) {
        const char* ext = SDL_AndroidGetExternalStoragePath();
        ui.addLog(std::string("[钥匙] 未找到本地钥匙 —— 把电脑上的 "
                              "C:\\Users\\<你>\\.go2\\keys.json push 到 ") +
                  (ext && *ext ? std::string(ext) : std::string("应用目录")) +
                  "/keys.json；或在「设置」页手工粘贴 32 位 hex");
    } else {
        mgr.setAesKeys(lk.keys);
        ui.addLog("[钥匙] 已加载 " + std::to_string(lk.keys.size()) + " 把（来源: " +
                  lk.source + "）");
    }
}

}  // namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    // ---- SDL 初始化（触摸→鼠标映射是 ImGui 后端的前提）----
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "1");
    SDL_SetHint(SDL_HINT_ANDROID_BLOCK_ON_PAUSE, "0");  // 切后台不阻塞主循环，交给下面的生命周期处理
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        LOGE("SDL_Init 失败: %s", SDL_GetError());
        return 1;
    }

    // ---- OpenGL ES 3.0 窗口 ----
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);

    SDL_Window* window = SDL_CreateWindow("Unitree Go2", SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED, 0, 0,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN |
                                              SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) {
        LOGE("创建窗口失败: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_GLContext gl = SDL_GL_CreateContext(window);
    if (!gl) {
        LOGE("创建 GLES 上下文失败: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_MakeCurrent(window, gl);
    SDL_GL_SetSwapInterval(1);

    // ---- ImGui ----
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    go2::applyTheme();  // 建立样式基线（applyUiScale 每次都从基线重算，不会累乘）
    loadFontsFromAssets();
    // 字号与控件间距不再在启动时定死：drawUi 每帧按断点调
    // setUiFontSizes() + applyUiScale()（见 ui/layout.cpp / ui/theme.cpp）
    g_pixelScale = detectPixelScale(window);

    ImGui_ImplSDL2_InitForOpenGL(window, gl);
    ImGui_ImplOpenGL3_Init("#version 300 es");

    // ---- Android 钥匙 / 缓存目录（桌面版靠 ~/.go2/keys.json，安卓根本没有这些路径）----
    // 用「应用专属外部目录」当钥匙目录，并 chdir 过去 —— 一次解决三件事：
    //   1) 钥匙文件  <ext>/keys.json      ← adb push 直接写入，无需任何权限
    //   2) 设置页手输钥匙保存的 keys.txt   ← 原来是相对路径，安卓上工作目录不可写，存不下来
    //   3) 连接成功后缓存的 go2_keys_cache.json
    // Android 10+ 的应用专属外部目录（/sdcard/Android/data/<包名>/files）不需要存储权限。
    {
        const char* ext = SDL_AndroidGetExternalStoragePath();
        const char* in  = SDL_AndroidGetInternalStoragePath();
        std::string dir;
        if (ext && *ext && ::chdir(ext) == 0)
            dir = ext;
        else if (in && *in && ::chdir(in) == 0)
            dir = in;
        if (!dir.empty()) {
            ::setenv("GO2_KEYS_FILE", (dir + "/keys.json").c_str(), 1);
            LOGI("钥匙目录: %s", dir.c_str());
        } else {
            LOGE("找不到可写目录，钥匙/缓存不可用");
        }
    }

    // ---- 业务状态（与桌面完全共用）----
    go2::RobotManager mgr;
    mgr.loadKeyCache();  // go2_keys_cache.json（已 chdir 到应用可写目录）
    go2::UiState ui;
    // 输入方式：Android 上默认手指；插上鼠标后主循环会自动切成"鼠标优先"
    // （按钮最小尺寸跟着变，见 layout.cpp 的 touch 修正）
    ui.touchInput = true;
    // 摇杆数值由触屏层（TouchSticks）驱动：ui 侧只负责画到前景层，不参与输入处理
    //（手指事件在进 ImGui 之前就被 TouchSticks 消费了，ImGui 收不到，也就没法走交互那条路）
    ui.joysticksByPlatform = true;
    // 安全区（刘海 / 圆角 / 手势条）：先用保守常量 ——
    // 安卓侧边返回手势区约 20dp、底部手势条约 24dp。
    // 要精确值需要在 MainActivity 里读 WindowInsets 再经 JNI 传过来。
    ui.safe.top = 0.0f;  // 全屏沉浸式，状态栏已隐藏
    ui.safe.bottom = 24.0f;
    ui.safe.left = 20.0f;
    ui.safe.right = 20.0f;
    ui.addLog("[Android] 启动：Go2 控制台（SDL2 + GLES3）");
    TouchSticks sticks;

    managerWireUp(mgr, ui);  // 见下方定义（回调接线与桌面一致）
    callActivityVoid("acquireMulticastLock");  // 组播发现必需

    // ---- 主循环 ----
    bool running = true;
    double lastFrame = 0.0;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            // 触屏：先问摇杆"吃不吃"这个事件；吃掉就不再转给 ImGui，
            // 否则 ImGui 会把它当成一次鼠标点击，误触摇杆下面的界面控件。
            bool forStick = false;
            if (e.type == SDL_FINGERDOWN || e.type == SDL_FINGERMOTION ||
                e.type == SDL_FINGERUP) {
                // 手指坐标必须换算到 **ImGui 的逻辑坐标系**（= 已按密度归一的 dp），
                // 不能用 SDL_GetWindowSize 的物理像素 —— 否则摇杆命中判定与绘制整体错位。
                const ImVec2 ds = ImGui::GetIO().DisplaySize;
                const SDL_FingerID fid = e.tfinger.fingerId;
                const float fx = e.tfinger.x * ds.x;
                const float fy = e.tfinger.y * ds.y;
                if (e.type == SDL_FINGERDOWN) {
                    ui.touchInput = true;  // 输入方式：手指优先（按钮最小尺寸跟着变）
                    forStick = sticks.down(fid, fx, fy);
                } else if (e.type == SDL_FINGERMOTION) {
                    if (sticks.owns(fid)) {
                        sticks.move(fid, fx, fy);
                        forStick = true;
                    }
                } else {
                    if (sticks.owns(fid)) {
                        sticks.up(fid);
                        forStick = true;
                    }
                }
            }
            // ★ 指针坐标也要在**源头**折算成 dp：SDL 给的是物理像素，而界面按 dp 排版。
            //   不能等 NewFrame 之后再补发坐标 —— ImGui 的输入 trickling 会在"同帧先移动后按键"
            //   时丢掉后补的坐标事件，结果按下用的还是物理坐标，点击就落到屏幕外了
            //（真机症状：按钮能 hover、但点不动；键盘正常，所以特别容易漏掉）。
            if (g_pixelScale > 1.01f) {
                if (e.type == SDL_MOUSEMOTION) {
                    e.motion.x = static_cast<int>(e.motion.x / g_pixelScale);
                    e.motion.y = static_cast<int>(e.motion.y / g_pixelScale);
                } else if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
                    e.button.x = static_cast<int>(e.button.x / g_pixelScale);
                    e.button.y = static_cast<int>(e.button.y / g_pixelScale);
                }
            }
            if (!forStick) ImGui_ImplSDL2_ProcessEvent(&e);
            switch (e.type) {
                case SDL_QUIT:
                    running = false;
                    break;
                case SDL_MOUSEBUTTONDOWN:
                    // 真鼠标（不是触摸合成的鼠标事件）→ 切成"鼠标优先"：
                    // 按钮回落到紧凑尺寸。SDL 用 which == SDL_TOUCH_MOUSEID 标记合成事件。
                    if (e.button.which != SDL_TOUCH_MOUSEID) ui.touchInput = false;
                    break;
                case SDL_APP_WILLENTERBACKGROUND:  // SDL 2.0.16+
                    onEnterBackground(mgr, ui);
                    break;
                case SDL_APP_DIDENTERFOREGROUND:
                    onEnterForeground(mgr, ui);
                    break;
                case SDL_KEYDOWN:
                    // Android 返回键仅在急停已锁定时允许退出
                    if (e.key.keysym.sym == SDLK_AC_BACK && !ui.estop) {
                        ui.addLog("[Android] 返回键：请先急停再退出");
                    } else if (e.key.keysym.sym == SDLK_AC_BACK) {
                        running = false;
                    }
                    break;
                default:
                    break;
            }
        }
        if (g_inBackground.load()) {
            SDL_Delay(80);  // 后台不渲染，省电
            continue;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        // ★ 坐标单位归一：让 ImGui 的单位 = dp（详见文件顶部 detectPixelScale 的说明）。
        // 必须在 NewFrame 之前改：DisplaySize 折成逻辑尺寸、FramebufferScale 设成密度，
        // 这样渲染出的 drawable 仍是物理分辨率（不糊），而所有布局常量都以 dp 计。
        // 指针坐标则在事件源头折算（见上面 SDL_MOUSEMOTION 分支），这里不用再补。
        if (g_pixelScale > 1.01f) {
            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize =
                ImVec2(io.DisplaySize.x / g_pixelScale, io.DisplaySize.y / g_pixelScale);
            io.DisplayFramebufferScale = ImVec2(g_pixelScale, g_pixelScale);
        }
        ImGui::NewFrame();

        // 摇杆几何来自断点布局；先把安全操作区交给摇杆做抓取互斥
        //（手指落在急停/阻尼上不能被摇杆吃掉）
        sticks.blockMinX = ui.safetyMinX;
        sticks.blockMinY = ui.safetyMinY;
        sticks.blockMaxX = ui.safetyMaxX;
        sticks.blockMaxY = ui.safetyMaxY;

        // 先把布局算出来：摇杆要在 drawUi 之前拿到几何，且数值要在同一帧内生效（无延迟）
        //（drawUi 里会再算一次，纯函数 + 滞回，结果相同）
        {
            const ImVec2 ds = ImGui::GetIO().DisplaySize;
            ui.layout = go2::makeLayout(ds.x, ds.y, ui.touchInput, ui.safe,
                                        ui.layout.viewW > 0.0f ? &ui.layout : nullptr);
        }
        // 摇杆几何来自断点布局（永远悬浮在两下角）
        sticks.applyLayout(ui.layout, ui.layout.screenW);
        // 把触屏算好的数值写进 ui —— 必须在 drawUi 之前，同一帧的 planMotion 才用得上（无延迟）
        ui.joyLx = sticks.lx;
        ui.joyLy = sticks.ly;
        ui.joyRx = sticks.rx;
        ui.joyRy = sticks.ry;
        ui.joyLActive = sticks.leftUsed;
        ui.joyRActive = sticks.rightUsed;

        go2::drawUi(mgr, ui);  // ★ 与桌面同一份界面代码（摇杆由它画到前景层，不再这里重复画）

        ImGui::Render();
        int w = 0, h = 0;
        SDL_GL_GetDrawableSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.055f, 0.067f, 0.086f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);

        // 限帧到 ~60fps（移动端省电）
        const double now = (double)SDL_GetTicks() / 1000.0;
        if (now - lastFrame < 1.0 / 60.0) SDL_Delay(1);
        lastFrame = now;
    }

    // ---- 退出：停车 + 优雅断开（否则机器狗侧会残留连接）----
    ui.addLog("[Android] 退出：停车并断开");
    for (const auto& s : mgr.snapshot())
        if (auto* c = mgr.find(s.ip); c && c->isReady()) c->stopMove();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    mgr.disconnectAll();
    mgr.shutdown();
    callActivityVoid("releaseMulticastLock");

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
