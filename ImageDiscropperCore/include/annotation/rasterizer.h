// ============================================================================
// 文件：include/annotation/rasterizer.h
// 作用：将 Shape 光栅化到 core::Image，包括描边、填充与 alpha 混合。
// ============================================================================
#pragma once

#include "core/color.h"
#include "core/image.h"
#include "geometry/shapes.h"

namespace idc::annotation {

// 光栅化参数：控制线宽、颜色与是否填充。
struct PaintStyle {
    core::Color color{core::kBlack};
    int strokeWidth{2};   // 描边宽度（像素），最小 1
    bool fill{false};     // 是否填充；对 LINE 类型无效
    bool antialias{true}; // 抗锯齿开关（简化实现，仅对描边生效）
};

// 将 shape 绘制到 image 上（就地修改）。
// 说明：
//   1. 若 style.fill = true 且形状非 LINE，则填充整个形状内部；
//   2. 否则沿形状轮廓按 strokeWidth 描边；
//   3. 支持 alpha 混合（style.color.a < 255 时半透明叠加）。
void rasterize(core::Image& image, const geometry::Shape& shape, const PaintStyle& style);

} // namespace idc::annotation
