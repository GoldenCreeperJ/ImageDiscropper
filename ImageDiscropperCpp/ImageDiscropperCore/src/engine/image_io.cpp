// ============================================================================
// 文件：src/engine/image_io.cpp
// 作用：image_io.h 的实现——图像编解码 I/O（SPEC §4.5）：把 core::Image 编码为
//       PNG/JPEG/BMP/WebP（写盘 / 编码到内存两种出口），并从文件解码图像为 core::Image。
// 分块依据：stb（header-only）负责解码（stb_image）与 PNG/JPEG/BMP 编码（stb_image_write），
//       其实现宏 STB_IMAGE_IMPLEMENTATION / STB_IMAGE_WRITE_IMPLEMENTATION 必须且只能在全工程
//       唯一一个 .cpp 中定义——即本文件；WebP 编码由 libwebp 补齐（stb 不支持 WebP 写）。
//       三者均只在本文件内使用，其余模块经 image_io.h 间接调用，不直接接触 stb / libwebp，
//       避免重复符号与依赖扩散。
// ============================================================================
#include "engine/image_io.h"

// stb_image（解码）+ stb_image_write（编码）：全工程仅此一处定义各自实现宏（header-only 库要求）。
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_write.h>

// libwebp：WebP 编码（WebPEncodeRGBA 见 encode.h；释放编码缓冲的 WebPFree 见 types.h）。
#include <webp/encode.h>

#include <algorithm>
#include <cstring>
#include <fstream>

namespace idc::engine {
namespace {

// 把任意格式图像整理为 RGBA 连续缓冲（PNG / WebP 用，保留 alpha 通道）。
std::vector<std::uint8_t> toRGBABuffer(const core::Image& img) {
    if (img.format() == core::ImageFormat::RGBA) {
        return std::vector(img.data(), img.data() + img.sizeInBytes());
    }
    const core::Image rgba = img.toRGBA(); // RGB/GRAY → RGBA。
    return std::vector(rgba.data(), rgba.data() + rgba.sizeInBytes());
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
void stbAppendCallback(void* context, void* data, const int size) {
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

// 从文件解码图像为 core::Image（统一加载为 RGBA，4 通道）；成功返回 true 并填充 out。
// 底层用 stb_image，支持 PNG/JPEG/BMP/WebP/GIF/TGA 等；path 为空 / 文件不存在 / 无法读取 /
// 格式不支持时返回 false（对应 CLI 输入文件错误 / E-8）。
bool readImageFile(const std::string& path, core::Image& out) {
    if (path.empty()) return false;
    int w = 0, h = 0, channels = 0;
    // 强制按 RGBA（4 通道）加载，与 core::Image 的 RGBA 布局一致，便于后续统一处理。
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (data == nullptr || w <= 0 || h <= 0) {
        stbi_image_free(data); // data 可能为 nullptr，stbi_image_free 对 nullptr 安全。
        return false;          // 解码失败：文件不存在 / 非图像 / 不支持的格式。
    }
    // core::Image 的 RGBA 缓冲为紧凑行主序（w*h*4，无行填充），与 stb 输出布局一致，直接整体拷贝。
    out.reallocate(w, h, core::ImageFormat::RGBA);
    std::memcpy(out.data(), data,
                static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    stbi_image_free(data);
    return true;
}

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
