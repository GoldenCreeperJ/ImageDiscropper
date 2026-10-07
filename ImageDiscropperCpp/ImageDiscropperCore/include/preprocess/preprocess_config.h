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
    RESIZE,  // 尺寸调整（目标宽高，重采样）
};

// 将 PreprocessType 转换为字符串，便于日志与序列化。
const char* preprocessTypeName(PreprocessType t);

// ---------------------------------------------------------------------------
// 各预处理操作类型的参数结构（用 std::variant 组合为 PreprocessConfig）
// 分块依据：一个 struct = 一种操作类型；成员即该操作的必要参数。
// ---------------------------------------------------------------------------

// 灰度化：无参数。
struct GrayOp {
    bool operator==(const GrayOp&) const { return true; }
};

// 通道分离：mask 为长度 3 的字符串（"1" 保留、"0" 置零）。灰度图无 R/G/B，返回副本不变。
struct SplitOp {
    std::string mask = "111";
    bool operator==(const SplitOp& o) const { return mask == o.mask; }
};

// 通道反色：mask 为长度 3 的字符串（"1" 反色、"0" 保留）。灰度图：mask 全 "0"（"000"）为空操作，否则逐字节取反（255-v）。
struct InvertOp {
    std::string mask = "111";
    bool operator==(const InvertOp& o) const { return mask == o.mask; }
};

// 旋转：angle 单位为度，仅支持 90 的整数倍。
struct RotateOp {
    int angle = 0;
    bool operator==(const RotateOp& o) const { return angle == o.angle; }
};

// 翻转：horizontal = true 为左右翻转，false 为上下翻转。
struct FlipOp {
    bool horizontal = true;
    bool operator==(const FlipOp& o) const { return horizontal == o.horizontal; }
};

// 尺寸调整：目标宽高（像素）。非正尺寸视为无操作（apply 时原样返回）。
struct ResizeOp {
    int width = 0;
    int height = 0;
    bool operator==(const ResizeOp& o) const { return width == o.width && height == o.height; }
};

// ---------------------------------------------------------------------------
// PreprocessConfig：使用 std::variant 组合所有操作类型
// 顺序即为 variant index 顺序（GrayOp=0, SplitOp=1, InvertOp=2, RotateOp=3,
// FlipOp=4, ResizeOp=5），修改时请同步更新相关测试。
// 不透明度已从本管线移除（旧 OpacityOp）：改由 canvas::ImageTransform.opacity 承载（合成期 alpha 乘子）。
// ---------------------------------------------------------------------------
using PreprocessConfig = std::variant<
    GrayOp, SplitOp, InvertOp, RotateOp, FlipOp, ResizeOp
>;

// 从 PreprocessType 构造一个默认参数的 PreprocessConfig，便于用户先添加再调参。
PreprocessConfig makeDefaultConfig(PreprocessType t);

// 获取 PreprocessConfig 的类型标签（用于分派）。
PreprocessType configType(const PreprocessConfig& cfg);

// 轻操作分类（几何 / 色彩）：Rotate/Flip = 几何，Gray/Split/Invert = 色彩，Resize 二者皆非。
// 单一真相源——Core 执行层融合（applyExec 计划构建 buildExecPlan）、控制器不变式（appendLightKeepingInvariant）、面板双列表下标映射均以此为准，口径不漂移。
bool isGeometryStep(const PreprocessConfig& cfg);
bool isColorStep(const PreprocessConfig& cfg);

} // namespace idc::preprocess
