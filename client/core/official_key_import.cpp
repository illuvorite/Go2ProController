#include "official_key_import.hpp"

#include "local_keys.hpp"  // userDataDir()：本应用数据目录（import 子目录）

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <system_error>

namespace fs = std::filesystem;

namespace go2 {
namespace {

// ---------------------------------------------------------------- 基础判定

bool isHexChar(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

std::string lowerAscii(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool isHex32(const std::string& s) {
    if (s.size() != 32) return false;
    return std::all_of(s.begin(), s.end(), isHexChar);
}

/// 像"钥匙"吗？排除明显的占位符 / 测试向量 / 重复串 —— 它们会让候选列表变成噪声。
///
/// 判据（与 tools/key_extract.py 的经验一致，但更严一点）：
///   · 至少 5 个不同字符（挡掉 000…0 / ffff…f / aaaa…a）
///   · 不是 0123456789abcdef 的重复（挡掉标准测试向量）
///   · 不是 32 位递增/递减序列
bool plausibleKey(const std::string& k) {
    if (!isHex32(k) || k.size() != 32) return false;
    const std::string low = lowerAscii(k);
    if (std::set<char>(low.begin(), low.end()).size() < 5) return false;
    if (low == "0123456789abcdef0123456789abcdef") return false;
    if (low == "fedcba9876543210fedcba9876543210") return false;
    bool inc = true, dec = true;
    for (size_t i = 1; i < low.size(); ++i) {
        const int prev = std::isdigit(static_cast<unsigned char>(low[i - 1]))
                             ? low[i - 1] - '0'
                             : low[i - 1] - 'a' + 10;
        const int cur = std::isdigit(static_cast<unsigned char>(low[i]))
                            ? low[i] - '0'
                            : low[i] - 'a' + 10;
        if ((prev + 1) % 16 != cur) inc = false;
        if ((prev + 15) % 16 != cur) dec = false;
    }
    if (inc || dec) return false;
    return true;
}

/// 词边界、忽略大小写地找关键词（避免 "monkey" 里的 key 也加分）
bool hasWordCI(const std::string& hay, const char* word) {
    const std::string w = lowerAscii(word);
    const std::string h = lowerAscii(hay);
    if (w.empty()) return false;
    size_t p = 0;
    while ((p = h.find(w, p)) != std::string::npos) {
        const bool leftOk =
            p == 0 || !std::isalnum(static_cast<unsigned char>(h[p - 1]));
        const size_t end = p + w.size();
        const bool rightOk =
            end >= h.size() || !std::isalnum(static_cast<unsigned char>(h[end]));
        if (leftOk && rightOk) return true;
        p = end;
    }
    return false;
}

/// 把家目录缩成 ~（显示用，避免在截图里带出用户名）
std::string abbreviatePath(std::string p) {
#ifdef _WIN32
    if (const char* up = std::getenv("USERPROFILE")) {
        const std::string home(up);
        if (!home.empty() && p.rfind(home, 0) == 0) p = "~" + p.substr(home.size());
    }
#else
    if (const char* h = std::getenv("HOME")) {
        const std::string home(h);
        if (!home.empty() && p.rfind(home, 0) == 0) p = "~" + p.substr(home.size());
    }
#endif
    return p;
}

/// path → UTF-8 字符串。
/// Windows 上 `path::string()` 按系统 locale 转（中文用户名会乱码），必须走 u8string。
std::string toUtf8(const fs::path& p) {
#if defined(__cpp_lib_char8_t)
    const auto u8 = p.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
#else
    return p.u8string();
#endif
}

/// 纯字符串取文件名（不构造 fs::path —— 避免 Windows 上的 ANSI/UTF-8 转换歧义）
std::string fileNameOf(const std::string& p) {
    const size_t slash = p.find_last_of("/\\");
    return slash == std::string::npos ? p : p.substr(slash + 1);
}

/// 文件名扩展名（小写，含点）。无扩展名返回空。
std::string extOf(const std::string& path) {
    const std::string name = fileNameOf(path);
    const size_t dot = name.find_last_of('.');
    if (dot == std::string::npos || dot == 0) return {};
    return lowerAscii(name.substr(dot));
}

/// 值得读的文件类型。白名单而非黑名单 —— 官方 App 目录里视频/贴图/so 很多，
/// 逐个判断"不是二进制"既慢又容易漏。
bool worthReading(const std::string& path) {    static const std::set<std::string> kExt = {
        ".json", ".xml",  ".txt",  ".log",   ".db",   ".sqlite", ".sqlite3",
        ".db3",  ".ldb",  ".sst",  ".prefs", ".conf", ".cfg",    ".dat",
        ".bin",  ".ini",  ".properties", ".plist", ".yaml", ".yml"};
    const std::string e = extOf(path);
    if (e.empty()) {
        // LevelDB 的 CURRENT / MANIFEST-000001 没有扩展名，但内容里可能有 key
        const std::string name = lowerAscii(fileNameOf(path));
        return name == "current" || name.rfind("manifest-", 0) == 0;
    }
    return kExt.count(e) > 0;
}

/// 本应用自己的钥匙库 —— 不能当作"官方 App 来源"扫进去。
/// 理由：① 来源显示会误导（明明是自己的，却写成"从官方 App 导入"）；
///       ② 它们启动时已经加载过，重扫只会挤掉真正的新候选（autoFill 填错那把）。
bool isOwnKeyStore(const std::string& path) {
    const std::string name = lowerAscii(fileNameOf(path));
    return name == "keys.json" || name == "keys.txt" || name == "keys_cache.json" ||
           name == "go2_keys_cache.json";
}

// ---------------------------------------------------------------- 内容归一化

/// 检测 UTF-16LE 并转成 UTF-8（SQLite 的 TEXT 默认 UTF-8，但 Windows 侧工具
/// 导出的库可能是 UTF-16）。只看前 4KB，避免为罕见情况付全场代价。
std::string normalizeIfUtf16(std::string blob) {
    const size_t probe = std::min<size_t>(blob.size(), 4096);
    if (probe < 32) return blob;
    size_t zerosAtOdd = 0, pairs = 0;
    for (size_t i = 1; i < probe; i += 2, ++pairs)
        if (blob[i] == '\0') ++zerosAtOdd;
    if (pairs == 0 || zerosAtOdd * 10 < pairs * 8) return blob;  // <80% → 不是 UTF-16LE

    std::string out;
    out.reserve(blob.size() / 2);
    for (size_t i = 0; i + 1 < blob.size(); i += 2) {
        const unsigned char lo = static_cast<unsigned char>(blob[i]);
        const unsigned char hi = static_cast<unsigned char>(blob[i + 1]);
        if (hi == 0 && lo < 0x80) out.push_back(static_cast<char>(lo));
        else if (lo == 0 && hi < 0x80) out.push_back(static_cast<char>(hi));
        else out.push_back('?');  // 非 ASCII 字符对钥匙无意义，压成占位符
    }
    return out;
}

// ---------------------------------------------------------------- 扫描

struct RawHit {
    std::string key;
    size_t pos = 0;
};

/// 扫出所有「正好 32 个连续 hex、两侧不是 hex」的串。
///
/// 两侧边界的理由：SN / MD5 / 长 token 里可能恰好含 32 个 hex 字符，
/// 不做边界判断就会截出假钥匙（tools/key_extract.py 也踩过这个坑）。
std::vector<RawHit> scanHex32(const std::string& t) {
    std::vector<RawHit> out;
    size_t i = 0;
    while (i < t.size()) {
        if (!isHexChar(t[i])) {
            ++i;
            continue;
        }
        size_t run = 0;
        while (i + run < t.size() && isHexChar(t[i + run])) ++run;
        if (run == 32) {
            const bool leftOk = (i == 0) || !isHexChar(t[i - 1]);
            const bool rightOk = (i + 32 >= t.size()) || !isHexChar(t[i + 32]);
            if (leftOk && rightOk) {
                std::string k = lowerAscii(t.substr(i, 32));
                if (plausibleKey(k)) out.push_back({std::move(k), i});
            }
        }
        i += run;
    }
    return out;
}

/// 命中点附近 320 字节的上下文 —— 官方 App 的字段名 / 注释就在钥匙旁边。
std::string contextOf(const std::string& t, size_t pos) {
    const size_t begin = pos > 320 ? pos - 320 : 0;
    const size_t end = std::min(t.size(), pos + 352);
    return t.substr(begin, end - begin);
}

/// 命中点紧邻的前缀（48 字节）—— 用来识别"这个 32hex 是别的字段"。
///
/// 为什么必须做：官方 App 的日志里每个 HTTP 请求都带 `"requestId":"<32hex>"`
/// （UUID 去横线），实测一天几千条，全是合法 32hex、随机分布 —— 光靠"32 位 hex"
/// 会导入一堆垃圾。这类字段的值**紧挨着**字段名，所以只看命中点前 48 字节即可，
/// 不必把整段上下文都判成噪声（否则同一条 JSON 里的 token 会误杀真 key）。
std::string prefixOf(const std::string& ctx, size_t pos, size_t n = 48) {
    const size_t begin = pos > n ? pos - n : 0;
    return ctx.substr(begin, pos - begin);
}

/// 这些字段的 32hex 值不是钥匙，直接丢弃（不是降分 —— 它们量大且完全合法）
bool isNoiseField(const std::string& prefix) {
    static const char* kNoise[] = {"requestid", "traceid", "spanid", "nonce",
                                   "uuid",      "sessionid", "msgid", "imgsign"};
    for (const char* w : kNoise)
        if (hasWordCI(prefix, w)) return true;
    return false;
}

/// 上下文打分（0..100）。只影响排序与"是否值得自动采用"，不影响正确性。
int scoreOf(const std::string& ctx, const std::string& format, int occurrences) {
    int s = 20;
    if (hasWordCI(ctx, "aes")) s += 25;
    if (hasWordCI(ctx, "gcm")) s += 20;
    // `key` 在官方数据里也常表示 IP（多播报文 {"sn":..,"key":"192.168.0.78"}），
    // 所以它的权重低于 aes/gcm；值本身不是 32hex 的 IP 已被 isHex32 挡掉。
    if (hasWordCI(ctx, "key")) s += 20;
    if (hasWordCI(ctx, "sn") || hasWordCI(ctx, "serial")) s += 10;
    if (hasWordCI(ctx, "unitree") || hasWordCI(ctx, "go2")) s += 10;
    if (format == "json") s += 15;        // 结构化存储，误报率最低
    if (format == "prefs-xml") s += 15;
    if (occurrences >= 2) s += 10;        // 同一个值重复出现，通常是真配置而不是哈希
    return std::min(s, 100);
}

std::string formatOf(const std::string& path, const std::string& blob) {
    const std::string e = extOf(path);
    if (blob.rfind("SQLite format 3", 0) == 0) return "sqlite";
    if (e == ".db" || e == ".sqlite" || e == ".sqlite3" || e == ".db3") return "sqlite";
    if (e == ".ldb" || e == ".sst" || e == ".log") return "leveldb";
    if (blob.rfind("<?xml", 0) == 0 || blob.find("<map") != std::string::npos)
        return "prefs-xml";
    if (e == ".plist") return "plist";
    const size_t first = blob.find_first_not_of(" \t\r\n");
    if (first != std::string::npos && (blob[first] == '{' || blob[first] == '[')) {
        if (nlohmann::json::accept(blob)) return "json";
    }
    return "text";
}

/// 在命中点附近找 16 位 SN（Go2 形如 E21D6000XXXXXXXX）。
/// 正则两侧要求非字母数字边界 —— 这样不会把 32hex 的前半截当成 SN。
std::string findSnNear(const std::string& ctx, size_t ctxPos) {
    static const std::regex kSn(R"((?:^|[^0-9A-Za-z])([0-9A-Z]{16})(?:[^0-9A-Za-z]|$))");
    auto begin = std::sregex_iterator(ctx.begin(), ctx.end(), kSn);
    std::string best;
    size_t bestDist = std::string::npos;
    for (auto it = begin; it != std::sregex_iterator(); ++it) {
        const std::string cand = (*it)[1].str();
        // 必须有大写字母：全小写的 16 位串更可能是哈希片段，不是 SN
        if (std::none_of(cand.begin(), cand.end(),
                         [](char c) { return c >= 'A' && c <= 'Z'; }))
            continue;
        const size_t p = static_cast<size_t>((*it).position(1));
        const size_t dist = p > ctxPos ? p - ctxPos : ctxPos - p;
        if (dist < bestDist) {
            bestDist = dist;
            best = cand;
        }
    }
    return best;
}

/// 从 SharedPreferences XML 里成对取 name→value。
/// 官方 App 常把 `aes_key` 和 `sn` 写成两个 `<string>`，中间隔着别的键，
/// 单纯靠 320 字节上下文可能配不上 —— 这里按"同文件里唯一"配对。
void pairXmlPrefs(const std::string& blob, std::map<std::string, std::string>& snOfKey) {
    static const std::regex kEntry(
        R"re(<string\s+name="([^"]{1,64})"\s*>\s*([^<]{4,64})\s*</string>)re");
    std::vector<std::pair<std::string, std::string>> keys;  // (value, name)
    std::vector<std::string> sns;
    for (auto it = std::sregex_iterator(blob.begin(), blob.end(), kEntry);
         it != std::sregex_iterator(); ++it) {
        const std::string name = lowerAscii((*it)[1].str());
        const std::string value = (*it)[2].str();
        if (name.find("key") != std::string::npos && plausibleKey(value))
            keys.emplace_back(lowerAscii(value), name);
        if (name.find("sn") != std::string::npos ||
            name.find("serial") != std::string::npos)
            if (value.size() == 16) sns.push_back(value);
    }
    if (sns.size() != 1) return;  // 多台设备的 SN 无法对应，宁可不猜
    for (const auto& kv : keys) snOfKey[kv.first] = sns.front();
}

/// 从 JSON 里按结构取 sn/key 对（比文本窗口可靠：两个字段可能隔着其它属性）
void pairJsonDevices(const nlohmann::json& j,
                     std::map<std::string, std::string>& snOfKey) {
    if (j.is_object()) {
        std::string sn;
        if (j.contains("sn") && j["sn"].is_string()) sn = j["sn"].get<std::string>();
        else if (j.contains("serialNumber") && j["serialNumber"].is_string())
            sn = j["serialNumber"].get<std::string>();
        if (!sn.empty()) {
            for (const char* f : {"key", "aesKey", "aes_key", "gcm_key", "aes_128_key"}) {
                if (!j.contains(f) || !j[f].is_string()) continue;
                const std::string k = lowerAscii(j[f].get<std::string>());
                if (plausibleKey(k)) snOfKey[k] = sn;
            }
        }
        for (auto it = j.begin(); it != j.end(); ++it) pairJsonDevices(it.value(), snOfKey);
    } else if (j.is_array()) {
        for (const auto& e : j) pairJsonDevices(e, snOfKey);
    }
}

// ---------------------------------------------------------------- 目录枚举

/// 环境变量里的路径列表（`|` 分隔 —— 不用 `:` / `;`，它们在 Windows 盘符里合法）
std::vector<std::string> splitPathList(const char* s) {
    std::vector<std::string> out;
    if (!s || !*s) return out;
    std::string cur;
    for (const char* p = s; *p; ++p) {
        if (*p == '|') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(*p);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

#if defined(__ANDROID__)
void addAndroidPackageRoots(std::vector<std::string>& out) {
    // 官方 App 的包名。Go2 = com.unitree.doggo2（平板上实测），
    // B2/G1/R1 走 com.unitree.b2dog；其余是历史/区域版本，留作兜底。
    static const char* kPkgs[] = {
        "com.unitree.doggo2",   "com.unitree.b2dog",     "com.unitree.go2",
        "com.unitree.go2app",   "com.unitree.unitreego", "com.unitree.explore",
        "com.unitree.g1",       "com.unitree.app",
    };
    for (const char* pkg : kPkgs) {
        // 私有目录：只有 root（su）能读；未 root 时 fs::exists 直接返回 false，
        // 不会打扰用户。root 场景由 ui/key_probe.cpp 的 su 分支负责读内容。
        out.push_back(std::string("/data/data/") + pkg);
        out.push_back(std::string("/data/user/0/") + pkg);
        // 外部私有目录：Android 11+ 的 FUSE 会把**别的 App** 的 Android/data
        // 从文件系统视图里整个隐藏（实测 opendir 返回 ENOENT，与
        // MANAGE_EXTERNAL_STORAGE 无关，见 AndroidManifest 的注释）。
        // 这里保留只是为了 root/旧系统上碰巧可读时不漏；常规平板读不到是正常的。
        out.push_back(std::string("/sdcard/Android/data/") + pkg);
        out.push_back(std::string("/storage/emulated/0/Android/data/") + pkg);
    }
    // 官方 App 可能在用户可见目录留导出文件（实测当前版本没有，保留兜底）
    out.push_back("/sdcard/Unitree");
    out.push_back("/storage/emulated/0/Unitree");
    out.push_back("/sdcard/Download/Unitree");
}
#endif  // __ANDROID__

bool isPrivateAndroidPath(const std::string& p) {
    return p.rfind("/data/data/", 0) == 0 || p.rfind("/data/user/0/", 0) == 0;
}

bool isSharedAndroidPath(const std::string& p) {
    return p.find("/Android/data/") != std::string::npos ||
           p.rfind("/sdcard/", 0) == 0 || p.rfind("/storage/emulated/0/", 0) == 0;
}

}  // namespace

// ============================================================================
// 候选根目录
// ============================================================================

std::vector<std::string> officialAppRootCandidates() {
    std::vector<std::string> out;

    // 1) 显式指定（最高优先级；支持 `|` 分隔多个）
    for (auto& p : splitPathList(std::getenv("GO2_OFFICIAL_APP_DIR"))) out.push_back(p);

    // 2) 本应用的「导入暂存目录」—— 官方 App 数据的副本放这里就能被自动接管。
    //    平板上的实际用法：电脑 `adb push` 到 files/import/（目录由 App 启动时创建并
    //    放开权限，见 ui/key_probe.cpp）。root 平板则走 su 分支直读私有目录。
    //    同时把 files/ 本身也作为根：adb 直接 push 到 files/ 也能被发现（老用户习惯）。
    out.push_back(userDataDir() + "/import");
    out.push_back(userDataDir());

    // 3) 各平台的官方 App 目录 / 用户手工拷贝目录
    if (const char* up = std::getenv("USERPROFILE"))
        out.push_back(std::string(up) + "\\.go2\\official");
    if (const char* home = std::getenv("HOME")) {
        out.push_back(std::string(home) + "/.go2/official");
        out.push_back(std::string(home) + "/Library/Application Support/Unitree");
        out.push_back(std::string(home) + "/.config/unitree");
        out.push_back(std::string(home) + "/.local/share/Unitree");
    }
    if (const char* ad = std::getenv("APPDATA"))
        out.push_back(std::string(ad) + "\\Unitree");
    if (const char* lad = std::getenv("LOCALAPPDATA"))
        out.push_back(std::string(lad) + "\\Unitree");

#if defined(__ANDROID__)
    addAndroidPackageRoots(out);
#endif

    // 去重（保留顺序）
    std::vector<std::string> uniq;
    for (auto& p : out)
        if (!p.empty() && std::find(uniq.begin(), uniq.end(), p) == uniq.end())
            uniq.push_back(std::move(p));
    return uniq;
}

// ============================================================================
// 单文件解析
// ============================================================================

std::vector<OfficialKeyHit> parseOfficialKeyBlob(const std::string& path,
                                                 const std::string& blob) {
    std::vector<OfficialKeyHit> out;
    if (blob.empty()) return out;

    const std::string text = normalizeIfUtf16(blob);
    const std::string format = formatOf(path, text);

    // 结构化的 SN 配对（能配上就配上，配不上也不影响 key 的使用）
    std::map<std::string, std::string> snOfKey;
    if (format == "prefs-xml") {
        pairXmlPrefs(text, snOfKey);
    } else if (format == "json") {
        try {
            const nlohmann::json j = nlohmann::json::parse(text);
            pairJsonDevices(j, snOfKey);
        } catch (...) {
            // accept() 过了但 parse 失败（超深嵌套等）—— 无所谓，走纯文本扫描
        }
    }

    const auto raw = scanHex32(text);
    if (raw.empty()) return out;

    // 统计出现次数：同值重复出现 → 更像真配置
    std::map<std::string, int> occurrences;
    for (const auto& r : raw) ++occurrences[r.key];

    std::set<std::string> seen;
    for (const auto& r : raw) {
        if (!seen.insert(r.key).second) continue;  // 同一文件内同 key 只报一次
        const std::string ctx = contextOf(text, r.pos);
        const size_t ctxPos = r.pos > 320 ? 320 : r.pos;
        if (isNoiseField(prefixOf(ctx, ctxPos))) continue;  // requestId 之类

        OfficialKeyHit hit;
        hit.key = r.key;
        hit.source = path;
        hit.format = format;
        hit.score = scoreOf(ctx, format, occurrences[r.key]);
        auto it = snOfKey.find(r.key);
        hit.sn = it != snOfKey.end() ? it->second : findSnNear(ctx, ctxPos);
        out.push_back(std::move(hit));
    }
    return out;
}

// ============================================================================
// 目录扫描
// ============================================================================

KeyImportReport scanOfficialAppKeys(const KeyImportOptions& opt) {
    KeyImportReport rep;

    std::vector<std::string> roots = opt.extraRoots;
    if (!opt.onlyExtraRoots) {
        for (auto& r : officialAppRootCandidates()) {
            if (!opt.includeAndroidPrivate && isPrivateAndroidPath(r)) continue;
            if (!opt.includeSharedStorage && isSharedAndroidPath(r)) continue;
            roots.push_back(r);
        }
    }

    std::map<std::string, OfficialKeyHit> best;  // key → 最高分的来源

    for (const auto& root : roots) {
        std::error_code ec;
        if (!fs::exists(root, ec) || ec) continue;
        if (!fs::is_directory(root, ec) || ec) continue;
        rep.anyRootExists = true;
        rep.rootsProbed.push_back(abbreviatePath(root));

        fs::recursive_directory_iterator it(
            root, fs::directory_options::skip_permission_denied, ec);
        if (ec) {
            if (ec == std::errc::permission_denied) rep.permissionDenied = true;
            rep.notes.push_back("无法读取 " + abbreviatePath(root) + "（" + ec.message() + "）");
            continue;
        }

        for (const auto& entry : it) {
            if (rep.filesScanned >= opt.maxFiles) {
                rep.notes.push_back("已达到单次扫描上限（" +
                                    std::to_string(opt.maxFiles) + " 个文件），其余跳过");
                break;
            }
            std::error_code e2;
            if (it.depth() >= opt.maxDepth) it.disable_recursion_pending();

            if (!entry.is_regular_file(e2) || e2) continue;
            const std::string path = toUtf8(entry.path());
            if (!worthReading(path) || isOwnKeyStore(path)) continue;

            const auto size = entry.file_size(e2);
            if (e2 || size == 0 || size > opt.maxFileBytes) continue;

            std::ifstream f(entry.path(), std::ios::binary);
            if (!f) {
                if (fs::exists(entry.path(), e2) && !e2) rep.permissionDenied = true;
                continue;
            }
            std::string blob(static_cast<size_t>(size), '\0');
            f.read(blob.data(), static_cast<std::streamsize>(blob.size()));
            blob.resize(static_cast<size_t>(f.gcount()));
            ++rep.filesScanned;

            for (auto& h : parseOfficialKeyBlob(path, blob)) {
                auto found = best.find(h.key);
                if (found == best.end()) {
                    best.emplace(h.key, std::move(h));
                } else if (h.score > found->second.score ||
                           (h.score == found->second.score && !h.sn.empty() &&
                            found->second.sn.empty())) {
                    // 保留分数更高的来源；同分时优先带 SN 的那条
                    if (found->second.sn.empty() && !h.sn.empty()) h.sn = h.sn;
                    found->second = std::move(h);
                }
            }
        }

        if (rep.filesScanned >= opt.maxFiles) break;
    }

    for (auto& kv : best) rep.hits.push_back(std::move(kv.second));
    std::sort(rep.hits.begin(), rep.hits.end(),
              [](const OfficialKeyHit& a, const OfficialKeyHit& b) {
                  if (a.score != b.score) return a.score > b.score;
                  return a.key < b.key;
              });

    // ---- 给用户的结论（不出现任何钥匙内容）----
    if (!rep.anyRootExists) {
        rep.notes.push_back(
            "没有找到官方 App 的数据目录。安卓 11+ 上其它应用的私有目录需要 root；"
            "桌面端可把官方 App 的数据导出到 ~/.go2/official（或用 GO2_OFFICIAL_APP_DIR 指定）");
    } else if (rep.hits.empty()) {
        if (rep.filesScanned == 0) {
            // 目录 stat 得到、但遍历不到任何文件 —— Android 11+ 的 FUSE 对
            // "别的 App 的 Android/data" 就是这个表现（ENOENT/空目录，与权限无关）
            rep.notes.push_back(
                "官方 App 的目录存在，但里面读不到任何文件 —— Android 11+ 会把其它 App 的 "
                "Android/data 从文件系统视图里隐藏（与授权无关）");
        } else if (rep.permissionDenied) {
            rep.notes.push_back(
                "目录存在但读不进去（权限不足）。官方 App 的私有目录需要 root；"
                "也可以用它自带的备份/导出功能把数据放到共享存储，再用 GO2_OFFICIAL_APP_DIR 指定");
        } else {
            rep.notes.push_back(
                "找到了官方 App 的数据目录，但没扫到明文钥匙 —— 它可能用 SQLCipher/MMKV 加密存储。"
                "这时请走「云端取钥匙」或「从机器狗找钥匙」");
        }
    } else {
        rep.notes.push_back("共扫描 " + std::to_string(rep.filesScanned) + " 个文件，找到 " +
                            std::to_string(rep.hits.size()) + " 把候选钥匙");
    }
    return rep;
}

std::string maskKey(const std::string& key) {
    if (key.size() != 32) return "****";
    return key.substr(0, 4) + "…" + key.substr(key.size() - 4);
}

}  // namespace go2
