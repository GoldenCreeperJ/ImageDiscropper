// ============================================================================
// 文件：src/processing/geometric_ops.cpp
// 作用：实现几何变换算法：90° 系列旋转、水平 / 垂直翻转、最近邻 / 双线性缩放。
//       对应 Kotlin ImageArray.kt 中的 rotate / flip，并补齐任意倍率缩放。
// ============================================================================
#include "processing/geometric_ops.h"

#include <algorithm>
#include <cmath>

namespace idc::processing {

// 内部工具：归一化角度到 [0, 360)。
static int normalizeAngle(const int angle) {
    int a = angle % 360;
    if (a < 0) a += 360;
    return a;
}

// 旋转 90°：新图宽高互换，(x, y) → (h - 1 - y, x)。
static core::Image rotate90(const core::Image& src) {
    const int w = src.width();
    const int h = src.height();
    core::Image out(h, w, src.format());
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            out.setPixel(h - 1 - y, x, src.getPixel(x, y));
        }
    }
    return out;
}

// 旋转 180°：(x, y) → (w - 1 - x, h - 1 - y)。
static core::Image rotate180(const core::Image& src) {
    const int w = src.width();
    const int h = src.height();
    core::Image out(w, h, src.format());
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            out.setPixel(w - 1 - x, h - 1 - y, src.getPixel(x, y));
        }
    }
    return out;
}

// 旋转 270°：等价于逆时针 90°，(x, y) → (y, w - 1 - x)。
static core::Image rotate270(const core::Image& src) {
    const int w = src.width();
    const int h = src.height();
    core::Image out(h, w, src.format());
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            out.setPixel(y, w - 1 - x, src.getPixel(x, y));
        }
    }
    return out;
}

// 旋转入口：归一化角度后按 90° 步长分派；非 90 倍数直接返回副本。
core::Image rotate(const core::Image& src, const int angle) {
    switch (normalizeAngle(angle)) {
        case 0: return src.clone();
        case 90: return rotate90(src);
        case 180: return rotate180(src);
        case 270: return rotate270(src);
        default: return src.clone();
    }
}

// 翻转：horizontal = true 时左右翻转，否则上下翻转。
core::Image flip(const core::Image& src, const bool horizontal) {
    const int w = src.width();
    const int h = src.height();
    core::Image out(w, h, src.format());
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (horizontal) out.setPixel(w - 1 - x, y, src.getPixel(x, y));
            else out.setPixel(x, h - 1 - y, src.getPixel(x, y));
        }
    }
    return out;
}

// 内部工具：最近邻采样单通道值。
static std::uint8_t nearestSample(const core::Image& src, const double sx, const double sy, const int channel) {
    const int x = std::clamp(static_cast<int>(std::round(sx)), 0, src.width() - 1);
    const int y = std::clamp(static_cast<int>(std::round(sy)), 0, src.height() - 1);
    const std::uint8_t* p = src.pixelPtr(x, y);
    if (!p) return 0;
    if (src.isGray()) return p[0];
    return p[channel];
}

// 内部工具：双线性插值单通道值。
static double bilinearSample(const core::Image& src, const double sx, const double sy, const int channel) {
    if (src.empty()) return 0.0;
    const double fx = std::clamp(sx, 0.0, static_cast<double>(src.width() - 1));
    const double fy = std::clamp(sy, 0.0, static_cast<double>(src.height() - 1));
    const int x0 = static_cast<int>(std::floor(fx));
    const int y0 = static_cast<int>(std::floor(fy));
    const int x1 = std::min(x0 + 1, src.width() - 1);
    const int y1 = std::min(y0 + 1, src.height() - 1);
    const double tx = fx - x0;
    const double ty = fy - y0;

    const auto sample = [&](const int x, const int y) -> double {
        const std::uint8_t* p = src.pixelPtr(x, y);
        if (!p) return 0.0;
        return src.isGray() ? p[0] : p[channel];
    };
    const double v00 = sample(x0, y0);
    const double v10 = sample(x1, y0);
    const double v01 = sample(x0, y1);
    const double v11 = sample(x1, y1);
    return (v00 * (1 - tx) + v10 * tx) * (1 - ty) + (v01 * (1 - tx) + v11 * tx) * ty;
}

// 尺寸调整：按目标宽高进行最近邻或双线性重采样。
core::Image resize(const core::Image& src, const int newWidth, const int newHeight, const ResampleMode mode) {
    if (newWidth <= 0 || newHeight <= 0 || src.empty()) return core::Image();
    core::Image out(newWidth, newHeight, src.format());
    const double sx = static_cast<double>(src.width()) / newWidth;
    const double sy = static_cast<double>(src.height()) / newHeight;

    const int channels = src.isGray() ? 1 : (src.format() == core::ImageFormat::RGBA ? 4 : 3);
    for (int y = 0; y < newHeight; ++y) {
        for (int x = 0; x < newWidth; ++x) {
            const double srcX = (x + 0.5) * sx - 0.5;
            const double srcY = (y + 0.5) * sy - 0.5;
            core::Color c;
            if (mode == ResampleMode::NEAREST) {
                c.r = nearestSample(src, srcX, srcY, 0);
                c.g = channels > 1 ? nearestSample(src, srcX, srcY, 1) : c.r;
                c.b = channels > 2 ? nearestSample(src, srcX, srcY, 2) : c.r;
                c.a = channels > 3 ? nearestSample(src, srcX, srcY, 3) : 255;
                if (src.isGray()) { c.g = c.b = c.r; c.a = 255; }
            } else {
                c.r = static_cast<std::uint8_t>(std::clamp(bilinearSample(src, srcX, srcY, 0), 0.0, 255.0));
                c.g = channels > 1 ? static_cast<std::uint8_t>(std::clamp(bilinearSample(src, srcX, srcY, 1), 0.0, 255.0)) : c.r;
                c.b = channels > 2 ? static_cast<std::uint8_t>(std::clamp(bilinearSample(src, srcX, srcY, 2), 0.0, 255.0)) : c.r;
                c.a = channels > 3 ? static_cast<std::uint8_t>(std::clamp(bilinearSample(src, srcX, srcY, 3), 0.0, 255.0)) : 255;
                if (src.isGray()) { c.g = c.b = c.r; c.a = 255; }
            }
            out.setPixel(x, y, c);
        }
    }
    return out;
}

// 按比例缩放：先计算目标宽高再委托给 resize。
core::Image scale(const core::Image& src, const double s, const ResampleMode mode) {
    if (s <= 0.0 || src.empty()) return core::Image();
    const int nw = std::max(1, static_cast<int>(std::round(src.width() * s)));
    const int nh = std::max(1, static_cast<int>(std::round(src.height() * s)));
    return resize(src, nw, nh, mode);
}

} // namespace idc::processing
