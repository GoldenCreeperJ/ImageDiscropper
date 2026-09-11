// ============================================================================
// 文件：src/core/image.cpp
// 作用：实现 include/core/image.h 中声明的 Image 类，包括像素缓冲管理、
//       像素读写、格式转换、尺寸调整与整体填充等。
// ============================================================================
#include "core/image.h"

#include <algorithm>
#include <cstring>

namespace idc::core {

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
        case ImageFormat::GRAY: {
            const int gray = static_cast<int>(0.299 * c.r + 0.587 * c.g + 0.114 * c.b);
            p[0] = static_cast<std::uint8_t>(std::clamp(gray, 0, 255));
            break;
        }
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
    const int gray = static_cast<int>(0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2]);
    return static_cast<std::uint8_t>(std::clamp(gray, 0, 255));
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

// 重置图像尺寸与格式，原像素数据被清空。
void Image::resize(const int width, const int height, const ImageFormat fmt) {
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

} // namespace idc::core
