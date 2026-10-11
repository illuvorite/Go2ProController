#pragma once

// ============================================================================
// 「舞蹈编排」—— 一串动作按顺序播放。
//
// 来源：参考实现「天树探界遥控」的 Go2 页（`go2-robot-control.html` 舞蹈编排页）。
// 它那边的编排存在**云端**（arboradmin.saikr.com，按企业 ID 区分），
// 我们这边没有那套云服务，所以改成**存本地文件** `robot_dances.json`：
//   数据模型（名字 / 说明 / 一串 {动作, 延时}）与它一致，播放语义也一致，
//   区别只是"存在哪" —— 离线可用，也不依赖企业账号。
//
// 播放语义（照它的 playDance）：
//   按顺序发每条动作 → 等这条的 delay → 发下一条；期间可以急停中止。
//   `wait` 是伪动作，只等待不发指令。
// ============================================================================

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace go2 {

/// 编排里的一步
struct DanceStep {
    std::string key;      ///< 动作 key（sport_library 的 key）或 "wait"（只等待）
    int delayMs = 1000;   ///< 这一步之后的等待（毫秒）
};

/// 一套编排
struct Dance {
    int id = 0;                    ///< 本地自增 id（0 = 还没落盘）
    std::string name;              ///< 名称
    std::string desc;              ///< 说明
    std::vector<DanceStep> steps;  ///< 动作序列
};

/// 某条动作的默认延时（毫秒）—— 参考实现 getActionDefaultDuration 的口径：
/// 一次性动作给足够时间做完再发下一条，持续性/即时动作给短一点的间隔。
int defaultDanceDelayMs(const std::string& key);

/// Dance <-> JSON（落盘与 /api/state 共用）
nlohmann::json danceToJson(const Dance& d);
Dance danceFromJson(const nlohmann::json& j);

}  // namespace go2
