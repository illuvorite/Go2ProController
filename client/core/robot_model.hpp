#pragma once

#include <string>

namespace go2 {

/// Go2 机型。用途：有些运动指令只有 Pro / Edu 支持（Air 会被服务端拒绝），
/// 动作库需要按"当前受控设备的机型"来标注 / 隐藏这些指令。
///
/// ★ 判定依据（唯一可靠来源）：宇树机器狗的 **16 位序列号第 5 个字符（下标 4）**：
///     `1` → Go2 Air（入门版）
///     `2` → Go2 Pro（增强版）
///     `4` → Go2 Edu（开发版）
///   例：`B210123456789ABC` 下标 4 = `1` → Air。
///   这条不是从官方文档抄的、而是固件工具链（go2_firmware_tools）里 `get_real_model()`
///   的实测实现；序列号本身来自硬件 uni 分区，**改不了**（软件侧那个可改的 `ver` 文件
///   属于"型号伪装"，用来在 Air 上解锁 Pro 功能，不能当判据）。
///
/// ★ 序列号来源：多播发现（UDP 231.1.1.1:10131 → `{"sn": ..., "ip": ...}`）。
///   手动填 IP 添加的设备拿不到 SN → 机型保持 Unknown，动作库不做任何过滤
///   （宁可不过滤，也不要按错误的机型把能用的动作藏掉）。
enum class RobotModel {
    Unknown = 0,
    Air,  ///< 入门版：高级动作（空翻 / 倒立 / 交叉步 等）会被拒
    Pro,  ///< 增强版
    Edu,  ///< 开发版
};

/// 从序列号解析机型（大小写不敏感的十六进制无关，只看第 5 个字符）。
/// 长度不足 / 字符不是 1/2/4 → RobotModel::Unknown。
RobotModel modelFromSn(const std::string& sn);

/// 机型名。Unknown → 空串（刻意返回空串：界面据此"不显示机型"，
/// 而不是显示一个"未知"占位，免得列表里每张卡片都挂个没用的标签）。
const char* modelName(RobotModel m);

/// 机型名 → 枚举（大小写不敏感；认不出 → Unknown）。用于从落盘文件恢复。
RobotModel modelFromName(const std::string& name);

}  // namespace go2
