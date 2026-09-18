// ============================================================================
// 文件：src/pixel_ops/color_ops.cpp
// 作用：实现 include/pixel_ops/color_ops.h 中的颜色与通道级处理算法。
//       含灰度化 / 按通道反色 / 按通道分离 / 颜色选取（pickColor），
//       对应终稿 FR-1.4「黑白、反色、色道分离、颜色选取」。
// ============================================================================
#include "pixel_ops/color_ops.h"

#include <algorithm>

namespace idc::pixel_ops {

// 灰度化：委托 core::Image::toGray——其内部已对 GRAY 输入短路返回副本，并完成 BT.601 加权。
core::Image toGray(const core::Image& src) {
    return src.toGray();
}

// 按通道反色：mask 长度必须为 3，否则视为 "111"。
// 灰度图只有单一亮度通道，反色即对每个字节取 255-v（mask 不适用于单通道）。
// 彩色图按 mask 逐通道取反，alpha 通道始终保留不变。
// 性能：先整体 clone 再基于行主序字节缓冲原地取反，避免逐像素 getPixel/setPixel 的边界检查与格式分支。
core::Image invertChannels(const core::Image& src, const std::string& mask) {
    if (src.empty()) return src.clone();
    core::Image out = src.clone();
    std::uint8_t* d = out.data();
    const std::size_t n = out.sizeInBytes();

    if (out.isGray()) {
        for (std::size_t i = 0; i < n; ++i) d[i] = static_cast<std::uint8_t>(255 - d[i]);
        return out;
    }

    const std::string m = mask.size() == 3 ? mask : std::string("111");
    const std::size_t bpp = core::bytesPerPixel(out.format());   // RGB=3 / RGBA=4
    for (std::size_t i = 0; i + bpp <= n; i += bpp) {
        if (m[0] == '1') d[i + 0] = static_cast<std::uint8_t>(255 - d[i + 0]);
        if (m[1] == '1') d[i + 1] = static_cast<std::uint8_t>(255 - d[i + 1]);
        if (m[2] == '1') d[i + 2] = static_cast<std::uint8_t>(255 - d[i + 2]);
        // d[i + 3]（alpha，若存在）不参与反色
    }
    return out;
}

// 按通道分离：mask 中 '0' 位置对应的通道被置 0，'1' 位置保留。
// 灰度图无 R/G/B 之分，直接返回副本。
core::Image splitChannels(const core::Image& src, const std::string& mask) {
    if (src.empty() || src.isGray()) return src.clone();
    core::Image out = src.clone();
    std::uint8_t* d = out.data();
    const std::size_t n = out.sizeInBytes();

    const std::string m = mask.size() == 3 ? mask : std::string("111");
    const std::size_t bpp = core::bytesPerPixel(out.format());
    for (std::size_t i = 0; i + bpp <= n; i += bpp) {
        if (m[0] != '1') d[i + 0] = 0;
        if (m[1] != '1') d[i + 1] = 0;
        if (m[2] != '1') d[i + 2] = 0;
    }
    return out;
}

// 拾取像素颜色：用于"颜色选取"功能，越界返回 nullopt。
std::optional<core::Color> pickColor(const core::Image& src, const int x, const int y) {
    if (!src.inBounds(x, y)) return std::nullopt;
    return src.getPixel(x, y);
}

} // namespace idc::pixel_ops
