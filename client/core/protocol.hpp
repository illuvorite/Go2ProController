#pragma once

// ============================================================================
// 协议报文的**纯解析**（M2-6 回放测试的落点）。
//
// 为什么要把这段从 RobotClient 里抽出来：
//   指令回执（`{"type":"res",...}`）决定"这条动作到底有没有被固件接受" ——
//   界面上动作瓷砖的 † 标注、以及"被拒后换另一套 api_id 重试"都依赖它。
//   而它的解析必须容忍**固件差异**（error_code 会出现在两个不同位置、字段缺失），
//   这正是最需要离线回放的一段：真机上偶发一次拒收很难复现，
//   但把报文固化成 fixture 之后，任何一次解析行为的变化都能在 ctest 里被抓住。
//
// 约束：本文件的函数必须是**纯函数**（不碰网络/线程/全局状态），
// 否则就没法当回放基座用。
// ============================================================================

#include <nlohmann/json.hpp>

#include <string>

namespace go2 {
namespace protocol {

/// 一条指令回执的解析结果
struct Response {
    bool parsed = false;   ///< 是否取到了 `data.header.status.code`（结构符合预期）
    int code = -1;         ///< status.code：0 = 成功；非 0 = 被拒（见 RobotClient::rejectReason）
    int apiId = 0;         ///< `data.header.identity.api_id`（0 = 报文没带）
    int errorCode = -1;    ///< 先试 `status.error_code`，再试 `data.error_code`（-1 = 两处都没有）
    std::string topic;     ///< 顶层 topic
    bool isSport = false;  ///< topic 里含 "/sport/"（只有运动类才做动作名/重试处理）
    std::string actionName;  ///< isSport 时由 api_id 翻出的动作名（空 = 没有对应名字）
};

/// 解析回执。**不抛异常**：字段缺失或类型不符时返回 `parsed == false` 的默认值。
Response parseResponse(const nlohmann::json& msg);

/// topic 是否属于运动服务（`rt/api/sport/...`）
bool isSportTopic(const std::string& topic);

}  // namespace protocol
}  // namespace go2
