// ============================================================================
// 文件：src/preprocess/preprocess_config.cpp
// 作用：实现 PreprocessConfig 数据模型的辅助函数：枚举 ↔ 字符串映射、
//       默认配置构造、类型标签提取。
// ============================================================================
#include "preprocess/preprocess_config.h"

#include <type_traits>

namespace idc::preprocess {

// PreprocessType → 字符串。
const char* preprocessTypeName(const PreprocessType t) {
    switch (t) {
        case PreprocessType::GRAY: return "Gray";
        case PreprocessType::INVERT: return "Invert";
        case PreprocessType::SPLIT: return "Split";
        case PreprocessType::ROTATE: return "Rotate";
        case PreprocessType::FLIP: return "Flip";
        case PreprocessType::RESIZE: return "Resize";
    }
    return "Unknown";
}

// 构造指定类型的默认参数 PreprocessConfig。
PreprocessConfig makeDefaultConfig(const PreprocessType t) {
    switch (t) {
        case PreprocessType::GRAY: return GrayOp{};
        case PreprocessType::INVERT: return InvertOp{};
        case PreprocessType::SPLIT: return SplitOp{};
        case PreprocessType::ROTATE: return RotateOp{};
        case PreprocessType::FLIP: return FlipOp{};
        case PreprocessType::RESIZE: return ResizeOp{};
    }
    return GrayOp{};
}

// 通过 std::visit 提取 PreprocessConfig 的类型标签。
PreprocessType configType(const PreprocessConfig& cfg) {
    return std::visit([]<typename T0>(const T0& v) -> PreprocessType {
        using T = std::decay_t<T0>;
        if constexpr (std::is_same_v<T, GrayOp>) return PreprocessType::GRAY;
        else if constexpr (std::is_same_v<T, SplitOp>) return PreprocessType::SPLIT;
        else if constexpr (std::is_same_v<T, InvertOp>) return PreprocessType::INVERT;
        else if constexpr (std::is_same_v<T, RotateOp>) return PreprocessType::ROTATE;
        else if constexpr (std::is_same_v<T, FlipOp>) return PreprocessType::FLIP;
        else if constexpr (std::is_same_v<T, ResizeOp>) return PreprocessType::RESIZE;
        else return PreprocessType::GRAY;
    }, cfg);
}

// 几何步分类：旋转 / 翻转（影响像素位置）。
bool isGeometryStep(const PreprocessConfig& cfg) {
    const auto t = configType(cfg);
    return t == PreprocessType::ROTATE || t == PreprocessType::FLIP;
}

// 色彩步分类：黑白 / 分离 / 反色（不改变像素位置）。Resize 二者皆非。
bool isColorStep(const PreprocessConfig& cfg) {
    const auto t = configType(cfg);
    return t == PreprocessType::GRAY || t == PreprocessType::SPLIT || t == PreprocessType::INVERT;
}

} // namespace idc::preprocess
