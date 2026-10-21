#pragma once

// ============================================================================
// 云账号凭据的本地保存（一次输入，之后自动登录）
//
// 存在**应用数据目录**的 `cloud_account.json`，POSIX 下权限 0600；
// 不进工程目录、不进日志、不进 /api/state。
//
// 为什么要存密码而不是只存 token：宇树没有公开的 refresh 接口约定，
// token 过期（code 1001）后必须重新登录；只存 token 会导致几天后又要手输。
// 存账号密码才能做到"启动即自动拉钥匙"。
//
// 用户可以随时在设置页「清除保存的账号」把它删掉。
// ============================================================================

#include <string>

namespace go2 {

struct CloudCredential {
    std::string account;   // 手机号 / 邮箱
    std::string password;  // 明文（仅存于应用私有目录，0600）
    bool regionCn = true;  // true=国内账号
};

/// `userDataDir()/cloud_account.json`
std::string cloudCredentialPath();

/// 读取；文件不存在 / 解析失败 / 字段为空 → 返回 false
bool loadCloudCredential(CloudCredential& out);

/// 写入（覆盖）。成功后尝试把权限收到 0600。
bool saveCloudCredential(const CloudCredential& c);

/// 删除保存的凭据
bool clearCloudCredential();

}  // namespace go2
