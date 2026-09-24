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

// ---- 手机端整体放大：字号与所有控件间距（触摸友好：手指点得准、眼睛看得清）----
// 字号是烘焙进 ImGui 字体图集的，只能在加载时定；控件间距则靠 ScaleAllSizes 跟着放大。
constexpr float kMobileFontScale = 1.55f;  // 标题 23→36、正文 18→28、小字 15→23
constexpr float kMobileUiScale = 1.35f;    // 内边距/间距/圆角/滚动条

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

    /// 尺寸更大、并从屏幕边缘往里收：贴着边时拇指够着别扭（还容易碰到系统的手势区），
    /// 内移之后正好是双手握持时拇指的自然落点。
    void layout(float w, float h) {
        radius = std::min(128.0f, std::max(74.0f, h * 0.22f));
        const float inset = radius * 0.95f + 44.0f;  // 左右各往里收
        const float cy = h - radius - 90.0f;         // 底部往上收（避开手势条 / 屏幕圆角）
        leftC = ImVec2(inset, cy);
        rightC = ImVec2(w - inset, cy);
    }

    /// 手指按下：落在哪个杆的作用圈里就归它（返回 true = 这个事件被摇杆消费掉）
    bool down(SDL_FingerID id, float x, float y) {
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
        if (go2::loadUiFontsFromMemory(data.data(), static_cast<int>(data.size()),
                                       kMobileFontScale)) {
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
    go2::applyTheme();
    loadFontsFromAssets();
    // 字号放大后，控件内边距 / 间距 / 圆角 / 滚动条必须跟着放大，否则又挤又难点
    ImGui::GetStyle().ScaleAllSizes(kMobileUiScale);

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
    ui.mobileLayout = true;  // 手游布局：摇杆贴左右两侧 + 隐藏日志面板
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
                int ww = 0, hh = 0;
                SDL_GetWindowSize(window, &ww, &hh);
                const SDL_FingerID fid = e.tfinger.fingerId;
                const float fx = e.tfinger.x * static_cast<float>(ww);
                const float fy = e.tfinger.y * static_cast<float>(hh);
                if (e.type == SDL_FINGERDOWN) {
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
            if (!forStick) ImGui_ImplSDL2_ProcessEvent(&e);
            switch (e.type) {
                case SDL_QUIT:
                    running = false;
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
        ImGui::NewFrame();

        // 手游布局：先按当前屏幕算好摇杆位置，并把手指算出的数值写进 ui
        //（必须在 drawUi 之前写，这样同一帧的 planMotion 就能用上，无延迟）
        sticks.layout(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y);
        if (ui.mobileLayout) {
            ui.joyLx = sticks.lx;
            ui.joyLy = sticks.ly;
            ui.joyRx = sticks.rx;
            ui.joyRy = sticks.ry;
        }

        go2::drawUi(mgr, ui);  // ★ 与桌面同一份界面代码

        // 两个摇杆画成屏幕浮层（在最上层，不占界面空间）—— 手游布局的核心
        if (ui.mobileLayout) {
            ImDrawList* fg = ImGui::GetForegroundDrawList();
            go2::drawJoystickAt("##tjoyL", fg, sticks.leftC.x, sticks.leftC.y, sticks.radius,
                                sticks.lx, sticks.ly, sticks.leftUsed);
            go2::drawJoystickAt("##tjoyR", fg, sticks.rightC.x, sticks.rightC.y, sticks.radius,
                                sticks.rx, sticks.ry, sticks.rightUsed);
            // 杆下方的极简标签（手游里一般不写太多字，够认就行）
            const ImU32 dim = ImGui::GetColorU32(ImVec4(1, 1, 1, 0.38f));
            const float fs = ImGui::GetFontSize() * 0.95f;
            ImFont* font = ImGui::GetFont();
            const std::pair<const char*, ImVec2> labels[2] = {
                {"左 · 移动", sticks.leftC},
                {"右 · 转向", sticks.rightC},
            };
            for (const auto& lb : labels) {
                const ImVec2 sz = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, lb.first);
                fg->AddText(font, fs,
                            ImVec2(lb.second.x - sz.x * 0.5f, lb.second.y + sticks.radius + 8.0f),
                            dim, lb.first);
            }
        }

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
