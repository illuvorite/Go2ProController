// 机型识别：SN 第 5 位 → Air / Pro / Edu。
//
// 为什么值得单测：这条映射是"动作库按机型过滤"的唯一依据 ——
// 判错一位就会把能用的动作藏掉、或把不能用的摆出来给用户点。
// 判据来自 go2_firmware_tools 的 get_real_model()，不是猜的。
#include "robot_model.hpp"

#include <cstdio>
#include <string>

using go2::modelFromName;
using go2::modelFromSn;
using go2::modelName;
using go2::RobotModel;

static int g_fail = 0;
static int g_total = 0;

static void check(bool ok, const std::string& what) {
    ++g_total;
    if (!ok) {
        ++g_fail;
        std::printf("  [FAIL] %s\n", what.c_str());
    }
}

static void checkModel(const std::string& sn, RobotModel want) {
    const RobotModel got = modelFromSn(sn);
    check(got == want, "sn=" + sn + " → " + modelName(want));
}

int main() {
    std::printf("== 机型识别（SN 第 5 位）==\n");

    // 三个真实格式的样例：**第 5 个字符（下标 4）** 是 1 / 2 / 4
    checkModel("B210123456789ABC", RobotModel::Air);  // 下标 4 = '1'
    checkModel("B210223456789ABC", RobotModel::Pro);  // 下标 4 = '2'
    checkModel("B210423456789ABC", RobotModel::Edu);  // 下标 4 = '4'
    // ★ 反例：同样的数字放在别的位子上**不算数**（只看下标 4）——
    //   写成"扫一遍前 8 位里有没有 1/2/4"就会把这两条判错。
    checkModel("B212023456789ABC", RobotModel::Unknown);
    checkModel("B242023456789ABC", RobotModel::Unknown);
    // 其它数字（0/3/5）不属于 Go2 三款机型 → 不猜
    checkModel("B210323456789ABC", RobotModel::Unknown);  // '3'
    checkModel("B210023456789ABC", RobotModel::Unknown);  // '0'
    checkModel("B210523456789ABC", RobotModel::Unknown);  // '5'
    // 下标 4 不是数字
    checkModel("B210A23456789ABC", RobotModel::Unknown);
    // 大小写无关（有些来源会把 SN 转小写）
    checkModel("b210123456789abc", RobotModel::Air);
    // 长度不足 / 空串：拿不到 SN，绝不能瞎判
    checkModel("", RobotModel::Unknown);
    checkModel("B2", RobotModel::Unknown);
    checkModel("B210", RobotModel::Unknown);
    checkModel("B2101", RobotModel::Air);  // 恰好 5 位（下标 4 存在）→ 判得出来

    std::printf("== 机型名互转 ==\n");
    check(std::string(modelName(RobotModel::Air)) == "Air", "Air 名");
    check(std::string(modelName(RobotModel::Pro)) == "Pro", "Pro 名");
    check(std::string(modelName(RobotModel::Edu)) == "Edu", "Edu 名");
    check(std::string(modelName(RobotModel::Unknown)).empty(), "Unknown 名为空串");
    check(modelFromName("Air") == RobotModel::Air, "名字 Air → Air");
    check(modelFromName("pro") == RobotModel::Pro, "名字小写 pro → Pro");
    check(modelFromName("EDU") == RobotModel::Edu, "名字大写 EDU → Edu");
    check(modelFromName("MAX") == RobotModel::Unknown, "MAX 不是 Go2 三款 → Unknown");
    check(modelFromName("") == RobotModel::Unknown, "空名字 → Unknown");
    // 往返一致
    for (RobotModel m : {RobotModel::Air, RobotModel::Pro, RobotModel::Edu}) {
        check(modelFromName(modelName(m)) == m, std::string("往返 ") + modelName(m));
    }

    std::printf("robot_model_test: %d/%d 通过\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
