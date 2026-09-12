// ============================================================================
// 文件：include/engine/image_io.h
// 作用：图像编码 I/O 封装（终稿 §5.5）——把 core::Image 编码为 PNG/JPEG/BMP/WebP 并写盘，
//       或编码到内存缓冲。底层用 stb_image_write（PNG/JPEG/BMP）+ libwebp（WebP），均由 vcpkg 提供。
// 分块依据：把“编码/写盘”这一 I/O 关注点从合成逻辑（composition）与导出编排（export）
//       中隔离；导出阶段只调用本模块，不直接接触 stb / libwebp，保持依赖单向。
// 说明：四种格式（PNG/JPEG/BMP/WebP）均受支持；具体编码器与第三方库封装在 image_io.cpp，
//       本公共头不暴露 stb / libwebp 的任何类型（依赖 PRIVATE）。
// ============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/color.h"
#include "core/image.h"
#include "engine/composition.h" // ExportFormat

namespace idc::engine {

// 将图像编码并写入文件。format 决定编码器；quality 仅对有损格式（JPEG/WebP）生效；
// jpegBg 用于不支持透明的格式（JPEG/BMP）把 alpha 压平到该背景色（§5.5）。
// 成功返回 true；写盘失败（E-8）返回 false。
bool writeImageFile(const std::string& path, const core::Image& image,
                    ExportFormat format, int quality, const core::Color& jpegBg);

// 将图像编码到内存缓冲。参数含义同 writeImageFile。
// 成功返回 true 并把编码字节写入 out；失败返回 false。
bool encodeImageToMemory(const core::Image& image, ExportFormat format,
                         int quality, const core::Color& jpegBg,
                         std::vector<std::uint8_t>& out);

} // namespace idc::engine
