// ============================================================================
// 文件：src/pixel_ops/color_ops.cpp
// 作用：实现 include/pixel_ops/color_ops.h 中的颜色与通道级处理算法。
//       含灰度化 / 按通道反色 / 按通道分离 / 颜色选取（pickColor），
//       对应 SPEC §3.1（FR-1.4）「黑白、反色、色道分离、颜色选取」。
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
// 性能：单遍「拷贝 + 变换」，mask 在循环外折成每通道常量，循环体直写 R/G/B 三通道 XOR、无内层循环；
//       alpha（第 4 字节）按 bpp 条件保留。RGB 为 3 字节、不对齐字边界，故只能逐字节而非打包位运算；
//       瓶颈在内存带宽，不值得为其上 SIMD。
core::Image invertChannels(const core::Image& src, const std::string& mask) {
    if (src.empty()) return src.clone();
    core::Image out(src.width(), src.height(), src.format());
    const std::uint8_t* s = src.data();
    std::uint8_t* d = out.data();
    const std::size_t n = src.sizeInBytes();

    if (src.isGray()) {
        for (std::size_t i = 0; i < n; ++i) d[i] = static_cast<std::uint8_t>(s[i] ^ 0xFF);  // 255 - v
        return out;
    }

    const std::string m = mask.size() == 3 ? mask : std::string("111");
    const auto xr0 = static_cast<std::uint8_t>(m[0] == '1' ? 0xFF : 0x00);
    const auto xr1 = static_cast<std::uint8_t>(m[1] == '1' ? 0xFF : 0x00);
    const auto xr2 = static_cast<std::uint8_t>(m[2] == '1' ? 0xFF : 0x00);
    const std::size_t bpp = core::bytesPerPixel(out.format());  // RGB=3 / RGBA=4
    for (std::size_t i = 0; i + bpp <= n; i += bpp) {
        d[i + 0] = static_cast<std::uint8_t>(s[i + 0] ^ xr0);
        d[i + 1] = static_cast<std::uint8_t>(s[i + 1] ^ xr1);
        d[i + 2] = static_cast<std::uint8_t>(s[i + 2] ^ xr2);
        if (bpp == 4) d[i + 3] = s[i + 3];
    }
    return out;
}

// 按通道分离：mask 中 '0' 位置对应的通道被置 0，'1' 位置保留。
// 灰度图无 R/G/B 之分，直接返回副本。
// 性能：同 invertChannels——单遍拷贝 + 循环外折常量，直写三通道 AND、无内层循环。
core::Image splitChannels(const core::Image& src, const std::string& mask) {
    if (src.empty() || src.isGray()) return src.clone();
    core::Image out(src.width(), src.height(), src.format());
    const std::uint8_t* s = src.data();
    std::uint8_t* d = out.data();
    const std::size_t n = src.sizeInBytes();

    const std::string m = mask.size() == 3 ? mask : std::string("111");
    const auto kp0 = static_cast<std::uint8_t>(m[0] == '1' ? 0xFF : 0x00);
    const auto kp1 = static_cast<std::uint8_t>(m[1] == '1' ? 0xFF : 0x00);
    const auto kp2 = static_cast<std::uint8_t>(m[2] == '1' ? 0xFF : 0x00);
    const std::size_t bpp = core::bytesPerPixel(out.format());
    for (std::size_t i = 0; i + bpp <= n; i += bpp) {
        d[i + 0] = static_cast<std::uint8_t>(s[i + 0] & kp0);
        d[i + 1] = static_cast<std::uint8_t>(s[i + 1] & kp1);
        d[i + 2] = static_cast<std::uint8_t>(s[i + 2] & kp2);
        if (bpp == 4) d[i + 3] = s[i + 3];
    }
    return out;
}

// 拾取像素颜色：用于"颜色选取"功能，越界返回 nullopt。
std::optional<core::Color> pickColor(const core::Image& src, const int x, const int y) {
    if (!src.inBounds(x, y)) return std::nullopt;
    return src.getPixel(x, y);
}

} // namespace idc::pixel_ops
