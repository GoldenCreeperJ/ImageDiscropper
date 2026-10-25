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

// scaleToTarget / composeIntoBox 定义在本文件下方 anon namespace（同名 anon namespace 合并）；
//   materializeLayer 需先组装（框+底色）→缩放→后预处理，故在此前置声明供其调用。
core::Image scaleToTarget(core::Image placed, const ImageLayer& layer);
core::Image composeIntoBox(const ImageLayer& layer, const core::Image& src);

} // namespace   // 闭合本文件顶部（namespace idc::canvas 内）开启的匿名命名空间：其内只放内部辅助函数与上述前置声明。

// 物化单层「整层位图」（已组装·已缩放·已预处理·未落位）：先取源（配方按 transform 目标尺寸现算 / 文件用注入的解码像素 / 空→空），
// 启用两级落位时把源合成进带 boxBackdrop 底色的图层框，再按 transform 缩放到目标尺寸，**最后**对整层跑 preprocess。
//   preprocess 置于组装/缩放之后是其语义关键：色彩连图层框底色一起变、几何（旋转/翻转/resize）改变最终落位足迹（不被 scaleToTarget 拉回旧框）。
core::Image materializeLayer(const ImageLayer& layer) {
    core::Image raw;
    if (layer.recipe) {
        raw = generate::makePattern(layer.transform.width, layer.transform.height, *layer.recipe);   // 配方源：按目标尺寸现算
    } else if (!layer.decodedPixels.empty()) {
        raw = layer.decodedPixels;   // 文件源：上层解码注入；未注入即空图（占位/降级）
    }
    // 组装整层：有内层落位→源铺进图层框（源空时仍产出纯底色框，保留现语义）；无落位→直接用源（空则短路）。
    core::Image content = raw;
    if (layer.hasImagePlacement) {
        content = composeIntoBox(layer, raw);
    } else if (content.empty()) {
        return core::Image{};
    }
    // 按 transform 目标尺寸缩放后，再对整层跑预处理。
    core::Image placed = scaleToTarget(std::move(content), layer);
    if (placed.empty()) return core::Image{};
    if (layer.preprocess.empty()) return placed;
    return layer.preprocess.applyExec(placed);
}

namespace {

// 合成期缩放：目标宽高均 > 0 且与组装尺寸不同才重采样（配方已在取源时按目标尺寸生成，此处自然跳过）。
//   由 materializeLayer 在组装后调用，使落位尺寸口径与拾取（layerPlacement）一致（单一真相源）。
core::Image scaleToTarget(core::Image placed, const ImageLayer& layer) {
    if (placed.empty()) return placed;
    const int tw = layer.transform.width;
    if (const int th = layer.transform.height; tw > 0 && th > 0 && (placed.width() != tw || placed.height() != th)) {
        placed = placed.resized(tw, th);
    }
    return placed;
}

// 内层：源图→图层框。以 boxBackdrop 图案（空→透明）铺 boxWidth×boxHeight 的底，再将解码源按 imageFit +
//   内层锚点/枢轴/偏移 source-over 落入、越框裁剪。fit==Stretch 先重采样铺满框；图域框非法则退回原生源（交给外层）。
core::Image composeIntoBox(const ImageLayer& layer, const core::Image& src) {
    const int bw = layer.boxWidth;
    const int bh = layer.boxHeight;
    if (bw <= 0 || bh <= 0 || bw > generate::kMaxDimension || bh > generate::kMaxDimension) return src;
    core::Image box = layer.boxBackdrop ? generate::makePattern(bw, bh, *layer.boxBackdrop) : core::Image{};
    if (box.empty()) {
        box = core::Image(bw, bh, core::ImageFormat::Rgba);
        box.fill(core::Color(0, 0, 0, 0));   // 无底色图案（或 makePattern 失败）→ 透明底
    }
    if (src.empty()) return box;
    const core::Image placed = layer.imageFit == ImageFit::Stretch ? src.resized(bw, bh) : src;
    // 内层落位复用 placementOffset（宽高留 0＝不缩放，已在 placed 体现）。
    ImageTransform inner;
    inner.inCanvasAnchor = layer.imageAnchor;
    inner.layerPivot = layer.imagePivot;
    inner.x = layer.imageX;
    inner.y = layer.imageY;
    int ox = 0;
    int oy = 0;
    placementOffset(inner, bw, bh, placed.width(), placed.height(), ox, oy);
    for (int y = 0; y < placed.height(); ++y) {
        for (int x = 0; x < placed.width(); ++x) {
            const int dx = ox + x;
            const int dy = oy + y;
            if (dx < 0 || dy < 0 || dx >= bw || dy >= bh) continue;   // 越框自然裁剪
            box.setPixel(dx, dy, core::compositeOver(box.getPixel(dx, dy), placed.getPixel(x, y)));
        }
    }
    return box;
}

// 逐像素 alpha 乘子：a' = (a·opacity + 127) / 255（整数四舍五入，与 composeCanvas 逐层不透明度同口径）。
core::Color scaleAlpha(const core::Color& c, const int opacity) {
    return core::Color(c.r, c.g, c.b, static_cast<std::uint8_t>((c.a * opacity + 127) / 255));
}

} // namespace

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
    // 纯图案底图（有 recipe 且无内层落位）：按画布(source)尺寸经 makePattern 现算图案。
    //   不走 materializeLayer（其配方路用 transform.width/height 现算，恒等 transform 尺寸为 0 会得空图）。
    if (base.recipe && !base.hasImagePlacement) {
        core::Image pat = generate::makePattern(width, height, *base.recipe);
        if (pat.empty()) return core::Image{};
        return base.preprocess.empty() ? pat : base.preprocess.applyExec(pat);
    }
    // 文件底图 / 带内层落位：当作单元素 layers、空底、transform 恒等，转调 composeCanvas 复用两段合成。
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
