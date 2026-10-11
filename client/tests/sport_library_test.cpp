// ============================================================================
// 运动指令表自测（纯数据，不连机器狗、不需要图形环境）。
//
// 为什么值得测：这张表是"界面按钮 → api_id"的唯一来源，而且一半的 id 是逆向来的。
// 一旦有人复制粘贴时把 key 写重复、或把 MCF 专属指令的两套 id 填错，
// 现象是"某个动作点了没反应"或"发到了别的动作上" —— 现场很难定位，
// 但在这里一眼就能拦住。
// ============================================================================

#include "sport_library.hpp"

#include "test_util.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

using go2::SportAction;
using go2::SportParam;
using go2::sportActions;

namespace {

const SportAction* find(const std::string& key) {
    for (const auto& a : sportActions())
        if (key == a.key) return &a;
    return nullptr;
}

}  // namespace

int main() {
    // ---------------------------------------------------------- 表结构不变量
    {
        // key 唯一：重复会造成 ImGui 的 PushID 冲突 + 回执标注串台
        std::set<std::string> keys;
        for (const auto& a : sportActions()) {
            CHECK_MSG(keys.insert(a.key).second, std::string("key 重复: ") + a.key);
        }
        CHECK(sportActions().size() > 30);  // 表被整体删空/截断时报警
        // 2026-10-11：「步态 / 速度 / 身高」整组从表里删除（界面上 2026-09-28 就撤下了）。
        // 这条断言是**防回潮**：有人把 Euler / BodyHeight / SpeedLevel 之类再加回表里的话，
        // 界面上会多出一组谁也点不到的按钮 —— 要么连 UI 一起加回来，要么就别加。
        for (const auto& a : sportActions())
            CHECK_MSG(a.group != go2::SportGroup::Gait,
                      std::string("该组已整组删除，不该再有条目: ") + a.key);
        // 「状态查询」同理（2026-10-11）：那 5 条只发不读，点了只会显示"成功"、
        // 看不到任何数据；真要用就该把返回的 data 接到状态区，而不是留个骗人的按钮。
        for (const auto& a : sportActions())
            CHECK_MSG(a.group != go2::SportGroup::Query,
                      std::string("查询组已整组删除，不该再有条目: ") + a.key);
    }
    {
        // 两套指令集不能同时为空（否则这条动作永远发不出去，界面上却是可点的）
        for (const auto& a : sportActions()) {
            CHECK_MSG(a.normalId != 0 || a.mcfId != 0,
                      std::string("两套指令集都没有 id: ") + a.key);
        }
    }
    {
        // 开关型动作（toggle）必须是 Flag 参数 —— 急停要靠 {"data": false} 关掉它们，
        // 参数类型不对的话"关不掉"，那是最危险的组合
        for (const auto& a : sportActions()) {
            if (a.toggle)
                CHECK_MSG(a.param == SportParam::Flag,
                          std::string("toggle 动作的参数类型必须是 Flag: ") + a.key);
        }
    }

    // ---------------------------------------------------------- apiIdFor 语义
    {
        const SportAction* flip = find("BackFlip");  // 普通 1044 / MCF 2043
        CHECK(flip != nullptr);
        if (flip) {
            CHECK(go2::apiIdFor(*flip, false) == 1044);
            CHECK(go2::apiIdFor(*flip, true) == 2043);
            CHECK(go2::differsInMcf(*flip));
        }
        const SportAction* hello = find("Hello");  // 两套相同
        CHECK(hello != nullptr);
        if (hello) {
            CHECK(go2::apiIdFor(*hello, false) == 1016);
            CHECK(go2::apiIdFor(*hello, true) == 1016);
            CHECK(!go2::differsInMcf(*hello));
        }
        const SportAction* mcfOnly = find("FreeJump");  // 只有 MCF 有（normalId=0）
        CHECK(mcfOnly != nullptr);
        if (mcfOnly) {
            CHECK(go2::apiIdFor(*mcfOnly, true) == 2047);
            CHECK(go2::apiIdFor(*mcfOnly, false) == 0);  // 非 MCF 下无此条 → 界面会兜底
        }
    }

    // ---------------------------------------------------------- 指令集互查
    {
        // 文档里点名的例子：后空翻 1044 <-> 2043（"被拒后自动换另一套 id"靠它）
        const auto alt = go2::alternateApiIds(1044);
        CHECK(std::find(alt.begin(), alt.end(), 2043) != alt.end());
        const auto back = go2::alternateApiIds(2043);
        CHECK(std::find(back.begin(), back.end(), 1044) != back.end());
        // 两套相同的指令没有"另一套"
        CHECK(go2::alternateApiIds(1016).empty());
        CHECK(go2::alternateApiIds(0).empty());
    }
    {
        // 持续模式 id 集合：必须覆盖全部 toggle 动作的两套 id，且去重
        auto ids = go2::persistentModeIds();
        std::set<int> uniq(ids.begin(), ids.end());
        CHECK(uniq.size() == ids.size());  // 无重复
        for (const auto& a : sportActions()) {
            if (!a.toggle) continue;
            if (a.normalId != 0)
                CHECK_MSG(std::find(ids.begin(), ids.end(), a.normalId) != ids.end(),
                          std::string("缺 normalId: ") + a.key);
            if (a.mcfId != 0)
                CHECK_MSG(std::find(ids.begin(), ids.end(), a.mcfId) != ids.end(),
                          std::string("缺 mcfId: ") + a.key);
        }
        // 自由行走 / 领航跟随共用 1045 —— 去重后只应出现一次
        CHECK(std::count(ids.begin(), ids.end(), 1045) == 1);
    }

    // ---------------------------------------------------------- 名字反查
    {
        CHECK(go2::labelForApiId(1001) == std::string("阻尼"));
        CHECK(go2::labelForApiId(2043) == std::string("后空翻"));
        CHECK(go2::labelForApiId(go2::kApiMove) == std::string("移动"));  // 1008 特例
        // 这三条已从动作表删除（整组撤下），但代码仍会下发 → 回执日志必须还能翻出名字
        CHECK(go2::labelForApiId(go2::kApiEuler) == std::string("姿态角"));
        CHECK(go2::labelForApiId(go2::kApiBodyHeight) == std::string("机身高度"));
        CHECK(go2::labelForApiId(go2::kApiContinuousGait) == std::string("持续步态"));
        CHECK(go2::labelForApiId(999999).empty());
        CHECK(go2::labelForApiId(0).empty());
    }

    // ---------------------------------------------------------- 参数打包
    {
        // ★ Int / Real / Euler 三种参数：2026-10-11 那组删除后**表里已经没有条目用它们**，
        //   但 buildSportParam 仍然支持（网页 `param` 指令 / 将来重新加滑条动作要用）。
        //   所以这里用**本地构造的合成条目**保住覆盖，免得这几个分支悄悄烂掉。
        const SportAction flag{nullptr, nullptr, 0, 0, SportParam::Flag, go2::SportGroup::Basic,
                               false};
        CHECK(go2::buildSportParam(flag, 0, 0.0f, 0, 0, 0, "").dump() ==
              std::string("{\"data\":true}"));
        const SportAction lvl{nullptr, nullptr, 0, 0, SportParam::Int, go2::SportGroup::Basic,
                              false};
        CHECK(go2::buildSportParam(lvl, 2, 0.0f, 0, 0, 0, "").dump() ==
              std::string("{\"data\":2}"));
        const SportAction bh{nullptr, nullptr, 0, 0, SportParam::Real, go2::SportGroup::Basic,
                             false};
        {
            // 浮点比较走数值：float → double 的十进制展开不是人类写的那一串
            const auto p = go2::buildSportParam(bh, 0, 0.31f, 0, 0, 0, "");
            CHECK(p.contains("data"));
            CHECK(std::fabs(p["data"].get<double>() - 0.31) < 1e-6);
        }
        const SportAction euler{nullptr, nullptr, 0, 0, SportParam::Euler, go2::SportGroup::Basic,
                                false};
        {
            const auto p = go2::buildSportParam(euler, 0, 0.0f, 0.1f, 0.2f, 0.3f, "");
            CHECK(std::fabs(p["x"].get<double>() - 0.1) < 1e-6);
            CHECK(std::fabs(p["y"].get<double>() - 0.2) < 1e-6);
            CHECK(std::fabs(p["z"].get<double>() - 0.3) < 1e-6);
        }
        const SportAction* none = find("Hello");
        CHECK(none != nullptr);
        if (none) CHECK(go2::buildSportParam(*none, 0, 0.0f, 0, 0, 0, "").is_null());
        const SportAction* json = find("TrajectoryFollow");  // Json 参数（Trigger 已随那组删除）
        CHECK(json != nullptr);
        if (json) {
            CHECK(go2::buildSportParam(*json, 0, 0.0f, 0, 0, 0, "{\"a\":1}").dump() ==
                  std::string("{\"a\":1}"));
            // 坏 JSON 不能抛异常（界面每帧都在调它）
            CHECK(go2::buildSportParam(*json, 0, 0.0f, 0, 0, 0, "{坏").is_null());
        }
    }

    return go2test::summary("sport_library_test");
}
