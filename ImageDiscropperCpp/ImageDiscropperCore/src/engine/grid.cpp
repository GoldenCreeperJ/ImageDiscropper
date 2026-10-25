// ============================================================================
// 文件：src/engine/grid.cpp
// 作用：实现诱导网格阶段（Grid-Selection-Export 阶段②）——参数化铺设 Grid::build（L3，
//       含单轴三态：周期 / 分割数 / 比例，及周期轴的余量策略 FR-L3.2）、切割线诱导铺设
//       Grid::buildFromLines，以及流水线自由函数 induceGrid（SPEC §1 / §3.4）。
// 分块依据：网格是“选择与导出的最小单位”的载体；本文件只负责铺单元，
//       切割线生成见 cut_line.cpp，切分见 split.cpp。
// ============================================================================
#include "engine/engine.h"

#include <algorithm>
#include <cmath>
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
// remainder 处理：Discard 丢弃 / KeepPartial 裁剪到图像内 / Pad 保留完整尺寸（越界）。
std::vector<AxisCell> layoutAxis(const int origin, const int cell, const int limit,
                                 const RemainderPolicy remainder) {
    std::vector<AxisCell> out;
    if (cell <= 0 || limit <= 0) return out; // 防御：避免取模 0 / 死循环。
    for (int lo = virtualStart(origin, cell); lo < limit; lo += cell) {
        const int hi = lo + cell;
        const bool partial = lo < 0 || hi > limit; // 跨越左/上或右/下边界。
        if (partial && remainder == RemainderPolicy::Discard) continue; // 丢弃跨界残缺单元。
        int keepLo = lo, keepHi = hi;
        if (partial && remainder == RemainderPolicy::KeepPartial) {    // 裁剪到图像内。
            keepLo = std::max(lo, 0);
            keepHi = std::min(hi, limit);
        }
        // Pad：保留完整尺寸（keepLo/keepHi 维持越界值），导出时以填充色补齐。
        out.push_back(AxisCell{keepLo, keepHi, partial});
    }
    return out;
}

// 按份数 / 权重切分单轴：自 0 起逐格累加「比例 × 缩放因子」的四舍五入值，末格吸收全部
// 取整误差（故恒恰好铺满 [0, limit]）。份数即权重全等的特例——count=k 与 weights 全 1
// 产出同一切割线集合。分不出正宽单元时返回空（由 checkAxis 先行拦截并定位报因）。
std::vector<AxisCell> splitAxis(const std::vector<double>& weights, const int limit) {
    std::vector<AxisCell> out;
    double total = 0.0;
    for (const double w : weights) total += w;
    if (weights.empty() || limit <= 0 || total <= 0.0) return out;
    const double scale = static_cast<double>(limit) / total;
    const std::size_t last = weights.size() - 1;
    int lo = 0;
    for (std::size_t i = 0; i < last; ++i) {
        const int hi = lo + static_cast<int>(std::lround(weights[i] * scale));
        if (hi <= lo) return {}; // 零宽格：定义不可展开。
        out.push_back(AxisCell{lo, hi, false});
        lo = hi;
    }
    if (limit <= lo) return {}; // 末格无余量可占：份数/权重相对图像过粗。
    out.push_back(AxisCell{lo, limit, false}); // 末格吸收取整误差，非残缺单元。
    return out;
}

// 按单轴有效定义展开为被保留的单元区间（三态分派）。
std::vector<AxisCell> layoutSpec(const GridAxisSpec& axis, const int limit) {
    switch (axis.mode) {
        case GridAxisMode::Pitch:
            return layoutAxis(axis.origin, axis.cell, limit, axis.remainder);
        case GridAxisMode::Count:
            return splitAxis(std::vector(static_cast<std::size_t>(std::max(axis.count, 0)), 1.0), limit);
        case GridAxisMode::Weights:
            return splitAxis(axis.weights, limit);
    }
    return {};
}

} // namespace

// 取某轴的有效定义：显式定义优先，否则由平铺字段合成周期定义（余量策略取网格级 remainder）。
GridAxisSpec effectiveAxis(const GridParams& params, const bool horizontal) {
    if (const std::optional<GridAxisSpec>& own = horizontal ? params.axisX : params.axisY; own.has_value()) return *own;
    GridAxisSpec axis; // 默认即 Pitch。
    axis.origin = horizontal ? params.originX : params.originY;
    axis.cell = horizontal ? params.cellWidth : params.cellHeight;
    axis.remainder = params.remainder;
    return axis;
}

// 校验单轴能否在 [0, limit] 上展开出 ≥1 个正宽单元——与 layoutSpec 的分派一一对应，
// 供引擎在铺网格前报出精确原因（而非落到笼统的 NoGrid）。
GridAxisError checkAxis(const GridAxisSpec& axis, const int limit) {
    if (limit <= 0) return GridAxisError::CellUnderflow;
    switch (axis.mode) {
        case GridAxisMode::Pitch:
            return axis.cell > 0 ? GridAxisError::None : GridAxisError::CellUnderflow;
        case GridAxisMode::Count:
            if (axis.count < 1) return GridAxisError::CountNotPositive;
            return axis.count > limit ? GridAxisError::CountExceedsLimit : GridAxisError::None;
        case GridAxisMode::Weights: {
            if (axis.weights.empty()) return GridAxisError::WeightNotPositive;
            for (const double w : axis.weights)
                if (!(w > 0.0)) return GridAxisError::WeightNotPositive;
            if (static_cast<int>(axis.weights.size()) > limit) return GridAxisError::CellUnderflow;
            return GridAxisError::None;
        }
    }
    return GridAxisError::CellUnderflow;
}

// 依据网格参数与图像尺寸铺设单元格（FR-L3.1 / FR-L3.2 / SPEC §3.4.3）。
// 每轴按其有效定义展开：周期轴自虚拟起点 (-cw,0]×(-ch,0] 双向铺满全图、跨界残缺单元按
// 余量策略处理；分割数 / 比例轴恒恰好铺满 [0, W] × [0, H]、末格吸收取整误差。
void Grid::build(const GridParams& params, const int imageWidth, const int imageHeight) {
    params_ = params;
    cells_.clear();
    cols_ = 0;
    rows_ = 0;

    // 防御：图像尺寸非法 → 不铺任何单元；下游据 cellCount()==0 报 NoGrid。
    if (imageWidth <= 0 || imageHeight <= 0) return;

    // 分别铺出横/纵两轴被保留的单元区间，再做笛卡尔积（行主序）得到二维单元格。
    const std::vector<AxisCell> xs = layoutSpec(effectiveAxis(params, true), imageWidth);
    const std::vector<AxisCell> ys = layoutSpec(effectiveAxis(params, false), imageHeight);
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
