#include "protocol.hpp"

#include "sport_library.hpp"  // labelForApiId（把 api_id 翻成动作名）

namespace go2 {
namespace protocol {

bool isSportTopic(const std::string& topic) {
    return topic.find("/sport/") != std::string::npos;
}

Response parseResponse(const nlohmann::json& msg) {
    Response r;
    // 契约：任何异常输入都不外泄异常（调用方在网络线程里，抛出去等于崩界面）。
    // 固件理论上只发对象，但真机上报过非对象的情况，解析必须自己兜住。
    if (!msg.is_object()) return r;

    // 不用 msg.value("topic", "")：topic 存在但不是字符串时它会抛 type_error
    const auto it = msg.find("topic");
    if (it != msg.end() && it->is_string()) r.topic = it->get<std::string>();
    r.isSport = isSportTopic(r.topic);

    // status.code 是"这条回执到底成不成"的唯一判据；取不到就认为结构不符
    try {
        r.code = msg.at("data").at("header").at("status").at("code").get<int>();
        r.parsed = true;
    } catch (...) {
    }

    try {
        r.apiId = msg.at("data").at("header").at("identity").at("api_id").get<int>();
    } catch (...) {
    }

    // error_code 的位置随固件版本不同 → 两处都试（先标准位置，再退化位置）
    if (r.parsed) {
        try {
            r.errorCode = msg.at("data").at("header").at("status").at("error_code").get<int>();
        } catch (...) {
            try {
                r.errorCode = msg.at("data").at("error_code").get<int>();
            } catch (...) {
            }
        }
    }

    if (r.isSport) r.actionName = labelForApiId(r.apiId);
    return r;
}

}  // namespace protocol
}  // namespace go2
