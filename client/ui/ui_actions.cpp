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
// ---------------------------------------------------------------- 动作 → 图标
/// 动作 → Phosphor 字形（见 ui/icons.hpp）。用不了图标字体时不生效（走手绘兜底形状）。
///
/// ★ 按 `a.key`（英文标识）**逐个精确对应** —— 用户要求"每个按钮的图标别重样、要见名知意"：
///   纯关键词匹配做不到（前跳 / 跳跃奔跑 / 自由跳跃 会全撞成兔子，四个空翻会全撞成一个箭头）。
///   表里漏掉的（新固件指令）再退回中文关键词兜底，至少不会没图标。
///
/// ★ **这张表由 tools/icons/gen_icons.py 生成**（语义表在脚本里，网页端 icons.js 同源）。
///   改图标请改脚本再跑一次，别手改这里 —— 否则网页端会跟这里漂移。
///
/// ★ 放在**文件作用域**（不是某个页面的局部 lambda）：动作库瓷砖与底部快捷栏共用同一份表，
///   免得两处各写一份、早晚对不上。
const char* iconGlyph(const SportAction& a) {
    using namespace go2::icon;  // NOLINT
    struct KeyIcon {
        const char* key;
        const char* glyph;
    };
    // ★ 动作 → 图标：**这张表与网页端 icons.js 的 ACTION_ICON 同源**
    //   （语义表在 tools/icons/gen_icons.py，跑一次两端一起更新）。
    //   动作图标绝大多数是**天树人形动作剪影**（go2::icon::ts::a_stand / m_damping …），
    //   见 ui/icons.hpp 末尾的 `namespace ts`；少数没有对应剪影的
    //   （参数 / 状态查询 / 避障这类"非肢体动作"）用 Phosphor 字形。
    static const KeyIcon kMap[] = {
        {"Damp", ts::m_damping},         // 阻尼：软腿瘫倒
        {"BalanceStand", ts::m_squat},   // 平衡站立：半蹲找平衡
        {"StopMove", HandPalm},      // 停止移动：举手示意停
        {"StandUp", ts::m_stand},        // 站立
        {"StandDown", ts::a_lieDown},    // 趴下：卧倒剪影
        {"RecoveryStand", ArrowUUpLeft}, // 恢复站立：起身箭头
        {"Sit", ts::a_sitDown},          // 坐下：坐姿剪影
        {"RiseSit", ts::m_seating},      // 起立(坐姿)：坐姿剪影
        {"Euler", ts::m_pose},           // 姿态角：姿态剪影
        {"SwitchGait", ts::m_step},      // 切换步态：换步
        {"BodyHeight", ts::m_climb},     // 机身高度：抬机身
        {"FootRaiseHeight", ts::m_a2_stand}, // 抬腿高度：A 字抬腿
        {"SpeedLevel", ts::m_run},       // 速度档位：奔跑速度
        {"ContinuousGait", ts::m_keepMoving}, // 持续步态：持续移动
        {"EconomicGait", ts::m_batteryLife}, // 经济步态：省电（续航）
        {"StaticWalk", ts::m_g1_walk},   // 静态行走：行走剪影
        {"TrotRun", ts::m_keepRunning},  // 小跑：奔跑剪影
        {"SwitchJoystick", Joystick}, // 手柄接管：手柄（无对应剪影）
        {"Trigger", Crosshair},      // 扳机：准星（无对应剪影）
        {"Hello", ts::a_hightWave},      // 打招呼：高挥手
        {"Stretch", ts::a_stretch},      // 伸懒腰：伸展剪影
        {"Content", ts::a_happy},        // 满意：开心脸
        {"Wallow", ts::a_hug},           // 撒娇打滚：拥抱（翻滚着黏人）
        {"Dance1", ts::a_dance1},        // 舞蹈 1
        {"Dance2", ts::a_dance2},        // 舞蹈 2
        {"Pose", ts::m_b2w_special},     // 摆姿势：展臂亮相
        {"Scrape", ts::a_newYear},       // 拜年(作揖)：新年作揖
        {"WiggleHips", ts::a_turnWave},  // 扭屁股：转身摇摆
        {"FingerHeart", ts::a_makeHeartBothHands}, // 比心：双手比心
        {"MoonWalk", ts::m_walk},        // 太空步：滑步后仰
        {"OnesidedStep", ts::m_sideStep}, // 单边踏步：侧踏剪影
        {"CrossStep", ts::m_crossStep},  // 交叉步：交叉步剪影
        {"StandOut", ts::m_standActive}, // 站立展示：站立激活
        {"LeadFollow", ts::m_b2w_all_terrain}, // 领航跟随：全地形跟随
        {"FreeWalk", ts::m_freeWalk},    // 自由行走：自由行走
        {"FrontJump", ts::a_jumpForward}, // 前跳：向前跳
        {"FrontPounce", ts::a_pounceForward}, // 前扑：向前扑
        {"FrontFlip", ts::a_rollOver},   // 前空翻：向前翻滚
        {"LeftFlip", ts::a_turnOver},    // 左空翻：侧身翻身
        {"RightFlip", ts::m_combat},     // 右空翻：出招式翻身
        {"BackFlip", ts::m_lieUp},       // 后空翻：躺地翻起
        {"Handstand", ts::m_hand_stand}, // 倒立：手倒立
        {"Bound", ts::m_runSideBySide},  // 跳跃奔跑：并腿跑
        {"FreeJump", ts::m_squatUp},     // 自由跳跃：蹲身起跳
        {"GetBodyHeight", Ruler},    // 查机身高度：量尺寸
        {"GetFootRaiseHeight", ArrowFatUp}, // 查抬腿高度：向上抬
        {"GetSpeedLevel", Speedometer}, // 查速度档位：速度表
        {"GetState", Pulse},         // 查运动状态：脉搏
        {"GetAutoRecovery", FirstAid}, // 查自动恢复：急救
        {"TrajectoryFollow", Path},  // 轨迹跟随：路径
        {"CrossWalk", ts::m_climbingStairs}, // 横向行走：阶梯式横移
        {"Standup", ts::m_zeroTorque},   // 起立(兼容)：从瘫软起身
        {"ClassicWalk", ts::m_walk},     // 经典步态：行走
        {"BackStand", ts::m_preparation}, // 后仰站立：准备姿势
        {"SetAutoRecovery", Wrench}, // 设自动恢复：扳手
        {"FreeAvoid", Shield},  // 自由避障：盾牌
        {"SwitchAvoidMode", Warning}, // 避障模式：注意
    };
    const std::string k = a.key ? a.key : "";
    for (const KeyIcon& m : kMap)
        if (k == m.key) return m.glyph;

    // 兜底：中文关键词（新指令进库、还没来得及加表时用）
    const std::string& l = a.label;
    auto has = [&](const char* s) { return l.find(s) != std::string::npos; };
    if (has("空翻") || has("翻")) return ts::a_turnOver;
    if (has("跳")) return ts::a_jumpForward;
    if (has("扑")) return ts::a_pounceForward;
    if (has("比心") || has("爱心")) return ts::a_makeHeartBothHands;
    if (has("握手") || has("招手") || has("打招呼")) return ts::a_hightWave;
    if (has("拜年") || has("作揖") || has("恭喜")) return ts::a_newYear;
    if (has("舞") || has("扭")) return ts::a_dance1;
    if (has("懒腰")) return ts::a_stretch;
    if (has("坐") || has("趴")) return ts::a_sitDown;
    if (has("站") || has("立") || has("起身")) return ts::m_stand;
    if (has("倒立") || has("姿势")) return ts::m_hand_stand;
    if (has("跑") || has("步") || has("行走")) return ts::m_walk;
    if (has("阻尼") || has("锁")) return ts::m_damping;
    if (has("高度")) return Sliders;
    if (has("速度") || has("档位") || has("步态")) return Gauge;
    if (has("状态") || has("查")) return Search;
    return Paw;  // 兜底：狗爪 —— 比通用人形更贴"机器狗"
}

/// 动作图标：按**码位区间**决定用哪份字体。
///
///   U+E100~U+E1FF → TianshuGo2（天树人形动作剪影，来自宇树官方 App）
///   其它           → Phosphor（参数 / 状态查询 / 避障这类非肢体动作）
///
/// 码位区间是唯一判据 —— 两套字体的码位段不重叠，一眼就能分流，
/// 不用在动作表里再维护一份"这个动作用哪个字体"的标记（那正是最容易漂移的地方）。
/// 天树私有区 U+E100~U+E1FF 的 UTF-8 首字节恒为 0xEE。
bool glyphIsTianshu(const char* glyph) {
    return glyph && *glyph && static_cast<unsigned char>(glyph[0]) == 0xEE;
}

// ---------------------------------------------------------------- 动作库页（常驻整屏）
// ★ 动作库是**常驻页面**，不是弹窗：铺满整屏宽度（按 pageW，比遥控页宽），
//   与下方摇杆带用一条分隔线明确分开；页眉固定不动，只有动作网格滚动。
//   一排排按钮平铺：分组只作为小标题，不折叠。
void drawActionPageHeader(UiState& ui) {
    // 与顶栏/更多菜单同一个图标（icon::List）—— 同一功能全站一致
    sectionTitle((std::string(icon::List) + " 动作库").c_str());
    ImGui::SameLine();
    {
        FontScope fs = fontSmall();
        ImGui::TextDisabled("指令发给 %s", ui.controlTargetText().c_str());
    }
    ImGui::SameLine(0, 18);
    // atomic 不能取址交给 ImGui → 取出副本给控件，再写回（见 ui.hpp 的线程契约说明）
    bool mcf = ui.mcfMode.load();
    if (ImGui::Checkbox("MCF 固件", &mcf) && takeTipShown()) mcf = !mcf;  // 长按看说明 → 撤销
    ui.mcfMode = mcf;
    helpTip("Go2 Pro 等 MCF 固件使用另一套 api_id（如后空翻 2043 vs 1044）；\n"
            "选错也没关系：被拒后会用另一套 id 自动重试一次");
    ImGui::SameLine();
    bool hideUnsup = ui.hideUnsupported.load();
    if (ImGui::Checkbox("隐藏不支持的", &hideUnsup) && takeTipShown()) hideUnsup = !hideUnsup;
    ui.hideUnsupported = hideUnsup;
    helpTip("隐藏「试过且被该固件拒绝（code=3203）」的动作，\n"
            "避免反复点到不存在的指令；取消勾选即可重新显示");
}

void drawActionPageBody(RobotManager& mgr, UiState& ui, const LayoutSpec& L) {
    const float actW1 = L.actW1;
    const float actW2 = L.actW2;
    const float actW3 = L.actW3;
    const float actH = L.actH;

// 参数类动作（带滑条的那些）：一行放 2 个、滑条自动撑满格子。
// 用户反馈"整体太空了" —— 整屏页面上如果每行只有左边一小截滑条，右边全是空白，非常难看。
// 横排瓷砖：图标在左、字在右，高度只需容纳图标（+少量余量）。
// 竖排时代是 actH + 32（给下方标签留一行），横排后那 32px 全是浪费。
const float tileH = actH + 6.0f;
const float kParamGap = 12.0f;
const float paramCellW = (L.actAreaW - kParamGap) * 0.5f;
const float paramSliderW = std::max(120.0f, paramCellW - actW2 - 9.0f);
int paramCol = 0;
const auto paramCell = [&] {
    if (paramCol % 2 != 0) ImGui::SameLine(0.0f, kParamGap);
    ++paramCol;
};

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
// 瓷砖：标签不带 ✓/✗（改用右上角小点），只保留"另一套指令集"的记号
const auto tileLabel = [](const SportAction& a, bool fellBack) {
    std::string t = a.label;
    if (differsInMcf(a)) t += "*";
    if (fellBack) t += "†";
    return t;
};
// 上次回执 → 小点状态：0 没试过 / 1 成功 / 2 失败
const auto apiState = [&ui](int id) {
    int c = 0;
    std::string n;
    if (!ui.apiResult(id, &c, &n)) return 0;
    return c == 0 ? 1 : 2;
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

// 发送一条动作：语义（指令集 id 解析 + 参数打包 + 群控分发 + 开关状态维护）全在
// cmd::sendAction 里 —— 桌面端与网页端共用，这里只补一条日志。
// flagValue 只对 Flag 类生效（开关型指令传 false 就是"关闭"）
auto sendAction = [&](const SportAction& a, bool flagValue = true) {
    // 触摸下"长按看说明"的那一下不算执行 —— 否则想看说明就变成了真的下发动作
    if (takeTipShown()) return;
    bool fellBack = false;
    const int id = resolveId(a, &fellBack);
    ManagerSink sink(mgr, ui);
    const int n = cmd::sendAction(sink, ui, a, flagValue);
    ui.addLog("[动作库] " + std::string(a.label) +
              (a.toggle ? (flagValue ? " 开启" : " 关闭") : "") + " (api " +
              std::to_string(id) + (fellBack ? "，当前指令集无此条，用另一套 id" : "") +
              ") → " + std::to_string(n) + " 台");
};

// ---------------------------------------------------------------- 官方 App 风格的动作瓷砖
// 参考宇树官方 App：圆角半透明面板 + 图标 + 下方小字标签的"瓷砖"网格。
//
// 图标用开源图标字体 **Phosphor Icons**（MIT，细线圆润 —— 与官方风格同类）：
// 官方那套美术素材在加密的 Web bundle 里、APK 里也取不到（1017 张资源图全是加固改名），
// 所以按动作名关键词挑字形，观感对齐（同样细线人物 / 手掌语义）。字形清单见 ui/icons.hpp。
// 找不到图标字体时（iconFontLoaded()==false）自动退回下面的手绘简笔形状，功能不受影响。
enum class TileIcon { Figure, Hand, Heart, Jump, Dance, Sit, Stand, Walk, Gear, Query, Lock };

const auto iconFor = [](const SportAction& a) -> TileIcon {
    const std::string l = a.label;
    auto has = [&](const char* s) { return l.find(s) != std::string::npos; };
    if (has("跳") || has("翻") || has("扑")) return TileIcon::Jump;
    if (has("舞蹈") || has("拜年") || has("太空步") || has("扭")) return TileIcon::Dance;
    if (has("比心")) return TileIcon::Heart;
    if (has("握手") || has("打招呼")) return TileIcon::Hand;
    if (has("坐") || has("趴") || has("伸懒腰") || has("满意") || has("撒娇")) return TileIcon::Sit;
    if (has("站") || has("立") || has("起身") || has("摆姿势")) return TileIcon::Stand;
    if (has("步") || has("行走") || has("小跑") || has("跟随") || has("避障") || has("踏步"))
        return TileIcon::Walk;
    if (has("查") || has("状态")) return TileIcon::Query;
    if (has("阻尼") || has("停止") || has("恢复") || has("锁定")) return TileIcon::Lock;
    if (has("高度") || has("档位") || has("步态") || has("扳机") || has("接管") || has("角度"))
        return TileIcon::Gear;
    return TileIcon::Figure;
};

// 图标映射已提到**文件作用域**（见上面的 iconGlyph）—— 底部快捷栏也要用它，
// 免得两处各写一份表、早晚对不上。

/// 在 c 处画一个尺寸约 s 的简笔图标
const auto drawTileIcon = [](ImDrawList* dl, TileIcon ic, ImVec2 c, float s, ImU32 col) {
    const float t = std::max(1.5f, s * 0.085f);
    switch (ic) {
        case TileIcon::Figure:
            dl->AddCircle(ImVec2(c.x, c.y - s * 0.34f), s * 0.13f, col, 0, t);
            dl->AddLine(ImVec2(c.x, c.y - s * 0.20f), ImVec2(c.x, c.y + s * 0.14f), col, t);
            dl->AddLine(ImVec2(c.x - s * 0.30f, c.y - s * 0.06f),
                        ImVec2(c.x + s * 0.30f, c.y - s * 0.06f), col, t);
            dl->AddLine(ImVec2(c.x, c.y + s * 0.14f), ImVec2(c.x - s * 0.20f, c.y + s * 0.42f),
                        col, t);
            dl->AddLine(ImVec2(c.x, c.y + s * 0.14f), ImVec2(c.x + s * 0.20f, c.y + s * 0.42f),
                        col, t);
            break;
        case TileIcon::Hand:
            dl->AddCircleFilled(ImVec2(c.x, c.y + s * 0.12f), s * 0.20f, col, 20);
            for (int i = -1; i <= 1; ++i)
                dl->AddLine(ImVec2(c.x + i * s * 0.13f, c.y + s * 0.02f),
                            ImVec2(c.x + i * s * 0.16f, c.y - s * 0.34f), col, t);
            dl->AddLine(ImVec2(c.x - s * 0.20f, c.y + s * 0.16f),
                        ImVec2(c.x - s * 0.38f, c.y + s * 0.26f), col, t);
            break;
        case TileIcon::Heart:
            dl->AddCircleFilled(ImVec2(c.x - s * 0.13f, c.y - s * 0.10f), s * 0.19f, col, 20);
            dl->AddCircleFilled(ImVec2(c.x + s * 0.13f, c.y - s * 0.10f), s * 0.19f, col, 20);
            dl->AddTriangleFilled(ImVec2(c.x - s * 0.315f, c.y - s * 0.02f),
                                  ImVec2(c.x + s * 0.315f, c.y - s * 0.02f),
                                  ImVec2(c.x, c.y + s * 0.40f), col);
            break;
        case TileIcon::Jump:
            dl->AddTriangleFilled(ImVec2(c.x, c.y - s * 0.42f), ImVec2(c.x - s * 0.22f, c.y - s * 0.06f),
                                  ImVec2(c.x + s * 0.22f, c.y - s * 0.06f), col);
            dl->AddRect(ImVec2(c.x - s * 0.24f, c.y + s * 0.16f), ImVec2(c.x + s * 0.24f, c.y + s * 0.42f),
                        col, s * 0.06f, 0, t);
            break;
        case TileIcon::Dance:
            dl->AddCircle(ImVec2(c.x - s * 0.04f, c.y - s * 0.34f), s * 0.12f, col, 0, t);
            dl->AddLine(ImVec2(c.x - s * 0.04f, c.y - s * 0.22f), ImVec2(c.x, c.y + s * 0.14f), col, t);
            dl->AddLine(ImVec2(c.x - s * 0.04f, c.y - s * 0.12f), ImVec2(c.x - s * 0.34f, c.y - s * 0.30f),
                        col, t);
            dl->AddLine(ImVec2(c.x, c.y + s * 0.14f), ImVec2(c.x + s * 0.30f, c.y + s * 0.40f), col, t);
            dl->AddLine(ImVec2(c.x, c.y + s * 0.14f), ImVec2(c.x - s * 0.24f, c.y + s * 0.38f), col, t);
            break;
        case TileIcon::Sit:
            dl->AddCircle(ImVec2(c.x - s * 0.18f, c.y - s * 0.28f), s * 0.13f, col, 0, t);
            dl->AddLine(ImVec2(c.x - s * 0.10f, c.y - s * 0.16f), ImVec2(c.x + s * 0.10f, c.y + s * 0.06f),
                        col, t);
            dl->AddLine(ImVec2(c.x + s * 0.10f, c.y + s * 0.06f), ImVec2(c.x + s * 0.34f, c.y + s * 0.36f),
                        col, t);
            dl->AddLine(ImVec2(c.x - s * 0.02f, c.y + s * 0.34f), ImVec2(c.x + s * 0.34f, c.y + s * 0.34f),
                        col, t);
            break;
        case TileIcon::Stand:
            dl->AddCircle(ImVec2(c.x, c.y - s * 0.32f), s * 0.13f, col, 0, t);
            dl->AddLine(ImVec2(c.x, c.y - s * 0.19f), ImVec2(c.x, c.y + s * 0.12f), col, t);
            dl->AddLine(ImVec2(c.x - s * 0.26f, c.y - s * 0.10f),
                        ImVec2(c.x + s * 0.26f, c.y - s * 0.10f), col, t);
            dl->AddLine(ImVec2(c.x, c.y + s * 0.12f), ImVec2(c.x - s * 0.16f, c.y + s * 0.42f), col, t);
            dl->AddLine(ImVec2(c.x, c.y + s * 0.12f), ImVec2(c.x + s * 0.16f, c.y + s * 0.42f), col, t);
            break;
        case TileIcon::Walk:
            dl->AddCircleFilled(ImVec2(c.x - s * 0.16f, c.y - s * 0.16f), s * 0.10f, col, 16);
            dl->AddCircleFilled(ImVec2(c.x + s * 0.14f, c.y + s * 0.14f), s * 0.10f, col, 16);
            dl->AddLine(ImVec2(c.x - s * 0.06f, c.y - s * 0.34f), ImVec2(c.x + s * 0.30f, c.y - s * 0.34f),
                        col, t);
            dl->AddLine(ImVec2(c.x + s * 0.20f, c.y - s * 0.42f),
                        ImVec2(c.x + s * 0.34f, c.y - s * 0.34f), col, t);
            dl->AddLine(ImVec2(c.x + s * 0.20f, c.y - s * 0.26f),
                        ImVec2(c.x + s * 0.34f, c.y - s * 0.34f), col, t);
            break;
        case TileIcon::Gear:
            for (int i = 0; i < 2; ++i) {
                const float y = c.y + (i == 0 ? -s * 0.18f : s * 0.18f);
                dl->AddLine(ImVec2(c.x - s * 0.36f, y), ImVec2(c.x + s * 0.36f, y), col, t);
                dl->AddCircleFilled(ImVec2(c.x + (i == 0 ? -s * 0.12f : s * 0.14f), y), s * 0.11f, col, 16);
            }
            break;
        case TileIcon::Query:
            dl->AddCircle(ImVec2(c.x - s * 0.08f, c.y - s * 0.10f), s * 0.26f, col, 0, t);
            dl->AddLine(ImVec2(c.x + s * 0.10f, c.y + s * 0.10f), ImVec2(c.x + s * 0.36f, c.y + s * 0.38f),
                        col, t);
            break;
        case TileIcon::Lock:
            dl->AddRectFilled(ImVec2(c.x - s * 0.26f, c.y - s * 0.04f),
                              ImVec2(c.x + s * 0.26f, c.y + s * 0.38f), col, s * 0.07f);
            dl->PathArcTo(ImVec2(c.x, c.y - s * 0.06f), s * 0.18f, 3.15f, 6.28f, 16);
            dl->PathStroke(col, 0, t);
            break;
    }
};

/// 瓷砖图标：有图标字体就画字形，否则回退手绘简笔。
const auto drawTileIconAuto = [&iconFor, &drawTileIcon](
                                  ImDrawList* dl, const SportAction& a, ImVec2 c, float s,
                                  ImU32 col) {
    const char* g = iconGlyph(a);
    const bool isTianshu = glyphIsTianshu(g);
    ImFont* f = nullptr;
    float gs = s;

    if (isTianshu && go2::actionFont()) {
        // 天树剪影：单字重、实心块，字面就比细线"重"，所以**不需要**加粗，
        // 反而要略微收一点尺寸，否则会撑出瓷砖。
        f = go2::actionFont();
        gs = s * 1.02f;
    } else if (go2::iconFontBold()) {
        // Phosphor 兜底项：加粗才在 84px 瓷砖上立得住
        f = go2::iconFontBold();
        gs = s * 0.98f;
    } else if (go2::iconFontLoaded()) {
        f = ImGui::GetFont();
        gs = s * 1.06f;
    }

    if (f) {
        const ImVec2 ts = f->CalcTextSizeA(gs, FLT_MAX, 0.0f, g);
        dl->AddText(f, gs, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), col, g);
        return;
    }
    drawTileIcon(dl, iconFor(a), c, s, col);
};

/// 官方 App 风格的"图标瓷砖"。返回是否被点击。
/// @param st 上次执行结果：0=没试过 1=成功 2=失败（右上角小圆点）
/// @param on 持续模式开关的当前状态（高亮）
const auto actionTile = [&drawTileIconAuto](const SportAction& a, const std::string& label,
                                            bool risky, int st, bool on,
                                            const ImVec2& size) -> bool {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##tile", size);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    const bool clicked = ImGui::IsItemClicked();
    const ImVec2 b(p.x + size.x, p.y + size.y);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImVec4 bg = on ? ImVec4(col::kAccent.x, col::kAccent.y, col::kAccent.z, 0.22f)
                   : ImVec4(1.0f, 1.0f, 1.0f, 0.045f);
    if (held) bg = ImVec4(1.0f, 1.0f, 1.0f, 0.14f);
    else if (hovered) bg = ImVec4(1.0f, 1.0f, 1.0f, 0.095f);
    const ImVec4 bd = risky ? kRed : (on ? col::kAccent : ImVec4(1, 1, 1, 0.10f));
    // ★ 悬停 / 开启时"离地"：ImGui 没有模糊，用 2~3 层低透明度圆角矩形近似投影
    if (hovered || on) {
        const int layers = on ? 3 : 2;
        for (int i = layers; i >= 1; --i) {
            const float o = static_cast<float>(i) * 1.7f;
            dl->AddRectFilled(ImVec2(p.x - o * 0.25f, p.y + o * 0.5f),
                              ImVec2(b.x + o * 0.25f, b.y + o),
                              ImGui::GetColorU32(ImVec4(0, 0, 0, 0.075f)), 12.0f);
        }
    }
    dl->AddRectFilled(p, b, ImGui::GetColorU32(bg), 12.0f);
    // ★ 顶部高光：沿上沿一条 1px 淡白线 —— 玻璃/金属质感其实就差这一笔
    dl->AddLine(ImVec2(p.x + 12.0f, p.y + 1.0f), ImVec2(b.x - 12.0f, p.y + 1.0f),
                ImGui::GetColorU32(ImVec4(1, 1, 1, hovered ? 0.22f : 0.11f)), 1.0f);
    dl->AddRect(p, b, ImGui::GetColorU32(bd), 12.0f, 0, risky ? 1.6f : 1.0f);

    // ★ 横排布局：图标在左、文字在右，整组**水平居中**（文字长短不一时也不偏）。
    const float pad = std::min(size.x, size.y) * 0.13f;
    const float iconSz = size.y * 0.72f;                 // 高度几乎全给图标
    float tw = 0.0f, th = 0.0f;
    {
        FontScope fs = fontSmall();
        const ImVec2 ts = ImGui::CalcTextSize(label.c_str());
        tw = ts.x;
        th = ts.y;
    }
    const float gap = pad * 0.9f;
    const float groupW = iconSz + gap + tw;               // 整组宽度
    const float gx = p.x + (size.x - groupW) * 0.5f;     // 整组左边界（居中）
    const float midY = (p.y + b.y) * 0.5f;
    // ★ 图标字形在 em 框里偏上（字体基线决定）：按实测偏移 0.2em 把它压下来，
    //   否则图标视觉中心比按钮中点高 6px，整组看着偏上。
    drawTileIconAuto(dl, a, ImVec2(gx + iconSz * 0.5f, midY + iconSz * 0.2f), iconSz,
                     ImGui::GetColorU32(col::kText));
    {
        FontScope fs = fontSmall();
        dl->AddText(ImVec2(gx + iconSz + gap, midY - th * 0.5f),
                    ImGui::GetColorU32(col::kText), label.c_str());
    }
    // 回执状态点放**左上角**（与网页端一致）
    if (st == 1)
        dl->AddCircleFilled(ImVec2(p.x + 8.0f, p.y + 8.0f), 3.0f, ImGui::GetColorU32(kGreen), 12);
    else if (st == 2)
        dl->AddCircleFilled(ImVec2(p.x + 8.0f, p.y + 8.0f), 3.0f, ImGui::GetColorU32(kRed), 12);
    return clicked && !takeTipShown();
};

struct GroupDef {
    SportGroup g;
    const char* title;
};
// ★ 顺序按用户要求：**姿势/动作类放上面，步态类（那几个带滑条的）放最下面**。
//   理由：常用的是站/趴/打招呼/舞蹈这些"姿势动作"，一眼就能点到；
//   步态/身高那组每行都带滑条、最占地方，压到页面底部不挡事。
static const GroupDef kGroups[] = {
    {SportGroup::Basic, "基础姿态"},
    {SportGroup::Show, "表演动作 / 姿势"},
    {SportGroup::Stunt, "跳跃特技（危险）"},
    {SportGroup::Query, "状态查询"},
    {SportGroup::Advanced, "其他 / 进阶"},
    // ★ 2026-09-28 用户要求：「步态 / 速度 / 身高（参数）」这一组**整组撤下**
    //   （那批滑条：姿态角 / 切换步态 / 机身高度 / 抬腿高度 / 速度档位，
    //    以及组里的持续步态 / 经济步态 / 静态行走 / 小跑 / 手柄接管 / 扳机 都不再显示）
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
        // 也参与网格排布 —— 整屏页面下一行能放 8 个，让开关单独占一整行会非常空旷。
        if (a.toggle) {
            if (L.actCols > 1 && (col % L.actCols) != 0) ImGui::SameLine();
            ++col;
            bool on = ui.toggleState(a.key);
            const std::string t = tileLabel(a, fellBack) + (on ? " · 开" : " · 关");
            if (actionTile(a, t, false, apiState(id), on, ImVec2(actW1, tileH))) {
                on = !on;
                // 开关状态维护（toggles / activeToggleIds）在 cmd::dispatchToggle 里，
                // 与网页端同一份 —— 这里不再自己 insert/erase
                ManagerSink sink(mgr, ui);
                const int n = cmd::dispatchToggle(sink, ui, a, on);
                ui.addLog("[动作库] " + std::string(a.label) + (on ? " 开启" : " 关闭") +
                          " → " + std::to_string(n) + " 台");
            }
            actionTip(id);
            helpTip("持续模式开关（开启后会一直生效，StopMove 停不掉）\n"
                    "点一下切换开/关；急停会自动把所有开关关掉");
            ImGui::PopID();
            continue;
        }

        if (a.param == SportParam::Int || a.param == SportParam::Real) {
            const std::string key = a.key;
            paramCell();  // 一行放两个参数：滑条撑满格子，右边不留空白
            // 这些参数是 atomic（Web 端 param 指令会写）→ 滑条操作副本再写回
            if (a.param == SportParam::Int) {
                const bool isGait = (key == "SwitchGait");
                int v = isGait ? ui.gaitType.load() : ui.speedLevel.load();
                iosSliderInt("##v", &v, 0, isGait ? 4 : 2, paramSliderW);
                if (isGait) ui.gaitType = v; else ui.speedLevel = v;
            } else {
                const bool isFoot = (key == "FootRaiseHeight");
                float v = isFoot ? ui.footRaise.load() : ui.bodyHeight.load();
                iosSliderFloat("##v", &v, 0.0f, 0.35f, "%.2f", paramSliderW);
                if (isFoot) ui.footRaise = v; else ui.bodyHeight = v;
            }
            ImGui::SameLine();
            if (ImGui::Button((label + "##b").c_str(), ImVec2(actW2, actH))) sendAction(a);
            actionTip(id);
            col = 0;
            ImGui::PopID();
            continue;
        }

        if (a.param == SportParam::Euler) {
            paramCol = 0;  // 姿态角占一整行：三个轴 + 发送
            const float ew = std::max(76.0f, (L.actAreaW - actW2 - 9.0f * 3.0f) / 3.0f);
            const bool dx = iosSliderFloat("roll##e", &ui.eulerX, -0.5f, 0.5f, "%.2f", ew);
            ImGui::SameLine();
            const bool dy = iosSliderFloat("pitch##e", &ui.eulerY, -0.5f, 0.5f, "%.2f", ew);
            ImGui::SameLine();
            const bool dz = iosSliderFloat("yaw##e", &ui.eulerZ, -0.6f, 0.6f, "%.2f", ew);
            // 手动拖滑条 = 用户要重新构图。此刻**先立刻把狗扶正**，再让用户慢慢调：
            //   · 只作废待归零任务 → 已经歪着的那一下永远没人收尾（人调着调着走开了）；
            //   · 保留待归零任务  → 回正那一刻会把用户刚拖出来的值一起抹成 0。
            //   待归零任务存在 = 狗当前确实是歪的，所以这次立即归零既安全又必要。
            if ((dx || dy || dz) && ui.eulerResetAt >= 0.0) {
                ManagerSink sink(mgr, ui);
                const int n = cmd::resetEuler(sink, ui);  // 内部会清掉 eulerResetAt
                ui.addLog("[姿态角] 重新调姿态，先归零 → " + std::to_string(n) + " 台");
            }
            ImGui::SameLine();
            // 按钮宽度用格子宽（不是 -1 全宽）：整屏页面上全宽按钮会被拉成一条长横条
            if (ImGui::Button((label + "##b").c_str(), ImVec2(actW2, actH))) {
                sendAction(a);
                // ★ 姿态角是保持型参数：不清零的话狗会一直歪着走下去。
                //   挂一个到期时刻，到点由 drawUi 自动下发全零（用户中途切走页面也照样执行）。
                ui.eulerResetAt = ImGui::GetTime() + cmd::kEulerHoldSeconds;
            }
            actionTip(id);
            helpTip("roll / pitch / yaw 三个轴的保持角度（rad）\n"
                    "发送后约 1.5 秒自动归零 —— 否则狗会一直保持这个歪斜姿态走下去\n"
                    "归零前重新拖滑条会立刻先把狗扶正");
            col = 0;
            ImGui::PopID();
            continue;
        }

        if (a.param == SportParam::Json) {
            paramCol = 0;
            // 输入框别拉满整行 —— 否则按钮被甩到最右边，看着和输入框没关系
            ImGui::SetNextItemWidth(std::min(std::max(L.actAreaW * 0.45f, 180.0f), 460.0f));
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
        if (actionTile(a, tileLabel(a, fellBack), a.risky, apiState(id), false,
                       ImVec2(actW1, tileH)))
            sendAction(a);
        actionTip(id);

        ImGui::PopID();
    }
}
// ★ 2026-09-28 用户要求：图例文字与「官方 App 常用动作快捷」整块（机身高度三档 / 姿态角 /
//   侧移 / 运动模式切换 / 舞蹈编排 / 待验证动作探测）**全部从界面撤下** —— 动作库页现在只有瓷砖网格。

        ImGui::EndDisabled();
}

/// 动作库页的骨架：固定页眉 + 可滚动的动作网格。
/// 高度扣掉摇杆带（−joyReserve）—— 内容永远不会渲染到摇杆的地盘上，
/// 这就是"动作库与摇杆区明确区分上下位置"的落点。
void drawActionPage(RobotManager& mgr, UiState& ui, const LayoutSpec& L) {
    // 半透明深色面板（官方 App 那种"浮在地面上的面板"观感）
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.11f, 0.14f, 0.62f));
    ImGui::BeginChild("actpage", ImVec2(0, -L.joyReserve), ImGuiChildFlags_Borders);
    drawActionPageHeader(ui);
    ImGui::Separator();
    ImGui::BeginChild("actscroll", ImVec2(0, 0), ImGuiChildFlags_None);
    touchDragScroll(L);  // 触摸：按住拖动即可滚动（不用去抓右边滚动条）
    drawActionPageBody(mgr, ui, L);
    ImGui::EndChild();
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

}  // namespace uix
}  // namespace go2
