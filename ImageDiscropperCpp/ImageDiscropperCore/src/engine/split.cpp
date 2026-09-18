// ============================================================================
// 文件：src/engine/split.cpp
// 作用：实现 Grid-Selection-Emit 的 split 阶段（SPEC §1 概念流程）——按诱导网格
//       把图像切分为互不重叠、恰好铺满的区域集合 RegionSet。
// 分块依据：split 只产出“区域描述”（RectRegion + 单元序号），不搬运像素；真正的
//       像素裁剪在导出阶段（export.cpp）用 Image::crop 完成，避免中间态持有大量
//       像素副本（NFR-8 大图友好）。故本文件独立于合成与导出。
// ============================================================================
#include "engine/split.h"

#include <algorithm>

namespace idc::engine {

// 按网格将图像切分为区域集合。
// 遍历网格单元，跳过“实际像素为空”的块（E-4 贴边选框 / 越界单元），
// 产出带单元序号的 Fragment——序号贯穿后续 applyPolarity 与 compose。
RegionSet split(const core::Image& image, const Grid& grid) {
    RegionSet out;
    for (const Cell& cell : grid.cells()) {
        // 计算单元与图像的实际交集，用于空块判定（不改写 cell.area 本身）。
        const int l = std::max(cell.area.left, 0);
        const int t = std::max(cell.area.top, 0);
        const int r = std::min(cell.area.right, image.width());
        if (const int b = std::min(cell.area.bottom, image.height()); r <= l || b <= t) continue; // 实际像素为空 → 跳过空块（E-4）。

        // 保留原始 cell.area：PAD 余量策略下 area 可能超出图像边界，
        // 导出时 Image::crop 会自动裁剪、画布以 padColor 补白，从而天然实现“补白至完整单元”。
        // 同时记录行列号，供分离导出的 {row}/{col} 命名模板使用。
        out.add(cell.area, cell.index, RegionKind::RECT, cell.row, cell.col);
    }
    return out;
}

} // namespace idc::engine
