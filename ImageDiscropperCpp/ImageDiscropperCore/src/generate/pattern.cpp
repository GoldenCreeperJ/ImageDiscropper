// ============================================================================
// 文件：src/generate/pattern.cpp
// 作用：实现 makePattern 与各图案类型的逐像素生成（见同名头文件说明）。
// 分块依据：先按类型铺一张底色（Solid 直接返回），再对纹理类逐像素判定前景/背景；
//           渐变类走颜色插值，图案类走整除/取模或极坐标判定。所有输出恒为 RGBA。
// ============================================================================
#include "generate/pattern.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace idc::generate {

const char* patternKindName(const PatternKind kind) {
    switch (kind) {
        case PatternKind::Solid:          return "纯色";
        case PatternKind::LinearGradient: return "线性渐变";
        case PatternKind::RadialGradient: return "径向渐变";
        case PatternKind::Checkerboard:   return "棋盘格";
        case PatternKind::Stripes:        return "条纹";
        case PatternKind::GridLines:      return "网格线";
        case PatternKind::DotGrid:        return "点阵";
        case PatternKind::Concentric:     return "同心环";
        case PatternKind::RadialRays:     return "放射线";
        case PatternKind::HueGradient:   return "色相渐变";
        case PatternKind::Noise:          return "噪声";
    }
    return "纯色";
}

namespace {

// 本地 π 常量（MSVC 需 _USE_MATH_DEFINES 才有 M_PI，这里自持避免依赖）。
constexpr double kPi = 3.14159265358979323846;

// 在 [0,1] 参数 t 下对两色逐通道（含 alpha）线性插值，四舍五入到 8 位。
core::Color lerp(const core::Color& a, const core::Color& b, double t) {
    t = std::clamp(t, 0.0, 1.0);
    auto chan = [t](const std::uint8_t x, const std::uint8_t y) {
        return static_cast<std::uint8_t>(std::lround(x + (static_cast<double>(y) - x) * t));
    };
    return core::Color(chan(a.r, b.r), chan(a.g, b.g), chan(a.b, b.b), chan(a.a, b.a));
}

// 向下取整除（floor division）：p>0 时对负 v 也给出正确的带号（供斜纹奇偶判定）。
int floorDiv(const int v, const int p) {
    int q = v / p;
    if (v % p != 0 && v < 0) --q;   // p>0（已钳 ≥2），余数符号同被除数。
    return q;
}

// 确定性 32 位整数哈希（splitmix/murmur 混合常量）：同 (seed,x,y,salt) 恒同值，
//   与迭代顺序无关、跨编译器/跨语言（Rust 按同常量同位宽复现）可逐像素对拍。
std::uint32_t hash32(const std::uint32_t seed, const int x, const int y, const std::uint32_t salt) {
    std::uint32_t h = seed
                      ^ (static_cast<std::uint32_t>(x) * 0x9E3779B9u)
                      ^ (static_cast<std::uint32_t>(y) * 0x85EBCA6Bu)
                      ^ (salt * 0xC2B2AE35u);
    h ^= h >> 16; h *= 0x7FEB352Du;
    h ^= h >> 15; h *= 0x84D297ADu;
    h ^= h >> 16;
    return h;
}

// source-over 合成已抽至 core::compositeOver（include/core/composite.h），与 canvas::composeCanvas 共用同一真相源。
//   本模块不再直接使用：「贴图图案」已移除，图片落位统一走 composeCanvas。

} // namespace

core::Image makePattern(int width, int height, const PatternParams& params) {
    // 非法尺寸：宽或高 <= 0，或任一边超上限，或总像素超上限 → 空图（均在分配前短路，不至 OOM）。
    if (width <= 0 || height <= 0 || width > kMaxDimension || height > kMaxDimension ||
        static_cast<long long>(width) * static_cast<long long>(height) > kMaxTotalPixels) {
        return core::Image{};
    }

    core::Image img(width, height, core::ImageFormat::RGBA);
    const core::Color bg = params.background;
    const core::Color fg = params.foreground;

    // 参数钳到合法下限。
    const int period = std::max(2, params.period);
    const int lineWidth = std::max(1, params.lineWidth);
    const int dotRadius = std::max(1, params.dotRadius);

    // 底色先铺满：Solid 即终点，纹理类在此基础上叠前景。
    img.fill(bg);
    if (params.kind == PatternKind::Solid) return img;

    const double cx = (width - 1) / 2.0;
    const double cy = (height - 1) / 2.0;

    switch (params.kind) {
        case PatternKind::LinearGradient: {
            const int span = params.orientation == Orientation::Horizontal ? width : height;
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const double t = span > 1 ? (params.orientation == Orientation::Horizontal ? x : y) / double(span - 1) : 0.0;
                    img.setPixel(x, y, lerp(bg, fg, t));
                }
            }
            break;
        }
        case PatternKind::RadialGradient: {
            const double maxR = std::hypot(cx, cy);
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const double r = std::hypot(x - cx, y - cy);
                    img.setPixel(x, y, lerp(bg, fg, maxR > 0 ? r / maxR : 0.0));
                }
            }
            break;
        }
        case PatternKind::Checkerboard: {
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    if ((x / period + y / period) % 2 != 0) img.setPixel(x, y, fg);
                }
            }
            break;
        }
        case PatternKind::Stripes: {
            // 条纹四向：水平=竖条(随 x)、垂直=横条(随 y)、斜↘(沿 x−y)、斜↗(沿 x+y)。
            const Orientation o = params.orientation;
            const bool diag = (o == Orientation::DiagonalDown || o == Orientation::DiagonalUp);
            const bool alongX = (o == Orientation::Horizontal);
            const bool mainDiag = (o == Orientation::DiagonalDown);   // ↘ 沿 (x−y)、↗ 沿 (x+y)
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    int band;
                    if (diag) {
                        const int v = mainDiag ? (x - y) : (x + y);   // ↗ 恒 ≥0，↘ 可负→floorDiv
                        band = floorDiv(v, period);
                    } else {
                        band = (alongX ? x : y) / period;             // 直向坐标恒 ≥0
                    }
                    if (band & 1) img.setPixel(x, y, fg);
                }
            }
            break;
        }
        case PatternKind::GridLines: {
            const int half = lineWidth / 2;
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const bool lineX = std::min(x % period, period - x % period) <= half;
                    if (const bool lineY = std::min(y % period, period - y % period) <= half; lineX || lineY) img.setPixel(x, y, fg);
                }
            }
            break;
        }
        case PatternKind::DotGrid: {
            const double r2 = static_cast<double>(dotRadius) * dotRadius;
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const double gx = std::round(x / static_cast<double>(period)) * period;
                    const double gy = std::round(y / static_cast<double>(period)) * period;
                    const double dx = x - gx;
                    if (const double dy = y - gy; dx * dx + dy * dy <= r2) {   // 平方距离比较，免重复开方（原 hypot×hypot）。
                        img.setPixel(x, y, fg);
                    }
                }
            }
            break;
        }
        case PatternKind::Concentric: {
            // 同心环两形：水平=圆环(欧氏 hypot)、垂直=方环(切比雪夫 max，回字纹)。
            const double half = lineWidth / 2.0;
            const bool square = params.orientation == Orientation::Vertical;
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const double r = square ? std::max(std::fabs(x - cx), std::fabs(y - cy))
                                             : std::hypot(x - cx, y - cy);
                    const double frac = r - std::floor(r / period) * period;   // 距本环起点
                    if (const double distToNode = std::min(frac, period - frac); distToNode <= half) img.setPixel(x, y, fg);
                }
            }
            break;
        }
        case PatternKind::RadialRays: {
            const double refR = 0.5 * std::min(width, height);
            constexpr double twoPi = 2.0 * kPi;
            const int sectors = std::max(4, static_cast<int>(std::lround(twoPi * refR / period)));
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    double ang = std::atan2(y - cy, x - cx);
                    if (ang < 0) ang += twoPi;
                    if (const int sector = static_cast<int>(ang / twoPi * sectors); sector % 2 != 0) img.setPixel(x, y, fg);
                }
            }
            break;
        }
        case PatternKind::HueGradient: {
            const bool alongX = params.orientation == Orientation::Horizontal;
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const int coord = alongX ? x : y;
                    double hue = std::fmod(coord / static_cast<double>(period), 1.0);   // period＝一道彩虹的像素宽
                    if (hue < 0) hue += 1.0;
                    std::uint8_t r, g, b;
                    core::hsvToRgb(static_cast<float>(hue), 1.0f, 1.0f, r, g, b);        // 全饱和全明度彩虹
                    img.setPixel(x, y, core::Color(r, g, b, 255));
                }
            }
            break;
        }
        case PatternKind::Noise: {
            const std::uint32_t sd = static_cast<std::uint32_t>(std::max(0, params.seed));
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    switch (params.noiseMode) {
                        case NoiseMode::FullColor:   // 三通道各独立随机
                            img.setPixel(x, y, core::Color(
                                static_cast<std::uint8_t>(hash32(sd, x, y, 0)),
                                static_cast<std::uint8_t>(hash32(sd, x, y, 1)),
                                static_cast<std::uint8_t>(hash32(sd, x, y, 2)), 255));
                            break;
                        case NoiseMode::Grayscale: { // 单通道随机灰阶
                            const auto v = static_cast<std::uint8_t>(hash32(sd, x, y, 0));
                            img.setPixel(x, y, core::Color(v, v, v, 255));
                            break;
                        }
                        case NoiseMode::TwoColor:    // 两色随机雪点（底色已铺满，奇数处叠前景）
                            if (hash32(sd, x, y, 0) & 1u) img.setPixel(x, y, fg);
                            break;
                    }
                }
            }
            break;
        }
        case PatternKind::Solid:
            break; // 已在上方返回。
    }
    return img;
}

} // namespace idc::generate
