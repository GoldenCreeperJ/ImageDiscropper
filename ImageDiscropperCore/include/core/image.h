// ============================================================================
// 文件：include/core/image.h
// 作用：定义图像像素容器 Image。
//       支持 RGB / RGBA / GRAY 三种像素模式，提供像素读写、拷贝、通道访问等
//       基础操作。所有 processing / annotation 模块的算法都在此数据结构上进行。
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/color.h"

namespace idc::core {

// ---------------------------------------------------------------------------
// ImageFormat：像素存储模式
//   RGB  —— 每像素 3 字节，无 alpha
//   RGBA —— 每像素 4 字节，带 alpha
//   GRAY —— 每像素 1 字节，灰度
// ---------------------------------------------------------------------------
enum class ImageFormat {
    RGB,
    RGBA,
    GRAY,
};

// 返回该格式下每像素字节数。
std::size_t bytesPerPixel(ImageFormat fmt);

// ---------------------------------------------------------------------------
// Image：行主序像素缓冲
// ---------------------------------------------------------------------------
class Image {
public:
    Image() = default;
    Image(int width, int height, ImageFormat fmt);

    // 拷贝构造与赋值：像素数据整体复制。
    Image(const Image&) = default;
    Image& operator=(const Image&) = default;
    Image(Image&&) noexcept = default;
    Image& operator=(Image&&) noexcept = default;

    // 深拷贝一份当前图像。
    Image clone() const;

    // 基本属性访问。
    int width() const { return width_; }
    int height() const { return height_; }
    ImageFormat format() const { return format_; }
    bool isGray() const { return format_ == ImageFormat::GRAY; }
    bool empty() const { return width_ <= 0 || height_ <= 0 || data_.empty(); }

    // 像素索引：越界时返回 false，调用方据此判断是否跳过。
    bool inBounds(int x, int y) const;

    // 读取像素颜色：GRAY 模式返回灰度重复的 RGB；RGB/RGBA 返回真实分量。
    Color getPixel(int x, int y) const;

    // 写入像素颜色：GRAY 模式忽略 alpha，取亮度作为灰度值；RGB 模式忽略 alpha。
    void setPixel(int x, int y, const Color& c);

    // 直接按字节访问像素，性能敏感路径使用。越界行为未定义。
    std::uint8_t* pixelPtr(int x, int y);
    const std::uint8_t* pixelPtr(int x, int y) const;

    // 灰度模式下直接读写单通道值（0~255）。彩色模式调用会返回 0 / 忽略。
    std::uint8_t getGray(int x, int y) const;
    void setGray(int x, int y, std::uint8_t v);

    // 底层缓冲访问，主要用于批量处理算法。
    std::uint8_t* data() { return data_.data(); }
    const std::uint8_t* data() const { return data_.data(); }
    std::size_t sizeInBytes() const { return data_.size(); }

    // 将当前图像统一转换为 RGBA，返回新对象。用于算法处理前的规范化。
    Image toRGBA() const;
    // 将当前图像转换为 GRAY（按 ITU-R BT.601 亮度公式加权）。
    Image toGray() const;

    // 重置图像尺寸，会清空原有像素数据。
    void resize(int width, int height, ImageFormat fmt);

    // 用指定颜色填充整个图像。
    void fill(const Color& c);

private:
    int width_{0};
    int height_{0};
    ImageFormat format_{ImageFormat::RGBA};
    std::vector<std::uint8_t> data_;

    // 计算给定坐标在 data_ 中的字节偏移，越界时返回 -1。
    std::ptrdiff_t byteOffset(int x, int y) const;
};

} // namespace idc::core
