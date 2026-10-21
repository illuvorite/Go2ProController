#pragma once

// ============================================================================
// 从「官方 App 的本地存储」导入每设备 AES-128 钥匙（data2=3 固件用）
//
// 背景：固件 ≥1.1.15 的 Go2 在局域网信令里返回 data2=3，握手里的 RSA 公钥被
// 一把**每设备**的 AES-128-GCM key 包着。官方 App 绑定机器狗时会从云端
// `v1/device/bind/list` 拿到 `dev.key`（32 位 hex）并缓存到本机 —— 于是同一台
// 手机/平板上，只要官方 App 连过一次，这把钥匙就已经在本地了。
//
// 本模块只做三件事：
//   1. **定位**：给出平台相关的候选目录（含用户显式指定的目录）；
//   2. **读取 + 解析**：容忍 JSON / SharedPreferences XML / SQLite / LevelDB /
//      纯文本日志等载体，扫出 32 位 hex 候选，并按上下文给置信度打分；
//   3. **报告**：返回候选列表 + 每一步的说明（没找到时告诉用户为什么）。
//
// ★ 只读。绝不写入、移动、删除官方 App 的任何文件；也不把读到的内容复制到
//   工程目录。落盘只发生在调用方（写本应用的 keys.txt），见 ui/key_probe.cpp。
//
// ★ 解析不出钥匙不是错误：官方 App 可能用 SQLCipher / MMKV 加密存储。这时
//   报告里会写明「为什么没找到」，由界面引导用户走「云端取钥匙」或
//   「从机器狗内网抓」。**不猜、不硬编码、不伪造**。
//
// ★ 候选的可信度只是排序依据，**不是承诺**。最终判定仍然是现有的握手逻辑：
//   逐个尝试，GCM 认证标签通过的那把才是这台狗的钥匙（见 signaling.cpp）。
//   这条设计让误报的代价只有"多试一把"，不会连错。
// ============================================================================

#include <cstddef>
#include <string>
#include <vector>

namespace go2 {

/// 一条从官方 App 本地存储里读到的候选钥匙
struct OfficialKeyHit {
    std::string key;         ///< 小写 32 位 hex（AES-128）
    std::string sn;          ///< 同一条记录旁边的 SN（可能为空 —— 空不影响使用）
    std::string source;      ///< 来源文件的完整路径（仅供界面显示）
    std::string format;      ///< json / prefs-xml / sqlite / leveldb / text
    int score = 0;           ///< 0..100，越高越可能是真正的钥匙（仅用于排序）
};

struct KeyImportOptions {
    /// 额外根目录（环境变量 GO2_OFFICIAL_APP_DIR / 用户手选目录 / adb 导出目录）
    std::vector<std::string> extraRoots;
    /// 只扫 extraRoots（单测与"只导这个目录"的场景用；默认还会扫平台默认目录）
    bool onlyExtraRoots = false;
    /// 尝试 /data/data/<官方包名>（安卓上需要 root，无权限时只记一条说明）
    bool includeAndroidPrivate = true;
    /// 尝试 /sdcard/Android/data/<官方包名> 等共享存储位置
    bool includeSharedStorage = true;
    std::size_t maxFiles = 600;                 ///< 单次扫描文件上限（防卡死）
    std::size_t maxFileBytes = 4u * 1024 * 1024;  ///< 单文件上限，超过直接跳过
    int maxDepth = 4;                            ///< 目录递归深度
};

struct KeyImportReport {
    std::vector<OfficialKeyHit> hits;       ///< 已去重，按 score 降序
    std::vector<std::string> rootsProbed;   ///< 实际存在并探测过的根目录
    std::vector<std::string> notes;         ///< 给用户看的说明（含失败原因）
    std::size_t filesScanned = 0;
    bool anyRootExists = false;  ///< 是否找到任何官方 App 数据目录
    bool permissionDenied = false;  ///< 是否遇到过 EACCES（安卓未 root 的典型症状）
};

/// 平台相关的候选根目录（按优先级；不保证存在）。
///
/// 顺序：GO2_OFFICIAL_APP_DIR → 本应用约定的导入目录 → 官方 App 的私有/共享目录。
std::vector<std::string> officialAppRootCandidates();

/// 只读扫描。永不抛异常；任何 IO 失败都转成 notes 里的一句话。
KeyImportReport scanOfficialAppKeys(const KeyImportOptions& opt = KeyImportOptions{});

/// 解析一段文件内容（暴露给单测）。
/// @param path 仅用于填 source / 推断 format（不看文件本身）
/// @param blob 文件内容（二进制安全）
std::vector<OfficialKeyHit> parseOfficialKeyBlob(const std::string& path,
                                                 const std::string& blob);

/// 掩码：abcdef...1234 → "abcd…1234"（界面与日志只显示掩码）
std::string maskKey(const std::string& key);

}  // namespace go2
