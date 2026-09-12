// ============================================================================
// 文件：src/engine/composition.cpp
// 作用：实现合成阶段（Grid-Selection-Emit 阶段⑤）——isCollapsible 判定坍缩可行性
//       （终稿 §5.4 定理），compose 依据输出模式生成合成描述（分离 / 坍缩 / 重排，§5.1-§5.3）。
// 分块依据：本文件只计算“怎么摆”（画布尺寸 + 各片段落位），不搬运像素；
//       落盘见 export.cpp，序列见 sequence.cpp。
// ============================================================================
#include "engine/composition.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace idc::engine {
namespace {

// 辅助：合并区间——排序后把重叠/相邻区间并为不重叠的有序区间集。
void mergeIntervals(std::vector<std::pair<int, int>>& iv) {
    if (iv.empty()) return;
    std::sort(iv.begin(), iv.end());
    std::vector<std::pair<int, int>> merged;
    merged.push_back(iv.front());
    for (std::size_t i = 1; i < iv.size(); ++i) {
        if (iv[i].first <= merged.back().second) {
            merged.back().second = std::max(merged.back().second, iv[i].second);
        } else {
            merged.push_back(iv[i]);
        }
    }
    iv.swap(merged);
}

// 辅助：区间集总跨度（各段宽度/高度之和）。
int totalSpan(const std::vector<std::pair<int, int>>& iv) {
    int sum = 0;
    for (const auto& s : iv) sum += s.second - s.first;
    return sum;
}

// 辅助：坍缩偏移——start 左侧（上方）所有保留区间的累计跨度。
// 即 destCoord = start - （start 左侧被删除的总宽），等价于左侧保留总宽。
int collapsedOffset(const std::vector<std::pair<int, int>>& iv, const int start) {
    int acc = 0;
    for (const auto& seg : iv) {
        if (seg.first >= start) break;                              // 已达/超过 start，停止
        if (seg.second <= start) acc += seg.second - seg.first;     // 完整保留段在左侧
        else acc += start - seg.first;                              // 跨越 start，计左侧部分
    }
    return acc;
}

// 按序列把保留块排成输出顺序（分离与重排共用）。sequence.order()[k]=单元序号，
// 仅保留属于 kept 的序号；再补入序列未覆盖的块（含 index<0 的兼容情形），保持原顺序。
std::vector<const Fragment*> orderBySequence(const std::vector<Fragment>& frags,
                                             const Sequence& sequence) {
    std::vector<const Fragment*> ordered;
    ordered.reserve(frags.size());
    int maxIdx = -1;
    for (const Fragment& f : frags) maxIdx = std::max(maxIdx, f.index);
    if (!sequence.empty() && maxIdx >= 0) {
        std::vector<const Fragment*> byIndex(static_cast<std::size_t>(maxIdx) + 1, nullptr);
        for (const Fragment& f : frags) {
            if (f.index >= 0 && f.index <= maxIdx) byIndex[f.index] = &f;
        }
        for (const int idx : sequence.order()) {
            if (idx >= 0 && idx <= maxIdx && byIndex[idx]) ordered.push_back(byIndex[idx]);
        }
    }
    std::vector<char> used(static_cast<std::size_t>(maxIdx) + 1, 0);
    for (const Fragment* f : ordered) {
        if (f->index >= 0 && f->index <= maxIdx) used[f->index] = 1;
    }
    for (const Fragment& f : frags) {
        const bool isUsed = (f.index >= 0 && f.index <= maxIdx) ? used[f.index] != 0 : false;
        if (!isUsed) ordered.push_back(&f);
    }
    return ordered;
}

} // namespace

// 判定保留集能否无空洞、无重叠地坍缩为矩形图（终稿 §5.4 定理）：
// 当且仅当被剔除单元恰好构成诱导网格中的若干整行和/或整列。
bool isCollapsible(const Selection& selection, const Grid& grid) {
    const int rows = grid.rowCount();
    const int cols = grid.colCount();
    if (rows <= 0 || cols <= 0) return false; // 空网格无坍缩意义。

    // 标记保留集（removed = 全集 \ 保留集）。
    const std::vector<std::size_t> keptIndices = selection.resolve();
    std::vector<char> kept(grid.cellCount(), 0);
    for (const std::size_t idx : keptIndices) {
        if (idx < kept.size()) kept[idx] = 1;
    }
    const auto isRemoved = [&](const Cell& cell) {
        return !(cell.index >= 0 &&
                 static_cast<std::size_t>(cell.index) < kept.size() &&
                 kept[cell.index]);
    };

    // 统计每行/每列被剔除的单元数。
    std::vector<int> removedInRow(rows, 0), removedInCol(cols, 0);
    for (const Cell& cell : grid.cells()) {
        if (!isRemoved(cell)) continue;
        if (cell.row >= 0 && cell.row < rows) ++removedInRow[cell.row];
        if (cell.col >= 0 && cell.col < cols) ++removedInCol[cell.col];
    }

    // 整行/整列全删标记。
    std::vector<char> fullRow(rows, 0), fullCol(cols, 0);
    for (int r = 0; r < rows; ++r) fullRow[r] = (removedInRow[r] == cols) ? 1 : 0;
    for (int c = 0; c < cols; ++c) fullCol[c] = (removedInCol[c] == rows) ? 1 : 0;

    // 校验：每个被剔除单元必须落在某全删行或某全删列；否则出现“孤立”剔除，不可坍缩。
    for (const Cell& cell : grid.cells()) {
        if (!isRemoved(cell)) continue;
        const bool inFullRow = (cell.row >= 0 && cell.row < rows && fullRow[cell.row]);
        const bool inFullCol = (cell.col >= 0 && cell.col < cols && fullCol[cell.col]);
        if (!inFullRow && !inFullCol) return false;
    }
    return true;
}

// 依据序列与布局把保留块合成为输出描述（阶段⑤）。
// 三种输出：SEPARATE（多图分离）/ MERGED+COLLAPSE（坍缩）/ MERGED+REARRANGE（重排）。
Composition compose(const RegionSet& kept, const Sequence& sequence,
                    const CompositionParams& params) {
    Composition comp;
    // 透传导出设置，使 exportImage 能按配置决定格式 / 命名 / 质量（无需再依赖路径扩展名）。
    comp.format = params.format;
    comp.naming = params.naming;
    comp.quality = params.quality;
    comp.padColor = params.padColor; // 透传填充色，供 exportMerged 填充画布（修复 --pad-color 不生效）。
    const std::vector<Fragment>& frags = kept.fragments();
    if (frags.empty()) return comp; // 空保留集：返回空合成（E-7 由上层处理）。

    // ---- 分离模式：每块独立成图，dest 从各自 (0,0) 起，无统一画布。----
    // 按 sequence 排序（修复“分离导出忽略 --sort”），落盘顺序即排序结果。
    if (params.mode == EmitMode::SEPARATE) {
        comp.canvasWidth = 0;
        comp.canvasHeight = 0;
        const std::vector<const Fragment*> ordered = orderBySequence(frags, sequence);
        int out = 0;
        for (const Fragment* f : ordered) {
            Placement p;
            p.source = f->region;
            p.dest = RectRegion(0, 0, f->region.width(), f->region.height());
            p.index = out++;
            p.row = f->row;
            p.col = f->col;
            comp.placements.push_back(p);
        }
        return comp;
    }

    // ---- 坍缩合并（§5.2）：由 kept 区域的 x/y 投影并集推导保留列/行区间。----
    if (params.layout == MergeLayout::COLLAPSE) {
        std::vector<std::pair<int, int>> xs, ys;
        xs.reserve(frags.size());
        ys.reserve(frags.size());
        for (const Fragment& f : frags) {
            xs.push_back({f.region.left, f.region.right});
            ys.push_back({f.region.top, f.region.bottom});
        }
        mergeIntervals(xs);
        mergeIntervals(ys);
        comp.canvasWidth = totalSpan(xs);   // = W − Δx
        comp.canvasHeight = totalSpan(ys);  // = H − Δy

        int out = 0;
        for (const Fragment& f : frags) {
            const int dx = collapsedOffset(xs, f.region.left);
            const int dy = collapsedOffset(ys, f.region.top);
            Placement p;
            p.source = f.region;
            p.dest = RectRegion(dx, dy, dx + f.region.width(), dy + f.region.height());
            p.index = out++;
            p.row = f.row;
            p.col = f.col;
            comp.placements.push_back(p);
        }
        return comp;
    }

    // ---- 重排合并（§5.3）：按 sequence 序填入 cols×rows 画布，空位补 padColor。----
    // 单元尺寸缺省取 kept 区域的最大宽/高（不缩放，纯像素搬运，NFR-2）。
    int cw = 0, ch = 0;
    for (const Fragment& f : frags) {
        cw = std::max(cw, f.region.width());
        ch = std::max(ch, f.region.height());
    }
    if (params.cellWidth.has_value()) cw = *params.cellWidth;
    if (params.cellHeight.has_value()) ch = *params.cellHeight;
    if (cw <= 0) cw = 1;
    if (ch <= 0) ch = 1;

    const int keptCount = static_cast<int>(frags.size());
    int cols = params.cols.value_or(keptCount > 0 ? keptCount : 1);
    if (cols <= 0) cols = 1;
    int rows = params.rows.value_or((keptCount + cols - 1) / cols);
    if (rows <= 0) rows = 1;
    if (cols * rows < keptCount) rows = (keptCount + cols - 1) / cols; // 容纳不下时扩展行数。
    comp.canvasWidth = cols * cw;
    comp.canvasHeight = rows * ch;

    // 按 sequence 顺序取出 kept 块（分离与重排共用同一排序助手）。
    const std::vector<const Fragment*> ordered = orderBySequence(frags, sequence);

    // 按填充顺序落位到 cols×rows 画布格子（第 k 个 → 行 k/cols、列 k%cols）。
    int out = 0;
    for (const Fragment* f : ordered) {
        const int r = out / cols;
        const int c = out % cols;
        if (r >= rows) break; // 超出画布行数（cols*rows 已保证容纳，此为防御）。
        Placement p;
        p.source = f->region;
        p.dest = RectRegion(c * cw, r * ch, c * cw + f->region.width(), r * ch + f->region.height());
        p.index = out;
        p.row = f->row;
        p.col = f->col;
        comp.placements.push_back(p);
        ++out;
    }
    return comp;
}

} // namespace idc::engine
