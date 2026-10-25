// ============================================================================
// 文件：include/engine/split.h
// 作用：声明 Grid-Selection-Export 的 split 阶段（SPEC §1 概念流程）——按诱导网格
//       把图像切分为互不重叠、恰好铺满的区域集合 RegionSet。
// 分块依据：split 是独立的流水线阶段，声明从 engine.h facade 下沉至此，使 split.cpp 只
//       依赖本阶段头（core::Image + Grid + RegionSet），不全量 include engine.h（保持解耦）。
// 说明：split 只产出“区域描述”（RectRegion + 单元序号），不搬运像素；像素裁剪在导出
//       阶段（export.cpp）用 Image::crop 完成。定义见 src/engine/split.cpp。
// ============================================================================
#pragma once

#include "core/image.h"
#include "engine/grid.h"
#include "engine/region_set.h"

namespace idc::engine {

// 按网格将图像切分为互不重叠、恰好铺满的区域集合（跳过实际像素为空的块，E-4）。
// 产出带单元序号的 Fragment——序号贯穿后续 applyPolarity 与 compose。
RegionSet split(const core::Image& image, const Grid& grid);

// 纯尺寸版 split：split 仅需图像宽高（从不读像素），此入口供「仅尺寸」引擎路径（实时预览）复用，
// 使其脱离全分辨率像素。语义与 split 完全一致，仅以 (width,height) 取代 image。
RegionSet splitSize(int width, int height, const Grid& grid);

} // namespace idc::engine
