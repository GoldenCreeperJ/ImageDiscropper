// ============================================================================
// 文件：src/core/image.cpp
// 作用：实现 include/core/image.h 中声明的 Image 类，包括像素缓冲管理、
//       像素读写、格式转换、尺寸调整与整体填充等。
// ============================================================================
#include "core/image.h"

#include <algorithm>
#include <cstring>

namespace idc::core {
namespace {

// BT.601 亮度加权系数（RGB → 灰度），统一供 setPixel(GRAY) 与 getGray 使用，避免公式重复。
constexpr double kLumaR = 0.299;
constexpr double kLumaG = 0.587;
constexpr double kLumaB = 0.114;

// 按 BT.601 将 RGB 分量压缩为单通道灰度值（结果夹紧到 [0, 255]）。
inline std::uint8_t rgbToGray(const int r, const int g, const int b) {
    const int v = static_cast<int>(kLumaR * r + kLumaG * g + kLumaB * b);
    return static_cast<std::uint8_t>(std::clamp(v, 0, 255));
}

} // namespace

// 每像素字节数：RGB = 3、RGBA = 4、GRAY = 1。
std::size_t bytesPerPixel(const ImageFormat fmt) {
    switch (fmt) {
        case ImageFormat::RGB: return 3;
        case ImageFormat::RGBA: return 4;
        case ImageFormat::GRAY: return 1;
    }
    return 4;
}

// 构造指定尺寸与格式的空白图像，默认清零。
Image::Image(const int width, const int height, const ImageFormat fmt)
    : width_(width), height_(height), format_(fmt) {
    if (width_ < 0) width_ = 0;
    if (height_ < 0) height_ = 0;
    data_.assign(static_cast<std::size_t>(width_) * height_ * bytesPerPixel(format_), 0);
}

// 深拷贝当前图像，返回全新对象。
Image Image::clone() const {
    Image out(width_, height_, format_);
    std::memcpy(out.data_.data(), data_.data(), data_.size());
    return out;
}

// 判断坐标是否位于图像范围内。
bool Image::inBounds(const int x, const int y) const {
    return x >= 0 && y >= 0 && x < width_ && y < height_;
}

// 计算字节偏移；越界返回 -1，用于内部快速定位。
std::ptrdiff_t Image::byteOffset(const int x, const int y) const {
    if (!inBounds(x, y)) return -1;
    const std::size_t bpp = bytesPerPixel(format_);
    return static_cast<std::ptrdiff_t>((static_cast<std::size_t>(y) * width_ + x) * bpp);
}

// 读取像素颜色。GRAY 模式下 R = G = B = 灰度值，A = 255。
Color Image::getPixel(const int x, const int y) const {
    const std::ptrdiff_t off = byteOffset(x, y);
    if (off < 0) return kTransparent;
    const std::uint8_t* p = data_.data() + off;
    switch (format_) {
        case ImageFormat::RGB:
            return Color(p[0], p[1], p[2], 255);
        case ImageFormat::RGBA:
            return Color(p[0], p[1], p[2], p[3]);
        case ImageFormat::GRAY:
            return Color(p[0], p[0], p[0], 255);
    }
    return kTransparent;
}

// 写入像素颜色。GRAY 模式使用 BT.601 亮度公式压缩为单通道。
void Image::setPixel(const int x, const int y, const Color& c) {
    const std::ptrdiff_t off = byteOffset(x, y);
    if (off < 0) return;
    std::uint8_t* p = data_.data() + off;
    switch (format_) {
        case ImageFormat::RGB:
            p[0] = c.r; p[1] = c.g; p[2] = c.b;
            break;
        case ImageFormat::RGBA:
            p[0] = c.r; p[1] = c.g; p[2] = c.b; p[3] = c.a;
            break;
        case ImageFormat::GRAY:
            p[0] = rgbToGray(c.r, c.g, c.b); // BT.601 亮度压缩为单通道。
            break;
    }
}

// 返回指定坐标的像素首字节指针；越界返回 nullptr。
std::uint8_t* Image::pixelPtr(const int x, const int y) {
    const std::ptrdiff_t off = byteOffset(x, y);
    return off < 0 ? nullptr : data_.data() + off;
}

const std::uint8_t* Image::pixelPtr(const int x, const int y) const {
    const std::ptrdiff_t off = byteOffset(x, y);
    return off < 0 ? nullptr : data_.data() + off;
}

// 读取灰度值：非灰度图按亮度公式即时计算，方便统一处理。
std::uint8_t Image::getGray(const int x, const int y) const {
    const std::ptrdiff_t off = byteOffset(x, y);
    if (off < 0) return 0;
    const std::uint8_t* p = data_.data() + off;
    if (format_ == ImageFormat::GRAY) return p[0];
    return rgbToGray(p[0], p[1], p[2]); // 非灰度图按 BT.601 即时计算。
}

// 写入灰度值：GRAY 模式直接写；彩色模式将 R = G = B = v，alpha 保持 255。
void Image::setGray(const int x, const int y, const std::uint8_t v) {
    const std::ptrdiff_t off = byteOffset(x, y);
    if (off < 0) return;
    std::uint8_t* p = data_.data() + off;
    switch (format_) {
        case ImageFormat::GRAY:
            p[0] = v;
            break;
        case ImageFormat::RGB:
            p[0] = p[1] = p[2] = v;
            break;
        case ImageFormat::RGBA:
            p[0] = p[1] = p[2] = v; p[3] = 255;
            break;
    }
}

// 转换为 RGBA 图像。原格式已为 RGBA 时返回克隆副本。
Image Image::toRGBA() const {
    if (format_ == ImageFormat::RGBA) return clone();
    Image out(width_, height_, ImageFormat::RGBA);
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            out.setPixel(x, y, getPixel(x, y));
        }
    }
    return out;
}

// 转换为 GRAY 图像。原格式已为 GRAY 时返回克隆副本。
Image Image::toGray() const {
    if (format_ == ImageFormat::GRAY) return clone();
    Image out(width_, height_, ImageFormat::GRAY);
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            out.setGray(x, y, getGray(x, y));
        }
    }
    return out;
}

// 重新分配像素缓冲为新尺寸/格式，原像素数据被丢弃并清零。
void Image::reallocate(const int width, const int height, const ImageFormat fmt) {
    width_ = std::max(0, width);
    height_ = std::max(0, height);
    format_ = fmt;
    data_.assign(static_cast<std::size_t>(width_) * height_ * bytesPerPixel(format_), 0);
}

// 使用给定颜色填充全部像素。
void Image::fill(const Color& c) {
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            setPixel(x, y, c);
        }
    }
}

// 裁剪子图：区间左闭右开，先裁剪到图像边界，再逐行 memcpy 拷出。
// 空区域（宽或高 <= 0）返回同格式的 0×0 图像，供调用方“跳过空块”。
Image Image::crop(const int left, const int top, const int right, const int bottom) const {
    // 将请求区间夹紧到 [0, width_] / [0, height_]（处理越界与贴边选框）。
    const int l = std::clamp(left, 0, width_);
    const int t = std::clamp(top, 0, height_);
    const int r = std::clamp(right, 0, width_);
    const int b = std::clamp(bottom, 0, height_);
    const int w = r - l;
    const int h = b - t;
    if (w <= 0 || h <= 0) return Image(0, 0, format_); // 空块：返回 0×0。

    Image out(w, h, format_);
    const std::size_t bpp = bytesPerPixel(format_);
    // 逐行拷贝：源行起点 (t+y, l)，宽 w 像素；行内连续，memcpy 最高效。
    for (int y = 0; y < h; ++y) {
        const std::uint8_t* srcRow =
            data_.data() + (static_cast<std::size_t>(t + y) * width_ + l) * bpp;
        std::uint8_t* dstRow =
            out.data_.data() + static_cast<std::size_t>(y) * w * bpp;
        std::memcpy(dstRow, srcRow, static_cast<std::size_t>(w) * bpp);
    }
    return out;
}

// 将 src 覆盖贴到 (destX, destY)：同格式时按行 memcpy（含左右越界裁剪），
// 异格式时逐像素 getPixel/setPixel 转换。不做 alpha 混合（落位区域互不重叠）。
void Image::blit(const Image& src, const int destX, const int destY) {
    if (src.empty() || empty()) return;

    if (format_ == src.format_) {
        // 快速路径：格式一致，逐行字节拷贝。
        const std::size_t bpp = bytesPerPixel(format_);
        for (int sy = 0; sy < src.height_; ++sy) {
            const int dy = destY + sy;
            if (dy < 0 || dy >= height_) continue; // 上/下越界行跳过。
            // 计算 src 行的有效列区间 [sxBegin, sxEnd)，使目标列落在 [0, width_) 内。
            int sxBegin = 0;
            int sxEnd = src.width_;
            if (destX < 0) sxBegin = -destX;                    // 左侧越界。
            if (destX + src.width_ > width_) sxEnd = width_ - destX; // 右侧越界。
            if (sxBegin >= sxEnd) continue;
            const std::uint8_t* srcRow =
                src.data_.data() + (static_cast<std::size_t>(sy) * src.width_ + sxBegin) * bpp;
            std::uint8_t* dstRow =
                data_.data() + (static_cast<std::size_t>(dy) * width_ + (destX + sxBegin)) * bpp;
            std::memcpy(dstRow, srcRow, static_cast<std::size_t>(sxEnd - sxBegin) * bpp);
        }
    } else {
        // 兼容路径：格式不同，逐像素读取并转换写入（由 setPixel 处理通道差异）。
        for (int sy = 0; sy < src.height_; ++sy) {
            for (int sx = 0; sx < src.width_; ++sx) {
                const int dx = destX + sx;
                const int dy = destY + sy;
                if (inBounds(dx, dy)) setPixel(dx, dy, src.getPixel(sx, sy));
            }
        }
    }
}

} // namespace idc::core
