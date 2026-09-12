// ============================================================================
// 文件：src/engine/grid.cpp
// 作用：实现诱导网格阶段（Grid-Selection-Emit 阶段②）——参数化铺设 Grid::build（L3，
//       含余量策略 FR-L3.2）、切割线诱导铺设 Grid::buildFromLines，以及流水线自由函数
//       induceGrid（终稿 §2 / §4.4）。
// 分块依据：网格是“选择与导出的最小单位”的载体；本文件只负责铺单元，
//       切割线生成见 cut_line.cpp，切分见 split.cpp。
// ============================================================================
#include "engine/engine.h"

namespace idc::engine {

// 依据网格参数与图像尺寸铺设单元格（FR-L3.1 / FR-L3.2）。
// 处理基准点、单元尺寸、间距、行列数（<=0 自动铺满）与余量策略（E-5）。
void Grid::build(const GridParams& params, const int imageWidth, const int imageHeight) {
    params_ = params;
    cells_.clear();

    const int stepX = params.cellWidth + params.gapX;
    const int stepY = params.cellHeight + params.gapY;
    // 非法单元尺寸：不铺任何单元（防御，避免死循环）。
    if (params.cellWidth <= 0 || params.cellHeight <= 0 || stepX <= 0 || stepY <= 0) {
        params_.cols = 0;
        params_.rows = 0;
        return;
    }

    // 目标行列数：显式指定则用之，否则给足够上界由截断循环决定（自动铺满）。
    const int colsWanted = params.cols > 0 ? params.cols : imageWidth + 1;
    const int rowsWanted = params.rows > 0 ? params.rows : imageHeight + 1;

    // 按余量策略截断，求实际列数：左边界越界 → 停；不足整单元且 DISCARD → 停。
    int ncols = 0;
    for (int c = 0; c < colsWanted; ++c) {
        const int left = params.originX + c * stepX;
        if (left >= imageWidth) break;
        if (left + params.cellWidth > imageWidth &&
            params.remainder == RemainderPolicy::DISCARD) break;
        ++ncols;
    }
    int nrows = 0;
    for (int r = 0; r < rowsWanted; ++r) {
        const int top = params.originY + r * stepY;
        if (top >= imageHeight) break;
        if (top + params.cellHeight > imageHeight &&
            params.remainder == RemainderPolicy::DISCARD) break;
        ++nrows;
    }

    // 逐单元生成（行主序，index = r*ncols + c），并按余量策略确定右/下边界与 partial 标记。
    for (int r = 0; r < nrows; ++r) {
        for (int c = 0; c < ncols; ++c) {
            const int left = params.originX + c * stepX;
            const int top  = params.originY + r * stepY;
            int right  = left + params.cellWidth;
            int bottom = top  + params.cellHeight;
            bool partial = false;
            // 横向余量：KEEP_PARTIAL 裁剪到图像内；PAD 保留完整尺寸（导出时补白）。
            if (right > imageWidth) {
                partial = true;
                if (params.remainder == RemainderPolicy::KEEP_PARTIAL) right = imageWidth;
            }
            // 纵向余量：同理。
            if (bottom > imageHeight) {
                partial = true;
                if (params.remainder == RemainderPolicy::KEEP_PARTIAL) bottom = imageHeight;
            }
            Cell cell;
            cell.row = r;
            cell.col = c;
            cell.index = r * ncols + c;
            cell.area = RectRegion(left, top, right, bottom);
            cell.partial = partial;
            cells_.push_back(cell);
        }
    }

    // 写回解析后的行列数，供 isCollapsible / compose 使用。
    params_.cols = ncols;
    params_.rows = nrows;
}

// 由切割线集合诱导铺设 m×n 单元（终稿 §2 阶段②）。
// 单元 C[i][j] = [xs[i], xs[i+1]) × [ys[j], ys[j+1])，行主序编号。
void Grid::buildFromLines(const std::vector<int>& xs, const std::vector<int>& ys) {
    cells_.clear();
    const int ncols = xs.empty() ? 0 : static_cast<int>(xs.size()) - 1;
    const int nrows = ys.empty() ? 0 : static_cast<int>(ys.size()) - 1;
    for (int r = 0; r < nrows; ++r) {
        for (int c = 0; c < ncols; ++c) {
            Cell cell;
            cell.row = r;
            cell.col = c;
            cell.index = r * ncols + c;
            cell.area = RectRegion(xs[c], ys[r], xs[c + 1], ys[r + 1]);
            cell.partial = false; // 线诱导网格恰好铺满原图，无余量残缺。
            cells_.push_back(cell);
        }
    }
    // 诱导网格无 GridParams 语义，但仍写回 cols/rows 供下游一致访问。
    params_ = GridParams{};
    params_.cols = ncols;
    params_.rows = nrows;
}

// 由切割线集合诱导网格（阶段②，流水线自由函数）。
// 切割线已经 normalizeCutLines 补入边界 0 与 W/H，故 source 尺寸隐含于 lines。
Grid induceGrid(const CutLineSet& lines, const SourceInfo& /*source*/) {
    Grid grid;
    grid.buildFromLines(lines.xs, lines.ys);
    return grid;
}

} // namespace idc::engine
