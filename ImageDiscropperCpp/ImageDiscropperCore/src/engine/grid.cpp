// ============================================================================
// 文件：src/engine/grid.cpp
// 作用：实现诱导网格阶段（Grid-Selection-Emit 阶段②）——参数化铺设 Grid::build（L3，
//       含余量策略 FR-L3.2）、切割线诱导铺设 Grid::buildFromLines，以及流水线自由函数
//       induceGrid（SPEC §1 / §3.4）。
// 分块依据：网格是“选择与导出的最小单位”的载体；本文件只负责铺单元，
//       切割线生成见 cut_line.cpp，切分见 split.cpp。
// ============================================================================
#include "engine/engine.h"

#include <algorithm>
#include <vector>

namespace idc::engine {
namespace {

// 单轴上被保留的一个单元区间 [lo, hi)（已按余量策略裁剪 / 保留）；
// partial 标记该单元是否跨越图像边界（残缺单元）。
struct AxisCell {
    int lo{0};
    int hi{0};
    bool partial{false};
};

// 计算单轴的虚拟起点：由基准点对单元尺寸取模倒推，落在 (-cell, 0]，
// 保证基准点恒为一条切割线（相位锚）。对正负 origin 均成立（C++ 取模符号随被除数）。
int virtualStart(const int origin, const int cell) {
    int s = origin % cell;
    if (s > 0) s -= cell;
    return s; // s ∈ (-cell, 0]
}

// 依据基准点、单元尺寸、图像边界与余量策略，铺出单轴上全部被保留的单元区间。
// 自虚拟起点 (-cell,0] 起，以 cell 为周期向右/下推进，直到起点越过图像边界为止，
// 从而双向铺满整幅图像（切割线贯穿全图，SPEC §3.4.3）；跨越边界的残缺单元按
// remainder 处理：DISCARD 丢弃 / KEEP_PARTIAL 裁剪到图像内 / PAD 保留完整尺寸（越界）。
std::vector<AxisCell> layoutAxis(const int origin, const int cell, const int limit,
                                 const RemainderPolicy remainder) {
    std::vector<AxisCell> out;
    for (int lo = virtualStart(origin, cell); lo < limit; lo += cell) {
        const int hi = lo + cell;
        const bool partial = lo < 0 || hi > limit; // 跨越左/上或右/下边界。
        if (partial && remainder == RemainderPolicy::DISCARD) continue; // 丢弃跨界残缺单元。
        int keepLo = lo, keepHi = hi;
        if (partial && remainder == RemainderPolicy::KEEP_PARTIAL) {    // 裁剪到图像内。
            keepLo = std::max(lo, 0);
            keepHi = std::min(hi, limit);
        }
        // PAD：保留完整尺寸（keepLo/keepHi 维持越界值），导出时以填充色补齐。
        out.push_back(AxisCell{keepLo, keepHi, partial});
    }
    return out;
}

} // namespace

// 依据网格参数与图像尺寸铺设单元格（FR-L3.1 / FR-L3.2 / SPEC §3.4.3）。
// 网格仅由「基准点 + 单元尺寸」定义：以基准点为相位锚、单元尺寸为周期，自虚拟起点
// (-cw,0]×(-ch,0] 双向铺满全图，行列数由图像边界自动推导；跨界残缺单元按余量策略处理。
// origin=0 时虚拟起点即 0，等价于自左/上边界单向周期铺满。
void Grid::build(const GridParams& params, const int imageWidth, const int imageHeight) {
    params_ = params;
    cells_.clear();
    cols_ = 0;
    rows_ = 0;

    // 防御：单元尺寸或图像尺寸非法 → 不铺任何单元（避免取模 0 / 死循环，对应 E-5）。
    if (params.cellWidth <= 0 || params.cellHeight <= 0 || imageWidth <= 0 || imageHeight <= 0) {
        return;
    }

    // 分别铺出横/纵两轴被保留的单元区间，再做笛卡尔积（行主序）得到二维单元格。
    const std::vector<AxisCell> xs =
        layoutAxis(params.originX, params.cellWidth, imageWidth, params.remainder);
    const std::vector<AxisCell> ys =
        layoutAxis(params.originY, params.cellHeight, imageHeight, params.remainder);
    cols_ = static_cast<int>(xs.size());
    rows_ = static_cast<int>(ys.size());

    // 逐单元生成（行主序，index = r*cols_ + c）；任一轴跨界即标记 partial。
    for (int r = 0; r < rows_; ++r) {
        for (int c = 0; c < cols_; ++c) {
            Cell cell;
            cell.row = r;
            cell.col = c;
            cell.index = r * cols_ + c;
            cell.area = RectRegion(xs[c].lo, ys[r].lo, xs[c].hi, ys[r].hi);
            cell.partial = xs[c].partial || ys[r].partial;
            cells_.push_back(cell);
        }
    }
}

// 由切割线集合诱导铺设 m×n 单元（SPEC §1 阶段②）。
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
    // 诱导网格无 GridParams 语义；派生行列数写入 cols_/rows_ 供下游一致访问。
    params_ = GridParams{};
    cols_ = ncols;
    rows_ = nrows;
}

// 由切割线集合诱导网格（阶段②，流水线自由函数）。
// 切割线已经 normalizeCutLines 补入边界 0 与 W/H，故 source 尺寸隐含于 lines。
Grid induceGrid(const CutLineSet& lines, const SourceInfo& /*source*/) {
    Grid grid;
    grid.buildFromLines(lines.xs, lines.ys);
    return grid;
}

} // namespace idc::engine
