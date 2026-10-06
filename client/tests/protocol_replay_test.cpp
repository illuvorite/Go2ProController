// ============================================================================
// 协议回放测试（M2-6）：把**固化下来的真实形状报文**喂给纯解析函数，逐字段断言。
//
// 为什么值得单独做：指令回执决定"这条动作到底有没有被固件接受" ——
// 界面的 † 可用性标注、以及"被拒后换另一套 api_id 重试一次"都依赖它。
// 而固件差异（error_code 出现在两个不同位置、status 缺失）在真机上偶发一次，
// 想复现极难；固化成 fixture 之后，任何一次解析行为的变化都会在 ctest 里立刻变红。
//
// 报文来源：docs/go2_webrtc_protocol.md 的回执样例 + `robot_client.cpp` 实际兼容的
//          两种 error_code 位置（固件 A/B）。fixture 在 client/tests/fixtures/。
//
// 说明：本轮覆盖的是**回执**这条路径。信令侧的 con_notify / offer·answer 解析
// （base64/AES-GCM/RSA/SDP 裁剪）已由 crypto_test 用真实抓包覆盖，不重复。
// ============================================================================

#include "protocol.hpp"

#include "sport_library.hpp"
#include "test_util.hpp"

#include <fstream>
#include <string>

#ifndef GO2_FIXTURE_DIR
#error "需要 -DGO2_FIXTURE_DIR=<client/tests/fixtures 的绝对路径>"
#endif

namespace {

/// 读一个 fixture；文件不存在直接算失败（而不是静默跳过 —— 那样测试会"假绿"）
bool loadFixture(const std::string& name, nlohmann::json* out, std::string* raw) {
    const std::string path = std::string(GO2_FIXTURE_DIR) + "/" + name;
    std::ifstream f(path);
    if (!f) {
        CHECK_MSG(false, "fixture 打不开: " + path);
        return false;
    }
    *raw = std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    try {
        *out = nlohmann::json::parse(*raw);
    } catch (...) {
        *out = nlohmann::json();  // 故意保留"解析失败"这一种情况（res_not_object.json）
    }
    return true;
}

using go2::protocol::Response;

}  // namespace

int main() {
    nlohmann::json j;
    std::string raw;

    // ---------------------------------------------------------------- 成功回执
    if (loadFixture("res_success.json", &j, &raw)) {
        const Response r = go2::protocol::parseResponse(j);
        CHECK(r.parsed);
        CHECK(r.code == 0);
        CHECK(r.apiId == 1003);
        CHECK(r.isSport);
        CHECK(r.errorCode == -1);
        CHECK(r.topic == std::string("rt/api/sport/response"));
        CHECK_MSG(!r.actionName.empty(), "1003 应能翻出动作名（平衡站立）");
    }

    // ---------------------------------------------------------------- 被拒：3202
    if (loadFixture("res_rejected_3202.json", &j, &raw)) {
        const Response r = go2::protocol::parseResponse(j);
        CHECK(r.parsed);
        CHECK(r.code == 3202);
        CHECK(r.apiId == 1008);
        CHECK_MSG(r.isSport, "sport topic → 要参与「换另一套 api_id 重试」");
    }

    // ---------------------------------------------------------------- 被拒：3203
    if (loadFixture("res_rejected_3203.json", &j, &raw)) {
        const Response r = go2::protocol::parseResponse(j);
        CHECK(r.parsed);
        CHECK(r.code == 3203);
        CHECK(r.apiId == 1044);
        CHECK_MSG(r.actionName == std::string("后空翻"), "1044 是普通指令集的后空翻");
    }

    // ---------------------------------------------------------------- error_code 两处位置
    if (loadFixture("res_errorcode_in_status.json", &j, &raw)) {
        const Response r = go2::protocol::parseResponse(j);
        CHECK(r.parsed);
        CHECK_MSG(r.errorCode == 1001, "固件 A：error_code 在 status 里");
    }
    if (loadFixture("res_errorcode_in_data.json", &j, &raw)) {
        const Response r = go2::protocol::parseResponse(j);
        CHECK(r.parsed);
        CHECK_MSG(r.errorCode == 2002, "固件 B：error_code 退化到 data 层，也必须取到");
    }

    // ---------------------------------------------------------------- 结构异常
    if (loadFixture("res_missing_status.json", &j, &raw)) {
        const Response r = go2::protocol::parseResponse(j);
        CHECK_MSG(!r.parsed, "没有 status.code → parsed=false（调用方会打印原始报文）");
        CHECK(r.code == -1);
        CHECK(r.apiId == 1003);  // identity 还是能取到
    }
    if (loadFixture("res_not_object.json", &j, &raw)) {
        // 合法的 JSON、但**不是对象**（固件发了别的东西）→ parseResponse 必须原样返回默认值
        CHECK_MSG(!j.is_object(), "该 fixture 是合法 JSON 但不是对象");
        const Response r = go2::protocol::parseResponse(j);
        CHECK(!r.parsed);
        CHECK(r.code == -1);
        CHECK(r.topic.empty());
    }
    if (loadFixture("res_malformed.json", &j, &raw)) {
        // 报文被截断 → JSON 解析失败（loadFixture 置 null）→ parseResponse 仍要安全返回
        CHECK_MSG(j.is_null(), "畸形报文应解析失败");
        const Response r = go2::protocol::parseResponse(j);
        CHECK(!r.parsed);
    }

    // ---------------------------------------------------------------- 非运动服务
    if (loadFixture("res_not_sport.json", &j, &raw)) {
        const Response r = go2::protocol::parseResponse(j);
        CHECK(r.parsed);
        CHECK_MSG(!r.isSport, "audiohub 的回执不算运动指令 → 不参与重试");
        CHECK(r.actionName.empty());
    }

    // ---------------------------------------------------------------- api_id 翻译
    if (loadFixture("res_mcf_backflip.json", &j, &raw)) {
        const Response r = go2::protocol::parseResponse(j);
        CHECK(r.apiId == 2043);
        CHECK_MSG(r.actionName == std::string("后空翻"), "2043 是 MCF 指令集的后空翻");
    }
    if (loadFixture("res_unknown_api.json", &j, &raw)) {
        const Response r = go2::protocol::parseResponse(j);
        CHECK(r.parsed);
        CHECK(r.apiId == 9999);
        CHECK_MSG(r.actionName.empty(), "未知 api_id → 没有动作名（日志回退成 topic）");
    }

    // ---------------------------------------------------------------- topic 判定
    CHECK(go2::protocol::isSportTopic("rt/api/sport/response"));
    CHECK(go2::protocol::isSportTopic("rt/api/sport/request"));
    CHECK(!go2::protocol::isSportTopic("rt/api/audiohub/response"));
    CHECK(!go2::protocol::isSportTopic(""));
    CHECK(!go2::protocol::isSportTopic("rt/lf/lowstate"));

    // ---------------------------------------------------------------- 与指令表一致
    // 回放里用到的 api_id 必须真能在指令表里查到（防止 fixture 与表脱节）
    CHECK(!go2::labelForApiId(1003).empty());
    CHECK(!go2::labelForApiId(1044).empty());
    CHECK(!go2::labelForApiId(2043).empty());

    return go2test::summary("protocol_replay_test");
}
