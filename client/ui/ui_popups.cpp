#include "ui_internal.hpp"

#include "command_service.hpp"
#include "icons.hpp"
#include "layout.hpp"
#include "sport_library.hpp"
#include "textures.hpp"
#include "theme.hpp"
#include "ui.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace go2 {
namespace uix {
// ============================================================ 设备列表（卡片式）
void drawDeviceList(RobotManager& mgr, UiState& ui) {
    sectionTitle((std::string(icon::Robot) + " 设备").c_str());
    ImGui::SameLine();
    {
        FontScope fs = fontSmall();
        const int sc = ui.selectedCount();
        ImGui::TextDisabled("已选 %d 台（%s）· 顶栏「单控 / 群控」一键切换", sc,
                            sc == 0 ? "无" : (sc > 1 ? "群控" : "单控"));
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
            // ---- 第一行：勾选 · 状态灯 · 名称 · 状态胶囊 · 操作 ----
            bool sel = ui.isSelected(e.ip);
            if (ImGui::Checkbox("##sel", &sel)) {
                // ★ 取消勾选一台正在行走的狗 → 它会保持最后的速度继续走，而且
                //   stopSelected 只遍历**新的**集合，从此再也停不到它。
                //   所以改集合之前先记一份，收尾交给 cmd::stopDeselected。
                const std::vector<std::string> before = ui.selectedIps();
                ui.setSelected(e.ip, sel);
                ManagerSink sink(mgr, ui);
                if (cmd::stopDeselected(sink, before) > 0)
                    ui.addLog("[受控] 取消勾选的那台已停车");
            }
            ImGui::SameLine(0, 4);
            statusDot(stc);
            ImGui::SameLine(0, 2);
            // 名称（没起名就显示 IP）
            // ★ 起了名就**只显示名字**、不再并排显示 IP（用户要求）；IP 收进悬停提示，排障仍看得到
            {
                const std::string nm = ui.nameOf(e.ip);
                FontScope fs = fontBody();
                ImGui::TextColored(stc, "%s",
                                   maskIps(nm.empty() ? e.ip : nm, ui.privacyMode).c_str());
                if (!nm.empty()) helpTip((e.ip + "（已起名，只显示名字）").c_str());
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

            // ---- 第二行：改名 · 单控 ----
            // 放这一行而不是第一行右侧：那儿要留给"连接 / 移除"，全挤一起窄屏会溢出
            ImGui::Spacing();
            if (ui.renamingIp == e.ip) {
                ImGui::SetNextItemWidth(
                    std::max(150.0f, ImGui::GetContentRegionAvail().x - 150.0f));
                const bool enter = ImGui::InputTextWithHint(
                    "##name", "给这台狗起个名字（留空 = 用 IP）", ui.nameBuf, sizeof(ui.nameBuf),
                    ImGuiInputTextFlags_EnterReturnsTrue);
                ImGui::SameLine();
                if (ImGui::SmallButton("保存") || enter) {
                    ui.setName(e.ip, ui.nameBuf);
                    ui.addLog("[UI] " + e.ip + " 命名为「" + ui.labelOf(e.ip) + "」");
                    ui.renamingIp.clear();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("取消")) {
                    ui.renamingIp.clear();
                    ui.nameBuf[0] = '\0';
                }
            } else {
                if (ImGui::SmallButton("改名")) {
                    ui.renamingIp = e.ip;
                    std::snprintf(ui.nameBuf, sizeof(ui.nameBuf), "%s", ui.nameOf(e.ip).c_str());
                }
                helpTip("给这台机器狗起个名字：显示在设备卡片和摇杆带上。\n"
                        "存在 robot_names.json（应用目录），下次启动还在。");
                ImGui::SameLine();
                if (ImGui::SmallButton("单控")) {
                    // 与摇杆带里的"单控"同一份语义（含"被移出受控的设备要停车"）
                    selectOne(mgr, ui, e.ip);
                }
                helpTip("只让这一台接收指令（其余设备自动取消勾选）");
                ImGui::SameLine(0, 14);
            }

            // ---- 第三行：电量条 · 电量 · 模式 ----
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

// 弹窗 ID（同一个 ID 栈里要唯一）。动作库**不是弹窗**了 —— 它是常驻页面，没有 ID。

// 弹窗统一去掉标题栏：标题 + 关闭按钮由各面板自己的 header() 画，
// 否则模态框自带的标题栏会和它重复（视觉上出现两个"设备"）。
constexpr ImGuiWindowFlags kPopupFlags = ImGuiWindowFlags_NoResize |
                                         ImGuiWindowFlags_NoMove |
                                         ImGuiWindowFlags_NoTitleBar |
                                         ImGuiWindowFlags_NoSavedSettings;

// ---------------------------------------------------------------- 设备弹窗
void drawDevicePanel(RobotManager& mgr, UiState& ui, const LayoutSpec& L) {
    touchDragScroll(L);  // 触摸：按住拖动即可滚动（不用去抓右边滚动条）
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
        // 走公开入口:startScanImpl 已随纯数据实现搬到 ui_state.cpp(内部链接),
        // 这里只能通过 ui.hpp 声明的 startScan 调用。
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

// ---------------------------------------------------------------- 设置弹窗
void drawSettingsPanel(RobotManager& mgr, UiState& ui, const LayoutSpec& L) {
    touchDragScroll(L);  // 触摸：按住拖动即可滚动
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
        // 写到应用数据目录（~/.go2/keys.txt），不再落到工程目录里
        const std::string keysPath = defaultKeysTxtPath();
        ensureParentDir(keysPath);
        std::ofstream f(keysPath, std::ios::app);
        f << k << "\n";
        ui.manualKey[0] = '\0';
        ui.addLog("[钥匙] 已保存到 " + keysPath);
        reloadKeysAndRetry(mgr, ui);
    }
}
{
    FontScope fs = fontSmall();
    ImGui::TextDisabled("每台新固件狗提取一次，永久保存；当前已加载 %d 把",
                        ui.localKeyCount.load());  // atomic：可变参数必须显式取值
}

// ---- 从机器狗找钥匙（与网页端对齐）：不连电脑，扫狗的内网服务抓 32 位 hex ----
{
    FontScope fs = fontSmall();
    ImGui::TextDisabled("狗本地持有明文钥匙 —— 扫它的内网端口，从 Web 服务里抓取候选");
}
if (ui.keyScanning) {
    ImGui::TextColored(col::kWarn, "探测中…（几秒钟）");
} else if (ImGui::Button("从机器狗找钥匙")) {
    // 不填 IP：内部自动回退到受控第一台 / 设备列表第一台
    std::thread(runKeyProbe, std::ref(mgr), std::ref(ui), std::string()).detach();
}
if (ui.keyScanning) helpTip("结果会显示在下方与运行日志里");
{
    std::lock_guard<std::mutex> lock(ui.keyScanMutex);
    if (!ui.keyScanNote.empty()) {
        FontScope fs = fontSmall();
        ImGui::TextWrapped("%s", ui.keyScanNote.c_str());
    }
    for (const auto& k : ui.keyCandidates) {
        ImGui::PushID(k.c_str());
        ImGui::TextDisabled("%s", k.c_str());
        ImGui::SameLine(0, 10);
        if (ImGui::SmallButton("采用")) applyKeyCandidate(mgr, ui, k, std::string());
        ImGui::PopID();
    }
}

ImGui::Spacing();
ImGui::Separator();
sectionTitle("说明");
{
    FontScope fs = fontSmall();
    ImGui::TextDisabled("· 空格键 / 顶栏「■ 急停」= 急停（锁定式；双杆回中后可解除）");
    ImGui::TextDisabled("· 两个摇杆中间的「单控 / 群控」决定指令发给谁；点「单控」会弹出选狗列表");
    ImGui::TextDisabled("· 设备卡片「改名」可给每台狗起名（存 robot_names.json，下次启动还在）");
    ImGui::TextDisabled("· 动作被拒会给出原因；指令集不匹配会自动换另一套 id 重试");
    ImGui::TextDisabled("· 「隐藏不支持的」可过滤掉该固件没有的动作");
    ImGui::TextDisabled("· 阻尼在「遥控」页（狗仍在自走时的兜底手段：会让它立即软腿趴下）");
}
}

// ---------------------------------------------------------------- 日志弹窗
void drawLogPanel(UiState& ui, const LayoutSpec& L) {
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
touchDragScroll(L);  // 触摸：按住拖动即可滚动
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

// ---------------------------------------------------------------- 三个弹窗（设备 / 设置 / 日志）
// ★ 弹窗要"覆盖在摇杆区域上方"：几何上按整屏居中（layout 里 popupH 取整屏的 90%），
//   绘制上**摇杆那一帧直接不画**（见 drawJoysticks 的 modalOpen 判断）——
//   否则悬浮摇杆画在前景层，永远压在弹窗上面，弹窗就成了"半遮半掩"。
//   （动作库不再是弹窗：它是常驻整屏页面，见 drawActionPage。）
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
        ui.modalOpen = true;  // 弹窗盖住摇杆带（摇杆这一帧不画、不响应）
        header("设备");
        drawDevicePanel(mgr, ui, L);
        ImGui::EndPopup();
    }

    place();
    if (ImGui::BeginPopupModal(kIdSettings, &ui.showSettings, kPopupFlags)) {
        ui.modalOpen = true;
        header("设置");
        drawSettingsPanel(mgr, ui, L);
        ImGui::EndPopup();
    }

    place();
    if (ImGui::BeginPopupModal(kIdLog, &ui.showLog, kPopupFlags)) {
        ui.modalOpen = true;
        ui.problemSeen = ui.problemCount.load();  // 打开日志就算"看过了"，顶栏角标随之清掉
        header("运行日志");
        drawLogPanel(ui, L);
        ImGui::EndPopup();
    }
}

}  // namespace uix
}  // namespace go2
