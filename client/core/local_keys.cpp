#include "local_keys.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>

#ifdef __linux__
#include <dirent.h>
#ifdef __ANDROID__
#include <sys/stat.h>
#endif
#endif

namespace go2 {
namespace {

bool isHexKey(const std::string& s) {
    if (s.size() != 32) return false;
    return std::all_of(s.begin(), s.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    });
}

/// 从任意 JSON 结构中收集钥匙。支持：
///   {"devices":[{"sn":..,"key":"32hex"}, ...]}   <- 标准格式（含 SN/备注）
///   {"keys":["32hex", ...]}
///   ["32hex", ...]
///   {"任意键名":"32hex", ...}
void collectFromJson(const nlohmann::json& j, std::vector<std::string>& out) {
    if (j.is_object()) {
        for (const char* arrKey : {"keys", "devices"}) {
            if (j.contains(arrKey) && j[arrKey].is_array()) {
                collectFromJson(j[arrKey], out);
                return;
            }
        }
        for (const char* field : {"key", "aesKey", "aes_key", "gcm_key", "aes_128_key"}) {
            if (j.contains(field) && j[field].is_string()) {
                const auto s = j[field].get<std::string>();
                if (isHexKey(s)) out.push_back(s);
            }
        }
        for (auto it = j.begin(); it != j.end(); ++it) {
            if (it.value().is_string()) {
                const auto s = it.value().get<std::string>();
                if (isHexKey(s)) out.push_back(s);
            }
        }
    } else if (j.is_array()) {
        for (const auto& e : j) {
            if (e.is_string()) {
                const auto s = e.get<std::string>();
                if (isHexKey(s)) out.push_back(s);
            } else if (e.is_object()) {
                collectFromJson(e, out);
            }
        }
    }
}

/// 从任意文本里扫出 32 位 hex —— 官方宇树 App 的日志不是 JSON，只能裸扫。
/// 只取"正好 32 个连续 hex 且两侧不是 hex"的串，避免从更长的 SN/MD5 里截出假钥匙。
void collectFromText(const std::string& text, std::vector<std::string>& out) {
    const auto isHex = [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    };
    size_t i = 0;
    while (i < text.size()) {
        size_t run = 0;
        while (i + run < text.size() && isHex(text[i + run])) ++run;
        if (run == 32 && (i == 0 || !isHex(text[i - 1])) &&
            (i + 32 >= text.size() || !isHex(text[i + 32])))
            out.push_back(text.substr(i, 32));
        i += run > 0 ? run : 1;
    }
}

#ifdef __ANDROID__
/// 列出目录里的普通文件（非递归，按名字倒序 —— 日志文件名就是日期，新的在前）
std::vector<std::string> listFilesNewestFirst(const std::string& dir, size_t limit,
                                              long maxBytes) {
    std::vector<std::string> out;
    DIR* d = ::opendir(dir.c_str());
    if (!d) return out;
    struct dirent* e = nullptr;
    while ((e = ::readdir(d)) != nullptr) {
        const std::string name = e->d_name;
        if (name.empty() || name[0] == '.') continue;
        const std::string full = dir + "/" + name;
        struct stat st{};
        if (::stat(full.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
        if (st.st_size > maxBytes) continue;   // 跳过媒体/大文件（官方 App 里有截图视频）
        out.push_back(full);
    }
    ::closedir(d);
    std::sort(out.rbegin(), out.rend());
    if (out.size() > limit) out.resize(limit);
    return out;
}
#endif

/// 候选文件（按优先级）。**刻意不搜索项目目录**：
/// 钥匙绝不放在工程里，避免随代码分享/提交外泄。
std::vector<std::string> candidateFiles() {
    std::vector<std::string> paths;

    // 1) 显式指定（最高优先级）
    if (const char* f = std::getenv("GO2_KEYS_FILE")) paths.emplace_back(f);

#ifdef _WIN32
    // 2) Windows：%USERPROFILE%\.go2\keys.json（用户配置目录，项目外）
    if (const char* up = std::getenv("USERPROFILE"))
        paths.push_back(std::string(up) + "\\.go2\\keys.json");
    if (const char* ad = std::getenv("APPDATA"))
        paths.push_back(std::string(ad) + "\\go2\\keys.json");
#else
    // 2) Linux / macOS：$HOME/.go2/keys.json
    if (const char* home = std::getenv("HOME")) {
        paths.push_back(std::string(home) + "/.go2/keys.json");
#ifdef __linux__
        // WSL 额外兼容：钥匙存在 Windows 侧时也能找到
        //   C:\Users\<用户>\.go2\keys.json -> /mnt/c/Users/<用户>/.go2/keys.json
        if (DIR* d = ::opendir("/mnt/c/Users")) {
            struct dirent* e = nullptr;
            while ((e = ::readdir(d)) != nullptr) {
                const std::string name = e->d_name;
                if (name.empty() || name[0] == '.') continue;
                if (name == "All Users" || name == "Default" ||
                    name == "Default User" || name == "Public" ||
                    name == "WDAGUtilityAccount")
                    continue;
                paths.push_back("/mnt/c/Users/" + name + "/.go2/keys.json");
            }
            ::closedir(d);
        }
#endif
        paths.push_back(std::string(home) + "/.go2_remote_keys.json");  // 兼容旧位置
    }
#endif

    // ------------------------------------------------------------------ 安卓
    // 用户明确的三条规矩：
    //   1) 不碰官方宇树 App 的任何东西（不读它的数据、不解析它的日志）
    //   2) 不在平板存储里"扫"钥匙（不遍历 Download / 根目录）
    //   3) 只从**本应用专属目录**读
    // 实现上：Android 启动时 main_android.cpp 已 chdir 到应用专属目录，并把
    // GO2_KEYS_FILE 指向 <应用目录>/keys.json（上面的第 1 项优先级最高）；
    // 设置页手工粘贴的钥匙写进同目录的 keys.txt。所以这里**不再额外加任何路径**。
    // 钥匙是一次性、与 IP/网络无关的：装好一次就长期有效，见 README 的"钥匙"一节。

    return paths;
}

}  // namespace

LocalKeys loadLocalAesKeys() {
    LocalKeys out;

    // ---- 1) 环境变量（逗号/分号/空格分隔）----
    if (const char* env = std::getenv("GO2_AES_KEYS")) {
        std::string s(env);
        for (char& c : s)
            if (c == ',' || c == ';') c = ' ';
        std::istringstream iss(s);
        std::string tok;
        while (iss >> tok)
            if (isHexKey(tok)) out.keys.push_back(tok);
        if (!out.keys.empty()) out.source = "环境变量 GO2_AES_KEYS";
    }

    // ---- 2) 钥匙文件（取第一个含有效钥匙的文件）----
    for (const auto& p : candidateFiles()) {
        std::ifstream f(p);
        if (!f.is_open()) continue;
        const std::string text((std::istreambuf_iterator<char>(f)),
                               std::istreambuf_iterator<char>());
        std::vector<std::string> found;
        try {
            collectFromJson(nlohmann::json::parse(text), found);
        } catch (...) {
            // 不是 JSON（官方 App 的日志就是纯文本日志）→ 退回裸文本扫 32 位 hex
            collectFromText(text, found);
        }
        if (found.empty()) collectFromText(text, found);  // JSON 里没钥匙也再扫一遍文本
        if (found.empty()) continue;
        for (auto& k : found) out.keys.push_back(k);
        out.source += (out.source.empty() ? "" : " + ");
        out.source += p;
        break;
    }

    // ---- 去重（统一小写）----
    std::vector<std::string> uniq;
    for (auto k : out.keys) {
        std::transform(k.begin(), k.end(), k.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
        if (std::find(uniq.begin(), uniq.end(), k) == uniq.end()) uniq.push_back(k);
    }
    out.keys = std::move(uniq);
    return out;
}

}  // namespace go2
