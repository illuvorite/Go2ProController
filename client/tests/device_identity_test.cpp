// ============================================================================
// 「设备身份 / 命名」自测 —— 同一台狗换 WiFi 之后必须还是**同一台、同一个名字**。
//
// 为什么值得测：2026-10-20 修的 bug 就是这个 ——
//   设备身份和名称都挂在 **IP** 上，而 IP 会随 WiFi / DHCP 变。
//   同一台狗在 WiFi A 下是 192.168.2.107、在 WiFi B 下是 192.168.2.161：
//     · 名字表查不到新 IP → 界面显示裸 IP → **同一台狗显示成两个不同的名字**；
//     · 设备列表按 IP 去重 → 多出一条 → 列表里同一台狗出现两次。
//   修法：身份与命名都改用**设备键**（SN 优先，跨网络稳定），IP 只当"当前在哪"。
//
// ★ 它不替代真机验证：这里只覆盖"身份/命名的纯逻辑"，不碰网络。
// ============================================================================

#include "test_util.hpp"
#include "ui.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace go2;

namespace {

const char* kSnA = "B42D2000P6CDL807";
const char* kSnB = "B42D2000P6CDL808";

/// 在临时目录里跑（UiState 的名字文件/设备文件都写在**当前工作目录**）
class TempCwd {
public:
    explicit TempCwd(const std::string& name) {
        old_ = std::filesystem::current_path();
        dir_ = old_ / ("_tmp_" + name);
        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);
        std::filesystem::create_directories(dir_, ec);
        std::filesystem::current_path(dir_);
    }
    ~TempCwd() {
        std::error_code ec;
        std::filesystem::current_path(old_, ec);
        std::filesystem::remove_all(dir_, ec);
    }

private:
    std::filesystem::path old_;
    std::filesystem::path dir_;
};

std::size_t countRobotsWithSn(UiState& ui, const std::string& sn) {
    std::size_t n = 0;
    for (const auto& r : ui.robotsSnapshot())
        if (r.sn == sn) ++n;
    return n;
}

}  // namespace

int main() {
    // ---------------------------------------------------------- 1. 换 IP 后名字不变
    {
        TempCwd cwd("devid1");
        UiState ui;
        // 旧网络：192.168.2.107，多播发现拿到了 SN
        ui.addOrUpdate("192.168.2.107", false, kSnA);
        ui.setName("192.168.2.107", "gopro2");
        CHECK_MSG(ui.labelOf("192.168.2.107") == std::string("gopro2"), "起名后应显示自定义名");

        // 换到新网络：同一台狗，同一个 SN，IP 变了
        const bool needConnect = ui.addOrUpdate("192.168.2.161", false, kSnA);
        CHECK_MSG(needConnect, "IP 变了 → 返回 true，调用方要连新 IP");

        const auto robots = ui.robotsSnapshot();
        CHECK_MSG(robots.size() == 1, "同一台狗换 IP 后列表里必须还是一条");
        if (!robots.empty()) {
            CHECK_MSG(robots[0].ip == std::string("192.168.2.161"), "条目应跟着 IP 走");
            CHECK_MSG(robots[0].sn == std::string(kSnA), "SN 不变");
        }
        CHECK_MSG(countRobotsWithSn(ui, kSnA) == 1, "同一个 SN 只应有一条");
        CHECK_MSG(ui.labelOf("192.168.2.161") == std::string("gopro2"),
                  "★ 核心断言：换 WiFi 之后名字必须还是同一个");
    }

    // ---------------------------------------------------------- 2. 先没 SN 起名 → 补上 SN → 再换 IP
    {
        TempCwd cwd("devid2");
        UiState ui;
        // 手动填 IP 添加的设备：没有 SN → 名字只能先记在 IP 上
        ui.addOrUpdate("192.168.2.107", true);
        ui.setName("192.168.2.107", "gopro2");
        CHECK(ui.labelOf("192.168.2.107") == std::string("gopro2"));

        // 之后扫描/握手拿到了 SN → 名字要迁到 SN 上
        ui.addOrUpdate("192.168.2.107", false, kSnA);
        CHECK_MSG(ui.nameOf("192.168.2.107") == std::string("gopro2"), "补上 SN 后名字不能丢");

        // 换 IP
        ui.addOrUpdate("192.168.2.161", false, kSnA);
        CHECK_MSG(ui.labelOf("192.168.2.161") == std::string("gopro2"),
                  "名字迁到 SN 之后，换 IP 也认得出是同一台");
        CHECK(ui.robotsSnapshot().size() == 1);
    }

    // ---------------------------------------------------------- 3. 两台不同的狗互不影响
    {
        TempCwd cwd("devid3");
        UiState ui;
        ui.addOrUpdate("192.168.2.107", false, kSnA);
        ui.addOrUpdate("192.168.2.108", false, kSnB);
        ui.setName("192.168.2.107", "狗A");
        ui.setName("192.168.2.108", "狗B");
        CHECK(ui.robotsSnapshot().size() == 2);
        CHECK(ui.labelOf("192.168.2.107") == std::string("狗A"));
        CHECK(ui.labelOf("192.168.2.108") == std::string("狗B"));
        // A 换网络，不能牵连 B
        ui.addOrUpdate("192.168.2.161", false, kSnA);
        CHECK(ui.robotsSnapshot().size() == 2);
        CHECK(ui.labelOf("192.168.2.161") == std::string("狗A"));
        CHECK(ui.labelOf("192.168.2.108") == std::string("狗B"));
    }

    // ---------------------------------------------------------- 4. 同一台狗同时出现在两个 IP
    {
        TempCwd cwd("devid4");
        UiState ui;
        ui.addOrUpdate("192.168.2.107", false, kSnA);
        ui.addOrUpdate("192.168.2.161", false, kSnA);  // 同一台狗，另一个网卡上的 IP
        CHECK_MSG(ui.robotsSnapshot().size() == 1,
                  "同一台狗在两个 IP 上也只留一条（机器狗同一时刻只允许一条控制连接）");
        CHECK(ui.labelOf("192.168.2.161") == std::string("192.168.2.161"));
    }

    // ---------------------------------------------------------- 5. 合并时受控勾选状态转移
    {
        TempCwd cwd("devid5");
        UiState ui;
        ui.addOrUpdate("192.168.2.107", false, kSnA);
        ui.setSelected("192.168.2.107", true);
        CHECK(ui.isSelected("192.168.2.107"));
        ui.addOrUpdate("192.168.2.161", false, kSnA);
        CHECK_MSG(ui.isSelected("192.168.2.161"), "合并后受控勾选状态要跟过去，别把狗丢在受控集合外");
    }

    // ---------------------------------------------------------- 6. 没起名 → 显示 IP
    {
        TempCwd cwd("devid6");
        UiState ui;
        ui.addOrUpdate("192.168.2.107", false, kSnA);
        CHECK(ui.labelOf("192.168.2.107") == std::string("192.168.2.107"));
        CHECK(ui.nameOf("192.168.2.107").empty());
    }

    // ---------------------------------------------------------- 7. 改名后清除 → 回到 IP
    {
        TempCwd cwd("devid7");
        UiState ui;
        ui.addOrUpdate("192.168.2.107", false, kSnA);
        ui.setName("192.168.2.107", "gopro2");
        CHECK(ui.labelOf("192.168.2.107") == std::string("gopro2"));
        ui.setName("192.168.2.107", "");  // 清除名字
        CHECK_MSG(ui.labelOf("192.168.2.107") == std::string("192.168.2.107"),
                  "清空名字后要回到显示 IP（不能被残留的旧记录挡住）");
    }

    // ---------------------------------------------------------- 8. 老版本 robot_names.json（裸 IP 键）能读
    {
        TempCwd cwd("devid8");
        {
            std::ofstream f("robot_names.json");
            f << R"({"192.168.2.107": "gopro2", "192.168.2.108": "狗B"})";
        }
        UiState ui;
        ui.loadNames();
        // 老文件里是"按 IP 记"的名字：IP 还没换成新的之前，名字照样认得出
        ui.addOrUpdate("192.168.2.107", false, kSnA);
        CHECK_MSG(ui.labelOf("192.168.2.107") == std::string("gopro2"),
                  "老格式的裸 IP 键必须还能用（老文件不许要求用户手工迁移）");
        // 而且补上 SN 之后它会被迁到 SN 键 → 换 IP 也认得出
        ui.addOrUpdate("192.168.2.161", false, kSnA);
        CHECK_MSG(ui.labelOf("192.168.2.161") == std::string("gopro2"),
                  "老文件里的名字应自动迁到 SN 上，换 IP 后依然认得出");
    }

    // ---------------------------------------------------------- 9. 老版本 robot_devices.json 里同 SN 两条 → 载入时并成一条
    {
        TempCwd cwd("devid9");
        {
            std::ofstream f("robot_devices.json");
            f << R"([{"ip":"192.168.2.107","sn":"B42D2000P6CDL807"},
                      {"ip":"192.168.2.161","sn":"B42D2000P6CDL807"}])";
        }
        UiState ui;
        ui.loadDevices();
        CHECK_MSG(ui.robotsSnapshot().size() == 1,
                  "老文件里同一台狗的两条残留（换网络前后）→ 载入时就要并成一条");
    }

    // ---------------------------------------------------------- 10. 跨重启：名字与"同一台"都还在
    {
        TempCwd cwd("devid10");
        {
            UiState ui;
            ui.addOrUpdate("192.168.2.107", false, kSnA);
            ui.setName("192.168.2.107", "gopro2");
        }
        // 模拟"重启"：重新构造 UiState，从落盘的两个文件恢复
        {
            UiState ui;
            ui.loadNames();
            ui.loadDevices();
            CHECK_MSG(ui.robotsSnapshot().size() == 1, "设备列表应恢复出 1 条");
            CHECK_MSG(ui.labelOf("192.168.2.107") == std::string("gopro2"),
                      "重启后名字必须还在");
            // 重启后换了网络 → 仍是同一台、同一个名字
            ui.addOrUpdate("192.168.2.161", false, kSnA);
            CHECK_MSG(ui.robotsSnapshot().size() == 1, "换网络后仍只有一条");
            CHECK_MSG(ui.labelOf("192.168.2.161") == std::string("gopro2"),
                      "★ 跨重启 + 换网络，名字还是同一个");
        }
    }

    return go2test::summary("device_identity_test");
}
