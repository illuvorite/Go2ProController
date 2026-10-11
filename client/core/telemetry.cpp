#include "telemetry.hpp"

#include <cmath>

namespace go2 {
namespace {

/// 取 `d` 下面第一层字段，类型不对/不存在都返回 false。
bool num1(const nlohmann::json& d, const char* key, float* out) {
    if (!d.is_object()) return false;
    auto it = d.find(key);
    if (it == d.end() || !it->is_number()) return false;
    const double v = it->get<double>();
    if (!std::isfinite(v)) return false;
    *out = static_cast<float>(v);
    return true;
}

/// 取 `d[a][b]`。
bool num2(const nlohmann::json& d, const char* a, const char* b, float* out) {
    if (!d.is_object()) return false;
    auto it = d.find(a);
    if (it == d.end() || !it->is_object()) return false;
    return num1(*it, b, out);
}

/// 取嵌套数组 `d[a][b][idx]`（用于 imu.rpy[2] 这类）。
bool numArr(const nlohmann::json& d, const char* a, const char* b, size_t idx, float* out) {
    if (!d.is_object()) return false;
    auto it = d.find(a);
    if (it == d.end() || !it->is_object()) return false;
    auto arr = it->find(b);
    if (arr == it->end() || !arr->is_array() || arr->size() <= idx) return false;
    const auto& e = (*arr)[idx];
    if (!e.is_number()) return false;
    const double v = e.get<double>();
    if (!std::isfinite(v)) return false;
    *out = static_cast<float>(v);
    return true;
}

/// 取字符串字段（类型不符返回 false）。
bool str1(const nlohmann::json& d, const char* key, std::string* out) {
    if (!d.is_object()) return false;
    auto it = d.find(key);
    if (it == d.end() || !it->is_string()) return false;
    *out = it->get<std::string>();
    return true;
}

/// 依次尝试多个候选路径取温度（不同固件放的位置不一样）
bool firstTemp(const nlohmann::json& d, float* out) {
    return num2(d, "bms_state", "mcu_ntc", out) ||   // 天树探界的首选
           num2(d, "bms_state", "bq_ntc", out) ||    // 其次
           num1(d, "temperature_ntc1", out) ||       // 老固件的平铺字段
           num1(d, "mcu_ntc", out) || num1(d, "bq_ntc", out) || num1(d, "ntc1", out);
}

}  // namespace

Telemetry parseTelemetry(const std::string& topic, const nlohmann::json& msg) {
    Telemetry t;
    try {
        if (!msg.is_object()) return t;

        // 报文形状：{"type":"msg","topic":"rt/lf/lowstate","data":{...}}
        // 但有些固件会再多包一层 data.data（参考实现就是 `message.data?.data || message.data`）
        const nlohmann::json* dp = nullptr;
        auto it = msg.find("data");
        if (it != msg.end() && it->is_object()) {
            dp = &*it;
            auto inner = it->find("data");
            if (inner != it->end() && inner->is_object()) dp = &*inner;
        } else {
            dp = &msg;  // 直接把裸状态帧传进来也能用（离线回放/测试）
        }
        const nlohmann::json& d = *dp;

        const bool isLow = topic.find("lowstate") != std::string::npos;
        const bool isSport = topic.find("sportmodestate") != std::string::npos;

        // ---- 电量：lowstate.bms_state.soc 或 sportmodestate.battery_level ----
        float v = 0.0f;
        if (num2(d, "bms_state", "soc", &v) || num1(d, "battery_level", &v)) {
            t.hasSoc = true;
            t.soc = v;
        }

        // ---- 温度：只在 lowstate 里有 ----
        if (isLow && firstTemp(d, &v)) {
            t.hasTemp = true;
            t.tempC = v;
        }

        // ---- 姿态角：imu.rpy / imu_state.rpy（低层状态）----
        if (numArr(d, "imu", "rpy", 2, &v) || numArr(d, "imu_state", "rpy", 2, &v)) {
            t.hasYaw = true;
            t.yaw = v;
        }
        // 角速度：yaw 的角速度（imu.gyroscope[2]）
        if (numArr(d, "imu", "gyroscope", 2, &v) || numArr(d, "imu_state", "gyroscope", 2, &v)) {
            t.hasYawRate = true;
            t.yawRate = v;
        }

        // ---- 运动模式名：sportmodestate.mode_name ----
        if (isSport) {
            std::string s;
            if (str1(d, "mode_name", &s)) t.mode = s;
            if (str1(d, "progress", &s)) t.progress = s;
        }
    } catch (...) {
        // 单帧坏数据绝不能影响整条链路
    }
    return t;
}

}  // namespace go2
