// ============================================================================
// 文件：include/annotation/rasterizer.h
// 作用：将 Shape 光栅化到 core::Image，包括描边、填充与 alpha 混合。
// ============================================================================
#pragma once

#include "core/color.h"
#include "core/core_nums.h"
#include "core/image.h"
#include "geometry/shapes.h"

namespace idc::annotation {

// 光栅化参数：控制边框/描边色、填充色、线宽与是否填充。
struct PaintStyle {
    core::Color color{core::kBlack};        // 边框 / 描边色
    core::Color fillColor{core::kBlack};    // 填充色（fill 为真且非 Line 时用于内部）
    int strokeWidth{core::nums::annotation::kDefaultStrokeWidth};   // 描边宽度（像素），最小 1
    bool fill{false};     // 是否填充；对 Line 类型无效
    bool antialias{true}; // 抗锯齿开关（简化实现，仅对描边生效）
};

// 将 shape 绘制到 image 上（就地修改）。
// 说明：
//   1. 若 style.fill = true 且形状非 Line，先用 style.fillColor 填充整个内部；
//   2. 随后始终按 style.color 以 strokeWidth 描边（填充形状因此也带边框，与画布预览一致）；
//   3. 支持 alpha 混合（颜色 a < 255 时半透明叠加）。
void rasterize(core::Image& image, const geometry::Shape& shape, const PaintStyle& style);

} // namespace idc::annotation
