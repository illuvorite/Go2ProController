// 发现模块自测：列出本机网段并扫描，验证能找到真机；附带 httplib 细节诊断
#include "discovery.hpp"

#include <httplib.h>

#include <cstdio>

int main() {
    // ---- 1. httplib 直连诊断 ----
    std::printf("=== httplib 直连诊断 ===\n");
    {
        httplib::Client cli("192.168.2.111", 9991);
        cli.set_connection_timeout(2, 0);
        cli.set_read_timeout(2, 0);
        httplib::Result res = cli.Get("/con_notify");
        if (res) {
            std::printf("status=%d body_size=%zu\n", res->status, res->body.size());
            std::printf("body[:100]=%s\n", res->body.substr(0, 100).c_str());
        } else {
            std::printf("GET 失败 error=%d (%s)\n", int(res.error()),
                        httplib::to_string(res.error()).c_str());
        }

        // 对比：不带 keep-alive 再试一次
        cli.set_keep_alive(false);
        httplib::Result res2 = cli.Get("/con_notify");
        std::printf("第二次(无keep-alive): %s\n",
                    res2 ? ("status=" + std::to_string(res2->status) +
                            " body_size=" + std::to_string(res2->body.size()))
                              .c_str()
                         : httplib::to_string(res2.error()).c_str());
    }

    // ---- 2. 网段扫描 ----
    std::printf("\n=== 网段扫描 ===\n");
    const auto subnets = go2::Discovery::localSubnets();
    if (subnets.empty()) {
        std::printf("未找到本机网段\n");
        return 1;
    }
    for (const auto& sn : subnets) std::printf("本机网段: %s\n", sn.c_str());

    for (const auto& sn : subnets) {
        for (const auto& r : go2::Discovery::scanSubnet(sn, 400, [](const std::string& s) {
                 std::printf("[进度] %s\n", s.c_str());
             })) {
            std::printf(">>> 发现 Go2: %s (verified=%d)\n", r.ip.c_str(), r.verified ? 1 : 0);
        }
    }
    return 0;
}
