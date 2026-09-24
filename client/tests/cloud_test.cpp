// 云 API 连通性自测：无需账号，验证 HTTPS + apk 头 + 签名是否被云接受
// 用法: cloud_test [global|cn]
#include "unitree_cloud.hpp"

#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    const std::string region = argc > 1 ? argv[1] : "global";
    go2::UnitreeCloud cloud(region);
    try {
        cloud.ping();
        std::printf("[PASS] 云 API (%s) 可达，system/pubKey code=100\n", region.c_str());
        return 0;
    } catch (const std::exception& e) {
        std::printf("[FAIL] %s\n", e.what());
        return 1;
    }
}
