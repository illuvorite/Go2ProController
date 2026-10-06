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
        const SportAction* staticWalk = find("StaticWalk");  // 只有 MCF 有
        CHECK(staticWalk != nullptr);
        if (staticWalk) {
            CHECK(go2::apiIdFor(*staticWalk, true) == 1061);
            CHECK(go2::apiIdFor(*staticWalk, false) == 0);  // 非 MCF 下无此条 → 界面会兜底
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
        CHECK(go2::labelForApiId(999999).empty());
        CHECK(go2::labelForApiId(0).empty());
    }

    // ---------------------------------------------------------- 参数打包
    {
        const SportAction* flag = find("ContinuousGait");
        CHECK(flag != nullptr);
        if (flag) {
            CHECK(go2::buildSportParam(*flag, 0, 0.0f, 0, 0, 0, "").dump() ==
                  std::string("{\"data\":true}"));
        }
        const SportAction* lvl = find("SpeedLevel");
        CHECK(lvl != nullptr);
        if (lvl) {
            CHECK(go2::buildSportParam(*lvl, 2, 0.0f, 0, 0, 0, "").dump() ==
                  std::string("{\"data\":2}"));
        }
        const SportAction* bh = find("BodyHeight");
        CHECK(bh != nullptr);
        if (bh) {
            // 浮点比较走数值：float → double 的十进制展开不是人类写的那一串
            const auto p = go2::buildSportParam(*bh, 0, 0.31f, 0, 0, 0, "");
            CHECK(p.contains("data"));
            CHECK(std::fabs(p["data"].get<double>() - 0.31) < 1e-6);
        }
        const SportAction* euler = find("Euler");
        CHECK(euler != nullptr);
        if (euler) {
            const auto p = go2::buildSportParam(*euler, 0, 0.0f, 0.1f, 0.2f, 0.3f, "");
            CHECK(std::fabs(p["x"].get<double>() - 0.1) < 1e-6);
            CHECK(std::fabs(p["y"].get<double>() - 0.2) < 1e-6);
            CHECK(std::fabs(p["z"].get<double>() - 0.3) < 1e-6);
        }
        const SportAction* none = find("Hello");
        CHECK(none != nullptr);
        if (none) CHECK(go2::buildSportParam(*none, 0, 0.0f, 0, 0, 0, "").is_null());
        const SportAction* json = find("Trigger");
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
