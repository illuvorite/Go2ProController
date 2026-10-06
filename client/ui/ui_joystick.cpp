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
void drawJoystickAt(const char* id, ImDrawList* dl, float cx, float cy, float radius, float x,
                    float y, bool active) {
    if (!dl) return;
    const ImVec2 c(cx, cy);
    constexpr float kPi = 3.14159265f;

    // ---- 底座：投影 + 径向渐变 + 深色盘 + 内圈 + 刻度点 ----
    // ★ 投影：让摇杆"离地"（ImGui 没有模糊 → 几层下偏移的同心圆近似）
    for (int i = 4; i >= 1; --i)
        dl->AddCircleFilled(ImVec2(c.x, c.y + static_cast<float>(i) * 2.2f),
                            radius + static_cast<float>(i) * 1.3f,
                            ImGui::GetColorU32(ImVec4(0, 0, 0, 0.055f)));
    // ★ 径向渐变：同心圆由内向外一层层叠加 → 中心亮、边缘暗，球面感就出来了
    for (int i = 5; i >= 1; --i)
        dl->AddCircleFilled(c, radius * (0.28f + static_cast<float>(i) * 0.145f),
                            ImGui::GetColorU32(ImVec4(1, 1, 1, 0.020f)));
    dl->AddCircleFilled(c, radius, ImGui::GetColorU32(ImVec4(0, 0, 0, 0.32f)));
    dl->AddCircleFilled(c, radius - 1.5f, ImGui::GetColorU32(ImVec4(1, 1, 1, 0.035f)));
    dl->AddCircle(c, radius, ImGui::GetColorU32(ImVec4(1, 1, 1, 0.10f)), 0, 1.5f);
    // 顶部高光弧（沿盘的上沿描一段淡白弧 —— 金属/玻璃的边缘反光）
    dl->PathArcTo(c, radius - 2.0f, -2.60f, -0.75f, 28);
    dl->PathStroke(ImGui::GetColorU32(ImVec4(1, 1, 1, 0.18f)), 0, 1.6f);
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

namespace uix {
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

/// 画两个摇杆 + 两杆中间的「单控 / 群控」面板。**必须在主窗口 End() 之后调用**。
/// 视觉画到前景层（GetForegroundDrawList）—— 页面内容永远盖不住它；
/// 但**弹窗打开时整条摇杆带不画**（用户要求：其余弹窗统一覆盖在摇杆区域上方）。
void drawJoysticks(RobotManager& mgr, UiState& ui, const LayoutSpec& L) {
    // ★ 弹窗盖住摇杆带：不画、不响应、数值清零。
    //   数值必须清零 —— 否则"推着摇杆时点开设备弹窗"会让摇杆值一直保持住、狗继续走。
    //   （movingSent 故意留在原值：下一帧遥控页会因此补发一次 StopMove）
    if (ui.modalOpen) {
        ui.joyLx = ui.joyLy = ui.joyRx = ui.joyRy = 0.0f;
        ui.joyLActive = ui.joyRActive = false;
        return;
    }

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
    // 注：摇杆带上沿曾经画过一条分隔线 + 极淡底板，用户要求去掉（"摇杆上面不要分界线"）——
    // 页面内容本来就通过 -joyReserve 让开了位置，不需要再画线提示。
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

    // ---- 两杆中间：「单控 / 群控」面板（用户要求按钮放这里）----
    // 一个独立小窗（画在最后 → 在所有窗口之上），里面是：
    //   [单控] [群控]      ← 点「单控」弹出选狗列表（用户要求"单控要能选机械狗"）
    //   单控 · 火烈鸟        ← 当前指令发给谁
    // 空档太窄（手机竖屏）时两个按钮竖排；面板宽高由 layout 给，保证不会压到摇杆。
    {
        const float gapW = L.joyGapMaxX - L.joyGapMinX;
        if (gapW > 40.0f) {
            const float cx = (L.joyGapMinX + L.joyGapMaxX) * 0.5f;
            // ★ 面板整块登记为"手指优先给 ImGui"的安全区：否则窄屏手机上
            //   落在面板上的手指会被摇杆的抓取圈吃掉，按钮点不动。
            ui.addSafetyRect(cx - L.joyPanelW * 0.5f, L.joyCenterY - L.joyPanelH * 0.5f,
                             cx + L.joyPanelW * 0.5f, L.joyCenterY + L.joyPanelH * 0.5f);
            ImGui::SetNextWindowPos(
                ImVec2(cx - L.joyPanelW * 0.5f, L.joyCenterY - L.joyPanelH * 0.5f),
                ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(L.joyPanelW, L.joyPanelH), ImGuiCond_Always);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 14.0f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.078f, 0.094f, 0.118f, 0.92f));
            // ⚠️ 千万**不要**加 ImGuiWindowFlags_NoBringToFrontOnFocus：
            //    实测（用 imgui_internal.h 打窗口顺序）它会让本窗永远排在最底层，
            //    主窗口（整屏不透明）于是把整个面板盖掉 —— 现象就是"面板消失"。
            ImGui::Begin("##joyPanel", nullptr,
                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoScrollWithMouse |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNavFocus);
            {
                // ---- 受控狗卡片行（与网页端对齐）：Go2 实拍图 + 名字 + 电量 + 勾选 ----
                // 点一张勾一台（可多选）；多台横向滚动。矮屏/窄面板时 layout 会关掉这一行。
                if (L.joyPanelCards) {
                    std::vector<RobotEntry> snap;
                    {
                        std::lock_guard<std::mutex> lock(ui.robotsMutex);
                        snap = ui.robots;
                    }
                    const ImTextureID cardImg = dogCardImage();
                    ImGui::BeginChild("##dogcards", ImVec2(0.0f, 52.0f), ImGuiChildFlags_None,
                                      ImGuiWindowFlags_HorizontalScrollbar |
                                          ImGuiWindowFlags_NoScrollWithMouse);
                    for (const auto& e : snap) {
                        ImGui::PushID(e.ip.c_str());
                        const std::string lab =
                            maskIps(ui.labelOf(e.ip), ui.privacyMode);  // 有名字显示名字
                        const bool sel = ui.isSelected(e.ip);
                        const ImVec2 p0 = ImGui::GetCursorScreenPos();
                        const ImVec2 sz(92.0f, 48.0f);
                        const bool hit = ImGui::InvisibleButton("card", sz);
                        ImDrawList* dl = ImGui::GetWindowDrawList();
                        const ImU32 bg = ImGui::GetColorU32(
                            sel ? ImVec4(0.30f, 0.58f, 1.00f, 0.20f) : ImVec4(1, 1, 1, 0.05f));
                        const ImU32 border = ImGui::GetColorU32(
                            sel ? ImVec4(0.30f, 0.58f, 1.00f, 0.85f) : ImVec4(1, 1, 1, 0.14f));
                        dl->AddRectFilled(p0, ImVec2(p0.x + sz.x, p0.y + sz.y), bg, 10.0f);
                        dl->AddRect(p0, ImVec2(p0.x + sz.x, p0.y + sz.y), border, 10.0f);
                        // 上半：Go2 实拍图（图片没加载时退化为爪印图标 —— 仍是字体字形，不手绘）
                        if (cardImg)
                            dl->AddImage(cardImg, ImVec2(p0.x + 5, p0.y + 4),
                                         ImVec2(p0.x + sz.x - 5, p0.y + 34));
                        else
                            dl->AddText(ImVec2(p0.x + 5, p0.y + 8),
                                        ImGui::GetColorU32(ImVec4(1, 1, 1, 0.65f)), icon::Paw);
                        // 下半：名字（有名字显示名字，隐私模式打码）+ 电量（右对齐）
                        dl->PushClipRect(p0, ImVec2(p0.x + sz.x, p0.y + sz.y), true);
                        dl->AddText(ImVec2(p0.x + 6, p0.y + 36), ImGui::GetColorU32(col::kText),
                                    lab.c_str());
                        const std::string bt =
                            e.battery >= 0 ? (std::to_string(int(e.battery)) + "%") : "—";
                        const float btw = ImGui::CalcTextSize(bt.c_str()).x;
                        dl->AddText(ImVec2(p0.x + sz.x - 6 - btw, p0.y + 36),
                                    ImGui::GetColorU32(ImVec4(1, 1, 1, 0.45f)), bt.c_str());
                        // 右上角勾选框：选中 = 主色圆 + check 字形
                        const ImVec2 cc(p0.x + sz.x - 10.0f, p0.y + 10.0f);
                        if (sel) {
                            dl->AddCircleFilled(cc, 8.0f, ImGui::GetColorU32(col::kAccent));
                            dl->AddText(ImVec2(cc.x - 5.0f, cc.y - 7.0f), IM_COL32_WHITE,
                                        icon::Check);
                        } else {
                            dl->AddCircle(cc, 8.0f, ImGui::GetColorU32(ImVec4(1, 1, 1, 0.40f)));
                        }
                        dl->PopClipRect();
                        if (hit) ui.setSelected(e.ip, !sel);  // 勾一台 / 取消一台（可多选）
                        ImGui::PopID();
                        ImGui::SameLine(0.0f, 6.0f);
                    }
                    if (snap.empty()) {
                        FontScope fs = fontSmall();
                        ImGui::TextDisabled("还没有设备 —— 顶栏「设备」添加");
                    }
                    ImGui::EndChild();
                }
                const int sel = ui.selectedCount();
                const float cw = ImGui::GetContentRegionAvail().x;
                const float bh = L.topBtnH;
                const float bw = L.joyPanelStack ? cw : (cw - 8.0f) * 0.5f;
                const auto panelBtn = [&](const char* label, bool active, const ImVec2& size) {
                    if (active)
                        ImGui::PushStyleColor(ImGuiCol_Button,
                                              ImVec4(col::kAccent.x, col::kAccent.y,
                                                     col::kAccent.z, 0.85f));
                    const bool hit = ImGui::Button(label, size);
                    if (active) ImGui::PopStyleColor();
                    return hit;
                };
                // 图标字体加载成功时按钮带图标（人 / 多人的字形一眼看清是单控还是群控）
                const bool ic = iconFontLoaded();
                const std::string labSingle =
                    ic ? std::string(icon::User) + " 单控" : std::string("单控");
                const std::string labGroup =
                    ic ? std::string(icon::Users) + " 群控" : std::string("群控");
                const bool singleHit = panelBtn(labSingle.c_str(), sel == 1, ImVec2(bw, bh));
                // 记下按钮矩形：下面的选择器要**向上弹**（摇杆带贴着屏幕下沿，
                // 往下弹会被裁掉、还会压住「群控」按钮）
                const ImVec2 pickAnchor = ImGui::GetItemRectMin();
                if (singleHit) ImGui::OpenPopup("##pick");
                if (L.joyPanelStack) {
                    if (panelBtn(labGroup.c_str(), sel > 1, ImVec2(bw, bh))) selectGroupAll(ui);
                } else {
                    ImGui::SameLine(0.0f, 8.0f);
                    if (panelBtn(labGroup.c_str(), sel > 1, ImVec2(bw, bh))) selectGroupAll(ui);
                }
                // 当前受控对象（居中一行小字）
                {
                    FontScope fs = fontSmall();
                    const std::string t = ui.controlTargetText();
                    const float tw = ImGui::CalcTextSize(t.c_str()).x;
                    if (tw < cw)
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                                             std::max(0.0f, (cw - tw) * 0.5f));
                    ImGui::TextColored(sel == 0 ? col::kDim
                                                : (sel == 1 ? col::kAccent : col::kOk),
                                       "%s", t.c_str());
                }
                // ---- 单控：选哪一台（**向上弹**的选择器）----
                // 定位用"底边对齐按钮顶边"的 pivot：高度交给 ImGui 自适应，不用预先估算行高。
                ImGui::SetNextWindowPos(ImVec2(pickAnchor.x, pickAnchor.y - 8.0f),
                                        ImGuiCond_Always, ImVec2(0.0f, 1.0f));
                ImGui::SetNextWindowSizeConstraints(ImVec2(230.0f, 0.0f), ImVec2(470.0f, 520.0f));
                if (ImGui::BeginPopup("##pick")) {
                    ImGui::TextDisabled("选择要单控的机器狗");
                    ImGui::Separator();
                    std::vector<RobotEntry> snap;
                    {
                        std::lock_guard<std::mutex> lock(ui.robotsMutex);
                        snap = ui.robots;
                    }
                    if (snap.empty()) {
                        ImGui::TextDisabled("（列表为空 —— 先去「设备」里扫描 / 添加）");
                    } else {
                        for (const auto& e : snap) {
                            RobotClient* c = mgr.find(e.ip);
                            const ConnState st = c ? c->state() : ConnState::Disconnected;
                            // ★ 起了名就**只显示名字**（用户要求）；IP 收进悬停提示，排障时仍看得到
                            const std::string nm = ui.nameOf(e.ip);
                            const std::string label =
                                (nm.empty() ? e.ip : nm) + std::string("    ") + stateText(st);
                            if (ImGui::Selectable(label.c_str(), ui.isSelected(e.ip))) {
                                selectOne(ui, e.ip);
                                ImGui::CloseCurrentPopup();
                            }
                            if (!nm.empty()) helpTip((e.ip + "（已起名，这里只显示名字）").c_str());
                        }
                    }
                    // ---- 取消当前单控：谁都不控制 ----
                    if (sel > 0) {
                        ImGui::Separator();
                        const std::string cancelLabel =
                            (ic ? std::string(icon::HandPalm) + "    " : std::string()) +
                            "取消单控（不控制任何设备）";
                        if (ImGui::Selectable(cancelLabel.c_str())) {
                            clearSelection(ui);
                            ImGui::CloseCurrentPopup();
                        }
                        helpTip("取消后摇杆 / 动作 / 快捷都不再发给任何设备；\n"
                                "想恢复再点「单控」挑一台即可");
                    }
                    ImGui::EndPopup();
                }
            }
            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
        }
    }
}

}  // namespace uix
}  // namespace go2
