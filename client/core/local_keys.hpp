#pragma once

#include <string>
#include <vector>

namespace go2 {

/// 本地钥匙加载结果
struct LocalKeys {
    std::vector<std::string> keys;  // 32 位 hex 的 AES-128 key（可能多把，连接时逐个试）
    std::string source;             // 来源描述（文件路径 / 环境变量）
};

/// 加载本地候选 AES key（供 data2=3 新固件机器狗使用）。
///
/// 来源优先级：
///   1. 环境变量 GO2_KEYS_FILE（显式指定文件路径）
///   2. 环境变量 GO2_AES_KEYS（逗号/分号/空格分隔的 32 位 hex）
///   3. C:\Users\<用户>\.go2\keys.json（Windows 侧，项目外的主存放位置）
///   4. ~/.go2_remote_keys.json（WSL 家目录，备用）
///
/// ⚠️ 刻意**不搜索项目目录**：钥匙绝不放进工程，避免随代码分享/提交外泄。
///
/// 文件格式支持四种（值只要够 32 位 hex 就会被收集）：
///   {"devices": [{"sn":"...","key":"dd15e0...", "note":"..."}, ...]}   // 标准格式
///   {"keys": ["dd15e0...", ...]}
///   ["dd15e0...", "aabbcc..."]
///   {"192.168.0.169": "dd15e0...", "备注": "..."}
///
/// 设计说明：钥匙与 IP、SN 都无关——连接时逐个尝试，GCM 校验通过的那个
/// 就是这台机器的钥匙。所以换 WiFi / 换网段 / 多台狗混用都不需要重新配置。
LocalKeys loadLocalAesKeys();

// ============================================================================
// 敏感文件的落盘位置
//
// 规矩：**钥匙 / 钥匙缓存绝不允许落在源码工作树里** —— 一旦随手打包、复制工程目录、
// 或者 `git add -f`，密钥就跟着出去了。所以统一放到"应用数据目录"：
//   · 安卓等显式部署：跟 `GO2_KEYS_FILE` **同一个目录**（那是应用私有目录，可写）；
//   · 桌面：`$HOME/.go2`（Linux/macOS）或 `%USERPROFILE%\.go2`（Windows）。
// ============================================================================

/// 该应用的数据目录（不存在就靠 `ensureParentDir` 建）
std::string userDataDir();

/// 本地钥匙库文件（每行一个 32 位 hex）：`userDataDir()/keys.txt`
std::string defaultKeysTxtPath();

/// 「IP → 每设备 AES-128 key」缓存：`userDataDir()/keys_cache.json`
std::string defaultKeyCachePath();

/// 确保 `path` 的父目录存在（已存在或建立失败都静默返回 —— 调用方按写入结果判断）
void ensureParentDir(const std::string& path);

}  // namespace go2
