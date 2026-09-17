// ============================================================================
// 文件：src/processing/geometric_ops.cpp
// 作用：实现几何变换算法：90° 系列旋转、水平 / 垂直翻转、最近邻 / 双线性缩放。
//       对应终稿 FR-1.1 旋转、FR-1.2 翻转、FR-1.4 缩放 / 尺寸调整。
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

// 尺寸调整：按目标宽高进行最近邻或双线性重采样。
// 性能要点：直接基于行主序字节缓冲计算——四个角点指针每像素只求一次并一次性读取全部通道，
// 避免旧实现「逐通道 × 逐角点」重复调用 pixelPtr/isGray 与逐像素 getPixel/setPixel 的边界检查、格式分支。
// 输出与旧实现逐像素等价（双线性加权公式、夹取范围、通道处理均一致）。
core::Image resize(const core::Image& src, const int newWidth, const int newHeight, const ResampleMode mode) {
    if (newWidth <= 0 || newHeight <= 0 || src.empty()) return core::Image();
    core::Image out(newWidth, newHeight, src.format());

    const int sw = src.width();
    const int sh = src.height();
    const int ch = src.isGray() ? 1 : (src.format() == core::ImageFormat::RGBA ? 4 : 3);
    const std::uint8_t* sd = src.data();
    std::uint8_t* dd = out.data();
    const double stepX = static_cast<double>(sw) / newWidth;
    const double stepY = static_cast<double>(sh) / newHeight;
    const double maxX = static_cast<double>(sw - 1);
    const double maxY = static_cast<double>(sh - 1);

    for (int y = 0; y < newHeight; ++y) {
        const double srcYf = (y + 0.5) * stepY - 0.5;
        for (int x = 0; x < newWidth; ++x) {
            const double srcXf = (x + 0.5) * stepX - 0.5;
            std::uint8_t* o = dd + (static_cast<std::size_t>(y) * newWidth + x) * ch;

            if (mode == ResampleMode::NEAREST) {
                const int xi = std::clamp(static_cast<int>(std::round(srcXf)), 0, sw - 1);
                const int yi = std::clamp(static_cast<int>(std::round(srcYf)), 0, sh - 1);
                const std::uint8_t* p = sd + (static_cast<std::size_t>(yi) * sw + xi) * ch;
                for (int c = 0; c < ch; ++c) o[c] = p[c];
                continue;
            }

            // 双线性：先把源坐标夹到 [0, sw-1] / [0, sh-1]，再取四邻域角点
            const double fx = std::clamp(srcXf, 0.0, maxX);
            const double fy = std::clamp(srcYf, 0.0, maxY);
            const int x0 = static_cast<int>(std::floor(fx));
            const int y0 = static_cast<int>(std::floor(fy));
            const int x1 = std::min(x0 + 1, sw - 1);
            const int y1 = std::min(y0 + 1, sh - 1);
            const double tx = fx - x0;
            const double ty = fy - y0;

            const std::uint8_t* p00 = sd + (static_cast<std::size_t>(y0) * sw + x0) * ch;
            const std::uint8_t* p10 = sd + (static_cast<std::size_t>(y0) * sw + x1) * ch;
            const std::uint8_t* p01 = sd + (static_cast<std::size_t>(y1) * sw + x0) * ch;
            const std::uint8_t* p11 = sd + (static_cast<std::size_t>(y1) * sw + x1) * ch;

            const double wx0 = 1.0 - tx;
            const double wy0 = 1.0 - ty;
            for (int c = 0; c < ch; ++c) {
                const double top = p00[c] * wx0 + p10[c] * tx;
                const double bot = p01[c] * wx0 + p11[c] * tx;
                const double v = top * wy0 + bot * ty;
                o[c] = static_cast<std::uint8_t>(std::clamp(v, 0.0, 255.0));
            }
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
