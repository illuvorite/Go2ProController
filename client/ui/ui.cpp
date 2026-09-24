#include "ui.hpp"

#include "discovery.hpp"
#include "robot_client.hpp"
#include "robot_manager.hpp"
#include "sport_library.hpp"
#include "local_keys.hpp"
#include "theme.hpp"

#include <imgui.h>

#include <chrono>
#include <cmath>
#include <regex>
#include <cstring>
#include <fstream>
#include <functional>
#include <thread>

namespace go2 {

namespace {
/// 前置声明：addLog 里要统计"异常行"用于日志按钮的角标，
/// 而它的定义在文件靠后（与"只看异常"过滤用的是同一套判断，避免两处标准不一致）
bool isProblemLine(const std::string& s);
}  // namespace

void UiState::addLog(const std::string& line) {
    std::lock_guard<std::mutex> lock(logMutex);
    logs.push_back(line);
    if (logs.size() > 500) logs.erase(logs.begin(), logs.begin() + 100);
    // 累计异常/失败行数：日志弹窗没打开时，靠顶栏「日志」按钮上的角标提醒
    // （否则出错了用户根本不知道，机器人控制里这种"静默失败"很危险）
    if (isProblemLine(line)) ++problemCount;
}

bool UiState::addOrUpdate(const std::string& ip, bool manual) {
    std::lock_guard<std::mutex> lock(robotsMutex);
    for (auto& r : robots)
        if (r.ip == ip) return false;
    robots.push_back({ip, false, manual, -1.0f, "-"});
    return true;
}

bool UiState::remove(const std::string& ip) {
    std::lock_guard<std::mutex> lock(robotsMutex);
    for (auto it = robots.begin(); it != robots.end(); ++it)
        if (it->ip == ip) {
            robots.erase(it);
            return true;
        }
    return false;
}

bool UiState::isSelected(const std::string& ip) {
    std::lock_guard<std::mutex> lock(robotsMutex);
    for (auto& r : robots)
        if (r.ip == ip) return r.selected;
    return false;
}

void UiState::setSelected(const std::string& ip, bool sel) {
    std::lock_guard<std::mutex> lock(robotsMutex);
    for (auto& r : robots)
        if (r.ip == ip) {
            r.selected = sel;
            return;
        }
}

void UiState::updateStatus(const std::string& ip, float battery, const std::string& mode) {
    std::lock_guard<std::mutex> lock(robotsMutex);
    for (auto& r : robots)
        if (r.ip == ip) {
            if (battery >= 0.0f) r.battery = battery;
            if (!mode.empty()) r.modeName = mode;
            return;
        }
}

std::vector<std::string> UiState::selectedIps() {
    std::lock_guard<std::mutex> lock(robotsMutex);
    std::vector<std::string> out;
    for (auto& r : robots)
        if (r.selected) out.push_back(r.ip);
    return out;
}

int UiState::selectedCount() {
    return static_cast<int>(selectedIps().size());
}

void UiState::noteApiResult(int apiId, int code, const std::string& note) {
    if (apiId == 0) return;
    std::lock_guard<std::mutex> lock(apiMutex);
    apiCode[apiId] = code;
    apiNote[apiId] = note;
}

bool UiState::apiResult(int apiId, int* code, std::string* note) {
    std::lock_guard<std::mutex> lock(apiMutex);
    auto it = apiCode.find(apiId);
    if (it == apiCode.end()) return false;
    if (code) *code = it->second;
    if (note) {
        auto nt = apiNote.find(apiId);
        *note = (nt == apiNote.end()) ? std::string() : nt->second;
    }
    return true;
}

std::vector<std::string> loadLocalKeysFile(const std::string& path) {
    std::vector<std::string> keys;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' ||
                                 line.back() == '\t'))
            line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        if (line.size() == 32) keys.push_back(line);
    }
    return keys;
}

namespace {

constexpr ImVec4 kGreen{0.35f, 0.85f, 0.45f, 1.0f};
constexpr ImVec4 kRed{0.95f, 0.40f, 0.40f, 1.0f};
constexpr ImVec4 kYellow{0.95f, 0.80f, 0.35f, 1.0f};
constexpr ImVec4 kGray{0.55f, 0.55f, 0.55f, 1.0f};

const char* stateText(ConnState s) {
    switch (s) {
        case ConnState::Disconnected: return "未连接";
        case ConnState::Signaling:    return "信令中";
        case ConnState::Connecting:   return "连接中";
        case ConnState::Validating:   return "校验中";
        case ConnState::Ready:        return "就绪";
        case ConnState::Failed:       return "失败";
    }
    return "?";
}

ImVec4 stateColor(ConnState s) {
    switch (s) {
        case ConnState::Ready:  return kGreen;
        case ConnState::Failed: return kRed;
        case ConnState::Disconnected: return kGray;
        default:                return kYellow;
    }
}

bool bigButton(const char* label, const ImVec2& size = ImVec2(0, 34)) {
    return ImGui::Button(label, size);
}

/// 主色按钮（强调操作用）
bool accentButton(const char* label, const ImVec2& size = ImVec2(0, 32)) {
    ImGui::PushStyleColor(ImGuiCol_Button,
                          ImVec4(col::kAccent.x, col::kAccent.y, col::kAccent.z, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.40f, 0.65f, 1.00f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.55f, 0.75f, 1.00f, 1.00f));
    const bool r = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return r;
}

/// 段落标题：左侧主色竖条 + 标题字号（small=true 用正文字号）
void sectionTitle(const char* text) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float lh = ImGui::GetTextLineHeight();
    dl->AddRectFilled(ImVec2(p.x, p.y + 2.0f), ImVec2(p.x + 3.0f, p.y + lh - 2.0f),
                      ImGui::GetColorU32(col::kAccent), 2.0f);
    ImGui::Dummy(ImVec2(3.0f, 0.0f));
    ImGui::SameLine(0.0f, 9.0f);
    FontScope fs = fontTitle();
    ImGui::TextUnformatted(text);
}

/// 圆形状态灯（带柔和光晕）
void statusDot(ImVec4 color, float r = 4.5f) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 c(p.x + r + 2.0f, p.y + ImGui::GetTextLineHeight() * 0.5f);
    dl->AddCircleFilled(c, r + 2.5f,
                        ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, 0.20f)));
    dl->AddCircleFilled(c, r, ImGui::GetColorU32(color));
    ImGui::Dummy(ImVec2(r * 2.0f + 5.0f, ImGui::GetTextLineHeight()));
}

/// 圆角胶囊标签
void chip(const char* text, ImVec4 color) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    FontScope fs = fontSmall();
    const ImVec2 ts = ImGui::CalcTextSize(text);
    const ImVec2 pad(9.0f, 3.0f);
    const ImVec2 sz(ts.x + pad.x * 2.0f, ts.y + pad.y * 2.0f);
    const ImVec2 b(p.x + sz.x, p.y + sz.y);
    dl->AddRectFilled(p, b, ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, 0.16f)),
                      sz.y * 0.5f);
    dl->AddRect(p, b, ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, 0.50f)), sz.y * 0.5f,
                0, 1.0f);
    dl->AddText(ImVec2(p.x + pad.x, p.y + pad.y), ImGui::GetColorU32(color), text);
    ImGui::Dummy(sz);
}

/// 电量条（pct < 0 表示未知）
void batteryBar(float pct, float width = 64.0f, float height = 6.0f) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float y = p.y + (ImGui::GetTextLineHeight() - height) * 0.5f;
    const ImVec2 a(p.x, y), b(p.x + width, y + height);
    dl->AddRectFilled(a, b, ImGui::GetColorU32(ImVec4(1, 1, 1, 0.10f)), height * 0.5f);
    if (pct >= 0.0f) {
        const float w = width * (pct > 100.0f ? 1.0f : pct / 100.0f);
        const ImVec4 cc = pct < 20.0f ? col::kErr : (pct < 40.0f ? col::kWarn : col::kOk);
        dl->AddRectFilled(a, ImVec2(a.x + w, b.y), ImGui::GetColorU32(cc), height * 0.5f);
    }
    ImGui::Dummy(ImVec2(width, ImGui::GetTextLineHeight()));
}

/// 日志分级着色：一眼区分"成功 / 失败 / 指令 / 提示"
ImVec4 logColor(const std::string& s) {
    if (s.find("失败") != std::string::npos || s.find("[错误]") != std::string::npos ||
        s.find("WARN") != std::string::npos || s.find("异常") != std::string::npos ||
        s.find("超时") != std::string::npos || s.find("掉线") != std::string::npos)
        return col::kErr;
    if (s.find("[急停]") != std::string::npos) return ImVec4(1.00f, 0.57f, 0.25f, 1.00f);
    if (s.find("成功") != std::string::npos || s.find("就绪") != std::string::npos ||
        s.find("已打开") != std::string::npos)
        return col::kOk;
    if (s.find("[回执]") != std::string::npos) return ImVec4(0.62f, 0.78f, 0.98f, 1.00f);
    if (s.find("[指令]") != std::string::npos) return ImVec4(0.74f, 0.78f, 0.86f, 1.00f);
    if (s.find("[UI]") != std::string::npos || s.find("[动作库]") != std::string::npos ||
        s.find("[App]") != std::string::npos || s.find("[扫描]") != std::string::npos)
        return col::kDim;
    return col::kText;
}

/// 是否属于"需要关注"的行（用于「只看异常」过滤）
bool isProblemLine(const std::string& s) {
    return s.find("失败") != std::string::npos || s.find("[错误]") != std::string::npos ||
           s.find("WARN") != std::string::npos || s.find("异常") != std::string::npos ||
           s.find("超时") != std::string::npos || s.find("掉线") != std::string::npos ||
           s.find("[急停]") != std::string::npos;
}

/// 指令读数框（深色内嵌面板，用于显示当前速度指令等）
void readout(const char* label, const char* value, ImVec4 valueColor) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    const float h = ImGui::GetTextLineHeight() + 14.0f;
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(ImVec4(0, 0, 0, 0.28f)),
                      7.0f);
    dl->AddRect(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(ImVec4(1, 1, 1, 0.07f)), 7.0f, 0,
                1.0f);
    {
        FontScope fs = fontSmall();
        dl->AddText(ImVec2(p.x + 11.0f, p.y + 7.0f), ImGui::GetColorU32(col::kDim), label);
    }
    {
        const ImVec2 ts = ImGui::CalcTextSize(value);
        dl->AddText(ImVec2(p.x + w - ts.x - 11.0f, p.y + 7.0f), ImGui::GetColorU32(valueColor),
                    value);
    }
    ImGui::Dummy(ImVec2(w, h));
}

/// 后台线程：扫描本机所有网段，发现的 Go2 自动加入列表并连接
void startScan(RobotManager& mgr, UiState& ui) {
    if (ui.scanning.exchange(true)) return;
    ui.addLog("[扫描] 启动局域网发现 ...");
    std::thread([&mgr, &ui] {
        const auto subnets = Discovery::localSubnets();
        if (subnets.empty()) {
            ui.addLog("[扫描] 未找到可用的局域网 IPv4 网卡");
            ui.scanning = false;
            return;
        }
        int added = 0;
        std::vector<std::string> toConnect;
        for (const auto& sn : subnets) {
            ui.addLog("[扫描] 本机网段 " + sn);
            for (const auto& r : Discovery::scanSubnet(
                     sn, 400, [&](const std::string& line) { ui.addLog("[扫描] " + line); })) {
                if (ui.addOrUpdate(r.ip, false)) {
                    ++added;
                    ui.addLog("[扫描] 发现 Go2: " + r.ip + " [" + r.note + "]");
                    toConnect.push_back(r.ip);
                }
            }
        }
        // 多播 SN 发现：补上跨网段/多网卡时的漏网设备，并给出 SN
        for (const auto& kv : Discovery::multicastSnScan(
                 1500, [&](const std::string& line) { ui.addLog("[扫描] " + line); })) {
            if (ui.addOrUpdate(kv.second, false)) {
                ++added;
                ui.addLog("[扫描] 多播发现 Go2: " + kv.second + " (SN=" + kv.first + ")");
                toConnect.push_back(kv.second);
            }
        }
        if (!toConnect.empty()) {
            ui.addLog("[扫描] 错峰连接 " + std::to_string(toConnect.size()) + " 台 ...");
            mgr.connectAll(toConnect, 600);
        }
        ui.addLog("[扫描] 完成，新增 " + std::to_string(added) + " 台");
        ui.scanning = false;
    }).detach();
}

// ---------------------------------------------------------------- 操作提示
// 鼠标：悬停即弹 tooltip（和原来一样）。
// 触摸：手指按住控件时正好把 tooltip 挡住（真机上基本看不到），所以改成
//      **按住 0.6 秒**才弹，并且弹在控件上方。
// ★ 关键：看过说明的那一下**不再算点击** —— 否则"想看说明"就变成"执行操作"了
//   （对急停/阻尼这类安全项尤其不能忍）。用法：
//       const bool hit = ImGui::Button(...);
//       helpTip("说明");
//       if (hit && !takeTipShown()) { ...真正执行... }
bool g_touchUi = false;          ///< 本帧的输入方式，由 drawUi 每帧写入
double g_tipHoldStart = 0.0;     ///< 本次按住的起点
bool g_tipShownInPress = false;  ///< 本次按住期间是否弹过说明

/// 取走"本次按住弹过说明"的标记（取走后清空，每次只生效一次）
bool takeTipShown() {
    const bool v = g_tipShownInPress;
    g_tipShownInPress = false;
    return v;
}

/// 把提示画在控件**上方**（手指在控件上，上方才看得见）；上方不够就改画下方
void showTipNearItem(const char* text) {
    const ImVec2 mn = ImGui::GetItemRectMin();
    const ImVec2 mx = ImGui::GetItemRectMax();
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float fs = ImGui::GetFontSize();
    float y = mn.y - 10.0f;
    ImVec2 pivot(0.0f, 1.0f);                  // pivot 在左下 → 窗口长在控件上方
    if (y < fs * 5.0f) {                       // 上方放不下 → 改画到控件下方
        y = mx.y + 10.0f;
        pivot = ImVec2(0.0f, 0.0f);
    }
    const float maxW = fs * 26.0f;
    const float x = std::min(mn.x, std::max(0.0f, ds.x - maxW - fs * 2.0f));
    ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always, pivot);
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(maxW);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

/// 统一的操作提示：鼠标悬停显示；触摸下长按 0.6 秒显示
void helpTip(const char* text) {
    if (!ImGui::IsItemHovered()) {
        g_tipHoldStart = 0.0;
        g_tipShownInPress = false;  // 指针移开 → 标记作废，避免影响下一次点击
        return;
    }
    if (!g_touchUi) {
        ImGui::SetTooltip("%s", text);
        return;
    }
    if (ImGui::IsItemActive()) {
        const double now = ImGui::GetTime();
        if (g_tipHoldStart <= 0.0) {  // 新一次按住：重新计时并清掉上次的标记
            g_tipHoldStart = now;
            g_tipShownInPress = false;
        }
        if (now - g_tipHoldStart >= 0.6) {
            showTipNearItem(text);
            g_tipShownInPress = true;
        }
    } else {
        g_tipHoldStart = 0.0;
    }
}

/// 对勾选的机器狗执行操作，返回实际执行的台数
int forEachSelected(RobotManager& mgr, UiState& ui,
                    const std::function<bool(RobotClient&)>& fn) {
    int n = 0;
    for (const auto& ip : ui.selectedIps()) {
        if (auto* c = mgr.find(ip); c && c->isReady() && fn(*c)) ++n;
    }
    return n;
}

/// 重载本地钥匙库并对处于失败状态的机器狗自动重连
void reloadKeysAndRetry(RobotManager& mgr, UiState& ui,
                        const std::string& path = "keys.txt") {
    const auto keys = loadLocalKeysFile(path);
    mgr.addAesKeys(keys);  // 与云账号钥匙合并
    ui.addLog("[钥匙] 本地钥匙库已装载 " + std::to_string(keys.size()) + " 把（累计 " +
              std::to_string(mgr.aesKeys().size()) + " 把）");
    mgr.reconnectFailed();
}

/// 校验 32 位 hex
bool isHex32(const std::string& s) {
    if (s.size() != 32) return false;
    for (char c : s) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F')))
            return false;
    }
    return true;
}


/// 隐私模式：把 IPv4 的中间两段打码（192.168.0.169 -> 192.168.*.***）
std::string maskIps(const std::string& text, bool on) {
    if (!on || text.empty()) return text;
    static const std::regex ipRe(R"((\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3}))");
    return std::regex_replace(text, ipRe, "$1.$2.*.***");
}

// 注：原来这里有个"内嵌在面板里的摇杆"（drawJoystick）。改造后摇杆**永远悬浮在两下角**
// 且要盖在所有窗口/弹窗之上，所以改由 joystickHitArea（透明命中区）+ 前景层绘制实现，
// 那个内嵌版本已无调用方，删掉避免留死代码。

// ---- 下面这个函数要被触屏入口（main_android.cpp）调用 → 必须在匿名命名空间**之外**，
//      否则是内部链接，链接期报 undefined symbol（踩过一次）。
}  // namespace  ← 临时关闭匿名命名空间

void drawJoystickAt(const char* id, ImDrawList* dl, float cx, float cy, float radius, float x,
                    float y, bool active) {
    if (!dl) return;
    const ImVec2 c(cx, cy);
    constexpr float kPi = 3.14159265f;

    // ---- 底座：深色盘 + 内圈 + 刻度点 ----
    dl->AddCircleFilled(c, radius, ImGui::GetColorU32(ImVec4(0, 0, 0, 0.32f)));
    dl->AddCircleFilled(c, radius - 1.5f, ImGui::GetColorU32(ImVec4(1, 1, 1, 0.035f)));
    dl->AddCircle(c, radius, ImGui::GetColorU32(ImVec4(1, 1, 1, 0.10f)), 0, 1.5f);
    dl->AddCircle(c, radius * 0.60f, ImGui::GetColorU32(ImVec4(1, 1, 1, 0.05f)), 0, 1.0f);
    for (int i = 0; i < 8; ++i) {
        const float a = i * kPi / 4.0f;
        const ImVec2 t(c.x + std::cos(a) * (radius - 8.0f), c.y + std::sin(a) * (radius - 8.0f));
        dl->AddCircleFilled(t, 1.7f, ImGui::GetColorU32(ImVec4(1, 1, 1, 0.16f)));
    }
    // 十字辅助线
    dl->AddLine(ImVec2(c.x - radius + 12.0f, c.y), ImVec2(c.x + radius - 12.0f, c.y),
                ImGui::GetColorU32(ImVec4(1, 1, 1, 0.07f)), 1.0f);
    dl->AddLine(ImVec2(c.x, c.y - radius + 12.0f), ImVec2(c.x, c.y + radius - 12.0f),
                ImGui::GetColorU32(ImVec4(1, 1, 1, 0.07f)), 1.0f);
    // 上方"前进"指示三角
    dl->AddTriangleFilled(ImVec2(c.x, c.y - radius + 7.0f),
                          ImVec2(c.x - 6.0f, c.y - radius + 17.0f),
                          ImVec2(c.x + 6.0f, c.y - radius + 17.0f),
                          ImGui::GetColorU32(ImVec4(1, 1, 1, 0.22f)));

    // ---- 旋钮：光晕 + 实心 + 高光 ----
    const ImVec2 knob(c.x + x * (radius - 18.0f), c.y + y * (radius - 18.0f));
    const ImVec4 base =
        active ? ImVec4(0.98f, 0.55f, 0.28f, 1.0f) : ImVec4(0.29f, 0.56f, 0.99f, 1.0f);
    dl->AddCircleFilled(knob, 21.0f, ImGui::GetColorU32(ImVec4(base.x, base.y, base.z, 0.20f)));
    dl->AddCircleFilled(knob, 15.0f, ImGui::GetColorU32(base));
    dl->AddCircleFilled(ImVec2(knob.x - 4.0f, knob.y - 4.5f), 4.5f,
                        ImGui::GetColorU32(ImVec4(1, 1, 1, 0.25f)));
    dl->AddCircle(knob, 15.0f, ImGui::GetColorU32(ImVec4(1, 1, 1, 0.35f)), 0, 1.2f);
    (void)id;
}

namespace {  // ← 重新打开匿名命名空间，后面的文件内部辅助函数保持内部链接

// ============================================================ 设备列表（卡片式）
void drawDeviceList(RobotManager& mgr, UiState& ui) {
    sectionTitle("设备");
    ImGui::SameLine();
    {
        FontScope fs = fontSmall();
        ImGui::TextDisabled("已选 %d 台（%s）", ui.selectedCount(),
                            ui.selectedCount() > 1 ? "群控" : "单控");
    }
    ImGui::Spacing();

    // 快照：避免遍历期间被扫描线程 push_back 导致迭代器失效
    std::vector<RobotEntry> snapshot;
    {
        std::lock_guard<std::mutex> lock(ui.robotsMutex);
        snapshot = ui.robots;
    }

    if (snapshot.empty()) {
        FontScope fs = fontSmall();
        ImGui::TextDisabled("（列表为空）");
        ImGui::TextDisabled("· 点上方「扫描局域网」自动发现");
        ImGui::TextDisabled("· 或输入 IP 后点「添加」");
        return;
    }

    for (const auto& e : snapshot) {
        ImGui::PushID(e.ip.c_str());
        RobotClient* c = mgr.find(e.ip);
        const ConnState st = c ? c->state() : ConnState::Disconnected;
        const ImVec4 stc = stateColor(st);

        ImGui::BeginChild("card", ImVec2(0, 0),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY,
                          ImGuiWindowFlags_NoScrollbar);
        {
            // ---- 第一行：勾选 · 状态灯 · IP · 状态胶囊 · 操作 ----
            bool sel = ui.isSelected(e.ip);
            if (ImGui::Checkbox("##sel", &sel)) ui.setSelected(e.ip, sel);
            ImGui::SameLine(0, 4);
            statusDot(stc);
            ImGui::SameLine(0, 2);
            {
                FontScope fs = fontBody();
                ImGui::TextColored(stc, "%s", maskIps(e.ip, ui.privacyMode).c_str());
            }
            ImGui::SameLine(0, 8);
            chip(stateText(st), stc);
            ImGui::SameLine();
            {
                const float need = 108.0f;
                const float avail = ImGui::GetContentRegionAvail().x;
                if (avail > need) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - need);
            }
            if (st == ConnState::Disconnected || st == ConnState::Failed) {
                if (ImGui::SmallButton("连接")) mgr.connect(e.ip);
            } else {
                if (ImGui::SmallButton("断开")) mgr.disconnect(e.ip);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("移除")) {
                mgr.disconnect(e.ip);  // 内部会先停车再断开
                ui.remove(e.ip);
                ui.addLog("[UI] 已移除 " + e.ip);
            }

            // ---- 第二行：电量条 · 电量 · 模式 ----
            ImGui::Spacing();
            batteryBar(e.battery);
            ImGui::SameLine(0, 8);
            {
                FontScope fs = fontSmall();
                const std::string batt =
                    e.battery >= 0 ? (std::to_string(int(e.battery)) + "%") : "电量 -";
                ImGui::TextDisabled("%s", batt.c_str());
                ImGui::SameLine(0, 14);
                ImGui::TextDisabled("模式 %s", e.modeName.c_str());
            }

            // ---- 第三行：链路详情 ----
            if (c) {
                const LinkStats ls = c->stats();
                std::string info;
                if (ls.signalingPort) info += "端口 " + std::to_string(ls.signalingPort);
                if (ls.data2) info += "   data2=" + std::to_string(ls.data2);
                if (!ls.keySource.empty()) info += "   " + ls.keySource;
                if (ls.ready) {
                    info += "   收 " + std::to_string(ls.rxMessages);
                    if (ls.lastRxAgeMs >= 0)
                        info += "（" + std::to_string(ls.lastRxAgeMs) + "ms 前）";
                    if (ls.rttMs >= 0) info += "   RTT " + std::to_string(ls.rttMs) + "ms";
                    if (ls.readySeconds > 0)
                        info += "   稳定 " + std::to_string(ls.readySeconds) + "s";
                    if (ls.reconnects > 0) info += "   重连 " + std::to_string(ls.reconnects);
                }
                FontScope fs = fontSmall();
                ImGui::TextDisabled("%s", info.c_str());

                if (st == ConnState::Failed) {
                    const std::string err = c->lastError();
                    if (!err.empty()) {
                        // 错误信息可能多行，逐行显示
                        size_t p = 0;
                        while (p <= err.size()) {
                            size_t nl = err.find('\n', p);
                            const std::string line =
                                nl == std::string::npos ? err.substr(p) : err.substr(p, nl - p);
                            if (!line.empty())
                                ImGui::TextColored(col::kErr, "%s", line.c_str());
                            if (nl == std::string::npos) break;
                            p = nl + 1;
                        }
                    }
                    if (ImGui::SmallButton("重连")) mgr.connect(e.ip);
                }
            }
        }
        ImGui::EndChild();
        ImGui::Spacing();
        ImGui::PopID();
    }
}

}  // namespace

// ============================================================================
// 界面：遥控为主 + 两下角悬浮摇杆 + 四个弹窗（设备 / 动作库 / 设置 / 日志）
//
// ★ 设计要点（2026-09-24 按用户反馈重做）：
//   · 双摇杆**永远悬浮在屏幕两个下角**，画在**前景层** —— 任何面板、任何弹窗都盖不住它，
//     也不该有任何页面"影响"到它（数值由平台层触屏或多点触控驱动）
//   · 主界面只有遥控；设备 / 动作库 / 设置 / 日志 全部是顶栏按钮 → 弹窗
//   · 动作库在弹窗里**一排排按钮平铺**，不再用折叠面板
// ============================================================================

namespace {

// 弹窗 ID（同一个 ID 栈里要唯一）
constexpr const char* kIdDevices = "设备##dlg";
constexpr const char* kIdActions = "动作库##dlg";
constexpr const char* kIdSettings = "设置##dlg";
constexpr const char* kIdLog = "日志##dlg";

// 弹窗统一去掉标题栏：标题 + 关闭按钮由各面板自己的 header() 画，
// 否则模态框自带的标题栏会和它重复（视觉上出现两个"设备"）。
constexpr ImGuiWindowFlags kPopupFlags = ImGuiWindowFlags_NoResize |
                                         ImGuiWindowFlags_NoMove |
                                         ImGuiWindowFlags_NoTitleBar |
                                         ImGuiWindowFlags_NoSavedSettings;

// ---------------------------------------------------------------- 顶栏
// 品牌 + 受控状态 + 四个入口按钮（设备 / 动作库 / 设置 / 日志）
void drawTopBar(RobotManager& mgr, UiState& ui, const LayoutSpec& L) {
    (void)mgr;
    ImGui::BeginChild("top", ImVec2(0, L.topBarH), ImGuiChildFlags_Borders);

    if (L.showBrand) {
        FontScope fs = fontTitle();
        ImGui::TextUnformatted("Go2 控制台");
        ImGui::SameLine();
    }
    {
        const int sel = ui.selectedCount();
        const int total = static_cast<int>(ui.robots.size());
        const std::string s = std::to_string(sel) + "/" + std::to_string(total) + " 台受控";
        chip(s.c_str(), sel > 0 ? col::kAccent : col::kIdle);
        ImGui::SameLine();
    }

    // 四个入口按钮靠右排
    const float need = L.topBtnW * 4.0f + 24.0f;
    const float avail = ImGui::GetContentRegionAvail().x;
    if (avail > need) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - need);

    const ImVec2 bs(L.topBtnW, L.topBtnH);
    // 按钮只登记"一次性请求"，真正的 OpenPopup 由 drawUi 在主窗口层级做 ——
    // 在子窗口里直接 OpenPopup 会因为 ID 栈前缀不同而和 BeginPopupModal 对不上。
    // badge > 0 时在按钮右上角画一个红色角标（用于「日志」提醒有新异常）。
    const auto entry = [&](const char* label, int req, bool open, ImVec4 tint, int badge = 0) {
        if (open) ImGui::PushStyleColor(ImGuiCol_Button, tint);
        const bool hit = ImGui::Button(label, bs);
        if (open) ImGui::PopStyleColor();
        if (hit) ui.popupRequest = req;
        if (badge > 0) {
            char t[8];
            std::snprintf(t, sizeof(t), badge > 99 ? "99+" : "%d", badge);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 mn = ImGui::GetItemRectMin();
            const ImVec2 mx = ImGui::GetItemRectMax();
            const float fs = ImGui::GetFontSize() * 0.72f;
            const ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.0f, t);
            const float r = std::max(9.0f, ts.x * 0.5f + 5.0f);
            const ImVec2 c(mx.x - r * 0.55f, mn.y + r * 0.55f);
            dl->AddCircleFilled(c, r, ImGui::GetColorU32(col::kErr), 16);
            dl->AddText(ImGui::GetFont(), fs, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f),
                        ImGui::GetColorU32(ImVec4(1, 1, 1, 1)), t);
        }
    };

    // 有新异常时给「日志」按钮加角标（日志弹窗没打开也能发现出错）
    const int unread = (ui.problemCount > ui.problemSeen) ? (ui.problemCount - ui.problemSeen) : 0;

    entry("设备", 1, ui.showDevices,
          ImVec4(col::kAccent.x, col::kAccent.y, col::kAccent.z, 0.75f));
    ImGui::SameLine();
    entry("动作库", 2, ui.showActions,
          ImVec4(col::kAccent.x, col::kAccent.y, col::kAccent.z, 0.75f));
    ImGui::SameLine();
    entry("设置", 3, ui.showSettings,
          ImVec4(col::kAccent.x, col::kAccent.y, col::kAccent.z, 0.75f));
    ImGui::SameLine();
    entry("日志", 4, ui.showLog, ImVec4(col::kWarn.x, col::kWarn.y, col::kWarn.z, 0.75f), unread);

    ImGui::EndChild();
}

// ---------------------------------------------------------------- 设备弹窗
void drawDevicePanel(RobotManager& mgr, UiState& ui, const LayoutSpec& L) {
    // ---- 发现 / 连接（这些按钮原来在顶栏第二行，现在收进设备弹窗）----
    const auto doAddManual = [&mgr, &ui] {
        // 支持一次粘贴多台（逗号 / 分号 / 空格分隔）
        std::vector<std::string> ips;
        std::string cur;
        const std::string text(ui.manualIp);
        for (char ch : text) {
            if (ch == ',' || ch == ';' || ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
                if (!cur.empty()) ips.push_back(cur);
                cur.clear();
            } else {
                cur += ch;
            }
        }
        if (!cur.empty()) ips.push_back(cur);

        if (ips.empty()) {
            ui.addLog("[UI] 请先输入至少一个 IP 地址");
            return;
        }
        std::vector<std::string> toConnect;
        for (const auto& ip : ips) {
            if (ui.addOrUpdate(ip, true)) {
                ui.addLog("[UI] 手动添加 " + ip);
                toConnect.push_back(ip);
            } else {
                ui.addLog("[UI] " + ip + " 已在列表中");
            }
        }
        if (!toConnect.empty()) {
            ui.addLog("[UI] 开始错峰连接 " + std::to_string(toConnect.size()) +
                      " 台（机器狗信令服务单线程，避免同时握手）");
            mgr.connectAll(toConnect, 600);
        }
    };
    const auto doConnectAll = [&mgr, &ui] {
        std::vector<RobotEntry> snapshot;
        {
            std::lock_guard<std::mutex> lock(ui.robotsMutex);
            snapshot = ui.robots;
        }
        std::vector<std::string> ips;
        for (const auto& e : snapshot) ips.push_back(e.ip);
        if (ips.empty()) {
            ui.addLog("[UI] 设备列表为空，先扫描或手动添加");
        } else {
            ui.addLog("[UI] 全部连接 " + std::to_string(ips.size()) + " 台（错峰 600ms）");
            mgr.connectAll(ips, 600);
        }
    };
    const auto doDisconnectAll = [&mgr, &ui] {
        mgr.disconnectAll();
        ui.addLog("[UI] 已断开全部连接");
    };

    const float bh = L.btnH;
    if (ui.scanning.load()) {
        ImGui::BeginDisabled();
        bigButton("扫描中...", ImVec2(-1, bh));
        ImGui::EndDisabled();
    } else if (accentButton("扫描局域网", ImVec2(-1, bh))) {
        startScan(mgr, ui);
    }

    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##manual", "手动添加 IP（可逗号分隔多台）", ui.manualIp,
                             sizeof(ui.manualIp));
    const float third = (ImGui::GetContentRegionAvail().x - 16.0f) / 3.0f;
    if (bigButton("添加", ImVec2(third, bh))) doAddManual();
    ImGui::SameLine();
    if (bigButton("全部连接", ImVec2(third, bh))) doConnectAll();
    ImGui::SameLine();
    if (bigButton("全部断开", ImVec2(third, bh))) doDisconnectAll();

    ImGui::Separator();
    drawDeviceList(mgr, ui);
}

// ---------------------------------------------------------------- 动作库弹窗
// 一排排按钮平铺：分组只作为小标题，不再折叠收起
void drawActionLibrary(RobotManager& mgr, UiState& ui, const LayoutSpec& L) {
    const float actW1 = L.actW1;
    const float actW2 = L.actW2;
    const float actW3 = L.actW3;
    const float actH = L.actH;
// ---- 宇树动作库（全量指令，见 sport_library.cpp）----
sectionTitle("宇树动作库");
ImGui::SameLine();
if (ImGui::Checkbox("MCF 固件", &ui.mcfMode) && takeTipShown())
    ui.mcfMode = !ui.mcfMode;  // 长按看说明 → 撤销这次切换
helpTip("Go2 Pro 等 MCF 固件使用另一套 api_id（如后空翻 2043 vs 1044）；\n"
        "选错也没关系：被拒后会用另一套 id 自动重试一次");
ImGui::SameLine();
if (ImGui::Checkbox("隐藏不支持的", &ui.hideUnsupported) && takeTipShown())
    ui.hideUnsupported = !ui.hideUnsupported;  // 长按看说明 → 撤销这次切换
helpTip("隐藏「试过且被该固件拒绝（code=3203）」的动作，\n"
        "避免反复点到不存在的指令；取消勾选即可重新显示");

// 当前指令集没有这条（如 MCF 专属指令）时，用另一套 id 兜底，避免"整条动作根本点不到"
auto resolveId = [&ui](const SportAction& a, bool* fellBack) {
    int id = apiIdFor(a, ui.mcfMode);
    bool fb = false;
    if (id == 0) {
        const int alt = ui.mcfMode ? a.normalId : a.mcfId;
        if (alt != 0) {
            id = alt;
            fb = true;
        }
    }
    if (fellBack) *fellBack = fb;
    return id;
};

// 动作按钮标题：* = MCF 下 id 不同，† = 用了另一套指令集，✓/✗ = 上次执行结果
auto actionLabel = [&ui](const SportAction& a, int id, bool fellBack) {
    std::string t = a.label;
    if (differsInMcf(a)) t += "*";
    if (fellBack) t += "†";
    int code = 0;
    std::string note;
    if (ui.apiResult(id, &code, &note)) t += (code == 0 ? "  ✓" : "  ✗");
    return t;
};
// 悬停显示上次结果与失败原因
auto actionTip = [&ui](int id) {
    int code = 0;
    std::string note;
    if (!ui.apiResult(id, &code, &note)) return;
    char buf[512];
    if (code == 0) {
        std::snprintf(buf, sizeof(buf), "上次执行：成功（api %d）", id);
    } else {
        std::snprintf(buf, sizeof(buf),
                      "上次执行失败（api %d，code=%d）\n%s\n\n"
                      "再点一次可重试；指令集不匹配会自动换另一套 api_id",
                      id, code, note.empty() ? "（无更多信息）" : note.c_str());
    }
    helpTip(buf);
};

// 可用性一览：点过的动作会累计成功/失败，方便"哪些动作能用"一眼看清
{
    int tried = 0, ok = 0, fail = 0;
    for (const auto& a : sportActions()) {
        bool fb = false;
        const int id = resolveId(a, &fb);
        if (id == 0) continue;
        int code = 0;
        std::string note;
        if (!ui.apiResult(id, &code, &note)) continue;
        ++tried;
        (code == 0 ? ok : fail)++;
    }
    if (tried > 0)
        ImGui::Text("可用性: 已试 %d 条 / 成功 %d / 失败 %d", tried, ok, fail);
    else
        ImGui::TextDisabled("可用性: 点过的动作会自动标注 ✓ / ✗（悬停看失败原因）");
}

ImGui::BeginDisabled(ui.selectedCount() == 0);

// 发送一条动作：解析指令集 id + 组装参数 + 群控分发
// flagValue 只对 Flag 类生效（开关型指令传 false 就是"关闭"）
auto sendAction = [&](const SportAction& a, bool flagValue = true) {
    // 触摸下"长按看说明"的那一下不算执行 —— 否则想看说明就变成了真的下发动作
    if (takeTipShown()) return;
    bool fellBack = false;
    const int id = resolveId(a, &fellBack);
    const std::string key = a.key;
    float realVal = ui.bodyHeight;
    if (key == "FootRaiseHeight") realVal = ui.footRaise;
    const int intVal = (key == "SwitchGait") ? ui.gaitType : ui.speedLevel;
    nlohmann::json p = buildSportParam(a, intVal, realVal, ui.eulerX, ui.eulerY,
                                       ui.eulerZ, std::string(ui.rawJson));
    if (a.param == SportParam::Flag) {
        p = nlohmann::json::object();
        p["data"] = flagValue;   // 开关型：{"data": true|false}
    }
    const int n = forEachSelected(
        mgr, ui, [&](RobotClient& c) { return c.sendSportCommand(id, p); });
    ui.addLog("[动作库] " + std::string(a.label) +
              (a.toggle ? (flagValue ? " 开启" : " 关闭") : "") + " (api " +
              std::to_string(id) + (fellBack ? "，当前指令集无此条，用另一套 id" : "") +
              ") → " + std::to_string(n) + " 台");
};

struct GroupDef {
    SportGroup g;
    const char* title;
};
static const GroupDef kGroups[] = {
    {SportGroup::Basic, "基础姿态"},
    {SportGroup::Show, "表演动作"},
    {SportGroup::Gait, "步态 / 速度 / 身高"},
    {SportGroup::Stunt, "跳跃特技（危险）"},
    {SportGroup::Query, "状态查询"},
    {SportGroup::Advanced, "其他 / 进阶"},
};

for (const auto& gd : kGroups) {
    // 基础姿态默认展开：最常用的 站立 / 平衡站立 / 趴下 / 停止移动 都在这一组
    // 分组只作小标题：不折叠，按钮直接一排排平铺
    ImGui::Spacing();
    {
        FontScope fs = fontBody();
        ImGui::TextColored(col::kAccent, "%s", gd.title);
    }
    int col = 0;
    for (const auto& a : sportActions()) {
        if (a.group != gd.g) continue;
        bool fellBack = false;
        const int id = resolveId(a, &fellBack);
        if (id == 0) continue;  // 两套指令集都没有这条
        // 阻尼（Damp）只在中间「遥控」面板保留一个按钮 → 这里跳过，避免同一个功能两处按钮
        if (std::strcmp(a.key, "Damp") == 0) continue;
        if (ui.hideUnsupported) {
            int code = 0;
            std::string note;
            if (ui.apiResult(id, &code, &note) && code != 0) continue;  // 隐藏已确认不支持的
        }
        ImGui::PushID(a.key);

        const std::string label = actionLabel(a, id, fellBack);

        // ---- 开关型（持续模式）：画成 开/关 按钮，再点一次即关闭 ----
        // 这类指令是 on/off 语义、会一直生效；StopMove 停不掉，必须带 false 关闭。
        if (a.toggle) {
            bool& on = ui.toggles[a.key];
            if (on)
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.55f, 0.28f, 1.0f));
            const std::string t = label + (on ? "  [开]" : "  [关]");
            if (ImGui::Button((t + "##b").c_str(), ImVec2(actW1, actH)) && !takeTipShown()) {
                on = !on;
                sendAction(a, on);
                // 记录"真正开过的开关"（含另一套指令集的 id），急停时只关这些
                std::vector<int> ids{id};
                for (int alt : alternateApiIds(id)) ids.push_back(alt);
                for (int tid : ids) {
                    if (on) ui.activeToggleIds.insert(tid);
                    else ui.activeToggleIds.erase(tid);
                }
            }
            actionTip(id);
            helpTip("持续模式开关（开启后会一直生效，StopMove 停不掉）\n"
                    "点一下切换开/关；急停会自动把所有开关关掉");
            if (on) ImGui::PopStyleColor();
            col = 0;
            ImGui::PopID();
            continue;
        }

        if (a.param == SportParam::Int || a.param == SportParam::Real) {
            const std::string key = a.key;
            if (a.param == SportParam::Int) {
                int* v = (key == "SwitchGait") ? &ui.gaitType : &ui.speedLevel;
                ImGui::SetNextItemWidth(90);
                ImGui::SliderInt("##v", v, 0, (key == "SwitchGait") ? 4 : 2);
            } else {
                float* v =
                    (key == "FootRaiseHeight") ? &ui.footRaise : &ui.bodyHeight;
                ImGui::SetNextItemWidth(90);
                ImGui::SliderFloat("##v", v, 0.0f, 0.35f, "%.2f");
            }
            ImGui::SameLine();
            if (ImGui::Button((label + "##b").c_str(), ImVec2(actW2, actH))) sendAction(a);
            actionTip(id);
            col = 0;
            ImGui::PopID();
            continue;
        }

        if (a.param == SportParam::Euler) {
            ImGui::SetNextItemWidth(62);
            ImGui::SliderFloat("roll##e", &ui.eulerX, -0.5f, 0.5f, "%.2f");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(62);
            ImGui::SliderFloat("pitch##e", &ui.eulerY, -0.5f, 0.5f, "%.2f");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(62);
            ImGui::SliderFloat("yaw##e", &ui.eulerZ, -0.6f, 0.6f, "%.2f");
            if (ImGui::Button((label + "##b").c_str(), ImVec2(-1, 0))) sendAction(a);
            actionTip(id);
            col = 0;
            ImGui::PopID();
            continue;
        }

        if (a.param == SportParam::Json) {
            ImGui::SetNextItemWidth(180);
            ImGui::InputText("##json", ui.rawJson, sizeof(ui.rawJson));
            ImGui::SameLine();
            if (ImGui::Button((label + "##b").c_str(), ImVec2(actW3, actH))) sendAction(a);
            actionTip(id);
            col = 0;
            ImGui::PopID();
            continue;
        }

        if (L.actCols > 1 && (col % L.actCols) != 0) ImGui::SameLine();
        ++col;
        if (a.risky)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.60f, 0.20f, 0.15f, 1.0f));
        if (ImGui::Button((label + "##b").c_str(), ImVec2(actW1, actH))) sendAction(a);
        actionTip(id);
        if (a.risky) ImGui::PopStyleColor();

        ImGui::PopID();
    }
}
ImGui::TextDisabled("* = MCF 固件下 api_id 不同   † = 当前指令集无此条，自动用另一套 id");
ImGui::TextDisabled("✓ = 上次成功   ✗ = 上次失败（悬停看原因；指令集不匹配会自动换一套 id 重试）");

// ---- 官方 App 常用动作快捷（高度 / 姿态 / 侧移 / 模式 / 舞蹈编排）----
// 这些在 App 里是"参数类"操作（不是独立 api_id）：用现有的 1013/1007/1008 带不同参数实现
ImGui::Spacing();
ImGui::Separator();
{  // 官方 App 快捷：同样平铺，不折叠
    auto sendJson = [&](int apiId, const nlohmann::json& p, const char* what) {
        if (takeTipShown()) return;  // 同上：长按看说明不算执行
        const int n = forEachSelected(
            mgr, ui, [&](RobotClient& c) { return c.sendSportCommand(apiId, p); });
        ui.addLog(std::string("[App] ") + what + " (api " + std::to_string(apiId) + ") → " +
                  std::to_string(n) + " 台");
    };
    auto jsonData = [](float v) {
        nlohmann::json p;
        p["data"] = v;
        return p;
    };

    // 机身高度三档（BodyHeight 1013；单位米）
    ImGui::TextDisabled("机身高度");
    if (ImGui::Button("机身最低 0.22")) sendJson(1013, jsonData(0.22f), "机身最低");
    ImGui::SameLine();
    if (ImGui::Button("机身正常 0.28")) sendJson(1013, jsonData(0.28f), "机身正常");
    ImGui::SameLine();
    if (ImGui::Button("机身最高 0.33")) sendJson(1013, jsonData(0.33f), "机身最高");
    ImGui::SameLine();
    ImGui::TextDisabled("(MCF 固件无 1013，会回复 3203)");
    helpTip("机身高度 = BodyHeight(1013)，MCF 固件的指令表里没有它\n"
            "→ 会被拒（3203）。等 id 确定后再补 MCF 的高度指令。");

    // 姿态（Euler 1007；x=roll 左正, y=pitch 低头为负）
    // 实测要点：左倾/低头这类姿态操作**只在"摆姿势(Pose 1028)"模式下生效**
    // （参考实现注明 "Pose (exit via StopMove)"），而且进入该模式有约 0.5s 的切换时间，
    // 紧跟着发 Euler 会被丢掉 → 所以「进模式 → 等 0.7s → 下发姿态角」放进后台线程。
    ImGui::TextDisabled("姿态（自动进入摆姿势模式后下发，幅度 0.25）");
    auto euler = [&](float roll, float pitch, const char* what, bool exitPose = false) {
        if (takeTipShown()) return;  // 同上：长按看说明不算执行
        const auto ips = ui.selectedIps();
        ui.addLog(std::string("[App] ") + what + "：进入摆姿势 → 0.7s 后下发姿态角");
        std::thread([&mgr, ips, roll, pitch, exitPose] {
            nlohmann::json poseOn;
            poseOn["data"] = true;
            for (const auto& ip : ips)
                if (auto* c = mgr.find(ip); c && c->isReady())
                    c->sendSportCommand(1028, poseOn);
            std::this_thread::sleep_for(std::chrono::milliseconds(700));
            nlohmann::json p;
            p["x"] = roll;
            p["y"] = pitch;
            p["z"] = 0.0f;
            for (const auto& ip : ips)
                if (auto* c = mgr.find(ip); c && c->isReady()) c->sendSportCommand(1007, p);
            if (exitPose) {  // 方向回正：再等 0.4s 用 StopMove 退出摆姿势模式
                std::this_thread::sleep_for(std::chrono::milliseconds(400));
                for (const auto& ip : ips)
                    if (auto* c = mgr.find(ip); c && c->isReady()) c->stopMove();
            }
        }).detach();
    };
    if (ImGui::Button("左倾")) euler(0.25f, 0.0f, "左倾");
    ImGui::SameLine();
    if (ImGui::Button("右倾")) euler(-0.25f, 0.0f, "右倾");
    ImGui::SameLine();
    if (ImGui::Button("低头")) euler(0.0f, -0.25f, "低头");
    ImGui::SameLine();
    if (ImGui::Button("抬头")) euler(0.0f, 0.25f, "抬头");
    ImGui::SameLine();
    if (ImGui::Button("方向回正")) euler(0.0f, 0.0f, "方向回正", true);
    helpTip("姿态角 = Euler(1007)，只在「摆姿势」模式下生效（已自动进入）。\n"
            "若回 code=0 但狗没动 = 该固件忽略姿态角；\n"
            "符号方向若相反（点左倾往右倒）告诉我，对调即可");

    // 侧移（官方 App 的"左移 / 右移"）：**按住不放持续横移，松开立即停**
    // 与左摇杆的横向轴是同一个功能，但这里保留 App 同款按住式按钮，手感更直观
    ImGui::TextDisabled("侧移（按住不放，松开即停）");
    ImGui::Button("◀ 左移（按住）", ImVec2(122, 0));
    if (ImGui::IsItemActive()) ui.sideHold = 1;
    ImGui::SameLine();
    ImGui::Button("右移（按住）▶", ImVec2(122, 0));
    if (ImGui::IsItemActive()) ui.sideHold = -1;

    // 运动模式（motion_switcher）
    ImGui::TextDisabled("运动模式");
    if (ImGui::Button("正常模式")) {
        const int n = forEachSelected(
            mgr, ui, [](RobotClient& c) { return c.setMotionMode("normal"); });
        ui.addLog("[App] 正常模式 → " + std::to_string(n) + " 台");
    }
    ImGui::SameLine();
    if (ImGui::Button("ai 模式")) {
        const int n = forEachSelected(
            mgr, ui, [](RobotClient& c) { return c.setMotionMode("ai"); });
        ui.addLog("[App] ai 模式 → " + std::to_string(n) + " 台");
    }
    ImGui::SameLine();
    if (ImGui::Button("mcf 模式")) {
        const int n = forEachSelected(
            mgr, ui, [](RobotClient& c) { return c.setMotionMode("mcf"); });
        ui.addLog("[App] mcf 模式 → " + std::to_string(n) + " 台");
    }

    // 舞蹈编排：舞 1 → 舞 2 连播
    if (ImGui::Button("舞蹈编排：舞 1 → 舞 2")) {
        const auto ips = ui.selectedIps();
        const int n = forEachSelected(
            mgr, ui, [](RobotClient& c) { return c.sendSportCommand(1022); });
        ui.addLog("[App] 舞蹈编排：舞 1 开始 → " + std::to_string(n) + " 台（12 秒后接舞 2）");
        std::thread([&mgr, ips] {
            std::this_thread::sleep_for(std::chrono::seconds(12));
            for (const auto& ip : ips)
                if (auto* c = mgr.find(ip); c && c->isReady()) c->sendSportCommand(1023);
        }).detach();
    }

    // 待验证动作候选探测：App 上有、但 id 未知的动作，逐个试并报告回执
    ImGui::TextDisabled("待验证动作（App 有、id 未知）：拜年 / 直立行走 / 并腿跑 / 原地踏步 / 翻身");
    if (ImGui::Button("候选探测（会依次尝试，狗可能有动作）")) {
        const auto ips = ui.selectedIps();
        ui.addLog("[App] 候选探测开始：确保机器狗周围安全、地面平坦");
        std::thread([&mgr, ips, &ui] {
            struct Cand {
                int id;
                const char* note;
            };
            static const Cand kCands[] = {
                {1029, "拜年候选1=作揖(Scrape)"}, {1037, "拜年候选2"}, {1038, "拜年候选3"},
                {1040, "直立行走候选1"},          {1041, "直立行走候选2"},
                {1052, "直立行走/踏步候选"},      {1053, "并腿跑候选1"},
                {1054, "并腿跑候选2"},            {1055, "并腿跑候选3"},
                {1303, "原地踏步候选=单边踏步"},  {1006, "翻身候选1=恢复站立"},
                {1300, "翻身候选2"},
            };
            for (const auto& cd : kCands) {
                for (const auto& ip : ips) {
                    if (auto* c = mgr.find(ip); c && c->isReady())
                        c->sendSportCommand(cd.id);
                }
                ui.addLog(std::string("[App] 探测 ") + cd.note + " (api " +
                          std::to_string(cd.id) + ")");
                std::this_thread::sleep_for(std::chrono::milliseconds(1800));
            }
            ui.addLog("[App] 候选探测结束：回执 code=0 的即为该动作的真实 id");
        }).detach();
    }
    ImGui::TextDisabled("　（探测结果看日志：code=0 就是命中；3203 = 该 id 不存在）");
}

        ImGui::EndDisabled();
}

// ---------------------------------------------------------------- 设置弹窗
void drawSettingsPanel(RobotManager& mgr, UiState& ui, const LayoutSpec& L) {
// ---- 本地钥匙库（data2=3 新固件；纯本地，不依赖云）----
sectionTitle("本地钥匙");
ImGui::SetNextItemWidth(std::min(280.0f, L.popupW - 150.0f));
ImGui::InputTextWithHint("##key", "32 位 hex（AES-128 key）", ui.manualKey,
                         sizeof(ui.manualKey));
ImGui::SameLine();
if (ImGui::Button("存入")) {
    std::string k(ui.manualKey);
    while (!k.empty() && (k.back() == ' ' || k.back() == '\t')) k.pop_back();
    for (auto& ch : k)
        if (ch >= 'A' && ch <= 'F') ch += 32;
    if (!isHex32(k)) {
        ui.addLog("[钥匙] 需要 32 位 hex（AES-128 key）");
    } else {
        std::ofstream f("keys.txt", std::ios::app);
        f << k << "\n";
        ui.manualKey[0] = '\0';
        ui.addLog("[钥匙] 已保存到本地 keys.txt");
        reloadKeysAndRetry(mgr, ui);
    }
}
{
    FontScope fs = fontSmall();
    ImGui::TextDisabled("每台新固件狗提取一次，永久保存；当前已加载 %d 把",
                        ui.localKeyCount);
}

ImGui::Spacing();
ImGui::Separator();
sectionTitle("说明");
{
    FontScope fs = fontSmall();
    ImGui::TextDisabled("· 空格键 = 急停（锁定式；双杆回中后可解除）");
    ImGui::TextDisabled("· 动作被拒会给出原因；指令集不匹配会自动换另一套 id 重试");
    ImGui::TextDisabled("· 「隐藏不支持的」可过滤掉该固件没有的动作");
    ImGui::TextDisabled("· 阻尼 / 急停在中间「遥控」面板");
}
}

// ---------------------------------------------------------------- 遥控主体
void drawRemotePanel(RobotManager& mgr, UiState& ui, const LayoutSpec& L) {
    const bool compactLabel = (L.widthClass == WidthClass::Compact);
    // 宽屏不把按钮拉得又长又扁：内容居中 + 宽度上限
    const float availW = ImGui::GetContentRegionAvail().x;
    if (availW > L.contentMaxW) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availW - L.contentMaxW) * 0.5f);
    }
    // ★ 高度直接扣掉「摇杆区」（负值 = 可用高度 − 该值）：
    //   这样内容**永远不会渲染到摇杆的地盘上**，两个下角永远留给摇杆。
    //   早期版本只是在末尾加一个 Dummy 留白，内容照样会滚到摇杆下面、被压住。
    ImGui::BeginChild("remote", ImVec2(L.contentMaxW, -L.joyReserve), ImGuiChildFlags_None);
sectionTitle("遥控");
ImGui::SameLine();
{
    FontScope fs = fontSmall();
    ImGui::TextDisabled("左杆移动 · 右杆转向 · 松手即停");
}
ImGui::Spacing();

// 急停：**锁定式**。按下后停全部就绪的机器狗（不只勾选的），
// 并在解除前让摇杆/快捷步彻底失效 —— 否则下一帧的 10Hz Move 重发会把车又"开起来"。
ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 11.0f);
ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.78f, 0.16f, 0.16f, 1.0f));
ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.90f, 0.22f, 0.22f, 1.0f));
ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(1.00f, 0.32f, 0.32f, 1.0f));
// 急停是安全关键操作：**永远全宽、永远在视口内**，高度按断点给（矮屏 72 / 竖屏 88 / 多栏 56~64）。
// 断线状态下也要能按 —— 所以这里不依赖任何连接状态。
const bool estopClicked =
    bigButton(compactLabel ? "■  急停" : "■  急停（全部停车 / 空格键）", ImVec2(-1, L.estopH));
// 记下安全操作区的屏幕矩形：触屏入口拿它做摇杆抓取互斥（手指落在急停上不能被摇杆吃掉）
ui.safetyMinX = ImGui::GetItemRectMin().x;
ui.safetyMinY = ImGui::GetItemRectMin().y;
ui.safetyMaxX = ImGui::GetItemRectMax().x;
ui.safetyMaxY = ImGui::GetItemRectMax().y;
ImGui::PopStyleColor(3);
ImGui::PopStyleVar();
// 键盘急停：空格（正在输入框里打字时不触发）
const bool estopHotkey =
    !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Space, false);

if (estopClicked || estopHotkey) {
    ui.estop = true;
    std::vector<RobotEntry> snapshot;
    {
        std::lock_guard<std::mutex> lock(ui.robotsMutex);
        snapshot = ui.robots;
    }
    // 直接（同步、最快）：先停速度
    int n = 0;
    for (const auto& e : snapshot)
        if (auto* c = mgr.find(e.ip); c && c->isReady() && c->stopMove()) ++n;

    // 只关"我们真正打开过"的开关 —— 连按急停也不会把通道灌爆
    // （上一版每次关 20 个，连按十几次 → 260 条指令把 SCTP 队列打满，指令反而被丢）
    const std::vector<int> toClose(ui.activeToggleIds.begin(), ui.activeToggleIds.end());
    if (!ui.estopBusy.exchange(true)) {
        std::thread([&mgr, &ui, snapshot, toClose] {
            // 狗在执行动作时可能吞掉第一条 StopMove → 补发两次
            for (int round = 0; round < 2; ++round) {
                std::this_thread::sleep_for(std::chrono::milliseconds(330));
                for (const auto& e : snapshot)
                    if (auto* c = mgr.find(e.ip); c && c->isReady()) c->stopMove();
            }
            if (!toClose.empty())
                for (const auto& e : snapshot)
                    if (auto* c = mgr.find(e.ip); c && c->isReady())
                        c->disablePersistentModes(toClose);
            ui.estopBusy = false;
        }).detach();
    }
    for (auto& kv : ui.toggles) kv.second = false;
    ui.activeToggleIds.clear();
    ui.movingSent = false;
    ui.addLog("[急停] 已锁定：停车 " + std::to_string(n) + " 台 + 关闭 " +
              std::to_string(toClose.size()) + " 个已开启的模式；连按不会叠加");
}

// 兜底：狗仍在自走时，阻尼是唯一能"立即停住"的手段 —— 狗会软腿趴下，需二次确认
// 阻尼与急停保持 ≥12dp 间距（靠 ItemSpacing 缩放后天然满足），避免误触
{
    if (!ui.dampArmed) {
        if (ImGui::Button("强制阻尼 (Damp)…", ImVec2(-1, L.dampH)) && !takeTipShown())
            ui.dampArmed = true;
        helpTip("兜底手段：切断电机力矩，狗会立刻软腿趴下\n"
                "（地面上安全；在桌上/台阶边别用）");
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.35f, 0.10f, 1.0f));
        if (ImGui::Button("确认：立即阻尼（狗会趴下）", ImVec2(-1, L.dampH))) {
            int m = 0;
            std::vector<RobotEntry> snap;
            {
                std::lock_guard<std::mutex> lock(ui.robotsMutex);
                snap = ui.robots;
            }
            for (const auto& e : snap)
                if (auto* c = mgr.find(e.ip); c && c->isReady() && c->damp()) ++m;
            ui.addLog("[急停] 强制阻尼 → " + std::to_string(m) + " 台");
            ui.dampArmed = false;
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::SmallButton("取消")) ui.dampArmed = false;
    }
    // 阻尼按钮也算安全操作区（它是"狗仍在自走"时唯一能立即停住的手段）
    ui.safetyMaxY = std::max(ui.safetyMaxY, ImGui::GetItemRectMax().y);
}

if (ui.estop) {
    const bool centered = std::fabs(ui.joyLx) < 0.02f && std::fabs(ui.joyLy) < 0.02f &&
                          std::fabs(ui.joyRx) < 0.02f;
    ImGui::TextColored(kRed, "⛔ 急停锁定中");
    // 窄屏软换行：这行字不换行会横向溢出（改造前手机上直接看不到后半句）
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
    ImGui::TextColored(kYellow,
                       "　一次性动作（舞蹈/空翻/拜年）固件不接受打断；"
                       "狗仍在动就点上面「强制阻尼」立即停住");
    ImGui::PopTextWrapPos();
    ImGui::BeginDisabled(!centered);
    if (bigButton(compactLabel ? "解除急停" : "解除急停（需双杆回中）",
                  ImVec2(-1, L.btnH > 0.0f ? L.btnH + 18.0f : 34.0f))) {
        ui.estop = false;
        ui.movingSent = false;
        ui.addLog("[急停] 已解除，可以继续遥控");
    }
    ImGui::EndDisabled();
    if (!centered)
        ImGui::TextColored(kYellow, "  双杆回中后才能解除急停");
}

ImGui::Spacing();

const bool canMove = ui.selectedCount() > 0;

ImGui::Spacing();
// ---- 参数：宽屏常显；矮屏 / 单栏收进「参数」折叠区，把纵向空间让给急停和摇杆 ----
// （「步速」滑条在下面的快捷区里，那里本来就要跟方向键放一起，所以不重复）
const auto drawParamSliders = [&] {
    ImGui::BeginDisabled(!canMove);
    ImGui::SetNextItemWidth(L.sliderW);
    ImGui::SliderFloat("线速度上限", &ui.maxLinSpeed, 0.05f, 1.5f, "%.2f m/s");
    ImGui::SetNextItemWidth(L.sliderW);
    ImGui::SliderFloat("转向角速度", &ui.yawRate, 0.2f, 2.0f, "%.2f rad/s");
    ImGui::EndDisabled();
};
if (L.paramInline) {
    drawParamSliders();
} else if (ImGui::CollapsingHeader("参数（线速度 / 角速度）")) {
    drawParamSliders();
}

// ---- 快捷单步（急停锁定期间不可用）----
ImGui::Spacing();
ImGui::TextDisabled("快捷（一次下发，速度持续到下一条指令）");
ImGui::BeginDisabled(!canMove || ui.estop);
{
    const float halfW = (ImGui::GetContentRegionAvail().x - 9.0f) * 0.5f;
    // 触摸时按钮高按断点给（≥48dp）；鼠标保持原来的 38
    const ImVec2 bs(halfW, L.btnH > 0.0f ? L.btnH + 6.0f : 38.0f);
    if (ImGui::Button("▲  前进", bs)) {
        const int n = forEachSelected(mgr, ui,
            [v = ui.speedScale](RobotClient& c) { return c.move(v, 0, 0); });
        ui.addLog("[群控] 前进 → " + std::to_string(n) + " 台");
    }
    ImGui::SameLine();
    if (ImGui::Button("◀  左转", bs)) {
        const int n = forEachSelected(mgr, ui,
            [v = ui.speedScale](RobotClient& c) { return c.move(0, 0, v); });
        ui.addLog("[群控] 左转 → " + std::to_string(n) + " 台");
    }
    if (ImGui::Button("▼  后退", bs)) {
        const int n = forEachSelected(mgr, ui,
            [v = ui.speedScale](RobotClient& c) { return c.move(-v, 0, 0); });
        ui.addLog("[群控] 后退 → " + std::to_string(n) + " 台");
    }
    ImGui::SameLine();
    if (ImGui::Button("右转  ▶", bs)) {
        const int n = forEachSelected(mgr, ui,
            [v = ui.speedScale](RobotClient& c) { return c.move(0, 0, -v); });
        ui.addLog("[群控] 右转 → " + std::to_string(n) + " 台");
    }
    ImGui::SetNextItemWidth(std::min(150.0f, L.sliderW * 0.6f));
    ImGui::SliderFloat("步速", &ui.speedScale, 0.05f, 1.5f, "%.2f");
}
ImGui::EndDisabled();

// ---- 当前指令与发送节拍 ----
// 决策全部交给纯函数 planMotion（急停语义有单元测试保证，见 tests/motion_test.cpp）
// 侧移按钮（按住）折算成左杆的横向分量：协议 y 左为正 → 按住"左移" = vy>0
float lxEff = ui.joyLx;
if (ui.sideHold != 0 && std::fabs(ui.joyLx) < 1e-3f) lxEff = -0.55f * float(ui.sideHold);
const MotionPlan plan = planMotion(lxEff, ui.joyLy, ui.joyRx, ui.joyRy, ui.estop,
                                   ui.maxLinSpeed, ui.yawRate, ui.movingSent);
const int mask = (std::fabs(lxEff) > 1e-3f || std::fabs(ui.joyLy) > 1e-3f ? 1 : 0) |
                 (std::fabs(ui.joyRx) > 1e-3f ? 2 : 0);
ui.cmdVx = plan.vx;
ui.cmdVy = plan.vy;
ui.cmdVz = plan.vz;

ImGui::Spacing();
{
    char buf[96];
    std::snprintf(buf, sizeof(buf), "vx %+.2f   vy %+.2f   vz %+.2f", plan.vx, plan.vy,
                  plan.vz);
    const char* state = ui.estop ? "急停锁定 · 未下发"
                                 : (plan.send ? "下发中 · 10Hz" : "松手即停");
    const ImVec4 sc = ui.estop ? col::kErr : (plan.send ? col::kOk : col::kIdle);
    readout(state, buf, sc);
}

if (canMove) {
    const double now = ImGui::GetTime();
    const bool sticksChanged = (mask != ui.activeMask);  // 松手/换杆：立即生效，不等下一个节拍
    if (plan.send && (sticksChanged || now - ui.lastMoveSend >= 0.1)) {  // 10 Hz 群发
        const float vx = plan.vx, vy = plan.vy, vz = plan.vz;
        const int n = forEachSelected(mgr, ui,
            [vx, vy, vz](RobotClient& c) { return c.move(vx, vy, vz); });
        ui.lastMoveSend = now;
        if (n == 0) ui.movingSent = false;
    }
    if (plan.stop) {
        const int n = forEachSelected(mgr, ui,
            [](RobotClient& c) { return c.stopMove(); });  // 松手/急停 → 勾选的狗全部停车
        if (n > 0)
            ui.addLog(std::string(ui.estop ? "[急停] 停车 → " : "[群控] 松开摇杆，停车 → ") +
                      std::to_string(n) + " 台");
    }
    ui.movingSent = plan.send;
    ui.activeMask = mask;
} else {
    ui.movingSent = false;
    ui.activeMask = 0;
}

// 单栏模式：底部给两个**悬浮**摇杆留出空间，免得面板内容被它们压住看不见。
// 预留高度来自 layout（与摇杆圆心同一个公式算出来），不再是写死的 250 ——
// 改造前这两处是两套算法，屏幕一变就错位（矮屏上摇杆压住「快捷」按钮）。
    // 内容区高度已经扣掉了摇杆区（见 drawRemotePanel 的 BeginChild），
    // 所以这里不再需要末尾留白 —— 内容根本不会进入两个下角。
    ImGui::EndChild();
}

// ---------------------------------------------------------------- 日志弹窗
void drawLogPanel(UiState& ui, const LayoutSpec& L) {
    (void)L;
{
    FontScope fs = fontSmall();
    ImGui::Checkbox("自动滚动", &ui.autoScroll);
    ImGui::SameLine();
    ImGui::Checkbox("只看异常", &ui.logErrorsOnly);
    ImGui::SameLine();
    if (ImGui::SmallButton("清空")) {
        std::lock_guard<std::mutex> lock(ui.logMutex);
        ui.logs.clear();
    }
}
ImGui::Spacing();
// 窄栏/单栏不再用横向滚动条（滚来滚去根本没法读），改成软换行
const bool wrapLog = true;  // 弹窗里一律软换行
ImGui::BeginChild("logscroll", ImVec2(0, 0), ImGuiChildFlags_None,
                  wrapLog ? ImGuiWindowFlags_None : ImGuiWindowFlags_HorizontalScrollbar);
{
    std::lock_guard<std::mutex> lock(ui.logMutex);
    FontScope fs = fontSmall();
    for (const auto& rawLine : ui.logs) {
        const std::string line = maskIps(rawLine, ui.privacyMode);
        if (ui.logErrorsOnly && !isProblemLine(line)) continue;
        ImGui::PushStyleColor(ImGuiCol_Text, logColor(line));
        if (wrapLog) ImGui::TextWrapped("%s", line.c_str());
        else ImGui::TextUnformatted(line.c_str());
        ImGui::PopStyleColor();
    }
}
if (ui.autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
    ImGui::SetScrollHereY(1.0f);
ImGui::EndChild();
}

// ---------------------------------------------------------------- 四个弹窗
void drawPopups(RobotManager& mgr, UiState& ui, const LayoutSpec& L) {
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const auto place = [&] {
        ImGui::SetNextWindowPos(
            ImVec2((ds.x - L.popupW) * 0.5f, (ds.y - L.popupH) * 0.5f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(L.popupW, L.popupH), ImGuiCond_Always);
    };
    const auto header = [&](const char* title) {
        {
            FontScope fs = fontTitle();
            ImGui::TextUnformatted(title);
        }
        ImGui::SameLine();
        const float w = 100.0f;
        const float avail = ImGui::GetContentRegionAvail().x;
        if (avail > w) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - w);
        if (ImGui::Button("关闭", ImVec2(w, L.btnH))) ImGui::CloseCurrentPopup();
        ImGui::Separator();
    };

    place();
    if (ImGui::BeginPopupModal(kIdDevices, &ui.showDevices, kPopupFlags)) {
        header("设备");
        drawDevicePanel(mgr, ui, L);
        ImGui::EndPopup();
    }

    place();
    if (ImGui::BeginPopupModal(kIdActions, &ui.showActions, kPopupFlags)) {
        header("动作库");
        drawActionLibrary(mgr, ui, L);
        ImGui::EndPopup();
    }

    place();
    if (ImGui::BeginPopupModal(kIdSettings, &ui.showSettings, kPopupFlags)) {
        header("设置");
        drawSettingsPanel(mgr, ui, L);
        ImGui::EndPopup();
    }

    place();
    if (ImGui::BeginPopupModal(kIdLog, &ui.showLog, kPopupFlags)) {
        ui.problemSeen = ui.problemCount;  // 打开日志就算"看过了"，顶栏角标随之清掉
        header("运行日志");
        drawLogPanel(ui, L);
        ImGui::EndPopup();
    }
}

// ---------------------------------------------------------------- 悬浮双摇杆
// 只在鼠标输入时用这个（安卓触屏由平台层接管手指事件，那边自己算）
bool joystickHitArea(const char* id, ImVec2 center, float radius, float* x, float* y) {
    ImGui::SetNextWindowPos(ImVec2(center.x - radius, center.y - radius), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(radius * 2.0f, radius * 2.0f), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin(id, nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus);
    ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
    ImGui::InvisibleButton("##hit", ImVec2(radius * 2.0f, radius * 2.0f));
    const bool active = ImGui::IsItemActive();

    if (active) {
        const ImVec2 m = ImGui::GetIO().MousePos;
        const float limit = radius - 18.0f;  // 留出旋钮半径
        float nx = (m.x - center.x) / limit;
        float ny = (m.y - center.y) / limit;
        const float len = std::sqrt(nx * nx + ny * ny);
        if (len > 1.0f) { nx /= len; ny /= len; }   // 限制在圆内
        if (len < 0.08f) { nx = 0.0f; ny = 0.0f; }  // 死区
        *x = nx;
        *y = ny;
    } else {
        *x = 0.0f;  // 松手即停
        *y = 0.0f;
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    return active;
}

/// 画两个摇杆 + 处理鼠标输入。**必须在主窗口 End() 之后调用**。
/// 视觉画到前景层（GetForegroundDrawList）—— 永远盖在所有窗口与弹窗之上。
void drawJoysticks(UiState& ui, const LayoutSpec& L) {
    float lx = ui.joyLx, ly = ui.joyLy, rx = ui.joyRx, ry = ui.joyRy;
    bool lActive = ui.joyLActive, rActive = ui.joyRActive;

    if (!ui.joysticksByPlatform) {
        // 鼠标：两个小透明窗口当命中区；安卓那边手指事件在进 ImGui 之前就被平台层消费了
        const bool canMove = ui.selectedCount() > 0;
        ImGui::BeginDisabled(!canMove);
        lActive = joystickHitArea("##joyLhit", ImVec2(L.joyInsetX, L.joyCenterY), L.joyRadius,
                                  &lx, &ly);
        rActive = joystickHitArea("##joyRhit",
                                  ImVec2(L.screenW - L.joyInsetX, L.joyCenterY), L.joyRadius,
                                  &rx, &ry);
        ImGui::EndDisabled();
        ui.joyLx = lx;
        ui.joyLy = ly;
        ui.joyRx = rx;
        ui.joyRy = ry;
    }

    ImDrawList* fg = ImGui::GetForegroundDrawList();
    const float cxL = L.joyInsetX;
    const float cxR = L.screenW - L.joyInsetX;
    drawJoystickAt("##joyL", fg, cxL, L.joyCenterY, L.joyRadius, lx, ly, lActive);
    drawJoystickAt("##joyR", fg, cxR, L.joyCenterY, L.joyRadius, rx, ry, rActive);

    // 杆下方的极简标签
    const ImU32 dim = ImGui::GetColorU32(ImVec4(1, 1, 1, 0.38f));
    const float fs = ImGui::GetFontSize() * 0.95f;
    ImFont* font = ImGui::GetFont();
    const char* labels[2] = {"左 · 移动", "右 · 转向"};
    const float xs[2] = {cxL, cxR};
    for (int i = 0; i < 2; ++i) {
        const ImVec2 sz = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, labels[i]);
        fg->AddText(font, fs,
                    ImVec2(xs[i] - sz.x * 0.5f, L.joyCenterY + L.joyRadius + 8.0f), dim,
                    labels[i]);
    }
}

}  // namespace

// ============================================================ 主界面
void drawUi(RobotManager& mgr, UiState& ui) {
    ui.sideHold = 0;  // 每帧重置：只有"按住侧移按钮"的那一帧会被置位

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("Go2 控制管理台", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

    // ---- 断点布局：每帧按「视口 + 输入方式 + 安全区」重算（纯函数，带 8dp 滞回，幂等）----
    // 注意：DisplaySize 必须是**已按设备密度归一**的逻辑尺寸（dp），
    // 否则 3x 屏上按钮实际只有 18dp —— 归一由平台入口负责。
    {
        const ImVec2 ds = ImGui::GetIO().DisplaySize;
        ui.layout = makeLayout(ds.x, ds.y, ui.touchInput, ui.safe,
                               ui.layout.viewW > 0.0f ? &ui.layout : nullptr);
        setUiFontSizes(ui.layout.fontTitle, ui.layout.fontBody, ui.layout.fontSmall);
        applyUiScale(ui.layout.styleScale);  // 内部从基线重算，不会累乘
    }
    const LayoutSpec& L = ui.layout;
    g_touchUi = L.touch;  // 提示文案：鼠标悬停显示 / 触摸长按显示

    drawTopBar(mgr, ui, L);       // 顶栏：品牌 + 状态 + 四个入口按钮
    drawRemotePanel(mgr, ui, L);  // 主界面：遥控（居中 + 宽度上限）

    // 弹窗要在**主窗口层级**打开：顶栏按钮画在子窗口里，而 ImGui 的弹窗 ID 会带上
    // 子窗口的 ID 栈前缀，直接在子窗口里 OpenPopup 会与这里的 BeginPopupModal 对不上
    //（症状：点按钮没反应，弹窗永远不出现 —— 真机上踩过一次）。
    switch (ui.popupRequest) {
        case 1: ImGui::OpenPopup(kIdDevices);  ui.showDevices = true;  break;
        case 2: ImGui::OpenPopup(kIdActions);  ui.showActions = true;  break;
        case 3: ImGui::OpenPopup(kIdSettings); ui.showSettings = true; break;
        case 4: ImGui::OpenPopup(kIdLog);      ui.showLog = true;      break;
        default: break;
    }
    ui.popupRequest = 0;
    drawPopups(mgr, ui, L);       // 设备 / 动作库 / 设置 / 日志

    ImGui::End();

    // ★ 摇杆在最后画、且画到前景层：任何窗口与弹窗都盖不住它
    drawJoysticks(ui, L);
}

}  // namespace go2
