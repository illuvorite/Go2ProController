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

struct UiState {
    // ---- 设备列表 ----
    std::vector<RobotEntry> robots;
    std::mutex robotsMutex;
    std::atomic<bool> scanning{false};
    bool privacyMode = false;  // 隐私模式：界面把 IP 等敏感信息打码

    // ---- 宇树动作库参数 ----
    bool mcfMode = false;        // MCF 固件指令集（Go2 Pro 等）
    int speedLevel = 1;          // SpeedLevel 0~2
    int gaitType = 1;            // SwitchGait: 0 idle / 1 trot / 2 trot-run / 3 climb / 4 obstacle
    float bodyHeight = 0.28f;    // BodyHeight (m)
    float footRaise = 0.06f;     // FootRaiseHeight (m)
    float eulerX = 0.0f;         // 姿态角 roll
    float eulerY = 0.0f;         // pitch
    float eulerZ = 0.0f;         // yaw
    char rawJson[256] = "{}";    // 自定义 JSON 参数
    int localKeyCount = 0;  // 本地加载的 AES key 数量（data2=3 新固件用）
    char manualIp[256] = "192.168.2.";  // 手动添加输入框预填（支持逗号分隔多台）

    // ---- 遥控（对勾选的机器人生效）----
    float speedScale = 0.5f;    // 快捷按钮步速
    float maxLinSpeed = 0.60f;  // 摇杆满偏时的线速度 (m/s)
    float yawRate = 1.20f;      // 转向角速度 (rad/s)
    float cmdVx = 0.0f;         // 最近一次下发的速度（仅显示）
    float cmdVy = 0.0f;
    float cmdVz = 0.0f;
    bool  movingSent = false;   // 内部：当前正在持续下发速度指令
    double lastMoveSend = 0.0;  // 内部：上次下发时间（ImGui::GetTime，秒）

    // ---- 双摇杆（-1..1，y 向下为正）+ 急停锁定 ----
    float joyLx = 0.0f, joyLy = 0.0f;  // 左杆：平移（上=前进 / 右=右移）
    float joyRx = 0.0f, joyRy = 0.0f;  // 右杆：转向（右=右转；纵向未启用）
    bool  estop = false;               // 急停锁定：锁定期间摇杆/快捷步都不得下发运动
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

    // ---- 弹窗（顶栏四个入口按钮）----
    // 注意：顶栏按钮是在**子窗口**里画的，而 ImGui 的弹窗 ID 会带 ID 栈前缀 ——
    // 在子窗口里直接 OpenPopup 会和主窗口层级的 BeginPopupModal 对不上（弹窗打不开）。
    // 所以按钮只写一次性请求，由 drawUi 在主窗口层级统一 OpenPopup。
    int popupRequest = 0;  ///< 一次性请求：0=无 1=设备 2=动作库 3=设置 4=日志
    bool showDevices = false;   ///< 弹窗开关（作为 BeginPopupModal 的 p_open）
    bool showActions = false;   ///< 动作库：一排排按钮平铺（不折叠）
    bool showSettings = false;  ///< 设置：钥匙库 / 隐私 / 说明
    bool showLog = false;       ///< 运行日志

    /// 摇杆数值是否由平台层驱动（安卓触屏多点触控）：
    /// true → 平台层负责画到前景层，ui 侧只读不写（否则会把触屏算好的值覆盖成 0）
    bool joysticksByPlatform = false;
    bool joyLActive = false;  ///< 左杆正在被操作（平台层回填，用于高亮）
    bool joyRActive = false;  ///< 右杆正在被操作

    /// 安全操作区（急停 / 阻尼按钮的屏幕矩形，每帧由 drawUi 写入）。
    /// 悬浮摇杆的抓取范围**不得覆盖**这里 —— 否则手指落在急停上会被摇杆吃掉，
    /// 那是安全项，必须优先交给 ImGui 处理。
    /// 用四个 float 而不是 ImVec2，免得 ui.hpp 被迫依赖 imgui.h。
    float safetyMinX = 0.0f, safetyMinY = 0.0f, safetyMaxX = 0.0f, safetyMaxY = 0.0f;

    // ---- 持续模式开关（自由行走 / 领航跟随 / 交叉步 / 经济步态 …）----
    // 这类指令是 on/off 语义、会一直生效，且 StopMove 停不掉；急停时会逐个关闭并复位这里的状态
    std::map<std::string, bool> toggles;
    std::set<int> activeToggleIds;      // 我们真正打开过的开关 id（急停只关这些，避免灌爆通道）
    std::atomic<bool> estopBusy{false}; // 急停序列进行中（防连按叠加）
    int sideHold = 0;                   // 侧移按住：+1 左 / -1 右 / 0 无
    bool hideUnsupported = false;  // 隐藏"已确认该固件不支持（3203）"的动作
    bool dampArmed = false;        // 「强制阻尼」二次确认

    // ---- 动作可用性（由回执驱动，界面上标注"这条动作能不能用"）----
    std::mutex apiMutex;
    std::map<int, int> apiCode;          // api_id -> 上次回执 code
    std::map<int, std::string> apiNote;  // api_id -> 上次失败说明
    void noteApiResult(int apiId, int code, const std::string& note);
    /// @return false = 还没试过；true 时写回上次结果
    bool apiResult(int apiId, int* code, std::string* note);

    // ---- 云账号（可选；默认不用）----
    int cloudRegionIdx = 0;          // 0=global 1=cn
    char cloudEmail[128] = "";
    char cloudPassword[128] = "";
    std::atomic<bool> cloudBusy{false};

    // ---- 本地钥匙库（不依赖云；每行一个 32 位 hex，存 keys.txt）----
    char manualKey[80] = "";

    // ---- 日志 ----
    std::vector<std::string> logs;
    std::mutex logMutex;
    bool autoScroll = true;
    bool logErrorsOnly = false;  // 只看异常（失败 / 错误 / 急停 / 警告）
    /// 累计的异常/失败行数（addLog 里统计），以及"上次打开日志时看到的数量"。
    /// 两者不等 → 顶栏「日志」按钮显示角标，提醒有新异常（日志弹窗没开时也能发现）
    int problemCount = 0;
    int problemSeen = 0;

    void addLog(const std::string& line);

    // ---- 设备列表辅助（内部已加锁）----
    bool addOrUpdate(const std::string& ip, bool manual);  // 新增返回 true
    bool remove(const std::string& ip);
    bool isSelected(const std::string& ip);
    void setSelected(const std::string& ip, bool sel);
    void updateStatus(const std::string& ip, float battery, const std::string& mode);
    std::vector<std::string> selectedIps();
    int selectedCount();
};

/// 绘制整个界面
void drawUi(RobotManager& mgr, UiState& ui);

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
