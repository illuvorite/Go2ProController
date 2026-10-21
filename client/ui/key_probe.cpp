#include "key_probe.hpp"

#include "cloud_store.hpp"  // 保存的云账号（启动时自动登录）
#include "local_keys.hpp"
#include "official_key_import.hpp"
#include "robot_manager.hpp"
#include "ui.hpp"
#include "unitree_cloud.hpp"  // 云账号：登录 → device/bind/list → 每设备 dev.key

#include "../platform/net.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <regex>
#include <set>
#include <thread>
#include <vector>

namespace go2 {

// ---------------------------------------------------------------- 网络探针（跨平台）

/// 非阻塞 TCP 端口探测
static bool tcpOpen(const std::string& ip, unsigned short port, int timeoutMs) {
    platform::initSockets();
    go2_socket_t s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s == GO2_INVALID_SOCKET) return false;
#ifdef _WIN32
    u_long nb = 1;
    ::ioctlsocket(s, FIONBIO, &nb);
#else
    int flags = ::fcntl(s, F_GETFL, 0);
    ::fcntl(s, F_SETFL, flags | O_NONBLOCK);
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = ::htons(port);
    ::inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
    const int rc = ::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    bool ok = (rc == 0);
    if (!ok) {
        const int err = platform::lastSocketError();
#ifdef _WIN32
        if (platform::wouldBlock(err)) {
#else
        if (platform::wouldBlock(err) || err == EINPROGRESS) {
#endif
            ok = platform::waitSocket(s, true, timeoutMs) > 0;
            if (ok) {
                int soErr = 0;
                socklen_t len = sizeof(soErr);
                ::getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soErr), &len);
                ok = (soErr == 0);
            }
        }
    }
    platform::closeSocket(s);
    return ok;
}

/// 阻塞式 HTTP GET（短超时；只拿小响应）
static std::string httpGet(const std::string& ip, unsigned short port,
                           const std::string& path, int timeoutMs) {
    platform::initSockets();
    go2_socket_t s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s == GO2_INVALID_SOCKET) return {};
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = ::htons(port);
    ::inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
    timeval tv{};
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&tv), sizeof(tv));
    ::setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<char*>(&tv), sizeof(tv));
    if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        platform::closeSocket(s);
        return {};
    }
    const std::string req = "GET " + path +
                            " HTTP/1.0\r\nHost: " + ip +
                            "\r\nUser-Agent: go2-remote-keyprobe\r\nConnection: close\r\n\r\n";
    std::string out;
    if (::send(s, req.c_str(), static_cast<int>(req.size()), 0) > 0) {
        char buf[4096];
        for (;;) {
            const int n = static_cast<int>(::recv(s, buf, sizeof(buf), 0));
            if (n <= 0) break;
            out.append(buf, static_cast<size_t>(n));
            if (out.size() > 256 * 1024) break;
        }
    }
    platform::closeSocket(s);
    return out;
}

/// 从文本里捞 32 位 hex 候选（去重 + 字符种类 > 4，滤掉 aaaa… 这种假货）
static std::vector<std::string> hexCandidates(const std::string& text) {
    static const std::regex kHex("\\b[0-9a-fA-F]{32}\\b");
    std::vector<std::string> out;
    std::set<std::string> seen;
    auto it = std::sregex_iterator(text.begin(), text.end(), kHex);
    for (auto end = std::sregex_iterator(); it != end; ++it) {
        std::string k = it->str();
        std::transform(k.begin(), k.end(), k.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const int distinct = static_cast<int>(std::set<char>(k.begin(), k.end()).size());
        if (distinct > 4 && seen.insert(k).second) out.push_back(k);
    }
    return out;
}

// ---------------------------------------------------------------- 对外入口

void runKeyProbe(RobotManager& mgr, UiState& ui, const std::string& ipIn) {
    (void)mgr;  // 本函数只扫狗的端口/Web 服务，用不到连接句柄；
                // 保留形参是为了与 applyKeyCandidate(mgr, ui, key, ip) 的调用形状一致
    std::string ip = ipIn;
    if (ip.empty()) {
        const auto sel = ui.selectedIps();
        if (!sel.empty()) ip = sel.front();
        else {
            std::lock_guard<std::mutex> lock(ui.robotsMutex);
            if (!ui.robots.empty()) ip = ui.robots.front().ip;
        }
    }
    if (ip.empty()) {
        std::lock_guard<std::mutex> lock(ui.keyScanMutex);
        ui.keyScanNote = "没有可探测的 IP —— 先在设备里添加一台狗";
        return;
    }

    ui.keyScanning = true;
    {
        std::lock_guard<std::mutex> lock(ui.keyScanMutex);
        ui.keyCandidates.clear();
        ui.keyScanNote = "探测中：" + ip;
    }
    ui.addLog("[钥匙] 开始探测 " + ip + " 的内网服务 ...");

    static const int kPorts[] = {22, 23, 80, 443, 5555, 2049, 8080,
                                 8081, 8888, 8000, 9090, 9991};
    std::vector<int> open;
    for (int p : kPorts)
        if (tcpOpen(ip, static_cast<unsigned short>(p), 400)) open.push_back(p);

    std::string note;
    if (open.empty()) {
        note = "没有开放端口 —— 狗不在线 / IP 不对 / 不在同一网段";
    } else {
        note = "开放端口:";
        for (size_t i = 0; i < open.size(); ++i)
            note += (i ? "," : " ") + std::to_string(open[i]);
    }
    ui.addLog("[钥匙] " + note);

    std::vector<std::string> found;
    const auto harvest = [&](const std::string& text) {
        for (const auto& k : hexCandidates(text))
            if (std::find(found.begin(), found.end(), k) == found.end()) found.push_back(k);
    };
    for (int p : open) {
        if (p == 80 || p == 8080 || p == 8081 || p == 8888 || p == 8000 || p == 9090) {
            for (const char* path : {"/", "/config", "/debug/vars"})
                harvest(httpGet(ip, static_cast<unsigned short>(p), path, 1500));
        }
    }

    std::string hint;
    for (int p : open) {
        if (p == 5555) hint += " 5555(ADB)";
        if (p == 22) hint += " 22(SSH)";
        if (p == 2049) hint += " 2049(NFS)";
    }
    if (!hint.empty())
        hint = "这些入口需要外部工具（adb/sshpass/mount），目前只能用电脑：" + hint + "；";

    {
        std::lock_guard<std::mutex> lock(ui.keyScanMutex);
        ui.keyCandidates = found;
        ui.keyScanNote = note + (found.empty() ? "" : ("；候选钥匙 " + std::to_string(found.size()) + " 把")) +
                         "。" + hint;
    }
    if (found.empty()) {
        ui.addLog(std::string("[钥匙] 没有直接捞到 32 位 hex。") + hint);
    } else {
        for (const auto& k : found) ui.addLog("[钥匙] 候选: " + k + "（点「采用」后握手验证）");
    }
    ui.keyScanning = false;
}

bool applyKeyCandidate(RobotManager& mgr, UiState& ui, const std::string& key,
                       const std::string& ip) {
    const bool hexOk = key.size() == 32 &&
                       std::all_of(key.begin(), key.end(),
                                   [](unsigned char c) { return std::isxdigit(c) != 0; });
    if (!hexOk) return false;

    std::string k = key;
    for (char& c : k) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    {
        // 与设置页手工粘贴走同一个位置：应用数据目录（不落工程目录）
        const std::string keysPath = defaultKeysTxtPath();
        ensureParentDir(keysPath);
        // 去重：自动导入每次启动都会重扫，不能反复往文件里追加同一把
        const auto existing = loadLocalKeysFile(keysPath);
        if (std::find(existing.begin(), existing.end(), k) == existing.end()) {
            std::ofstream f(keysPath, std::ios::app);
            f << k << "\n";
        }
    }
    mgr.addAesKeys({k});
    if (!ip.empty()) mgr.bindKey(ip, k);
    // 日志只留掩码 —— 钥匙明文不进日志（README「密钥绝不暴露」）
    ui.addLog("[钥匙] 已采用 " + maskKey(k) + (ip.empty() ? "" : (" → 绑定 " + ip)) +
              "（下次连接生效；握手成功即为正确钥匙）");
    return true;
}

// ============================================================================
// 官方 App 钥匙导入（data2=3 新固件）
//
// 定位 → 读取 → 解析 → 自动填入/落盘 → 自动重连，全流程后台跑。
// 边界说明见 core/official_key_import.hpp：只读，不碰官方 App 的任何文件。
// ============================================================================

namespace {

/// 安卓：root 平板才有机会读官方 App 的私有目录（/data/data/<pkg>）。
///
/// ★ 未 root 时这里**静默返回**：Android 的沙箱不允许普通 App 读别的 App 私有目录，
///   也没有任何系统 API 能绕过（run-as 只对 debuggable 包有效，adb backup 在
///   Android 12+ 已废弃）。这不是实现缺陷，是平台边界 —— 界面会给出替代路径。
#if defined(__ANDROID__)

std::string runShell(const std::string& cmd, std::size_t maxBytes) {
    std::string out;
    FILE* p = ::popen(cmd.c_str(), "r");
    if (!p) return out;
    char buf[4096];
    while (out.size() < maxBytes) {
        const std::size_t n = std::fread(buf, 1, sizeof(buf), p);
        if (n == 0) break;
        out.append(buf, n);
    }
    ::pclose(p);
    return out;
}

/// 单引号安全包裹（路径里可能有空格 / $）
std::string shellQuote(const std::string& s) {
    std::string o = "'";
    for (char c : s) {
        if (c == '\'') o += "'\\''";
        else o.push_back(c);
    }
    o += "'";
    return o;
}

/// 动态发现官方包名（`pm list packages` 不需要 root，比硬编码更抗改名）
std::vector<std::string> unitreePackages() {
    std::vector<std::string> out;
    const std::string txt = runShell("pm list packages 2>/dev/null", 1u << 20);
    std::istringstream iss(txt);
    std::string line;
    while (std::getline(iss, line)) {
        // 形如 "package:com.unitree.doggo2"
        const std::string tag = "package:";
        const size_t p = line.find(tag);
        if (p == std::string::npos) continue;
        std::string pkg = line.substr(p + tag.size());
        while (!pkg.empty() && (pkg.back() == '\r' || pkg.back() == ' ')) pkg.pop_back();
        const std::string low = [&] {
            std::string s = pkg;
            for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }();
        if (low.find("unitree") != std::string::npos ||
            low.find("doggo") != std::string::npos)
            out.push_back(pkg);
    }
    return out;
}

/// root 分支：读官方 App 私有目录里的候选文件，交给同一个解析器。
/// 返回命中的候选条数（0 = 未 root / 没找到）。
std::size_t importFromRootedAndroid(std::vector<OfficialKeyHit>& hits,
                                    std::vector<std::string>& notes) {
    // 先确认 su 真的可用（未 root 的机器上 `su` 不存在，popen 只返回空）
    const std::string idOut = runShell("timeout 8 su -c id 2>/dev/null", 4096);
    if (idOut.find("uid=0") == std::string::npos) {
        notes.push_back(
            "平板未 root：官方 App 的私有目录（/data/data/<包名>）受 Android 沙箱保护，"
            "任何第三方 App 都读不到。可用「从机器狗找钥匙」，或把导出的数据放进 "
            "应用目录的 import/ 子目录");
        return 0;
    }

    const auto pkgs = unitreePackages();
    if (pkgs.empty()) {
        notes.push_back("root 可用，但没找到官方宇树 App（pm list packages 无 unitree 包）");
        return 0;
    }

    std::size_t added = 0;
    for (const auto& pkg : pkgs) {
        // 钥匙最可能落在 shared_prefs / files / databases 三处，只扫这些子目录
        for (const char* sub : {"shared_prefs", "files", "databases"}) {
            const std::string dir = "/data/data/" + pkg + "/" + sub;
            const std::string inner = "grep -rlaE '[0-9a-fA-F]{32}' " + dir +
                                      " 2>/dev/null | head -20";
            const std::string listed =
                runShell("timeout 12 su -c " + shellQuote(inner), 1u << 18);
            std::istringstream iss(listed);
            std::string file;
            while (std::getline(iss, file)) {
                while (!file.empty() && (file.back() == '\r' || file.back() == ' '))
                    file.pop_back();
                if (file.empty() || file[0] != '/') continue;
                const std::string blob = runShell(
                    "timeout 10 su -c " + shellQuote("cat " + shellQuote(file) + " 2>/dev/null"),
                    512u * 1024);
                if (blob.empty()) continue;
                for (auto& h : parseOfficialKeyBlob(file, blob)) {
                    // 官方私有目录里的命中，本身就是强证据 → 抬高分数
                    h.score = std::min(h.score + 20, 100);
                    hits.push_back(std::move(h));
                    ++added;
                }
            }
        }
    }
    if (added == 0)
        notes.push_back("已用 root 读取官方 App 私有目录，但没扫到明文钥匙"
                        "（可能用 SQLCipher/MMKV 加密存储）");
    return added;
}

#endif  // __ANDROID__

/// 合并候选：同 key 保留分数更高的；两边都有 SN 时互补
/// （非 Android 构建里 root 分支不参与，函数暂时没人调用）
[[maybe_unused]] void mergeHits(std::vector<OfficialKeyHit>& dst,
                                const std::vector<OfficialKeyHit>& src) {
    for (const auto& h : src) {
        auto it = std::find_if(dst.begin(), dst.end(),
                               [&](const OfficialKeyHit& x) { return x.key == h.key; });
        if (it == dst.end()) {
            dst.push_back(h);
            continue;
        }
        if (it->sn.empty() && !h.sn.empty()) it->sn = h.sn;
        if (h.score > it->score) {
            const std::string sn = it->sn;
            *it = h;
            if (it->sn.empty()) it->sn = sn;
        }
    }
    std::sort(dst.begin(), dst.end(), [](const OfficialKeyHit& a, const OfficialKeyHit& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.key < b.key;
    });
}

/// 钥匙库变化后：重新读一遍、并入候选、对失败的狗触发重连。
///
/// 不复用 ui.cpp 的 uix::reloadKeysAndRetry：那个定义在 ui.cpp，而 go2_serve
/// 的 target 里没有 ui.cpp（只有 ui_state/command_service/web_bridge/key_probe），
/// 复用会链接失败。这里的语义与它一致（多一层旧路径兼容对导入无意义）。
void reloadKeysAfterImport(RobotManager& mgr, UiState& ui) {
    const auto keys = loadLocalKeysFile(defaultKeysTxtPath());
    mgr.addAesKeys(keys);
    ui.localKeyCount = static_cast<int>(keys.size());
    mgr.reconnectFailed();
}

}  // namespace

void runOfficialKeyImport(RobotManager& mgr, UiState& ui, bool autoApply) {
    ui.officialKeyScanning = true;

    // 确保 import/ 存在，并且电脑 adb 能往里写。
    // 目录必须由**本应用**创建（owner 才正确；adb 创建的目录属 shell，App 进不去）；
    // 权限放开到 0777 是为了让 adb push 落地 —— 这里只放"待导入的副本"，
    // 真正的钥匙库（keys.txt）仍在 files/ 下、不受影响。
    {
        const std::string dir = userDataDir() + "/import";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        std::filesystem::permissions(dir, std::filesystem::perms::all,
                                     std::filesystem::perm_options::replace, ec);
    }

    ui.addLog("[钥匙] 正在查找官方 App 保存的钥匙 ...");

    KeyImportReport rep = scanOfficialAppKeys();

#if defined(__ANDROID__)
    std::vector<OfficialKeyHit> rootHits;
    importFromRootedAndroid(rootHits, rep.notes);
    mergeHits(rep.hits, rootHits);
#endif

    // 自动采用的阈值：低于这个分数只列出来让用户点，不擅自写进钥匙库。
    // （误报的代价其实只是"多试一把"，但一把垃圾钥匙会让连接多等一轮超时。）
    constexpr int kAutoApplyScore = 55;

    std::string note;
    for (const auto& n : rep.notes) {
        if (!note.empty()) note += "；";
        note += n;
    }

    std::size_t applied = 0;
    if (autoApply) {
        for (const auto& h : rep.hits) {
            if (h.score < kAutoApplyScore) continue;
            if (applyKeyCandidate(mgr, ui, h.key, "")) ++applied;
        }
    }

    if (!rep.hits.empty()) {
        std::lock_guard<std::mutex> lock(ui.officialKeyMutex);
        ui.officialKeyHits = rep.hits;
        ui.officialKeyNote = note;
        // 填进设置页输入框（界面/网页下一帧消费）—— 让用户看得见"从哪来的哪把"
        ui.officialKeyAutoFill = rep.hits.front().key;
        ui.officialKeyFillPending = true;
        ui.officialKeyImported = applied > 0;
    } else {
        std::lock_guard<std::mutex> lock(ui.officialKeyMutex);
        ui.officialKeyHits.clear();
        ui.officialKeyNote = note;
        ui.officialKeyImported = false;
    }

    if (applied > 0) {
        reloadKeysAfterImport(mgr, ui);  // 重新装载 + 对失败的狗自动重连
        ui.addLog("[钥匙] 已从官方 App 自动导入 " + std::to_string(applied) +
                  " 把（握手通过即为正确钥匙）");
    } else if (!rep.hits.empty()) {
        ui.addLog("[钥匙] 找到 " + std::to_string(rep.hits.size()) +
                  " 个候选，但置信度不足，未自动采用 —— 可在设置页点「采用」");
    } else {
        ui.addLog("[钥匙] 未找到官方 App 的钥匙。" + note);
    }

    ui.officialKeyScanning = false;
}

void startOfficialKeyImport(RobotManager& mgr, UiState& ui, bool autoApply) {
    if (ui.officialKeyScanning.exchange(true)) return;  // 已在跑，别叠加线程
    std::thread([&mgr, &ui, autoApply] { runOfficialKeyImport(mgr, ui, autoApply); }).detach();
}

// ============================================================================
// 云账号拉钥匙
// ============================================================================

void runCloudKeyImport(RobotManager& mgr, UiState& ui, std::string account,
                       std::string password, bool cnRegion) {
    {
        std::lock_guard<std::mutex> lock(ui.cloudMutex);
        ui.cloudNote = "正在登录宇树云 ...";
    }
    ui.addLog(std::string("[云] 正在登录宇树云（") + (cnRegion ? "国内" : "海外") + "）...");

    std::string note;
    int total = 0, withKey = 0;
    bool ok = false;
    std::vector<std::string> added;
    try {
        UnitreeCloud cloud(cnRegion ? "cn" : "global");
        cloud.loginEmail(account, password);
        const auto devs = cloud.listDevices();
        total = static_cast<int>(devs.size());

        // 本地已有的 key→SN 先读出来：setKeySnTable 是**覆盖**语义，
        // 直接灌云端结果会把本地 keys.json 里的 SN 表清掉。
        auto pairs = loadLocalAesKeys().snByKey;
        std::set<std::string> known;
        for (const auto& k : loadLocalKeysFile(defaultKeysTxtPath())) known.insert(k);

        for (const auto& d : devs) {
            if (d.aesKey.size() != 32) continue;
            std::string k = d.aesKey;
            for (char& c : k) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            ++withKey;
            if (applyKeyCandidate(mgr, ui, k, "")) {
                if (known.insert(k).second) added.push_back(k);
            }
            if (!d.sn.empty()) {
                bool merged = false;
                for (auto& p : pairs)
                    if (p.first == k) {
                        p.second = d.sn;
                        merged = true;
                        break;
                    }
                if (!merged) pairs.emplace_back(k, d.sn);
            }
        }
        if (!pairs.empty()) ui.setKeySnTable(std::move(pairs));

        if (total == 0) {
            note = "账号下没有绑定设备";
        } else if (withKey == 0) {
            note = "账号下有 " + std::to_string(total) +
                   " 台设备，但没有返回钥匙（旧固件本来就不需要）";
        } else {
            note = "账号下 " + std::to_string(total) + " 台设备、" + std::to_string(withKey) +
                   " 台带钥匙，新增 " + std::to_string(added.size()) + " 把";
        }
        ok = true;
    } catch (const std::exception& e) {
        note = std::string("失败：") + e.what();
    } catch (...) {
        note = "失败：未知错误";
    }

    if (!added.empty()) reloadKeysAfterImport(mgr, ui);  // 重新装载 + 失败设备自动重连

    if (ok) {
        // 登录成功就把凭据存下来：下次启动直接自动登录，不用再填。
        saveCloudCredential(CloudCredential{account, password, cnRegion});
    }
    {
        std::lock_guard<std::mutex> lock(ui.cloudMutex);
        ui.cloudNote = note;
        ui.cloudSaved = ok || ui.cloudSaved.load();
        if (ok) ui.cloudSavedAccount = account;
    }
    ui.addLog("[云] " + note);
    ui.cloudBusy = false;
}

void startCloudKeyImport(RobotManager& mgr, UiState& ui, std::string account,
                         std::string password, bool cnRegion) {
    if (ui.cloudBusy.exchange(true)) return;  // 已在登录，别叠加
    std::thread([&mgr, &ui, account = std::move(account), password = std::move(password),
                 cnRegion] { runCloudKeyImport(mgr, ui, account, password, cnRegion); })
        .detach();
}

void startAutoKeyImport(RobotManager& mgr, UiState& ui, bool scanOfficial) {
    std::thread([&mgr, &ui, scanOfficial] {
        // 串行：两路都会往 keys.txt 追加，并发写没有意义还可能交错
        if (scanOfficial) runOfficialKeyImport(mgr, ui, true);

        CloudCredential c;
        if (!loadCloudCredential(c)) return;  // 没存过账号 → 什么都不做（不打扰用户）

        if (ui.cloudBusy.exchange(true)) return;  // 用户刚好手动点了登录
        {
            std::lock_guard<std::mutex> lock(ui.cloudMutex);
            ui.cloudSaved = true;
            ui.cloudSavedAccount = c.account;
        }
        runCloudKeyImport(mgr, ui, c.account, c.password, c.regionCn);
    }).detach();
}

}  // namespace go2
