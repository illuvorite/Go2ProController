#include "cloud_store.hpp"

#include "local_keys.hpp"  // userDataDir / ensureParentDir

#include <nlohmann/json.hpp>

#include <cstdio>
#include <fstream>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace go2 {

std::string cloudCredentialPath() { return userDataDir() + "/cloud_account.json"; }

bool loadCloudCredential(CloudCredential& out) {
    std::ifstream f(cloudCredentialPath());
    if (!f) return false;
    try {
        const auto j = nlohmann::json::parse(f);
        out.account = j.value("account", "");
        out.password = j.value("password", "");
        out.regionCn = j.value("regionCn", true);
    } catch (...) {
        return false;  // 文件坏了当没有，不阻塞启动
    }
    return !out.account.empty() && !out.password.empty();
}

bool saveCloudCredential(const CloudCredential& c) {
    if (c.account.empty() || c.password.empty()) return false;
    const std::string path = cloudCredentialPath();
    ensureParentDir(path);

    const nlohmann::json j = {
        {"account", c.account}, {"password", c.password}, {"regionCn", c.regionCn}};
    std::ofstream f(path, std::ios::trunc);
    if (!f) return false;
    f << j.dump(2) << "\n";
    f.close();

#ifndef _WIN32
    // 只有本应用能读；失败也不影响功能，只是权限没收紧
    ::chmod(path.c_str(), 0600);
#endif
    return true;
}

bool clearCloudCredential() { return std::remove(cloudCredentialPath().c_str()) == 0; }

}  // namespace go2
