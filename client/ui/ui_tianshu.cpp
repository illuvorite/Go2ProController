// ============================================================================
// 对齐参考实现「天树探界遥控」Go2 页的三块界面：
//   · 快捷动作（复合指令）—— 动作执行页顶部的一条
//   · 模式设置页          —— 普通 / AI / MCF
//   · 舞蹈编排页          —— 本地序列：新建 / 编辑 / 播放 / 删除 / 急停
//
// 为什么单独一个 TU：这几块逻辑彼此相关（都源自同一份参考实现），
// 但跟既有的"遥控 / 动作库"两块界面关系不大 —— 塞进 ui_actions.cpp 只会让它更长。
//
// 线程规矩：编辑器里的输入缓冲是**仅界面线程**的临时状态，所以放在本文件的匿名
// 命名空间里（不是 UiState 成员）—— 它不是跨线程共享数据，进 UiState 只会让那份
// 结构体的"线程契约"更难读。真正共享的数据（编排列表 / 模式 / 脚本状态）都在 UiState。
// ============================================================================

#include "ui_internal.hpp"

#include "command_service.hpp"
#include "dance.hpp"
#include "icons.hpp"
#include "quick_actions.hpp"
#include "theme.hpp"

#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace go2 {
namespace uix {

namespace {

/// 舞蹈编排编辑器（仅界面线程）的临时状态
struct DanceEditor {
    bool open = false;          ///< 编辑器是否展开
    int id = 0;                 ///< 0 = 新建
    char name[64] = "";
    char desc[96] = "";
    std::vector<DanceStep> steps;
    int addKey = 0;             ///< 「添加动作」下拉当前选中项（索引进可编排动作表）
};
DanceEditor g_danceEdit;

/// 可编排的动作（跳过 Gait 组：那组是步态/参数，不适合编排）
std::vector<const SportAction*> danceChoices() {
    std::vector<const SportAction*> out;
    for (const SportAction& a : sportActions())
        if (a.group != SportGroup::Gait) out.push_back(&a);
    return out;
}

/// 编排里某一步的中文显示名
std::string stepLabel(const std::string& key) {
    if (key == "wait") return "等待";
    for (const SportAction& a : sportActions())
        if (key == a.key) return a.label;
    for (const QuickAction& q : quickActions())
        if (key == q.key) return q.label;
    return key;
}

/// 触发一条快捷动作（脚本型会在后台按节奏下发）
void runQuick(RobotManager& mgr, UiState& ui, const QuickAction& q) {
    const auto sink = std::make_shared<ManagerSink>(mgr, ui);
    const int n = cmd::runQuickAction(sink, ui, q.key);
    if (n < 0)
        ui.addLog(std::string("[快捷动作] 未知动作: ") + q.key);
    else if (n == 0)
        ui.addLog(std::string("[快捷动作] ") + q.label +
                  " 未执行：急停锁定中或没有受控就绪设备");
    else
        ui.addLog(std::string("[快捷动作] ") + q.label + " → " + std::to_string(n) + " 台");
}

}  // namespace

// ---------------------------------------------------------------- 快捷动作

void drawQuickActionSection(RobotManager& mgr, UiState& ui, const LayoutSpec& L) {
    using namespace go2::icon;  // NOLINT
    const bool ic = iconFontLoaded();
    sectionTitle(ic ? (std::string(Wave) + " 快捷动作").c_str() : "快捷动作");

    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float h = g_touchUi ? 44.0f : 32.0f;  // 触摸要 44dp 起，鼠标可以矮一点
    const float w = std::max(88.0f, (L.actAreaW - gap * 3.0f) / 4.0f);  // 一行 4 个
    int col = 0;
    for (const QuickAction& q : quickActions()) {
        const std::string label =
            ic ? std::string(iconGlyph(q.key, q.label)) + " " + q.label : std::string(q.label);
        if (col > 0 && col % 4 != 0) ImGui::SameLine();
        // 「恢复移动」是安全方向（StopMove），单独给个蓝色，跟其它动作区分开
        const bool safe = (q.kind == QuickKind::StopMove);
        if (safe) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.45f, 0.80f, 0.85f));
        const bool hit = ImGui::Button(label.c_str(), ImVec2(w, h));
        if (safe) ImGui::PopStyleColor();
        helpTip(q.hint);
        if (hit) runQuick(mgr, ui, q);
        ++col;
    }
    ImGui::NewLine();
}

// ---------------------------------------------------------------- 模式设置页

void drawModePage(RobotManager& mgr, UiState& ui, const LayoutSpec& L) {
    using namespace go2::icon;  // NOLINT
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.11f, 0.14f, 0.62f));
    ImGui::BeginChild("modepage", ImVec2(0, -L.joyReserve), ImGuiChildFlags_Borders);

    sectionTitle((std::string(Gear) + " 模式设置").c_str());
    {
        FontScope fs = fontSmall();
        const std::string cur = ui.motionMode();
        ImGui::TextDisabled("当前：%s", cur.empty() ? "未知（机器人还没上报过）" : cur.c_str());
    }
    ImGui::TextDisabled(
        "模式切换走运动切换服务（rt/api/motion_switcher/request），不是运动指令。");
    ImGui::TextDisabled(
        "「MCF 模式」用另一套 api_id 指令集 —— 动作库页的「MCF 固件」要与它一致，"
        "否则会看到大量动作被拒（code=3203）。");
    ImGui::Separator();

    struct ModeDef {
        const char* key;
        const char* label;
        const char* glyph;
        const char* desc;
    };
    static const ModeDef kModes[] = {
        {"normal", "普通模式", Paw, "默认运动控制：手动遥控、动作库、舞蹈编排全功能可用。"},
        {"ai", "AI 模式", Robot,
         "启用机器人自身的智能功能（跟随 / 避障等），部分手动动作会被限制。"},
        {"mcf", "MCF 模式", Shield,
         "运动控制框架模式：使用另一套 api_id。动作库的「MCF 固件」要与它一致。"},
    };
    const bool canSend = ui.selectedCount() > 0 && !ui.estop.load();
    const std::string cur = ui.motionMode();
    const float gap = 12.0f;
    const float cardW = std::max(160.0f, (L.actAreaW - gap * 2.0f) / 3.0f);
    const float cardH = g_touchUi ? 96.0f : 76.0f;
    int i = 0;
    for (const ModeDef& m : kModes) {
        if (i > 0) ImGui::SameLine(0.0f, gap);
        ImGui::BeginGroup();
        const bool on = (cur == m.key);
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.55f, 0.95f, 0.55f));
        if (!canSend) ImGui::BeginDisabled();
        const std::string label = iconFontLoaded()
                                      ? std::string(m.glyph) + " " + m.label
                                      : std::string(m.label);
        const bool hit = ImGui::Button(label.c_str(), ImVec2(cardW, cardH));
        if (!canSend) ImGui::EndDisabled();
        if (on) ImGui::PopStyleColor();
        if (hit) {
            const auto sink = std::make_shared<ManagerSink>(mgr, ui);
            const int n = cmd::setMotionMode(sink, ui, m.key);
            ui.addLog(std::string("[模式] 切换到 ") + m.label + " → " + std::to_string(n) + " 台");
        }
        {
            FontScope fs = fontSmall();
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cardW);
            ImGui::TextDisabled("%s", m.desc);
            ImGui::PopTextWrapPos();
            if (on) ImGui::TextColored(kGreen, "当前模式");
        }
        ImGui::EndGroup();
        ++i;
    }
    if (!canSend) {
        ImGui::NewLine();
        ImGui::TextColored(kYellow, ui.estop.load() ? "急停锁定中，无法切换模式"
                                                    : "未选择受控设备");
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// ---------------------------------------------------------------- 舞蹈编排页

void drawDancePage(RobotManager& mgr, UiState& ui, const LayoutSpec& L) {
    using namespace go2::icon;  // NOLINT
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.11f, 0.14f, 0.62f));
    ImGui::BeginChild("dancepage", ImVec2(0, -L.joyReserve), ImGuiChildFlags_Borders);

    sectionTitle((std::string(MusicNote) + " 舞蹈编排").c_str());
    {
        FontScope fs = fontSmall();
        ImGui::TextDisabled("存本机 robot_dances.json（参考实现存云端，这里改成离线可用）");
    }
    ImGui::Separator();

    const bool canPlay = ui.selectedCount() > 0 && !ui.estop.load();
    const float bh = g_touchUi ? 42.0f : 30.0f;

    if (!g_danceEdit.open) {
        if (accentButton((std::string(Pencil) + " 新建编排").c_str(), ImVec2(170.0f, bh))) {
            g_danceEdit = DanceEditor{};
            g_danceEdit.open = true;
            std::snprintf(g_danceEdit.name, sizeof(g_danceEdit.name), "%s", "新编排");
            // 给一个能直接播的起始步（站立），免得新建出来是空的
            g_danceEdit.steps.push_back({"StandUp", defaultDanceDelayMs("StandUp")});
        }
        ImGui::SameLine();
        if (ui.scriptRunning.load()) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.80f, 0.28f, 0.24f, 0.9f));
            if (ImGui::Button((std::string(Estop) + " 急停").c_str(), ImVec2(140.0f, bh))) {
                ManagerSink sink(mgr, ui);
                cmd::abortScript(ui);
                const int n = cmd::stopSelected(sink);
                ui.addLog("[编排] 急停：停车 " + std::to_string(n) + " 台");
            }
            ImGui::PopStyleColor();
            ImGui::SameLine();
            FontScope fs = fontSmall();
            ImGui::TextColored(kYellow, "播放中：%s", ui.scriptName().c_str());
        } else if (!canPlay) {
            ImGui::SameLine();
            FontScope fs = fontSmall();
            ImGui::TextColored(kYellow, "未选择受控设备 / 急停锁定中，无法播放");
        }
    }

    ImGui::Separator();
    ImGui::BeginChild("dancelist", ImVec2(0, 0), ImGuiChildFlags_None);
    touchDragScroll(L);

    // ---- 编辑器 ----
    if (g_danceEdit.open) {
        sectionTitle(g_danceEdit.id > 0 ? "编辑编排" : "新建编排");
        ImGui::SetNextItemWidth(220.0f);
        ImGui::InputText("名称", g_danceEdit.name, sizeof(g_danceEdit.name));
        ImGui::SetNextItemWidth(360.0f);
        ImGui::InputText("说明", g_danceEdit.desc, sizeof(g_danceEdit.desc));

        int removeAt = -1, moveFrom = -1, moveTo = -1;
        for (size_t s = 0; s < g_danceEdit.steps.size(); ++s) {
            DanceStep& st = g_danceEdit.steps[s];
            ImGui::PushID(static_cast<int>(s));
            ImGui::Text("%2d", static_cast<int>(s) + 1);
            ImGui::SameLine(0.0f, 8.0f);
            ImGui::Text("%s", stepLabel(st.key).c_str());
            ImGui::SameLine(210.0f);
            ImGui::SetNextItemWidth(120.0f);
            int delay = st.delayMs;
            if (ImGui::InputInt("ms", &delay)) st.delayMs = delay < 0 ? 0 : delay;
            ImGui::SameLine();
            if (ImGui::SmallButton("↑") && s > 0) {
                moveFrom = static_cast<int>(s);
                moveTo = moveFrom - 1;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("↓") && s + 1 < g_danceEdit.steps.size()) {
                moveFrom = static_cast<int>(s);
                moveTo = moveFrom + 1;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("✕")) removeAt = static_cast<int>(s);
            ImGui::PopID();
        }
        if (removeAt >= 0) g_danceEdit.steps.erase(g_danceEdit.steps.begin() + removeAt);
        if (moveFrom >= 0 && moveTo >= 0)
            std::swap(g_danceEdit.steps[static_cast<size_t>(moveFrom)],
                      g_danceEdit.steps[static_cast<size_t>(moveTo)]);

        // 添加动作：下拉选 + 「添加」/「添加等待」
        const std::vector<const SportAction*> choices = danceChoices();
        if (!choices.empty()) {
            if (g_danceEdit.addKey >= static_cast<int>(choices.size())) g_danceEdit.addKey = 0;
            ImGui::SetNextItemWidth(200.0f);
            if (ImGui::BeginCombo("##addaction",
                                  choices[static_cast<size_t>(g_danceEdit.addKey)]->label)) {
                for (int c = 0; c < static_cast<int>(choices.size()); ++c) {
                    const bool sel = (c == g_danceEdit.addKey);
                    if (ImGui::Selectable(choices[static_cast<size_t>(c)]->label, sel))
                        g_danceEdit.addKey = c;
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            if (ImGui::Button("＋ 添加动作")) {
                const SportAction* a = choices[static_cast<size_t>(g_danceEdit.addKey)];
                g_danceEdit.steps.push_back({a->key, defaultDanceDelayMs(a->key)});
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("＋ 添加等待")) g_danceEdit.steps.push_back({"wait", 1000});

        ImGui::NewLine();
        if (accentButton("保存", ImVec2(120.0f, bh))) {
            if (g_danceEdit.steps.empty()) {
                ui.addLog("[编排] 保存失败：至少要有一步");
            } else {
                Dance d;
                d.id = g_danceEdit.id;
                d.name = g_danceEdit.name[0] ? g_danceEdit.name : "未命名编排";
                d.desc = g_danceEdit.desc;
                d.steps = g_danceEdit.steps;
                const int id = ui.upsertDance(d);
                ui.addLog("[编排] 已保存「" + d.name + "」#" + std::to_string(id) + "（" +
                          std::to_string(d.steps.size()) + " 步）");
                g_danceEdit.open = false;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("取消", ImVec2(100.0f, bh))) g_danceEdit.open = false;
        ImGui::EndChild();   // dancelist
        ImGui::EndChild();   // dancepage
        ImGui::PopStyleColor();
        return;
    }

    // ---- 列表 ----
    const std::vector<Dance> dances = ui.dancesSnapshot();
    if (dances.empty()) {
        ImGui::TextDisabled("还没有编排 —— 点「新建编排」把一串动作串起来一次播放。");
    }
    for (const Dance& d : dances) {
        ImGui::PushID(d.id);
        ImGui::Separator();
        {
            FontScope ft = fontTitle();
            ImGui::Text("%s", d.name.c_str());
        }
        {
            FontScope fs = fontSmall();
            ImGui::TextDisabled("%s · %d 个动作", d.desc.empty() ? "无说明" : d.desc.c_str(),
                                static_cast<int>(d.steps.size()));
        }
        const float bw = g_touchUi ? 96.0f : 78.0f;
        if (ImGui::Button((std::string(Pulse) + " 播放").c_str(), ImVec2(bw, bh))) {
            const auto sink = std::make_shared<ManagerSink>(mgr, ui);
            if (!cmd::playDance(sink, ui, d.id))
                ui.addLog("[编排] 播放失败：急停锁定 / 没有受控设备");
            else
                ui.addLog("[编排] 开始播放「" + d.name + "」");
        }
        ImGui::SameLine();
        if (ImGui::Button("编辑", ImVec2(bw, bh))) {
            g_danceEdit = DanceEditor{};
            g_danceEdit.open = true;
            g_danceEdit.id = d.id;
            std::snprintf(g_danceEdit.name, sizeof(g_danceEdit.name), "%s", d.name.c_str());
            std::snprintf(g_danceEdit.desc, sizeof(g_danceEdit.desc), "%s", d.desc.c_str());
            g_danceEdit.steps = d.steps;
        }
        ImGui::SameLine();
        if (ImGui::Button("删除", ImVec2(bw, bh))) {
            ui.removeDance(d.id);
            ui.addLog("[编排] 已删除 #" + std::to_string(d.id));
        }
        ImGui::PopID();
    }

    ImGui::EndChild();  // dancelist
    ImGui::EndChild();  // dancepage
    ImGui::PopStyleColor();
}

}  // namespace uix
}  // namespace go2
