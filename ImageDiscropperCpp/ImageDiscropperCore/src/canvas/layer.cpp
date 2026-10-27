// ============================================================================
// 文件：src/canvas/layer.cpp
// 作用：实现 canvas/layer.h——materializeLayer（叠加层物化）与 composeCanvas（多层画布合成）。
// 分块依据：物化（配方现算 / 文件解码像素 / 逐层预处理）与合成（缩放 → 定位 → source-over 落位）
//           两段职责分离，供 GUI 按 (layerId, preprocess 签名) 缓存物化结果、改 transform 只重组。
// 说明：合成写入 width×height 的 RGBA 缓冲，越界像素写入时被边界判定自然裁掉（导出即舍弃）；
//       source-over 复用 core::compositeOver（单一真相源）。
//       底图 base（画布基座）先于叠加层铺于 (0,0)、不受图片图层显隐开关门控（它是画布本身）。
// ============================================================================
#include "canvas/layer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <vector>

#include "core/composite.h"

namespace idc::canvas {
namespace {

// 3×3 锚点 → 归一化位置分数 (fx,fy)∈{0,0.5,1}²。TopLeft=(0,0)、Center=(0.5,0.5)、BottomRight=(1,1)。
void anchorFraction(const generate::ImageAnchor a, double& fx, double& fy) {
    switch (a) {
        case generate::ImageAnchor::TopLeft:      fx = 0.0; fy = 0.0; break;
        case generate::ImageAnchor::TopCenter:    fx = 0.5; fy = 0.0; break;
        case generate::ImageAnchor::TopRight:     fx = 1.0; fy = 0.0; break;
        case generate::ImageAnchor::MiddleLeft:   fx = 0.0; fy = 0.5; break;
        case generate::ImageAnchor::Center:       fx = 0.5; fy = 0.5; break;
        case generate::ImageAnchor::MiddleRight:  fx = 1.0; fy = 0.5; break;
        case generate::ImageAnchor::BottomLeft:   fx = 0.0; fy = 1.0; break;
        case generate::ImageAnchor::BottomCenter: fx = 0.5; fy = 1.0; break;
        case generate::ImageAnchor::BottomRight:  fx = 1.0; fy = 1.0; break;
    }
}

// 图层落位左上偏移：把「图层枢轴点」对齐到「画布锚点」，再叠加自由偏移 (x,y)。
//   画布锚点（画布坐标）= (fx_c·W, fy_c·H)；图层枢轴点（图层坐标）= (fx_p·pw, fy_p·ph)。
//   offset = round(画布锚点 − 图层枢轴点) + (x,y)。center/center/(0,0) → 居中。
void placementOffset(const ImageTransform& t, const int canvasW, const int canvasH,
                     const int layerW, const int layerH, int& ox, int& oy) {
    double cfx = 0.0, cfy = 0.0, pfx = 0.0, pfy = 0.0;   // 入参锚点来自枚举解析恒合法；显式初始化兼做未覆盖枚举值的兜底（消除 GCC 未初始化告警）
    anchorFraction(t.inCanvasAnchor, cfx, cfy);
    anchorFraction(t.layerPivot, pfx, pfy);
    const double ax = cfx * canvasW;
    const double ay = cfy * canvasH;
    const double px = pfx * layerW;
    const double py = pfy * layerH;
    ox = static_cast<int>(std::llround(ax - px)) + t.x;
    oy = static_cast<int>(std::llround(ay - py)) + t.y;
}

// 逐像素 alpha 乘子：a' = (a·opacity + 127) / 255（整数四舍五入，与 composeCanvas 逐层不透明度同口径）。
//   元素外观·不透明度与节点外观·不透明度共用此口径。
core::Color scaleAlpha(const core::Color& c, const int opacity) {
    return core::Color(c.r, c.g, c.b, static_cast<std::uint8_t>((c.a * opacity + 127) / 255));
}

} // namespace   // 闭合本文件顶部（namespace idc::canvas 内）开启的匿名命名空间：其内只放内部辅助函数。

// 图片元素是否存在（见头文件）。
bool hasImageElement(const ImageLayer& layer) {
    return !layer.fileName.empty() || !layer.decodedPixels.empty() || layer.hasImagePlacement;
}

// 物化单层「整层位图」（已组装·已缩放·已预处理·未落位）——装配序（SPEC §2.2a，与 composeBase 唯一口径）：
//   图片元素先处理源（适应重采样→元素预处理）→ 区域定尺寸（有框＝box；无框＝**预处理后源尺寸**——
//   几何步改尺寸不被原尺寸裁剪/留白）→ 图案元素（生成→其预处理→其透明度）→ 图片元素落位合成施其透明度
//   → 区域缩放 → 节点外观（preprocess）。节点外观置于组装/缩放之后：色彩连图案底一起变、几何改变最终落位足迹。
//   区域（图层框）尺寸：有内层落位 → boxWidth×boxHeight；无落位 → 图片元素预处理后原生尺寸 / 图案按 transform 目标。
core::Image materializeLayer(const ImageLayer& layer) {
    const bool hasImage = hasImageElement(layer);

    // 有内层落位且框尺寸为正 → box 路径；框 ≤0（退化＝无图层框）或无落位 → 无框路径（区域＝预处理后源尺寸）。
    const bool inBox = layer.hasImagePlacement && layer.boxWidth > 0 && layer.boxHeight > 0;

    // 0) 图片元素源：适应重采样（仅 box 路径按框尺寸拉伸）→ 元素预处理（可含几何步：无框路径的区域由
    //    预处理后源尺寸定，几何步不再被原尺寸裁剪/留白）。
    core::Image src;
    if (hasImage && !layer.decodedPixels.empty()) {
        src = inBox && layer.imageFit == ImageFit::Stretch ? layer.decodedPixels.resized(layer.boxWidth, layer.boxHeight)
                                                           : layer.decodedPixels;
        if (!layer.imagePreprocess.empty()) src = layer.imagePreprocess.applyExec(src);
    }

    // 区域尺寸。
    int regionW = 0;
    int regionH = 0;
    if (inBox && layer.imageFit == ImageFit::Native && !src.empty()) {
        // Native＝图层框恒等于源图原生尺寸：区域按源尺寸收敛（调用方不保证、Core 不校验，直接收敛）。
        regionW = src.width();
        regionH = src.height();
    } else if (inBox) {
        regionW = layer.boxWidth;
        regionH = layer.boxHeight;
    } else if (!src.empty()) {
        regionW = src.width();    // 无框：区域＝预处理后源尺寸（几何步改尺寸随之变，不裁不留白）。
        regionH = src.height();
    } else if (hasImage && !layer.decodedPixels.empty()) {
        // 元素预处理失败（src 空）而解码像素仍在：按解码像素尺寸兜底定区域——仍产出图案底，不整层丢弃。
        regionW = layer.decodedPixels.width();
        regionH = layer.decodedPixels.height();
    } else {
        regionW = layer.transform.width;    // 纯图案层：区域 = transform 目标（≤0 → 空）
        regionH = layer.transform.height;
    }
    if (regionW <= 0 || regionH <= 0 || regionW > generate::kMaxDimension || regionH > generate::kMaxDimension)
        return core::Image{};   // 区域非法 → 空（与 makePattern 同口径）
    // 无图片元素且无图案元素（空占位）→ 空。
    if (!hasImage && !layer.backdrop) return core::Image{};
    // 文件源未注入解码像素：无落位 → 空占位（合成跳过）；有落位 → 仅底色框。
    if (hasImage && layer.decodedPixels.empty() && !layer.hasImagePlacement) return core::Image{};

    // 1) 图案元素：生成区域尺寸图案 → 元素预处理 → 元素透明度。
    core::Image box;
    if (layer.backdrop) {
        box = generate::makePattern(regionW, regionH, *layer.backdrop);
        if (box.empty()) {
            box = core::Image(regionW, regionH, core::ImageFormat::Rgba);
            box.fill(core::Color(0, 0, 0, 0));   // 生成失败 → 透明底（保守降级）
        }
        if (!layer.backdropPreprocess.empty()) box = layer.backdropPreprocess.applyExec(box);
        applyAlpha(box, layer.backdropOpacity);   // 元素外观·不透明度（RGBA 图案恒可施）
    }
    if (box.empty()) {
        box = core::Image(regionW, regionH, core::ImageFormat::Rgba);
        box.fill(core::Color(0, 0, 0, 0));   // 无图案元素 → 透明底
    }

    // 2) 图片元素：落位合成（逐像素施元素透明度，source-over 叠于图案之上）。
    if (!src.empty()) {
        int ox = 0;
        int oy = 0;
        if (inBox) {
            // 内层落位：锚点/枢轴/偏移复用 placementOffset（宽高留 0＝不缩放，已在 fit 体现）。
            ImageTransform inner;
            inner.inCanvasAnchor = layer.imageAnchor;
            inner.layerPivot = layer.imagePivot;
            inner.x = layer.imageX;
            inner.y = layer.imageY;
            placementOffset(inner, regionW, regionH, src.width(), src.height(), ox, oy);
        }   // 无框 / 框 ≤0：内容即源图自身（区域＝源尺寸），直接铺满叠上——不顶替图案底。
        const bool scaleImgOp = layer.imageOpacity < 255;
        for (int y = 0; y < src.height(); ++y) {
            const int dy = oy + y;
            if (dy < 0 || dy >= regionH) continue;
            for (int x = 0; x < src.width(); ++x) {
                const int dx = ox + x;
                if (dx < 0 || dx >= regionW) continue;   // 越框自然裁剪
                core::Color c = src.getPixel(x, y);
                if (scaleImgOp) c = scaleAlpha(c, layer.imageOpacity);
                if (c.a == 0) continue;
                box.setPixel(dx, dy, core::compositeOver(box.getPixel(dx, dy), c));
            }
        }
    }   // 元素预处理失败（src 空）：保守降级为仅底色框（不整层丢弃）。

    // 3) 区域缩放（transform 目标尺寸 >0 且不同才重采样；纯图案已按目标生成，此步对其恒等跳过）。
    if (const int tw = layer.transform.width, th = layer.transform.height;
        tw > 0 && th > 0 && (box.width() != tw || box.height() != th))
        box = box.resized(tw, th);
    if (box.empty()) return core::Image{};

    // 4) 节点外观（层级预处理）。
    if (layer.preprocess.empty()) return box;
    return layer.preprocess.applyExec(box);
}

// 整体淡出 RGBA 图：逐像素复用 scaleAlpha（合成期同款 alpha 乘子）。非 RGBA / 空图 / opacity>=255 → 恒等早退。
//   供合成边界（GUI fadedCanvasBase、CLI 底图解出后）施加底图整体不透明度，与 composeCanvas 逐层不透明度同口径。
void applyAlpha(core::Image& image, const std::uint8_t opacity) {
    if (image.empty() || opacity >= 255) return;
    if (image.format() != core::ImageFormat::Rgba) return;   // 无 alpha 通道→不改（避免灰度/彩色底格式漂移）
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            image.setPixel(x, y, scaleAlpha(image.getPixel(x, y), opacity));
}

core::Image composeCanvas(const std::vector<ImageLayer>& layers, const int width, const int height, const core::Image& base,
                          const std::unordered_map<int, core::Image>* materialized) {
    // 非法尺寸 / 超护栏 → 空图（与 makePattern 同口径，护栏在分配前短路）。
    if (width <= 0 || height <= 0 || width > generate::kMaxDimension || height > generate::kMaxDimension ||
        static_cast<long long>(width) * static_cast<long long>(height) > generate::kMaxTotalPixels) {
        return core::Image{};
    }

    // Image 构造即 data_.assign(...,0) 全零缓冲，RGBA 全零字节 == Color(0,0,0,0)：画布无级背景天然成立，无需再 fill 一遍。
    core::Image canvas(width, height, core::ImageFormat::Rgba);

    // 底图（画布基座，input 生态位）：非空则 source-over 铺于 (0,0)，按 w×h 自然裁剪 / 留空。
    // 无 transform、不缩放——它是画布本身，恒在所有叠加层之下。
    // 快路：此刻 canvas 恒为「全透明零底」，compositeOver(零底, src) 数学上恒等于 src 本身（da=0 时 blend 公式
    //   退化为 o.rgb=src.rgb、o.a=src.a；src.a=0 时又退化为 dst 即零底）——故 src.a=0 直接跳过（dst 本就零），
    //   src.a!=0 直接落 4 字节（绕开 compositeOver 的双精度混合与 getPixel/setPixel 的重复边界检查/格式分支）。
    if (!base.empty()) {
        const int limX = std::min(base.width(), width);
        const int limY = std::min(base.height(), height);
        for (int y = 0; y < limY; ++y) {
            std::uint8_t* dstRow = canvas.pixelPtr(0, y);
            for (int x = 0; x < limX; ++x) {
                const core::Color src = base.getPixel(x, y);
                if (src.a == 0) continue;   // 目标已透明清零，跳过写入即等价 source-over 直通
                std::uint8_t* d = dstRow + static_cast<std::size_t>(x) * 4;
                d[0] = src.r; d[1] = src.g; d[2] = src.b; d[3] = src.a;
            }
        }
    }

    // 按 zOrder 升序稳定合成（同 zOrder 维持原相对顺序）。
    std::vector<std::size_t> order(layers.size());
    std::iota(order.begin(), order.end(), 0);
    std::ranges::stable_sort(order, [&layers](const std::size_t a, const std::size_t b) {
        return layers[a].zOrder < layers[b].zOrder;
    });

    for (const std::size_t i : order) {
        const ImageLayer& layer = layers[i];
        // 物化取用：预物化表命中则直取（跳过 materializeLayer，避免拖动时重跑预处理），否则现算。
        core::Image tmp;
        const core::Image* placed = nullptr;
        if (materialized != nullptr) {
            if (const auto it = materialized->find(layer.id); it != materialized->end() && !it->second.empty())
                placed = &it->second;
        }
        if (placed == nullptr) {
            tmp = materializeLayer(layer);   // 已组装（框+底色）→缩放→预处理的整层位图
            placed = &tmp;
        }
        if (placed->empty()) continue;

        const int pw = placed->width();
        const int ph = placed->height();
        const int op = std::clamp(layer.transform.opacity, 0, 255);
        const bool scaleOp = op < 255;
        int ox = 0;
        int oy = 0;
        placementOffset(layer.transform, width, height, pw, ph, ox, oy);

        // 快路：canvas 恒为 RGBA（本函数自建），直接用 pixelPtr 定位行首 + 手写四字节读写，绕开 setPixel/getPixel
        //   重复的边界检查与格式分支；仅当 src 为半透明（alpha 非 0/255）时才需真正跑一次 compositeOver 混合公式
        //   （与逐字原语义逐像素一致）。
        for (int y = 0; y < ph; ++y) {
            const int dy = oy + y;
            if (dy < 0 || dy >= height) continue;   // 越出画布自然裁剪（整行跳过）
            std::uint8_t* dstRow = canvas.pixelPtr(0, dy);
            for (int x = 0; x < pw; ++x) {
                const int dx = ox + x;
                if (dx < 0 || dx >= width) continue;   // 越出画布自然裁剪
                core::Color src = placed->getPixel(x, y);
                if (scaleOp) src = scaleAlpha(src, op);   // 层不透明度（灰度层亦生效）
                std::uint8_t* d = dstRow + static_cast<std::size_t>(dx) * 4;
                if (src.a == 0) continue;   // source-over 直通：src 全透明 → dst 不变
                if (src.a == 255) {
                    d[0] = src.r; d[1] = src.g; d[2] = src.b; d[3] = 255;
                    continue;
                }
                const core::Color out = core::compositeOver(core::Color(d[0], d[1], d[2], d[3]), src);
                d[0] = out.r; d[1] = out.g; d[2] = out.b; d[3] = out.a;
            }
        }
    }
    return canvas;
}

core::Image composeBase(const ImageLayer& base, const int width, const int height,
                        const std::unordered_map<int, core::Image>* materialized) {
    // 纯图案底图（有 backdrop 且无图片元素）：按画布(source)尺寸现算图案（区域=画布），元素外观后接节点外观。
    //   不走 materializeLayer——其无落位图案路按 transform 目标现算，恒等 transform 尺寸为 0 会得空图。
    if (base.backdrop && !hasImageElement(base)) {
        core::Image pat = generate::makePattern(width, height, *base.backdrop);
        if (pat.empty()) return core::Image{};
        if (!base.backdropPreprocess.empty()) pat = base.backdropPreprocess.applyExec(pat);
        applyAlpha(pat, base.backdropOpacity);
        return base.preprocess.empty() ? pat : base.preprocess.applyExec(pat);
    }
    // 文件底图 / 带内层落位：当作单元素 layers、空底、transform 恒等，转调 composeCanvas 复用装配链。
    //   composeCanvas 会按元素 transform.opacity 施乘，故此处把 opacity 归一 255——底图整体透明度一律
    //   由调用方在合成边界经 applyAlpha 施加（单一应用点，防双淡）。
    ImageLayer normalized = base;
    normalized.transform.opacity = 255;
    return composeCanvas({normalized}, width, height, {}, materialized);
}

std::optional<LayerPlacement> layerPlacement(const ImageLayer& layer, const int canvasWidth, const int canvasHeight) {
    const core::Image placed = materializeLayer(layer);   // 已组装·缩放·预处理的整层位图（与 composeCanvas 落位尺寸同口径）
    if (placed.empty()) return std::nullopt;
    const int pw = placed.width();
    const int ph = placed.height();
    int ox = 0;
    int oy = 0;
    placementOffset(layer.transform, canvasWidth, canvasHeight, pw, ph, ox, oy);
    return LayerPlacement{ ox, oy, pw, ph };
}

} // namespace idc::canvas
