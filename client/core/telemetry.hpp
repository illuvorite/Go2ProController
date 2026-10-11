#pragma once

// ============================================================================
// 机器人**状态话题**的解析（rt/lf/lowstate、rt/lf/sportmodestate）。
//
// 为什么单独抽出来：
//   桌面端 / 服务端 / 安卓端三个入口各写了一遍几乎一样的解析 lambda，
//   新增一个字段（温度、姿态角）就要改三处 —— 改漏一处就变成"某个界面看不到"。
//   更麻烦的是里面全是 `d["a"]["b"]` 这种会抛的写法，而它们跑在**网络回调线程**上，
//   一个坏帧就是整个进程挂掉（robot_client.cpp 的 handleMessage 已经为同类问题
//   做过异常兜底，这里从源头上就不抛）。
//
// 约束：本文件的函数必须是**纯函数**（不碰网络/线程/全局状态），便于离线回放测试。
//
// 字段名来源：参考实现「天树探界遥控」go2-robot-plugin.js 的 lowstate 解析
//   （bms_state.soc / imu.rpy / 温度），以及本机实测抓到的 rt/lf/lowstate 报文。
//   不同固件字段位置不一样，所以每项都按"多个候选路径依次试"的方式取。
// ============================================================================

#include <nlohmann/json.hpp>

#include <string>

namespace go2 {

/// 一帧状态话题里我们关心的全部信息。字段缺失时对应的 has* 为 false。
struct Telemetry {
    bool hasSoc = false;      ///< 有电量
    float soc = -1.0f;        ///< 电量百分比（0..100）
    bool hasTemp = false;     ///< 有温度
    float tempC = 0.0f;       ///< 温度（摄氏度）
    bool hasYaw = false;      ///< 有姿态角
    float yaw = 0.0f;         ///< 偏航角 yaw（弧度，"方向回正"用它判断是否已摆正）
    bool hasYawRate = false;  ///< 有偏航角速度
    float yawRate = 0.0f;     ///< 偏航角速度（rad/s）
    std::string mode;         ///< 运动模式名（sportmodestate.mode_name）
    std::string progress;     ///< sportmodestate.progress（部分固件有，用于判断动作是否在跑）
};

/// 从一帧状态话题解析出上述信息。**不抛异常**：字段缺失/类型不符就跳过该项。
/// @param topic 话题名（用它区分 lowstate / sportmodestate）
/// @param msg   整条报文 `{"type":"msg","topic":...,"data":{...}}`
Telemetry parseTelemetry(const std::string& topic, const nlohmann::json& msg);

}  // namespace go2
