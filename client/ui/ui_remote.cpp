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
// ---------------------------------------------------------------- 遥控主体
void drawRemotePanel(RobotManager& mgr, UiState& ui, const LayoutSpec& L) {
    const bool compactLabel = (L.widthClass == WidthClass::Compact);
    // ★ 铺满：不再居中限宽（用户明确要求"铺满"，居中留白观感太空）。
    //   高度扣掉摇杆带（负值 = 可用高度 − 该值）：内容**永远不会渲染到摇杆的地盘上**。
    ImGui::BeginChild("remote", ImVec2(0, -L.joyReserve), ImGuiChildFlags_None);
    touchDragScroll(L);  // 触摸：手指按住拖动即可滚动（不用去抓右边滚动条）
sectionTitle((std::string(icon::Joystick) + " 遥控").c_str());
ImGui::SameLine();
{
    FontScope fs = fontSmall();
    ImGui::TextDisabled("左杆移动 · 右杆转向 · 松手即停 —— 指令发给：%s",
                        ui.controlTargetText().c_str());
}
ImGui::Spacing();

// 急停：**锁定式**。按下后停全部就绪的机器狗（不只勾选的），
// 并在解除前让摇杆/快捷步彻底失效 —— 否则下一帧的 10Hz Move 重发会把车又"开起来"。
// 语义实现在 triggerEstop()（顶栏那个红色「■ 急停」按钮走的是同一份代码）。
ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 11.0f);
ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.78f, 0.16f, 0.16f, 1.0f));
ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.90f, 0.22f, 0.22f, 1.0f));
ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(1.00f, 0.32f, 0.32f, 1.0f));
// 急停是安全关键操作：**永远全宽、永远在视口内**，高度按断点给（矮屏 64 / 竖屏 78 / 桌面 52）。
// 断线状态下也要能按 —— 所以这里不依赖任何连接状态。
const bool estopClicked =
    bigButton(compactLabel ? "■  急停" : "■  急停（全部停车 / 空格键）", ImVec2(-1, L.estopH));
// 记下安全操作区的屏幕矩形：触屏入口拿它做摇杆抓取互斥（手指落在急停上不能被摇杆吃掉）
ui.addSafetyRect(ImGui::GetItemRectMin().x, ImGui::GetItemRectMin().y,
                 ImGui::GetItemRectMax().x, ImGui::GetItemRectMax().y);
ImGui::PopStyleColor(3);
ImGui::PopStyleVar();
// 键盘急停（空格）在 drawUi 里统一处理 —— 动作库页也必须能按空格停车
if (estopClicked) triggerEstop(mgr, ui);

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
    ui.addSafetyRect(ImGui::GetItemRectMin().x, ImGui::GetItemRectMin().y,
                     ImGui::GetItemRectMax().x, ImGui::GetItemRectMax().y);
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
        // 语义在 cmd::unestop（与网页端同一份）：centered 是界面每帧算的真实双杆状态，
        // 服务层还会再查一次"最近一次下发的速度是否为零"。
        if (cmd::unestop(ui, centered))
            ui.addLog("[急停] 已解除，可以继续遥控");
        else
            ui.addLog("[急停] 摇杆未回中，拒绝解除");
    }
    ImGui::EndDisabled();
    if (!centered)
        ImGui::TextColored(kYellow, "  双杆回中后才能解除急停");
}

ImGui::Spacing();

// ---- 参数 / 快捷：铺满之后"一行只放一个控件"会显得很空 → 宽屏左右两栏 ----
const bool canMove = ui.selectedCount() > 0;

const auto drawParams = [&] {
    sectionTitle("参数");
    ImGui::BeginDisabled(!canMove);
    // 这三个是 atomic（Web 端 param 指令会写）→ 滑条操作副本，画完统一写回
    float lin = ui.maxLinSpeed.load();
    float yaw = ui.yawRate.load();
    float spd = ui.speedScale.load();
    paramRowF("线速度上限", &lin, 0.05f, 1.5f, "%.2f m/s", 0.60f);
    paramRowF("转向角速度", &yaw, 0.2f, 2.0f, "%.2f rad/s", 1.20f);
    paramRowF("快捷步速", &spd, 0.05f, 1.5f, "%.2f", 0.50f);
    ui.maxLinSpeed = lin;
    ui.yawRate = yaw;
    ui.speedScale = spd;
    ImGui::EndDisabled();
};

// 快捷方向键按 D-pad 排：上行「前进」居中，下行「左转 / 后退 / 右转」——
// 铺满整屏后比"两个一行"更直观，纵向也只占两行。
const auto drawQuick = [&] {
    sectionTitle("快捷");
    ImGui::SameLine();
    {
        FontScope fs = fontSmall();
        ImGui::TextDisabled("一次下发，速度持续到下一条指令");
    }
    ImGui::BeginDisabled(!canMove || ui.estop);
    {
        const float gap = 9.0f;
        const float bw = (ImGui::GetContentRegionAvail().x - gap * 2.0f) / 3.0f;
        const float bh = L.btnH > 0.0f ? L.btnH + 6.0f : 38.0f;
        const float x0 = ImGui::GetCursorPosX();
        // 快捷方向：语义在 cmd::quickMove（步速取 ui.speedScale），与网页端同一份
        ManagerSink sink(mgr, ui);
        ImGui::SetCursorPosX(x0 + bw + gap);  // 上行只放"前进"，保持在中间那一格
        // ★ D-pad 四向**统一用箭头家族**（arrow-*）。原来前进是 arrow-up、其余三个是
        //   caret-*（尖括号）—— 四个并排按钮混着两种字形 family，粗细和视觉重量都不同。
        if (ImGui::Button((std::string(icon::ArrowUp) + "  前进").c_str(), ImVec2(bw, bh))) {
            const int n = cmd::quickMove(sink, ui, "fwd");
            ui.addLog("[群控] 前进 → " + std::to_string(n) + " 台");
        }
        ImGui::SetCursorPosX(x0);
        if (ImGui::Button((std::string(icon::ArrowLeft) + "  左转").c_str(), ImVec2(bw, bh))) {
            const int n = cmd::quickMove(sink, ui, "left");
            ui.addLog("[群控] 左转 → " + std::to_string(n) + " 台");
        }
        ImGui::SameLine(0.0f, gap);
        if (ImGui::Button((std::string(icon::ArrowDown) + "  后退").c_str(), ImVec2(bw, bh))) {
            const int n = cmd::quickMove(sink, ui, "back");
            ui.addLog("[群控] 后退 → " + std::to_string(n) + " 台");
        }
        ImGui::SameLine(0.0f, gap);
        if (ImGui::Button(("右转  " + std::string(icon::ArrowRight)).c_str(), ImVec2(bw, bh))) {
            const int n = cmd::quickMove(sink, ui, "right");
            ui.addLog("[群控] 右转 → " + std::to_string(n) + " 台");
        }
    }
    ImGui::EndDisabled();
};

// 宽屏且纵向够 → 参数与快捷并排（把横向空间用起来）；否则依次往下排
if (L.paramInline && L.pageW >= 900.0f) {
    if (ImGui::BeginTable("remoteMain", 2,
                          ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoPadOuterX)) {
        ImGui::TableNextColumn();
        drawParams();
        ImGui::TableNextColumn();
        drawQuick();
        ImGui::EndTable();
    }
} else {
    if (L.paramInline)
        drawParams();
    else if (ImGui::CollapsingHeader("参数（线速度 / 角速度 / 步速）"))
        drawParams();
    drawQuick();
}

// ---- 当前指令与发送节拍 ----
// 决策全部交给纯函数 planMotion（急停语义有单元测试保证，见 tests/motion_test.cpp）
float lxEff = ui.joyLx;
const MotionPlan plan = planMotion(lxEff, ui.joyLy, ui.joyRx, ui.joyRy, ui.estop,
                                   ui.maxLinSpeed, ui.yawRate, ui.movingSent);
const int mask = (std::fabs(lxEff) > 1e-3f || std::fabs(ui.joyLy) > 1e-3f ? 1 : 0) |
                 (std::fabs(ui.joyRx) > 1e-3f ? 2 : 0);
// ★ 只有"本页摇杆正在驱动"（或上一帧是本页在驱动）时才回写共享速度。
//   否则网页界面正在摇杆时，这里每帧把 cmdVx/Vy/Vz 踩成 0 ——
//   既让网页端读数归零，也让"回中判定"（cmd::unestop 读 cmdVx/Vy/Vz）失效。
if (plan.send || ui.movingSent.load()) {
    ui.cmdVx = plan.vx;
    ui.cmdVy = plan.vy;
    ui.cmdVz = plan.vz;
}

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
    ManagerSink sink(mgr, ui);
    if (plan.send && (sticksChanged || now - ui.lastMoveSend >= 0.1)) {  // 10 Hz 群发
        const int n = cmd::moveSelected(sink, ui, plan.vx, plan.vy, plan.vz);
        ui.lastMoveSend = now;
        if (n == 0) ui.movingSent = false;
    }
    if (plan.stop) {
        const int n = cmd::stopSelected(sink);  // 松手/急停 → 勾选的狗全部停车
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

}  // namespace uix
}  // namespace go2
