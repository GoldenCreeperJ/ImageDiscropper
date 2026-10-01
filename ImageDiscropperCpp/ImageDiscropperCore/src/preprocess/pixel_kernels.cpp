// ============================================================================
// 文件：src/preprocess/pixel_kernels.cpp
// 作用：实现 src/preprocess/pixel_kernels.h 声明的私有像素核。
//       几何/色彩基础核自原 pixel_ops 模块（geometric_ops.cpp / color_ops.cpp）搬入，
//       语义零改动；色彩多通道路径已收敛到融合核 channelMap（split/invert 降为特例适配器），
//       几何单趟融合核 remapD4 供 execPlan 使用（rotate/flip 保留作逐步 oracle）。
//       对应 SPEC §3.1 FR-1.1 旋转、FR-1.2 翻转、FR-1.4 黑白 / 反色 / 色道分离。
// ============================================================================
#include "pixel_kernels.h"

#include <algorithm>
#include <cstdint>

namespace idc::preprocess {

// 内部工具：归一化角度到 [0, 360)。
static int normalizeAngle(const int angle) {
    int a = angle % 360;
    if (a < 0) a += 360;
    return a;
}

// 内部工具：像素搬运——旋转 / 翻转均为纯位置置换，dst 与 src 同格式。
// 非灰度走 getPixel/setPixel（字节一一对应，无损）；GRAY 必须走 getGray/setGray 直接拷单字节，
// 否则 setPixel(GRAY) 会把 getPixel 返回的 (v,v,v) 经 BT.601 浮点重压缩并截断，使灰度值每趟漂移 ±1~2，
// 破坏“旋转 / 翻转不改变像素值”的语义（亦使流水线几何折叠无法逐像素等价）。
static void copyPixel(core::Image& dst, const int dx, const int dy,
                      const core::Image& src, const int sx, const int sy) {
    if (src.isGray()) dst.setGray(dx, dy, src.getGray(sx, sy));
    else dst.setPixel(dx, dy, src.getPixel(sx, sy));
}

// 旋转 90°：新图宽高互换，(x, y) → (h - 1 - y, x)。
static core::Image rotate90(const core::Image& src) {
    const int w = src.width();
    const int h = src.height();
    core::Image out(h, w, src.format());
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            copyPixel(out, h - 1 - y, x, src, x, y);
        }
    }
    return out;
}

// 旋转 180°：(x, y) → (w - 1 - x, h - 1 - y)。
static core::Image rotate180(const core::Image& src) {
    const int w = src.width();
    const int h = src.height();
    core::Image out(w, h, src.format());
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            copyPixel(out, w - 1 - x, h - 1 - y, src, x, y);
        }
    }
    return out;
}

// 旋转 270°：等价于逆时针 90°，(x, y) → (y, w - 1 - x)。
static core::Image rotate270(const core::Image& src) {
    const int w = src.width();
    const int h = src.height();
    core::Image out(h, w, src.format());
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            copyPixel(out, y, w - 1 - x, src, x, y);
        }
    }
    return out;
}

// 旋转入口：归一化角度后按 90° 步长分派；非 90 倍数直接返回副本。
core::Image rotate(const core::Image& src, const int angle) {
    switch (normalizeAngle(angle)) {
        case 0: return src.clone();
        case 90: return rotate90(src);
        case 180: return rotate180(src);
        case 270: return rotate270(src);
        default: return src.clone();
    }
}

// 翻转：horizontal = true 时左右翻转，否则上下翻转。
core::Image flip(const core::Image& src, const bool horizontal) {
    const int w = src.width();
    const int h = src.height();
    core::Image out(w, h, src.format());
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (horizontal) copyPixel(out, w - 1 - x, y, src, x, y);
            else copyPixel(out, x, h - 1 - y, src, x, y);
        }
    }
    return out;
}

// 融合几何核：g = Fh(flipH) ∘ Rot(quad·90)，单趟 scatter-free 完成任意 D4 元素（与 rotate/flip 逐步复合逐像素等价）。
// 采样用闭式逆映射：输出 (x,y) 先解镜像（mx = flipH ? W-1-x : x），再按象限解旋转逆映射：
//   quad0:(mx,my)  quad1:(my, W-1-mx)  quad2:(W-1-mx, H-1-my)  quad3:(H-1-my, mx)
// 其中 (W,H) 为输出尺寸（quad 奇数时 W=h、H=w，否则 W=w、H=h），my=y。
// 与 rotate/flip 逐步复合的三种规范分解（纯旋转 / 单 FlipH / 单 FlipV=R180∘Fh / 对角=Rot∘Fh）逐点核对一致。
core::Image remapD4(const core::Image& src, int quad, const bool flipH) {
    if (src.empty()) return src.clone();
    const int w = src.width();
    const int h = src.height();
    quad = (quad % 4 + 4) % 4;
    const bool swap = quad == 1 || quad == 3;    // 奇数象限宽高互换
    const int W = swap ? h : w;
    const int H = swap ? w : h;
    core::Image out(W, H, src.format());
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const int mx = flipH ? W - 1 - x : x;
            const int my = y;
            int sx = 0, sy = 0;
            switch (quad) {
                case 0: sx = mx; sy = my; break;
                case 1: sx = my; sy = W - 1 - mx; break;
                case 2: sx = W - 1 - mx; sy = H - 1 - my; break;
                default: sx = H - 1 - my; sy = mx; break;   // quad == 3
            }
            copyPixel(out, x, y, src, sx, sy);
        }
    }
    return out;
}

// 融合位映射核：逐通道 f(v) = (v & K[c]) ^ J[c]，一步覆盖 直通/反色/强制0/强制255 四态。
// 归一口径与 split/invert 完全一致：k 长度非 3 → "111"（AND 恒等）；**j 长度非 3 → "000"（XOR 恒等）**，
// 切勿照搬 normMask3 把 J 也归成 "111"（会凭空翻转语义）。alpha 字节原样保留；GRAY 防御性返回副本。
// 性能：同原色彩核——单遍「拷贝 + 变换」，每通道 AND/XOR 常量在循环外折好，循环体直写三通道、无内层循环。
core::Image channelMap(const core::Image& src, const std::string& k, const std::string& j) {
    if (src.empty() || src.isGray()) return src.clone();
    core::Image out(src.width(), src.height(), src.format());
    const std::uint8_t* s = src.data();
    std::uint8_t* d = out.data();
    const std::size_t n = src.sizeInBytes();

    const std::string km = k.size() == 3 ? k : std::string("111");
    const std::string jm = j.size() == 3 ? j : std::string("000");
    std::uint8_t ka[3], ja[3];
    for (int c = 0; c < 3; ++c) {
        ka[c] = km[c] == '1' ? 0xFF : 0x00;
        ja[c] = jm[c] == '1' ? 0xFF : 0x00;
    }
    const std::size_t bpp = core::bytesPerPixel(out.format());   // RGB=3 / RGBA=4
    for (std::size_t i = 0; i + bpp <= n; i += bpp) {
        d[i + 0] = static_cast<std::uint8_t>(s[i + 0] & ka[0] ^ ja[0]);
        d[i + 1] = static_cast<std::uint8_t>(s[i + 1] & ka[1] ^ ja[1]);
        d[i + 2] = static_cast<std::uint8_t>(s[i + 2] & ka[2] ^ ja[2]);
        if (bpp == 4) d[i + 3] = s[i + 3];
    }
    return out;
}

// 按通道反色：mask 长度必须为 3，否则视为 "111"。
// 灰度图只有单一亮度通道，逐通道选择对其不适用：mask 全 '0'（三通道都不反色）与彩色路径一致地视为空操作，否则对每字节取 255-v。
// 彩色图即 channelMap("111", mask) 特例：逐通道 XOR，alpha 始终保留不变。
core::Image invertChannels(const core::Image& src, const std::string& mask) {
    if (src.empty()) return src.clone();
    if (src.isGray()) {
        // 灰度单亮度通道：mask 全 '0' 不反色（与彩色恒等语义对齐），否则对每字节取 255-v。
        if (const std::string m = mask.size() == 3 ? mask : std::string("111"); m == "000") return src.clone();
        core::Image out(src.width(), src.height(), src.format());
        const std::size_t n = src.sizeInBytes();
        for (std::size_t i = 0; i < n; ++i) out.data()[i] = static_cast<std::uint8_t>(src.data()[i] ^ 0xFF);  // 255 - v
        return out;
    }
    const std::string m = mask.size() == 3 ? mask : std::string("111");
    return channelMap(src, "111", m);
}

// 按通道分离：mask 中 '0' 位置对应的通道被置 0，'1' 位置保留。即 channelMap(mask, "000") 特例。
// 灰度图无 R/G/B 之分，直接返回副本。
core::Image splitChannels(const core::Image& src, const std::string& mask) {
    if (src.empty() || src.isGray()) return src.clone();
    const std::string m = mask.size() == 3 ? mask : std::string("111");
    return channelMap(src, m, "000");
}

} // namespace idc::preprocess
