// ============================================================================
// 官方 App 钥匙导入：解析与扫描的离线自测（不连狗、不读真实官方 App 数据）。
//
// 为什么值得测：
//   1. 官方 App 的日志里每条 HTTP 请求都带 `"requestId":"<32hex>"` —— 全是合法
//      32 位 hex，裸扫会导进一堆垃圾。这里用真实日志形态做回归，防止以后有人
//      "简化"成纯正则扫描。
//   2. 载体格式五花八门（JSON / SharedPreferences XML / SQLite / LevelDB / 纯文本），
//      任何一种解析退化都会表现为"平板上一把都找不到"，现场很难定位。
//   3. 自动导入会**直接写** keys.txt，误报的代价是连接多等一轮超时 —— 宁可少收，
//      不可乱收。
// ============================================================================

#include "official_key_import.hpp"

#include "test_util.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using go2::KeyImportOptions;
using go2::OfficialKeyHit;

namespace {

const char* kKeyA = "9f3c1a7b52d84e60c1b9a4f7d20e6b83";
const char* kKeyB = "c07b5e19a3d846f2b91c7e50ad63842f";
const char* kSn = "B42D1000P4J815C5";

const OfficialKeyHit* findHit(const std::vector<OfficialKeyHit>& hits, const std::string& k) {
    for (const auto& h : hits)
        if (h.key == k) return &h;
    return nullptr;
}

int countOf(const std::vector<OfficialKeyHit>& hits, const std::string& k) {
    int n = 0;
    for (const auto& h : hits)
        if (h.key == k) ++n;
    return n;
}

void writeFile(const fs::path& p, const std::string& data) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f.write(data.data(), static_cast<std::streamsize>(data.size()));
}

std::string u8(const fs::path& p) { return p.u8string(); }

}  // namespace

int main() {
    // ---------------------------------------------------------------- JSON
    {
        const std::string blob = std::string(R"({"code":0,"devices":[{"sn":")") + kSn +
                                 R"(","key":")" + kKeyA + R"(","note":"Pro"}]})";
        const auto hits = go2::parseOfficialKeyBlob("/x/keys.json", blob);
        const auto* h = findHit(hits, kKeyA);
        CHECK_MSG(h != nullptr, "标准 devices JSON 里的 key 必须被识别");
        if (h) {
            CHECK(h->sn == kSn);
            CHECK(h->format == "json");
            CHECK(h->score >= 60);
        }
    }

    // ------------------------------------------------ requestId 不是钥匙（回归）
    {
        const std::string blob =
            std::string(R"({"requestId":")") + kKeyA + R"(","url":"https://robot-api"})";
        const auto hits = go2::parseOfficialKeyBlob("/x/log.txt", blob);
        CHECK_MSG(hits.empty(), "requestId 的 32hex 必须被丢弃（官方日志里每天几千条）");
    }
    {
        // traceId / nonce 同理
        const std::string blob = std::string(R"({"traceId":")") + kKeyB + R"("})";
        CHECK(go2::parseOfficialKeyBlob("/x/log.txt", blob).empty());
    }

    // ------------------------------------------- SharedPreferences XML（含 SN 配对）
    {
        const std::string blob =
            "<?xml version='1.0' encoding='utf-8' standalone='yes' ?>\n<map>\n"
            "  <string name=\"device_sn\">" + std::string(kSn) + "</string>\n"
            "  <string name=\"aes_key\">" + std::string(kKeyB) + "</string>\n"
            "</map>\n";
        const auto hits = go2::parseOfficialKeyBlob(
            "/data/data/com.unitree.doggo2/shared_prefs/devices.xml", blob);
        const auto* h = findHit(hits, kKeyB);
        CHECK_MSG(h != nullptr, "Prefs XML 里的 aes_key 必须被识别");
        if (h) {
            CHECK(h->format == "prefs-xml");
            // SN 与本条记录隔着别的键 —— 靠"同文件唯一 SN"配对，而不是文本距离
            CHECK(h->sn == kSn);
        }
    }

    // ---------------------------------------------------------------- SQLite
    {
        std::string blob = "SQLite format 3";
        blob.push_back('\0');
        blob += std::string(96, '\x01');
        blob += std::string("...") + "\"aes_key\":\"" + kKeyA + "\",...";
        const auto hits = go2::parseOfficialKeyBlob(
            "/data/data/com.unitree.doggo2/databases/devices.db", blob);
        const auto* h = findHit(hits, kKeyA);
        CHECK_MSG(h != nullptr, "SQLite 二进制里的明文 key 必须被扫出来");
        if (h) CHECK(h->format == "sqlite");
    }

    // ---------------------------------------------------------------- LevelDB
    {
        std::string blob("\x00\x01\xff\xfe", 4);
        blob += std::string(R"({"aesKey":")") + kKeyB + R"("})";
        const auto hits =
            go2::parseOfficialKeyBlob("/x/Local Storage/leveldb/000005.ldb", blob);
        const auto* h = findHit(hits, kKeyB);
        CHECK_MSG(h != nullptr, "LevelDB/localStorage 里的 key 必须被识别");
        if (h) CHECK(h->format == "leveldb");
    }

    // ------------------------------------------------- UTF-16LE（部分 SQLite/工具导出）
    {
        const std::string utf8 = std::string(R"({"aes_key":")") + kKeyA + R"("})";
        std::string u16;
        for (char c : utf8) {
            u16.push_back(c);
            u16.push_back('\0');
        }
        const auto hits = go2::parseOfficialKeyBlob("/x/utf16.json", u16);
        CHECK_MSG(findHit(hits, kKeyA) != nullptr, "UTF-16LE 内容要先归一化再扫");
    }

    // ---------------------------------------------------------------- 假钥匙过滤
    {
        CHECK(go2::parseOfficialKeyBlob("/x/a.txt", "key=00000000000000000000000000000000")
                  .empty());
        CHECK(go2::parseOfficialKeyBlob("/x/a.txt", "key=0123456789abcdef0123456789abcdef")
                  .empty());
        CHECK(go2::parseOfficialKeyBlob("/x/a.txt", "key=11111111111111111111111111111111")
                  .empty());
    }

    // ---------------------------------------------------------------- 掩码
    {
        CHECK(go2::maskKey(kKeyA) == "9f3c…6b83");
        CHECK(go2::maskKey("short") == "****");
    }

    // ---------------------------------------------------------------- 目录扫描
    {
        const fs::path root = fs::temp_directory_path() / "go2_key_import_test";
        std::error_code ec;
        fs::remove_all(root, ec);

        // 同一把 key 出现在两个文件 → 结果里只能有一条
        writeFile(root / "import" / "a.json",
                  std::string(R"({"devices":[{"sn":")") + kSn + R"(","key":")" + kKeyA +
                      R"("}]})");
        writeFile(root / "import" / "sub" / "b.xml",
                  "<map><string name=\"aes_key\">" + std::string(kKeyA) +
                      "</string></map>");
        writeFile(root / "import" / "c.txt",
                  std::string("aes_key=") + kKeyB + "\n");

        KeyImportOptions opt;
        opt.extraRoots = {u8(root)};
        opt.onlyExtraRoots = true;
        const auto rep = go2::scanOfficialAppKeys(opt);

        CHECK(rep.anyRootExists);
        CHECK(rep.filesScanned >= 3);
        CHECK_MSG(countOf(rep.hits, kKeyA) == 1, "同一把 key 在多文件出现时必须去重");
        CHECK_MSG(findHit(rep.hits, kKeyB) != nullptr, "纯文本里的 key 也要收");
        if (!rep.hits.empty())
            CHECK_MSG(rep.hits.front().score >= rep.hits.back().score, "结果按置信度降序");
        CHECK(!rep.notes.empty());

        // 不存在的路径：不崩、不报假命中
        KeyImportOptions bad;
        bad.extraRoots = {"/definitely/not/a/real/dir"};
        bad.onlyExtraRoots = true;
        const auto rep2 = go2::scanOfficialAppKeys(bad);
        CHECK(rep2.hits.empty());
        CHECK(!rep2.anyRootExists);

        // maxFiles 上限生效（扫描不会把整个存储拖死）
        KeyImportOptions capped;
        capped.extraRoots = {u8(root)};
        capped.onlyExtraRoots = true;
        capped.maxFiles = 1;
        const auto rep3 = go2::scanOfficialAppKeys(capped);
        CHECK(rep3.filesScanned <= 1);

        // 超大文件直接跳过
        writeFile(root / "import" / "huge.txt", std::string(8192, 'x'));
        KeyImportOptions small;
        small.extraRoots = {u8(root)};
        small.onlyExtraRoots = true;
        small.maxFileBytes = 1024;
        const auto rep4 = go2::scanOfficialAppKeys(small);
        CHECK(rep4.filesScanned >= 1);  // 小文件仍读，大文件被跳过
        CHECK(findHit(rep4.hits, kKeyB) != nullptr);

        fs::remove_all(root, ec);
    }

    return go2test::summary("official_key_import_test");
}
