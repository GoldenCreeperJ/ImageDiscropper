// ============================================================================
// 文件：src/core/color.cpp
// 作用：实现 include/core/color.h 声明的颜色运算与 RGB ↔ HSV 转换。
// ============================================================================
#include "core/color.h"

#include <algorithm>
#include <cmath>

namespace idc::core {

// 反色：每个 RGB 分量取 255 - v，alpha 保留。
Color inverse(const Color& c) {
    return Color(
        static_cast<std::uint8_t>(255 - c.r),
        static_cast<std::uint8_t>(255 - c.g),
        static_cast<std::uint8_t>(255 - c.b),
        c.a);
}

// 感知反色：BT.709 亮度公式 0.2126R + 0.7152G + 0.0722B，
// 大于 128 视为亮色，返回黑；否则返回白。
Color perceivedInverse(const Color& c) {
    const double luma = 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b;
    return luma > 128.0 ? Color(kBlack) : Color(kWhite);
}

// 互补色：H 分量旋转 0.5（180°），S、V 保留；alpha 保留。
Color complementary(const Color& c) {
    float h = 0.0f, s = 0.0f, v = 0.0f;
    rgbToHsv(c.r, c.g, c.b, h, s, v);
    h = std::fmod(h + 0.5f, 1.0f);
    std::uint8_t nr = 0, ng = 0, nb = 0;
    hsvToRgb(h, s, v, nr, ng, nb);
    return Color(nr, ng, nb, c.a);
}

// RGB → HSV：采用经典分段算法，输出 h ∈ [0,1)、s ∈ [0,1]、v ∈ [0,1]。
void rgbToHsv(const std::uint8_t r, const std::uint8_t g, const std::uint8_t b,
              float& h, float& s, float& v) {
    const float rf = r / 255.0f;
    const float gf = g / 255.0f;
    const float bf = b / 255.0f;
    const float mx = std::max({rf, gf, bf});
    const float mn = std::min({rf, gf, bf});
    const float d = mx - mn;

    v = mx;
    s = mx == 0.0f ? 0.0f : d / mx;

    if (d == 0.0f) {
        h = 0.0f;
        return;
    }
    if (mx == rf) {
        h = std::fmod((gf - bf) / d, 6.0f);
    } else if (mx == gf) {
        h = (bf - rf) / d + 2.0f;
    } else {
        h = (rf - gf) / d + 4.0f;
    }
    h /= 6.0f;
    if (h < 0.0f) h += 1.0f;
}

// HSV → RGB：经典六分段还原，输入 h ∈ [0,1)。
void hsvToRgb(const float h, const float s, const float v,
              std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) {
    if (s <= 0.0f) {
        const auto gray = static_cast<std::uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f);
        r = g = b = gray;
        return;
    }
    const float hh = std::fmod(h, 1.0f) * 6.0f;
    const int i = static_cast<int>(std::floor(hh));
    const float f = hh - i;
    const float p = v * (1.0f - s);
    const float q = v * (1.0f - s * f);
    const float t = v * (1.0f - s * (1.0f - f));

    float rf = 0, gf = 0, bf = 0;
    switch (i) {
        case 0: rf = v; gf = t; bf = p; break;
        case 1: rf = q; gf = v; bf = p; break;
        case 2: rf = p; gf = v; bf = t; break;
        case 3: rf = p; gf = q; bf = v; break;
        case 4: rf = t; gf = p; bf = v; break;
        default: rf = v; gf = p; bf = q; break;
    }
    r = static_cast<std::uint8_t>(std::clamp(rf, 0.0f, 1.0f) * 255.0f);
    g = static_cast<std::uint8_t>(std::clamp(gf, 0.0f, 1.0f) * 255.0f);
    b = static_cast<std::uint8_t>(std::clamp(bf, 0.0f, 1.0f) * 255.0f);
}

// 颜色打包为 0xAARRGGBB 整数。
std::uint32_t colorToArgb(const Color& c) {
    return static_cast<std::uint32_t>(c.a) << 24 |
           static_cast<std::uint32_t>(c.r) << 16 |
           static_cast<std::uint32_t>(c.g) << 8 |
           static_cast<std::uint32_t>(c.b);
}

// 从 0xAARRGGBB 整数还原颜色。
Color colorFromArgb(const std::uint32_t argb) {
    return Color(
        static_cast<std::uint8_t>(argb >> 16 & 0xFF),
        static_cast<std::uint8_t>(argb >> 8 & 0xFF),
        static_cast<std::uint8_t>(argb & 0xFF),
        static_cast<std::uint8_t>(argb >> 24 & 0xFF));
}

} // namespace idc::core
