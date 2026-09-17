// ============================================================================
// 文件：include/preprocess/preprocess_config.h
// 作用：定义编辑步骤的数据模型 PreprocessType / PreprocessConfig。
// ============================================================================
#pragma once

#include <string>
#include <variant>

namespace idc::preprocess {

// ---------------------------------------------------------------------------
// PreprocessType：预处理操作类型枚举
// 分块依据：一个枚举值 = 一种独立的预处理动作，与 PreprocessConfig 变体的一个分支一一对应。
// ---------------------------------------------------------------------------
enum class PreprocessType {
    GRAY,    // 灰度化（黑白）
    INVERT,  // 按通道反色
    SPLIT,   // 按通道分离
    ROTATE,  // 旋转（90 / 180 / 270）
    FLIP,    // 翻转（水平 / 垂直）
};

// 将 PreprocessType 转换为字符串，便于日志与序列化。
const char* preprocessTypeName(PreprocessType t);

// ---------------------------------------------------------------------------
// 各预处理操作类型的参数结构（用 std::variant 组合为 PreprocessConfig）
// 分块依据：一个 struct = 一种操作类型；成员即该操作的必要参数。
// ---------------------------------------------------------------------------

// 灰度化：无参数。
struct GrayOp {};

// 通道分离：mask 为长度 3 的字符串（"1" 保留、"0" 置零）。灰度图无 R/G/B，返回副本不变。
struct SplitOp { std::string mask = "111"; };

// 通道反色：mask 为长度 3 的字符串（"1" 反色、"0" 保留）。灰度图对单一亮度通道取反（忽略 mask）。
struct InvertOp { std::string mask = "111"; };

// 旋转：angle 单位为度，仅支持 90 的整数倍。
struct RotateOp { int angle = 0; };

// 翻转：horizontal = true 为左右翻转，false 为上下翻转。
struct FlipOp { bool horizontal = true; };

// ---------------------------------------------------------------------------
// PreprocessConfig：使用 std::variant 组合所有操作类型
// 顺序即为 variant index 顺序（GrayOp=0, SplitOp=1, InvertOp=2, RotateOp=3,
// FlipOp=4），修改时请同步更新相关测试。
// ---------------------------------------------------------------------------
using PreprocessConfig = std::variant<
    GrayOp, SplitOp, InvertOp, RotateOp, FlipOp
>;

// 从 PreprocessType 构造一个默认参数的 PreprocessConfig，便于用户先添加再调参。
PreprocessConfig makeDefaultConfig(PreprocessType t);

// 获取 PreprocessConfig 的类型标签（用于分派）。
PreprocessType configType(const PreprocessConfig& cfg);

} // namespace idc::preprocess
