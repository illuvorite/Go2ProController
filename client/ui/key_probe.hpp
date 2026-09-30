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
/// key 不是 32 位 hex 时返回 false。
bool applyKeyCandidate(class RobotManager& mgr, struct UiState& ui,
                       const std::string& key, const std::string& ip);

}  // namespace go2
