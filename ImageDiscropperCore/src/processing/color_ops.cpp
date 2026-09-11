// ============================================================================
// 文件：src/processing/color_ops.cpp
// 作用：实现 include/processing/color_ops.h 中的颜色与通道级处理算法。
//       对应 Kotlin ImageArray.kt 中的 gray / invert / split，
//       以及颜色选取（pickColor）。
// ============================================================================
#include "processing/color_ops.h"

#include <algorithm>

namespace idc::processing {

// 灰度化：彩色图直接调用 core::Image::toGray 完成 BT.601 加权。
core::Image toGray(const core::Image& src) {
    if (src.isGray()) return src.clone();
    return src.toGray();
}

// 按通道反色：mask 长度必须为 3，否则视为 "111"。
core::Image invertChannels(const core::Image& src, const std::string& mask) {
    if (src.isGray()) {
        // 灰度图不支持按通道反色，直接返回原图副本。
        return src.clone();
    }
    std::string m = mask;
    if (m.size() != 3) m = "111";

    core::Image out(src.width(), src.height(), src.format());
    for (int y = 0; y < src.height(); ++y) {
        for (int x = 0; x < src.width(); ++x) {
            core::Color c = src.getPixel(x, y);
            if (m[0] == '1') c.r = static_cast<std::uint8_t>(255 - c.r);
            if (m[1] == '1') c.g = static_cast<std::uint8_t>(255 - c.g);
            if (m[2] == '1') c.b = static_cast<std::uint8_t>(255 - c.b);
            out.setPixel(x, y, c);
        }
    }
    return out;
}

// 按通道分离：mask 中 '0' 位置对应的通道被置 0。
core::Image splitChannels(const core::Image& src, const std::string& mask) {
    if (src.isGray()) return src.clone();
    std::string m = mask;
    if (m.size() != 3) m = "111";

    core::Image out(src.width(), src.height(), src.format());
    for (int y = 0; y < src.height(); ++y) {
        for (int x = 0; x < src.width(); ++x) {
            core::Color c = src.getPixel(x, y);
            if (m[0] != '1') c.r = 0;
            if (m[1] != '1') c.g = 0;
            if (m[2] != '1') c.b = 0;
            out.setPixel(x, y, c);
        }
    }
    return out;
}

// 拾取像素颜色：用于"颜色选取"功能，越界返回 nullopt。
std::optional<core::Color> pickColor(const core::Image& src, const int x, const int y) {
    if (!src.inBounds(x, y)) return std::nullopt;
    return src.getPixel(x, y);
}

} // namespace idc::processing
