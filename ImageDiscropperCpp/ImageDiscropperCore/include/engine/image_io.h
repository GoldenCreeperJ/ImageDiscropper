// ============================================================================
// 文件：include/engine/image_io.h
// 作用：图像编解码 I/O 封装（终稿 §5.5）——把 core::Image 编码为 PNG/JPEG/BMP/WebP 并写盘
//       或编码到内存缓冲；以及从文件解码图像为 core::Image。底层用 stb_image（解码）+
//       stb_image_write（PNG/JPEG/BMP 编码）+ libwebp（WebP 编码），均由 vcpkg 提供。
// 分块依据：把“编码/解码/读写盘”这一 I/O 关注点从合成逻辑（composition）与导出编排（export）
//       中隔离；上层（导出阶段 / CLI）只调用本模块，不直接接触 stb / libwebp，保持依赖单向。
// 说明：解码统一加载为 RGBA；编码支持 PNG/JPEG/BMP/WebP。具体编解码器与第三方库封装在
//       image_io.cpp，本公共头不暴露 stb / libwebp 的任何类型（依赖 PRIVATE）。
// ============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/color.h"
#include "core/image.h"
#include "engine/composition.h" // ExportFormat

namespace idc::engine {

// 从文件解码图像为 core::Image（统一加载为 RGBA，4 通道）。
// 底层用 stb_image，支持 PNG/JPEG/BMP/WebP/GIF/TGA 等常见格式。
// 成功返回 true 并填充 out；path 为空、文件不存在、无法读取或格式不支持时返回 false
// （对应 CLI “输入文件错误”退出码 3 / E-8）。
bool readImageFile(const std::string& path, core::Image& out);

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
