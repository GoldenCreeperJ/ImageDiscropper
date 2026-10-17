// ============================================================================
// 文件：include/io/image_io.h
// 作用：图像编解码 I/O 封装（SPEC §4.5）——把 core::Image 编码为 PNG/JPEG/BMP/WebP 并写盘
//       或编码到内存缓冲；以及从文件解码图像为 core::Image。底层用 stb_image（解码）+
//       stb_image_write（PNG/JPEG/BMP 编码）+ libwebp（WebP 编码），均由 vcpkg 提供。
// 分块依据：把“编码/解码/读写盘”这一基础 I/O 能力独立成 io 模块（与 core 的 path_utf8
//       平级），从合成逻辑（composition）与导出编排（export）中隔离；上层（导出阶段 / CLI）
//       只调用本模块，不直接接触 stb / libwebp，保持依赖单向。
// 说明：解码统一加载为 RGBA；编码支持 PNG/JPEG/BMP/WebP。具体编解码器与第三方库封装在
//       image_io.cpp，本公共头不暴露 stb / libwebp 的任何类型（依赖 PRIVATE）。
// ============================================================================
#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

#include "core/color.h"
#include "core/core_error.h"  // core::CoreError（I/O 失败侧载荷）
#include "core/image.h"
#include "engine/composition.h" // ExportFormat

namespace idc::io {

// ExportFormat 由合成层（engine/composition.h）定义；本模块只借用其枚举做编码分发。
using engine::ExportFormat;

// 从文件解码图像为 core::Image（统一加载为 RGBA，4 通道），成功填充 out。
// 底层用 stb_image，支持 PNG/JPEG/BMP/WebP/GIF/TGA 等常见格式。
// 失败返回 core::CoreError：path 为空 → Code=PathEmpty；文件不存在 / 无法读取 / 格式不支持 →
// Code=DecodeFailed（两者均对应 CLI “输入文件错误”退出码 3 / E-8）。保留 out 参数，调用方可以 operator bool 直接判定。
std::expected<void, core::CoreError> readImageFile(const std::string& path, core::Image& out);

// 将图像编码并写入文件。format 决定编码器；quality 仅对有损格式（JPEG/WebP）生效；
// jpegBg 用于不支持透明的格式（JPEG/BMP）把 alpha 压平到该背景色（SPEC §4.5）。
// 写盘失败返回 core::CoreError（Code=Io，E-8）。
std::expected<void, core::CoreError> writeImageFile(const std::string& path, const core::Image& image,
                    ExportFormat format, int quality, const core::Color& jpegBg);

// 将图像编码到内存缓冲。参数含义同 writeImageFile，成功把编码字节写入 out。
// 编码失败返回 core::CoreError（Code=EncodeFailed）。
std::expected<void, core::CoreError> encodeImageToMemory(const core::Image& image, ExportFormat format,
                         int quality, const core::Color& jpegBg,
                         std::vector<std::uint8_t>& out);

} // namespace idc::io
