#pragma once

#include "layout.hpp"
#include "motion.hpp"

#include <atomic>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

// ImDrawList 的前向声明 —— 必须在**全局**作用域：写在 namespace go2 里会变成
// go2::ImDrawList，和 ImGui 真正的 ::ImDrawList 冲突（编译报 incomplete type）。
struct ImDrawList;

namespace go2 {

class RobotManager;

/// 设备列表里的一台机器狗
struct RobotEntry {
    std::string ip;
    bool selected = false;  // 勾选：接收控制指令。勾 1 台=单控，勾多台=群控
    bool manual = false;    // 手动添加（false=扫描发现）
    float battery = -1.0f;
    std::string modeName = "-";
};

/// 主界面页面（顶栏页签切换）。
/// 动作库是**常驻整屏页面**（按 pageW 铺满），不再是弹窗；
/// 设备 / 设置 / 日志 仍是弹窗，打开时盖住摇杆带。
enum class UiPage { Remote, Actions };

/// 主界面状态。**线程契约**（改这个结构体前请先读这一段）：
///
/// 有两类线程同时访问它：
///   1. **界面线程** —— `drawUi()` 每帧读参数、写命令值；
///   2. **HTTP 线程** —— `web_bridge.cpp` 的 `/api/state`（读）与 `/api/command`（写）。
///
/// 因此：
///   · 会被两端**同时读写**的标量 → 一律 `std::atomic`（原实现是裸 `bool/float/int`，
///     属于数据竞争；`std::map/std::set` 更是会在并发重哈希时崩溃）；
///   · `robots` 由 `robotsMutex` 保护；`names`/`toggles`/`activeToggleIds` 各自有锁 +
///     访问器，**不要**直接碰成员；
///   · 只在界面线程用的东西（`eulerX`、`rawJson`、弹窗开关、改名缓冲、`lastMoveSend` 等）
///     保持普通成员，不加锁 —— 别为了"整齐"把它们也变成 atomic，那只会让代码变难读。
///
/// ⚠ 把 atomic 交给 ImGui 控件要当心：`ImGui::Checkbox(..., &ui.mcfMode)` 这种取址绑定
///   编译不过。正确写法是"取出副本 → 控件改副本 → 写回"，见 `ui.cpp` 的 `drawActionPageHeader`。
/// ⚠ 用 `printf` 风格的可变参数（`ImGui::Text("%d", ...)`）或构造 `nlohmann::json` 时
///   必须显式 `.load()`，否则 class 类型进不了 `...` / 也不是 json 的兼容类型。
struct UiState {
    // ---- 设备列表 ----
    std::vector<RobotEntry> robots;
    std::mutex robotsMutex;
    std::atomic<bool> scanning{false};
    std::atomic<bool> privacyMode{false};  // 隐私模式：界面把 IP 等敏感信息打码

    // ---- 宇树动作库参数（Web 端 param 指令会改，界面每帧读 → atomic）----
    std::atomic<bool>  mcfMode{false};          // MCF 固件指令集（Go2 Pro 等）
    std::atomic<int>   speedLevel{1};           // SpeedLevel 0~2
    std::atomic<int>   gaitType{1};             // SwitchGait: 0 idle / 1 trot / 2 trot-run / 3 climb / 4 obstacle
    std::atomic<float> bodyHeight{0.28f};       // BodyHeight (m)
    std::atomic<float> footRaise{0.06f};        // FootRaiseHeight (m)
    float eulerX = 0.0f;         // 姿态角 roll（仅界面线程）
    float eulerY = 0.0f;         // pitch（仅界面线程）
    float eulerZ = 0.0f;         // yaw（仅界面线程）
    char rawJson[256] = "{}";    // 自定义 JSON 参数（仅界面线程）
    std::atomic<int> localKeyCount{0};  // 本地加载的 AES key 数量（data2=3 新固件用）
    char manualIp[256] = "192.168.2.";  // 手动添加输入框预填（支持逗号分隔多台）

    // ---- 遥控（对勾选的机器人生效）----
    std::atomic<float> speedScale{0.5f};    // 快捷按钮步速
    std::atomic<float> maxLinSpeed{0.60f};  // 摇杆满偏时的线速度 (m/s)
    std::atomic<float> yawRate{1.20f};      // 转向角速度 (rad/s)
    std::atomic<float> cmdVx{0.0f};         // 最近一次下发的速度（仅显示；Web 端会读）
    std::atomic<float> cmdVy{0.0f};
    std::atomic<float> cmdVz{0.0f};
    std::atomic<bool>  movingSent{false};   // 内部：当前正在持续下发速度指令
    double lastMoveSend = 0.0;  // 内部：上次下发时间（ImGui::GetTime，秒；仅界面线程）

    // ---- 双摇杆（-1..1，y 向下为正）+ 急停锁定 ----
    float joyLx = 0.0f, joyLy = 0.0f;  // 左杆：平移（上=前进 / 右=右移）
    float joyRx = 0.0f, joyRy = 0.0f;  // 右杆：转向（右=右转；纵向未启用）
    std::atomic<bool> estop{false};    // 急停锁定：锁定期间摇杆/快捷步都不得下发运动
    int   activeMask = 0;              // 内部：上一帧在操作的摇杆（1=左 2=右），用于即时生效

    /// 断点布局参数（每帧由 makeLayout 算出，见 layout.hpp）。
    ///
    /// 取代了原来的 `bool mobileLayout` —— 那种"桌面 / 手机"二选一在真实设备上两头不讨好。
    /// 现在主界面只有遥控 + 两下角悬浮摇杆，其余功能全走弹窗。
    LayoutSpec layout;
    /// 设备安全区（刘海 / 圆角 / 手势条），由平台入口填
    SafeArea safe;
    /// 输入方式：最近一次操作是手指 → true，决定按钮最小尺寸（触摸 50dp / 鼠标 34dp）。
    /// **按输入方式判定而不是按平台** —— 触屏笔记本、平板接外接鼠标都能自动适配。
    bool touchInput = false;

    // ---- 页面（顶栏页签）----
    UiPage page = UiPage::Remote;  ///< 遥控 / 动作库（动作库是常驻整屏页面）

    // ---- 机器狗名称（用户可改，持久化到 robot_names.json）----
    // 用 ip → 名称 的独立映射：改名不依赖设备是否在线，设备被移除/重新添加也不丢。
    // ★ names 由 namesMutex 保护：Web 端的 `rename` 指令在 HTTP 线程调用 setName，
    //   而界面线程同时在读 —— 原注释写"不需要加锁"是错的（并发重哈希会崩）。
    //   **不要再直接访问 names**，一律走下面这些方法。
    std::string renamingIp;      ///< 正在改名的设备（空 = 没有在改名；仅界面线程）
    char nameBuf[64] = "";       ///< 改名输入框缓冲（仅界面线程）
    /// 从 robot_names.json 读取（drawUi 里惰性调用一次，桌面/安卓共用）
    void loadNames();
    void saveNames();
    /// 改名并立即落盘（name 为空 = 恢复成显示 IP）
    void setName(const std::string& ip, const std::string& name);
    std::string nameOf(const std::string& ip) const;   ///< 自定义名称（空 = 没起名）
    std::string labelOf(const std::string& ip) const;  ///< 显示名：有名称用名称，否则 IP
    /// 群控：勾选列表内**全部**设备，返回台数（未就绪的勾上也无害，指令只发给就绪的）
    int selectAll();
    /// 单控：只勾选这一台（其余全部取消）
    bool selectOnly(const std::string& ip);
    /// "当前指令发给谁"的一句话（摇杆带中间显示：单控 · 名字 / 群控 · N 台）
    std::string controlTargetText();

    // （底部快捷栏 / 编辑常用动作已于 2026-09-28 按用户要求撤掉）

    // ---- 弹窗（顶栏入口按钮）----
    // 注意：顶栏按钮是在**子窗口**里画的，而 ImGui 的弹窗 ID 会带 ID 栈前缀 ——
    // 在子窗口里直接 OpenPopup 会和主窗口层级的 BeginPopupModal 对不上（弹窗打不开）。
    // 所以按钮只写一次性请求，由 drawUi 在主窗口层级统一 OpenPopup。
    int popupRequest = 0;  ///< 一次性请求：0=无 1=设备 2=设置 3=日志
    bool showDevices = false;   ///< 弹窗开关（作为 BeginPopupModal 的 p_open）
    bool showSettings = false;  ///< 设置：钥匙库 / 隐私 / 说明
    bool showLog = false;       ///< 运行日志

    /// 本帧是否有模态弹窗打开（设备 / 设置 / 日志）—— 弹窗要**盖住摇杆带**：
    /// 打开时摇杆不画、不响应、数值清零（避免"眼睛看弹窗、手指还在推摇杆"）。
    /// ⚠️ 安卓触屏入口（apps/android/native/main_android.cpp）也要读这个标志：
    ///    为真时不要把手指标成摇杆、也不要把摇杆数值写进 ui（同步平板端时需要补上）。
    bool modalOpen = false;

    /// 摇杆数值是否由平台层驱动（安卓触屏多点触控）：
    /// true → 平台层负责画到前景层，ui 侧只读不写（否则会把触屏算好的值覆盖成 0）
    bool joysticksByPlatform = false;
    bool joyLActive = false;  ///< 左杆正在被操作（平台层回填，用于高亮）
    bool joyRActive = false;  ///< 右杆正在被操作

    /// 安全操作区（急停 / 阻尼按钮的屏幕矩形，每帧由 drawUi 写入）。
    /// 悬浮摇杆的抓取范围**不得覆盖**这里 —— 否则手指落在急停上会被摇杆吃掉，
    /// 那是安全项，必须优先交给 ImGui 处理。
    /// 用四个 float 而不是 ImVec2，免得 ui.hpp 被迫依赖 imgui.h。
    /// ★ 改成**多矩形**：摇杆带中间多了「单控 / 群控」面板之后，单个矩形取并集会大得离谱
    ///   （急停在上、面板在下 → 整块屏幕都变成禁区，触屏就彻底失灵了）。
    static constexpr int kMaxSafetyRects = 8;
    float safetyRects[kMaxSafetyRects][4] = {};
    int safetyRectCount = 0;
    /// 登记一块"手指优先交给 ImGui"的屏幕矩形；每帧由 drawUi 开头清零
    void addSafetyRect(float x0, float y0, float x1, float y1) {
        if (safetyRectCount >= kMaxSafetyRects) return;
        float* r = safetyRects[safetyRectCount++];
        r[0] = x0; r[1] = y0; r[2] = x1; r[3] = y1;
    }

    // ---- 持续模式开关（自由行走 / 领航跟随 / 交叉步 / 经济步态 …）----
    // 这类指令是 on/off 语义、会一直生效，且 StopMove 停不掉；急停时会逐个关闭并复位这里的状态。
    // ★ 两个容器都由 toggleMutex 保护（Web 端急停会 clear，界面线程同时在读）。
    std::atomic<bool> estopBusy{false}; // 急停序列进行中（防连按叠加）
    std::atomic<bool> hideUnsupported{false};  // 隐藏"已确认该固件不支持（3203）"的动作
    bool dampArmed = false;        // 「强制阻尼」二次确认（仅界面线程）

    /// 某个「持续模式」开关当前的开/关状态（未知 = false）
    bool toggleState(const std::string& key) const;
    /// 记下开关状态（sendAction 与实际下发保持一致）
    void setToggle(const std::string& key, bool on);
    /// 把开关状态全部复位（急停用）
    void clearToggles();
    /// 开关状态的快照（给 `/api/state` 用；不要在锁外直接遍历 toggles）
    std::map<std::string, bool> togglesSnapshot() const;
    /// 记下"真正打开过"的开关 api_id（含另一套指令集的 id），急停时只关这些，避免灌爆通道
    void setToggleActive(int apiId, bool active);
    /// 已打开过的开关 id 快照
    std::vector<int> activeToggleIdsSnapshot() const;
    void clearActiveToggles();

    // ---- 动作可用性（由回执驱动，界面上标注"这条动作能不能用"）----
    std::mutex apiMutex;
    std::map<int, int> apiCode;          // api_id -> 上次回执 code
    std::map<int, std::string> apiNote;  // api_id -> 上次失败说明
    void noteApiResult(int apiId, int code, const std::string& note);
    /// @return false = 还没试过；true 时写回上次结果
    bool apiResult(int apiId, int* code, std::string* note);

    // （云账号字段已删：云登录表单从未实现；要拉云钥匙用 tools/unitree_cloud.py 走离线路线，
    //   C++ 侧完整实现在 core/unitree_cloud.{hpp,cpp}，见 tests/cloud_test.cpp）

    // ---- 本地钥匙库（不依赖云；每行一个 32 位 hex，存 keys.txt）----
    char manualKey[80] = "";

    // ---- 钥匙探测（设置页「从机器狗找钥匙」：不连电脑，从狗的内网服务里提取）----
    // 结果都在这：网页/设置页通过 /api/state 的 keyScan 字段读
    std::atomic<bool> keyScanning{false};
    std::mutex keyScanMutex;
    std::string keyScanNote;                 // 最近一次探测的说明（开放端口 / 提示）
    std::vector<std::string> keyCandidates;  // 探测到的 32 位 hex 候选（未验证）

    // ---- 日志 ----
    std::vector<std::string> logs;
    std::mutex logMutex;
    bool autoScroll = true;
    bool logErrorsOnly = false;  // 只看异常（失败 / 错误 / 急停 / 警告）
    /// 累计的异常/失败行数（addLog 里统计），以及"上次打开日志时看到的数量"。
    /// 两者不等 → 顶栏「日志」按钮显示角标，提醒有新异常（日志弹窗没开时也能发现）。
    /// ★ atomic：addLog 由**所有**线程调用（连接线程 / HTTP 线程），读方在界面线程与 Web 端。
    std::atomic<int> problemCount{0};
    std::atomic<int> problemSeen{0};

    void addLog(const std::string& line);

    // ---- 设备列表辅助（内部已加锁）----
    bool addOrUpdate(const std::string& ip, bool manual);  // 新增返回 true
    bool remove(const std::string& ip);
    bool isSelected(const std::string& ip);
    void setSelected(const std::string& ip, bool sel);
    void updateStatus(const std::string& ip, float battery, const std::string& mode);
    std::vector<std::string> selectedIps();
    int selectedCount();

private:
    // ---- 受 namesMutex 保护的名称表 ----
    mutable std::mutex namesMutex;
    std::map<std::string, std::string> names;  // ip -> 用户起的名字
    std::string nameOfLocked(const std::string& ip) const;  // 需已持有 namesMutex
    void saveNamesLocked() const;                           // 需已持有 namesMutex

    // ---- 受 toggleMutex 保护的持续模式开关状态 ----
    mutable std::mutex toggleMutex;
    std::map<std::string, bool> toggles;  // 动作 key -> 开/关
    std::set<int> activeToggleIds;        // 真正打开过的开关 id
};

/// 绘制整个界面
void drawUi(RobotManager& mgr, UiState& ui);

/// 扫描局域网（后台线程）：网段 TCP 探测 + SN 多播，发现的狗自动加入列表并连接。
/// 实现在 ui_state.cpp（ui.hpp 里只给声明 —— 网页桥 web_bridge.cpp 也调这份）
///
/// 线程所有权：扫描线程是**可 join 的**（不是 detach），由本文件统一持有。
/// 进程退出前必须调 `joinScans()`，否则线程可能在 `RobotManager`/`UiState` 析构之后
/// 才去访问它们（use-after-free）。
void startScan(RobotManager& mgr, UiState& ui);

/// 等待所有后台扫描线程结束。桌面端/服务端在退出前调用。
/// 幂等：重复调用安全。
void joinScans();

/// 是否属于"需要关注"的行（用于日志角标统计与「只看异常」过滤）。
/// 实现在 ui_state.cpp —— 判断标准只留这一份：角标统计（addLog）和日志列表过滤
/// 走同一个函数，两处标准不一致的话角标数字和列表内容会对不上。
bool isProblemLine(const std::string& s);

/// 在指定屏幕位置画一个摇杆（**不处理输入，只负责画**）。
/// 供触屏多点触控使用：数值（x/y）由调用方自己算好传进来 —— ImGui 只有一个"指针"，
/// 两个摇杆无法同时拖动，所以手游布局下由触屏入口自己接管手指事件再调这里画。
/// @param dl     画到哪个 draw list（窗口内用 GetWindowDrawList，全局浮层用 GetForegroundDrawList）
/// @param cx,cy  摇杆圆心（屏幕坐标，像素）
/// @param x,y    归一化偏移 -1..1（y 向下为正）
/// @param active 是否正在被操作（决定旋钮颜色）
void drawJoystickAt(const char* id, ImDrawList* dl, float cx, float cy, float radius, float x,
                    float y, bool active);

/// 从本地钥匙文件加载（每行一个 32 位 hex；忽略空行与 # 注释）
std::vector<std::string> loadLocalKeysFile(const std::string& path);

}  // namespace go2
