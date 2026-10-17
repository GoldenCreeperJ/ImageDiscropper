// ============================================================================
// 文件：src/preprocess/preprocess_config.cpp
// 作用：实现 PreprocessConfig 数据模型的辅助函数：枚举 ↔ 字符串映射、
//       默认配置构造、类型标签提取。
// ============================================================================
#include "preprocess/preprocess_config.h"

#include <type_traits>

namespace idc::preprocess {

// PreprocessType → 字符串（日志 / 序列化显示名，串值固定）。
const char* preprocessTypeName(const PreprocessType t) {
    switch (t) {
        case PreprocessType::Gray: return "Gray";
        case PreprocessType::Invert: return "Invert";
        case PreprocessType::Split: return "Split";
        case PreprocessType::Rotate: return "Rotate";
        case PreprocessType::Flip: return "Flip";
        case PreprocessType::Resize: return "Resize";
    }
    return "Unknown";
}

// 构造指定类型的默认参数 PreprocessConfig。
PreprocessConfig makeDefaultConfig(const PreprocessType t) {
    switch (t) {
        case PreprocessType::Gray: return GrayOp{};
        case PreprocessType::Invert: return InvertOp{};
        case PreprocessType::Split: return SplitOp{};
        case PreprocessType::Rotate: return RotateOp{};
        case PreprocessType::Flip: return FlipOp{};
        case PreprocessType::Resize: return ResizeOp{};
    }
    return GrayOp{};
}

// 通过 std::visit 提取 PreprocessConfig 的类型标签。
PreprocessType configType(const PreprocessConfig& cfg) {
    return std::visit([]<typename T0>(const T0&) -> PreprocessType {
        using T = std::decay_t<T0>;
        if constexpr (std::is_same_v<T, GrayOp>) return PreprocessType::Gray;
        else if constexpr (std::is_same_v<T, SplitOp>) return PreprocessType::Split;
        else if constexpr (std::is_same_v<T, InvertOp>) return PreprocessType::Invert;
        else if constexpr (std::is_same_v<T, RotateOp>) return PreprocessType::Rotate;
        else if constexpr (std::is_same_v<T, FlipOp>) return PreprocessType::Flip;
        else if constexpr (std::is_same_v<T, ResizeOp>) return PreprocessType::Resize;
        else return PreprocessType::Gray;
    }, cfg);
}

// 几何步分类：旋转 / 翻转（影响像素位置）。
bool isGeometryStep(const PreprocessConfig& cfg) {
    const auto t = configType(cfg);
    return t == PreprocessType::Rotate || t == PreprocessType::Flip;
}

// 色彩步分类：黑白 / 分离 / 反色（不改变像素位置）。Resize 二者皆非。
bool isColorStep(const PreprocessConfig& cfg) {
    const auto t = configType(cfg);
    return t == PreprocessType::Gray || t == PreprocessType::Split || t == PreprocessType::Invert;
}

} // namespace idc::preprocess
