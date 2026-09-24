#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace go2 {

/// 参数类型（决定界面上给什么输入控件、以及 parameter 怎么打包）
enum class SportParam {
    None,    ///< 无参数（Hello / Dance1 ...）
    Flag,    ///< {"data": true}（Pose / FrontFlip / SwitchJoystick ...）
    Int,     ///< {"data": <int>}（SpeedLevel / SwitchGait ...）
    Real,    ///< {"data": <float>}（BodyHeight / FootRaiseHeight ...）
    Euler,   ///< {"x":roll,"y":pitch,"z":yaw}
    Move,    ///< {"x":vx,"y":vy,"z":vyaw}
    Json,    ///< 用户直接输入 JSON
};

/// 分类（界面按此分组）
enum class SportGroup {
    Basic,     ///< 基础姿态
    Gait,      ///< 步态 / 速度 / 身高（带参数）
    Show,      ///< 表演动作
    Stunt,     ///< 跳跃特技（危险）
    Query,     ///< 状态查询
    Advanced,  ///< 其他 / 进阶
};

/// 一条宇树运动指令
struct SportAction {
    const char* key;      ///< 英文标识（用于界面 ID）
    const char* label;    ///< 中文显示名
    int normalId;         ///< normal 指令集 api_id（0 = 该模式无此指令）
    int mcfId;            ///< MCF 指令集 api_id（0 = 与 normalId 相同）
    SportParam param;
    SportGroup group;
    bool risky;           ///< 危险动作（跳跃/翻滚），界面标红
    /// 「持续模式」开关（自由行走 / 领航跟随 / 交叉步 / 经济步态 …）：
    /// 这类指令是 on/off 语义（`{"data": true|false}`），**会一直生效**，
    /// 且 **StopMove(1003) 停不掉它们** —— 必须用同一个 api_id 带 `{"data": false}` 关闭。
    /// 界面把它们画成开关按钮，急停时逐个关闭（见 persistentModeIds）。
    bool toggle = false;
};

/// 全部指令表（源：参考实现 constants.py 的 SPORT_CMD / SPORT_CMD_MCF，
/// 以及 unitree_sdk2 的 SportClient）
const std::vector<SportAction>& sportActions();

/// 按指令集解析出实际 api_id；mode 为 true 表示 MCF 固件
int apiIdFor(const SportAction& a, bool mcfMode);

/// 该指令在 MCF 下是否需要切换指令集（用于界面提示）
bool differsInMcf(const SportAction& a);

/// 该 api_id 对应的动作中文名（未知返回空串）。用于把回执翻译成「哪个动作 + 结果」
std::string labelForApiId(int apiId);

/// 同一动作在「另一套指令集」里的 api_id（没有则返回空表）。
/// 用途：某条指令被机器人拒绝时，自动改用另一套 id 重试
/// （normal 与 MCF 固件的运动服务只认自己那套 api_id，是"很多动作用不了"的主因）
std::vector<int> alternateApiIds(int apiId);

/// 全部「持续模式」的 api_id（两套指令集都含）。
/// 急停时必须逐个用 `{"data": false}` 关掉：StopMove 只停速度，
/// 停不掉"自由行走 / 领航跟随 / 交叉步 / 经济步态"这类一直生效的开关。
std::vector<int> persistentModeIds();

/// 包装参数：
///   Flag -> {"data": true}
///   Int  -> {"data": n}
///   Real -> {"data": v}
///   Euler-> {"x":roll,"y":pitch,"z":yaw}
///   Move -> {"x":vx,"y":vy,"z":vyaw}
///   None -> null（发送时为空字符串）
///   Json -> 解析 rawJson（失败则 null）
nlohmann::json buildSportParam(const SportAction& a, int intVal, float realVal,
                               float x, float y, float z, const std::string& rawJson);

}  // namespace go2
