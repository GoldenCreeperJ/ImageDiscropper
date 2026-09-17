// ============================================================================
// 文件：include/processing/color_ops.h
// 作用：颜色与通道级像素处理算法。
// ============================================================================
#pragma once

#include <optional>
#include <string>

#include "core/color.h"
#include "core/image.h"

namespace idc::processing {

// 灰度化：将彩色图转换为单通道 GRAY 图像，公式采用 BT.601（0.299R + 0.587G + 0.114B）。
// 已经是灰度图时直接返回原图。返回新对象，不修改输入。
core::Image toGray(const core::Image& src);

// 按通道反色：mask 为长度为 3 的字符串（形如 "101"），
// '1' 表示对应通道 R/G/B 取 255 - v，'0' 表示保持不变（alpha 始终不变）。
// 灰度图只有单一亮度通道，对每个像素取 255 - v（忽略 mask）。返回新对象。
core::Image invertChannels(const core::Image& src, const std::string& mask = "111");

// 按通道分离：mask 中 '1' 表示保留对应通道，'0' 表示置 0。
// 灰度图直接返回原图。返回新对象。
core::Image splitChannels(const core::Image& src, const std::string& mask = "111");

// 拾取指定坐标的像素颜色，用于"颜色选取"功能。
// 越界时返回 std::nullopt。
std::optional<core::Color> pickColor(const core::Image& src, int x, int y);

} // namespace idc::processing
