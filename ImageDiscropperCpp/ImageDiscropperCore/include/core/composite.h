// ============================================================================
// 文件：include/core/composite.h
// 作用：核心像素合成原语——source-over（非预乘 alpha）逐通道混合，供「图案生成」
//       （generate::makePattern 的 Image 落位）与「多层画布合成」（canvas::composeCanvas）
//       共用同一真相源，避免两处各写一份造成混合口径漂移（C++/Rust 亦据同一公式对拍）。
// 分块依据：纯颜色级无状态函数，入 core 命名空间作最底层公共原语（仅依赖 core/color.h）。
// 说明：src 全不透明直接取 src、全透明直接取 dst；其余按标准 alpha 合成逐通道求值并四舍五入到 8 位。
// ============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "core/color.h"

namespace idc::core {

// source-over 合成：把半透明源色 src 盖到目标色 dst 上（非预乘 alpha）。
inline Color compositeOver(const Color& dst, const Color& src) {
    if (src.a == 255) return src;
    if (src.a == 0) return dst;
    const double sa = src.a / 255.0;
    const double da = dst.a / 255.0;
    const double oa = sa + da * (1.0 - sa);
    if (oa <= 0.0) return Color(0, 0, 0, 0);
    auto ch = [&](const std::uint8_t sc, const std::uint8_t dc) {
        const double v = (sc * sa + dc * da * (1.0 - sa)) / oa;
        return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0, 255.0)));
    };
    return Color(ch(src.r, dst.r), ch(src.g, dst.g), ch(src.b, dst.b),
                 static_cast<std::uint8_t>(std::lround(std::clamp(oa * 255.0, 0.0, 255.0))));
}

} // namespace idc::core
