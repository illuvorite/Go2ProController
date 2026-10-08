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
/// 急停（锁定式）：停**全部就绪**的机器狗（不只勾选的）+ 逐个关掉我们打开过的「持续模式」开关。
/// ★ 顶栏、遥控页、网页端三个入口共用同一份语义（`cmd::estop`）—— 急停是安全项，
///   绝不允许各处行为悄悄分叉。这里只负责"调用 + 写日志"。
void triggerEstop(RobotManager& mgr, UiState& ui) {
    // asyncClose=true：关闭持续模式每条要 45ms，界面线程不能等
    const auto sink = std::make_shared<ManagerSink>(mgr, ui);
    const cmd::EstopResult r = cmd::estop(sink, ui, /*asyncClose=*/true);
    ui.addLog("[急停] 已锁定：停车 " + std::to_string(r.stopped) + " 台 + 关闭 " +
              std::to_string(r.toggles) + " 个已开启的模式；连按不会叠加");
}

// ---------------------------------------------------------------- 顶栏
// 一行（宽松屏）：品牌 · 受控状态 | 遥控 动作库 | 设备 设置 日志 | ■ 急停
// 两行（C 档极窄屏）：第一行 状态 + 急停；第二行 遥控 动作库 + 设备 设置 日志
//
// ★「单控 / 群控」已按用户要求挪到**两个摇杆中间**（见 drawJoysticks 的摇杆带面板），
//   顶栏因此从 8 个按钮减到 6 个，宽松屏上每个按钮更宽、更好点。
void drawTopBar(RobotManager& mgr, UiState& ui, const LayoutSpec& L) {
    ImGui::BeginChild("top", ImVec2(0, L.topBarH), ImGuiChildFlags_Borders);
    // 顶栏"玻璃感"：自上而下的淡白渐变 + 底部一条暗色收边（与页面区分层）
    {
        const ImVec2 wp = ImGui::GetWindowPos();
        const ImVec2 ws = ImGui::GetWindowSize();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilledMultiColor(wp, ImVec2(wp.x + ws.x, wp.y + ws.y),
                                    IM_COL32(255, 255, 255, 13), IM_COL32(255, 255, 255, 13),
                                    IM_COL32(255, 255, 255, 2), IM_COL32(255, 255, 255, 2));
        dl->AddLine(ImVec2(wp.x, wp.y + ws.y - 0.5f), ImVec2(wp.x + ws.x, wp.y + ws.y - 0.5f),
                    IM_COL32(0, 0, 0, 95), 1.0f);
    }

    const ImVec2 bs(L.topBtnW, L.topBtnH);
    const float sp = ImGui::GetStyle().ItemSpacing.x;
    const ImVec4 accent(col::kAccent.x, col::kAccent.y, col::kAccent.z, 0.80f);
    const int sel = ui.selectedCount();
    // ★ 走快照而不是直接读 ui.robots：扫描线程/Web 端正在 push_back 与 erase，
    //   顶栏每帧遍历它 —— 边遍历边被改就是迭代器失效（ui.hpp 的线程契约）。
    const std::vector<RobotEntry> robots = ui.robotsSnapshot();
    const int total = static_cast<int>(robots.size());

    // 按钮文案带图标（Phosphor 字形，见 ui/icons.hpp）。
    // 图标字体没加载成功时退回纯文字 —— 免得按钮上出现一片"缺字方块"。
    const bool ic = iconFontLoaded();
    // ★ "动作库"全站统一 icon::List（squares-four，动作卡片网格）：
    //   顶栏切换按钮、更多菜单、动作库页标题三处必须是**同一个**图标。
    //   原来顶栏/页标题用 TaiChi(太极人形)、菜单用 List —— 同一个功能两个图标。
    const std::string labRemote = ic ? std::string(icon::Joystick) + " 遥控" : std::string("遥控");
    const std::string labActions = ic ? std::string(icon::List) + " 动作库" : std::string("动作库");

    // 把接下来的 n 个按钮推到右端（两边留白，别贴着边）
    const auto alignRight = [&](int n) {
        const float need = L.topBtnW * static_cast<float>(n) + sp * static_cast<float>(n - 1);
        const float avail = ImGui::GetContentRegionAvail().x;
        if (avail > need) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - need));
    };
    // 高亮按钮：active = 当前就是这一页
    const auto hiBtn = [&](const char* label, bool active) {
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, accent);
        const bool hit = ImGui::Button(label, bs);
        if (active) ImGui::PopStyleColor();
        return hit;
    };
    const auto plainBtn = [&](const char* label) { return ImGui::Button(label, bs); };

    const auto statusChip = [&] {
        const std::string s = (sel == 0) ? std::string("未选择受控")
                                         : std::to_string(sel) + "/" + std::to_string(total) +
                                               " 台受控";
        chip(s.c_str(), sel > 0 ? col::kAccent : col::kIdle);
    };
    const auto goPage = [&](UiPage p) {
        if (ui.page == p) return;
        ui.page = p;
        ui.addLog(std::string("[UI] 切到") + (p == UiPage::Actions ? "动作库" : "遥控") + "页");
    };

    // ---------------------------------------------------------------- 顶栏
    // 左：品牌 + 状态胶囊 + 电量；右：设备 · 页面切换 · ⋯ 更多 · ■ 急停
    //
    // ★ 按用户 2026-09-28 的调整：
    //   · **没有返回键**（页面切换按钮本身就是"遥控/动作库"来回切，不需要返回）
    //   · 「设备」提到顶栏当独立按钮（不再埋在「更多」菜单里）
    //   · 急停回到顶栏最右（那条右侧竖排红条已撤掉）
    const std::string labEquipment =
        ic ? std::string(icon::Robot) + " 设备" : std::string("设备");
    const std::string labMore = ic ? std::string(icon::Dots) + " 更多" : std::string("更多");
    const std::string labEstop = ic ? std::string(icon::Estop) + " 急停" : std::string("■ 急停");

    // 电量胶囊：取**受控设备里最低**的那台 —— 最需要关注的那台
    const auto batteryChip = [&]() -> bool {
        float low = -1.0f;
        for (const auto& e : robots) {
            if (!e.selected || e.battery < 0.0f) continue;
            if (low < 0.0f || e.battery < low) low = e.battery;
        }
        if (low < 0.0f) return false;
        char t[40];
        if (ic)
            std::snprintf(t, sizeof(t), "%s %d%%", icon::Battery, static_cast<int>(low));
        else
            std::snprintf(t, sizeof(t), "%d%%", static_cast<int>(low));
        chip(t, low > 30.0f ? col::kOk : col::kWarn);
        return true;
    };
    // 「⋯ 更多」：设置 / 日志 + 页面切换（「设备」已提到顶栏，不再重复放这里）
    const auto moreBtn = [&] {
        const bool hit = ImGui::Button(labMore.c_str(), bs);
        // 有新异常就在按钮上画红色角标（日志弹窗没打开也能发现出错）
        const int total = ui.problemCount.load();
        const int seen = ui.problemSeen.load();
        const int unread = total > seen ? (total - seen) : 0;
        if (unread > 0) {
            char t[8];
            std::snprintf(t, sizeof(t), unread > 99 ? "99+" : "%d", unread);
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
        if (hit) ImGui::OpenPopup("##more");
        if (ImGui::BeginPopup("##more")) {
            const auto item = [&](const char* glyph, const char* text) {
                const std::string s = ic ? std::string(glyph) + "   " + text : std::string(text);
                return ImGui::MenuItem(s.c_str());
            };
            if (item(icon::Gear, "设置")) ui.popupRequest = 2;
            if (item(icon::Log, "日志")) ui.popupRequest = 3;
            ImGui::Separator();
            if (item(icon::List, "动作库")) goPage(UiPage::Actions);
            if (item(icon::Joystick, "遥控")) goPage(UiPage::Remote);
            ImGui::EndPopup();
        }
        return hit;
    };
    // 页面切换按钮：遥控页上显示「动作库」，动作库页上显示「遥控」
    const auto pageBtn = [&] {
        const bool onRemote = (ui.page == UiPage::Remote);
        if (hiBtn(onRemote ? labActions.c_str() : labRemote.c_str(), false))
            goPage(onRemote ? UiPage::Actions : UiPage::Remote);
    };

    // 急停：红色、顶栏最右（用户要求放回顶栏；断线时也能按，不依赖任何连接状态）
    const auto estopBtn = [&] {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.78f, 0.16f, 0.16f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.90f, 0.22f, 0.22f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.00f, 0.32f, 0.32f, 1.0f));
        const std::string lab = ui.estop ? std::string("已急停") : labEstop;
        const bool hit = ImGui::Button(lab.c_str(), bs);
        ImGui::PopStyleColor(3);
        return hit;
    };

    if (L.topTwoRows) {
        // ---- 第一行：状态 + 电量 ----
        statusChip();
        ImGui::SameLine();
        batteryChip();
        // ---- 第二行：设备 · 页面切换 · 更多 · 急停 ----
        if (plainBtn(labEquipment.c_str())) ui.popupRequest = 1;
        ImGui::SameLine();
        alignRight(3);
        pageBtn();
        ImGui::SameLine();
        moreBtn();
        ImGui::SameLine();
        if (estopBtn()) triggerEstop(mgr, ui);
    } else {
        if (L.showBrand) {
            // 品牌位：优先画 logo 字标（assets/web/img/logo-word.png，透明背景）。
            // 找不到纹理就回退到原来的文字 —— 品牌位不能因为缺资源而空一块。
            const LoadedImage logo = brandLogo();
            if (logo.tex && logo.h > 0) {
                const float h = ImGui::GetFontSize() * 0.98f;          // 与标题字号同量级
                const float w = h * (logo.w / static_cast<float>(logo.h));
                ImGui::Image(logo.tex, ImVec2(w, h));
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("H-bbot · 幻核睛山");
            } else {
                FontScope fs = fontTitle();
                ImGui::TextUnformatted("Go2 控制台");
            }
            ImGui::SameLine();
        }
        statusChip();
        ImGui::SameLine();
        if (batteryChip()) ImGui::SameLine();
        alignRight(4);
        if (plainBtn(labEquipment.c_str())) ui.popupRequest = 1;
        ImGui::SameLine();
        pageBtn();
        ImGui::SameLine();
        moreBtn();
        ImGui::SameLine();
        if (estopBtn()) triggerEstop(mgr, ui);
    }

    ImGui::EndChild();
}

}  // namespace uix
}  // namespace go2
