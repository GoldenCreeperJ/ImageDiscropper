// ============================================================================
// 文件：src/engine/grid.cpp
// 作用：诱导网格 Grid 的“算法型”接口桩实现占位。
// 说明：不实现网格铺设算法，真正实现留待 L3（v2）阶段（终稿 §8）。
// ============================================================================
#include "engine/grid.h"

namespace idc::engine {

// 依据网格参数与图像尺寸铺设单元格。
// TODO(引擎实现阶段 v2): 按 FR-L3.1 / FR-L3.2（含余量策略）实现单元生成。
void Grid::build(const GridParams& params, const int /*imageWidth*/, const int /*imageHeight*/) {
    params_ = params;
    cells_.clear(); // 桩实现：不生成任何单元格。
}

} // namespace idc::engine
