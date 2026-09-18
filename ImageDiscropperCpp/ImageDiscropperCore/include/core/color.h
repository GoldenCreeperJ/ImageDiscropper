// ============================================================================
// 文件：include/core/color.h
// 作用：定义 RGBA 颜色结构体 Color，以及对应的颜色运算工具函数
//       （反色 / 感知反色 / 互补色 / HSV 转换）。
// ============================================================================
#pragma once

#include <cstdint>

namespace idc::core {

// ---------------------------------------------------------------------------
// Color：8 位 RGBA 颜色
// 说明：分量顺序 R、G、B、A，取值范围 [0, 255]；A = 255 表示完全不透明。
// ---------------------------------------------------------------------------
struct Color {
    std::uint8_t r{0};
    std::uint8_t g{0};
    std::uint8_t b{0};
    std::uint8_t a{255};

    Color() = default;
    constexpr Color(const std::uint8_t r_, const std::uint8_t g_, const std::uint8_t b_, const std::uint8_t a_ = 255)
        : r(r_), g(g_), b(b_), a(a_) {}

    // 判断两个颜色是否完全相同（包含 alpha 分量）。
    constexpr bool operator==(const Color& other) const {
        return r == other.r && g == other.g && b == other.b && a == other.a;
    }
    constexpr bool operator!=(const Color& other) const { return !(*this == other); }
};

// 常用颜色常量。以 inline 变量方式提供，避免多处包含导致重复定义。
inline constexpr Color kBlack{0, 0, 0, 255};
inline constexpr Color kWhite{255, 255, 255, 255};
inline constexpr Color kRed{255, 0, 0, 255};
inline constexpr Color kGreen{0, 255, 0, 255};
inline constexpr Color kBlue{0, 0, 255, 255};
inline constexpr Color kTransparent{0, 0, 0, 0};

// 按分量取反色（保留 alpha）。
Color inverse(const Color& c);

// 感知反色：根据 ITU-R BT.709 亮度公式判断颜色偏亮或偏暗，
// 返回黑或白，用于文本前景色自适应。
Color perceivedInverse(const Color& c);

// 互补色：将 HSV 中的 H 分量旋转 180°，保留 alpha。
Color complementary(const Color& c);

// RGB → HSV 转换，h ∈ [0,1)、s ∈ [0,1]、v ∈ [0,1]。
void rgbToHsv(std::uint8_t r, std::uint8_t g, std::uint8_t b,
              float& h, float& s, float& v);

// HSV → RGB 转换，输出 8 位分量。
void hsvToRgb(float h, float s, float v,
              std::uint8_t& r, std::uint8_t& g, std::uint8_t& b);

// 将颜色打包为 0xAARRGGBB 整数，便于序列化 / 调试。
std::uint32_t colorToArgb(const Color& c);

// 从 0xAARRGGBB 整数还原颜色。
Color colorFromArgb(std::uint32_t argb);

} // namespace idc::core
