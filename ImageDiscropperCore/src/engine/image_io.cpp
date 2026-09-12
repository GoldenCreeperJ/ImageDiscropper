// ============================================================================
// 文件：src/engine/image_io.cpp
// 作用：image_io.h 的实现——把 core::Image 编码为 PNG/JPEG/BMP/WebP，
//       支持写盘与编码到内存两种出口（终稿 §5.5）。
// 分块依据：stb（header-only）负责 PNG/JPEG/BMP，实现宏 STB_IMAGE_WRITE_IMPLEMENTATION
//       必须且只能在全工程唯一一个 .cpp 中定义——即本文件；WebP 由 libwebp 补齐
//       （stb 不支持 WebP 写）。二者均只在本文件内使用，其余模块经 image_io.h 间接调用，
//       不直接接触 stb / libwebp，避免重复符号与依赖扩散。
// ============================================================================
#include "engine/image_io.h"

// stb_image_write：全工程仅此一处定义实现宏（header-only 库要求）。
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

// libwebp：WebP 编码（WebPEncodeRGBA 见 encode.h；释放编码缓冲的 WebPFree 见 decode.h）。
#include <webp/decode.h>
#include <webp/encode.h>

#include <algorithm>
#include <cmath>
#include <fstream>

namespace idc::engine {
namespace {

// 把任意格式图像整理为 RGBA 连续缓冲（PNG 用，保留 alpha 通道）。
std::vector<std::uint8_t> toRGBABuffer(const core::Image& img) {
    if (img.format() == core::ImageFormat::RGBA) {
        return std::vector<std::uint8_t>(img.data(), img.data() + img.sizeInBytes());
    }
    const core::Image rgba = img.toRGBA(); // RGB/GRAY → RGBA。
    return std::vector<std::uint8_t>(rgba.data(), rgba.data() + rgba.sizeInBytes());
}

// 把图像压平为 RGB 缓冲（JPEG/BMP 无 alpha，半透明像素以 bg 做 src-over 合成）。
std::vector<std::uint8_t> toRGBBuffer(const core::Image& img, const core::Color& bg) {
    const int w = img.width();
    const int h = img.height();
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(w) * h * 3);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const core::Color c = img.getPixel(x, y);
            const float a = c.a / 255.0f; // src-over：out = c*a + bg*(1-a)。
            std::uint8_t* p = buf.data() + (static_cast<std::size_t>(y) * w + x) * 3;
            p[0] = static_cast<std::uint8_t>(std::clamp(c.r * a + bg.r * (1.0f - a), 0.0f, 255.0f));
            p[1] = static_cast<std::uint8_t>(std::clamp(c.g * a + bg.g * (1.0f - a), 0.0f, 255.0f));
            p[2] = static_cast<std::uint8_t>(std::clamp(c.b * a + bg.b * (1.0f - a), 0.0f, 255.0f));
        }
    }
    return buf;
}

// stb 内存写回调：把编码字节追加到 vector<uint8_t>（供编码到内存出口）。
void stbAppendCallback(void* context, void* data, int size) {
    auto* out = static_cast<std::vector<std::uint8_t>*>(context);
    const auto* p = static_cast<const std::uint8_t*>(data);
    out->insert(out->end(), p, p + size);
}

// 用 libwebp 把图像编码为 WebP 字节（有损，quality 夹到 [1,100]）；成功返回 true 并填充 out。
// WebP 保留 alpha，故走 RGBA 通道；WebPEncodeRGBA 分配的缓冲须用 WebPFree 释放
// （不可用 free——libwebp 可能使用自有分配器）。
bool encodeWebP(const core::Image& image, const int quality, std::vector<std::uint8_t>& out) {
    const std::vector<std::uint8_t> rgba = toRGBABuffer(image);
    const int w = image.width();
    const int h = image.height();
    std::uint8_t* enc = nullptr;
    const float q = static_cast<float>(std::clamp(quality, 1, 100));
    const std::size_t n = WebPEncodeRGBA(rgba.data(), w, h, w * 4, q, &enc);
    if (n == 0 || enc == nullptr) { WebPFree(enc); return false; }
    out.assign(enc, enc + n);
    WebPFree(enc);
    return true;
}

} // namespace

// 将图像编码到内存缓冲；成功返回 true。支持 PNG/JPEG/BMP（stb）与 WebP（libwebp）。
bool encodeImageToMemory(const core::Image& image, const ExportFormat format,
                         const int quality, const core::Color& jpegBg,
                         std::vector<std::uint8_t>& out) {
    if (image.empty()) return false;
    out.clear();
    const int w = image.width();
    const int h = image.height();

    switch (format) {
        case ExportFormat::PNG: {
            const std::vector<std::uint8_t> rgba = toRGBABuffer(image);
            return stbi_write_png_to_func(stbAppendCallback, &out, w, h, 4,
                                          rgba.data(), w * 4) != 0;
        }
        case ExportFormat::BMP: {
            const std::vector<std::uint8_t> rgb = toRGBBuffer(image, jpegBg);
            return stbi_write_bmp_to_func(stbAppendCallback, &out, w, h, 3, rgb.data()) != 0;
        }
        case ExportFormat::JPEG: {
            const std::vector<std::uint8_t> rgb = toRGBBuffer(image, jpegBg);
            const int q = std::clamp(quality, 1, 100);
            return stbi_write_jpg_to_func(stbAppendCallback, &out, w, h, 3, rgb.data(), q) != 0;
        }
        case ExportFormat::WEBP:
            return encodeWebP(image, quality, out); // WebP：libwebp 编码到内存。
    }
    return false;
}

// 将图像编码并写入文件；成功返回 true。支持 PNG/JPEG/BMP（stb）与 WebP（libwebp）；写盘失败（E-8）返回 false。
bool writeImageFile(const std::string& path, const core::Image& image,
                    const ExportFormat format, const int quality, const core::Color& jpegBg) {
    if (image.empty() || path.empty()) return false;
    const int w = image.width();
    const int h = image.height();

    switch (format) {
        case ExportFormat::PNG: {
            const std::vector<std::uint8_t> rgba = toRGBABuffer(image);
            return stbi_write_png(path.c_str(), w, h, 4, rgba.data(), w * 4) != 0;
        }
        case ExportFormat::BMP: {
            const std::vector<std::uint8_t> rgb = toRGBBuffer(image, jpegBg);
            return stbi_write_bmp(path.c_str(), w, h, 3, rgb.data()) != 0;
        }
        case ExportFormat::JPEG: {
            const std::vector<std::uint8_t> rgb = toRGBBuffer(image, jpegBg);
            const int q = std::clamp(quality, 1, 100);
            return stbi_write_jpg(path.c_str(), w, h, 3, rgb.data(), q) != 0;
        }
        case ExportFormat::WEBP: {
            std::vector<std::uint8_t> buf;
            if (!encodeWebP(image, quality, buf)) return false; // 编码失败。
            std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
            if (!ofs) return false;                             // E-8：无法创建/写入。
            ofs.write(reinterpret_cast<const char*>(buf.data()),
                      static_cast<std::streamsize>(buf.size()));
            ofs.close();
            return !ofs.fail();
        }
    }
    return false;
}

} // namespace idc::engine
