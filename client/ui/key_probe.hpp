#pragma once

// ============================================================================
// 从机器狗内网找钥匙（data2=3 新固件）：扫端口 + Web 服务抓 32 位 hex。
//
// 原理：狗本地必须持有**明文** AES 钥匙（信令服务要用它加密握手 data1），
// 所以只要狗的内网服务暴露，就有机会把钥匙捞出来。
// 思路来自 tools/key_extract.py，但移植进 app —— 平板上不连电脑也能跑。
//
// 结果写在 UiState::keyScan*（keyScanning / keyScanNote / keyCandidates），
// ImGui 设置页与网页端（web_bridge）都从这里读。
//
// 能力边界：SSH(22) / ADB(5555) / NFS(2049) 需要外部工具（sshpass / adb / mount），
// App 里没有 —— 探测到开放时只会在 note 里提示"需要电脑"。
// ============================================================================

#include <string>

namespace go2 {

struct UiState;

/// 后台线程探测（结果写 ui.keyScan*）。ip 为空时内部会回退到受控第一台 / 列表第一台。
void runKeyProbe(class RobotManager& mgr, struct UiState& ui, const std::string& ip);

/// 采用一把候选钥匙：追加 keys.txt + addAesKeys + 绑定到 ip（空 = 不绑定）。
/// key 不是 32 位 hex 时返回 false。**已存在的 key 不会重复写文件**。
bool applyKeyCandidate(class RobotManager& mgr, struct UiState& ui,
                       const std::string& key, const std::string& ip);

// ============================================================================
// 官方 App 钥匙导入（只读官方 App 的数据，绝不修改）
//
// 流程：定位（core/official_key_import 的候选目录 + 安卓 root 分支）
//       → 读取/解析（JSON / Prefs XML / SQLite / LevelDB / 文本）
//       → 自动写入 keys.txt + 加入候选 + 回填设置页输入框
//       → reloadKeysAndRetry() 对失败的狗自动重连。
//
// 结果写在 UiState::officialKey*（note / hits / autoFill / imported），
// ImGui 设置页与网页端都从这里读，不各写一份。
// ============================================================================

/// 后台线程跑一次导入。autoApply=true 时，置信度 ≥55 的候选会直接落盘。
void runOfficialKeyImport(class RobotManager& mgr, struct UiState& ui, bool autoApply);

/// 启动后台线程（正在跑时直接返回，不叠加）。
/// 三个入口（desktop / serve / android）在启动时各调一次；也可以在设置页手动触发。
void startOfficialKeyImport(class RobotManager& mgr, struct UiState& ui, bool autoApply);

// ============================================================================
// 云账号拉钥匙（宇树云 device/bind/list 的 dev.key）
//
// 无 root 平板上真正"自动"的路径：官方 App 的 key 本来就来自云端，
// 这里用同一个接口登录一次 → 拿到账号下所有设备的 key → 自动落盘 + 重连。
//
// ★ 账号密码只在内存里传参，不写文件、不写日志；失败原因写 UiState::cloudNote。
// ★ 会访问宇树官方服务器（robot-api.unitree.com / global-robot-api.unitree.com），
//   按项目纪律由使用者本人触发（设置页点按钮 / 网页发命令）。
// ============================================================================

void runCloudKeyImport(class RobotManager& mgr, struct UiState& ui, std::string account,
                       std::string password, bool cnRegion);

void startCloudKeyImport(class RobotManager& mgr, struct UiState& ui, std::string account,
                         std::string password, bool cnRegion);

/// 启动时的自动导入：先扫官方 App 本地目录，再用**已保存的账号**自动登录拉钥匙。
/// 三个入口调用这一个函数（串行执行，避免两路同时往 keys.txt 写）。
void startAutoKeyImport(class RobotManager& mgr, struct UiState& ui, bool scanOfficial);

}  // namespace go2
