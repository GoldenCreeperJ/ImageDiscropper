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
    }
    return GrayOp{};
}

// 通过 std::visit 提取 PreprocessConfig 的类型标签。
PreprocessType configType(const PreprocessConfig& cfg) {
    return std::visit([](const auto& v) -> PreprocessType {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, GrayOp>) return PreprocessType::GRAY;
        else if constexpr (std::is_same_v<T, SplitOp>) return PreprocessType::SPLIT;
        else if constexpr (std::is_same_v<T, InvertOp>) return PreprocessType::INVERT;
        else if constexpr (std::is_same_v<T, RotateOp>) return PreprocessType::ROTATE;
        else if constexpr (std::is_same_v<T, FlipOp>) return PreprocessType::FLIP;
        else return PreprocessType::GRAY;
    }, cfg);
}

} // namespace idc::preprocess
