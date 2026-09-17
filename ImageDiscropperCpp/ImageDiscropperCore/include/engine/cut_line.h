// ============================================================================
// 文件：include/engine/cut_line.h
// 作用：定义切割线（cut line）与切割线集合 CutLineSet——Grid-Selection-Emit
//       流水线阶段 ①（终稿 §2）的数据载体。
// 分块依据：
//   - 切割线是本工具区别于常规裁剪的公理：贯穿全图的“直线”，而非线段（§3.2）。
//   - 本文件只描述切割线数据与归一化接口，具体“如何由选框/网格生成切割线”
//     属于 engine.h 的 generateCutLines 职责，不在此实现。
// 说明：normalizeCutLines 与 generateCutLines 的真实实现见 src/engine/cut_line.cpp。
// ============================================================================
#pragma once

#include <cstddef>
#include <vector>

namespace idc::engine {

// ---------------------------------------------------------------------------
// CutOrientation：切割线方向。
// ---------------------------------------------------------------------------
enum class CutOrientation {
    VERTICAL,    // 竖线 X = const，贯穿全高
    HORIZONTAL,  // 横线 Y = const，贯穿全宽
};

// ---------------------------------------------------------------------------
// CutLine：单条切割线（方向 + 位置）。
// ---------------------------------------------------------------------------
struct CutLine {
    CutOrientation orientation{CutOrientation::VERTICAL};
    int position{0};  // 竖线为 x 坐标，横线为 y 坐标
};

// ---------------------------------------------------------------------------
// CutLineSet：一组切割线，拆分为竖线 X 集合与横线 Y 集合。
// 约定（终稿 §2 阶段①）：
//   xs = {0 = x0 < x1 < … < xm = W}，ys = {0 = y0 < y1 < … < yn = H}，
//   即始终包含图像两条边界，升序且去重。
// ---------------------------------------------------------------------------
struct CutLineSet {
    std::vector<int> xs;  // 竖线 x 坐标集合
    std::vector<int> ys;  // 横线 y 坐标集合

    // 竖线数量（m+1）。
    std::size_t verticalCount() const { return xs.size(); }
    // 横线数量（n+1）。
    std::size_t horizontalCount() const { return ys.size(); }
    // 诱导网格的列数（相邻竖线之间的单元列数），至少为 0。
    std::size_t columns() const { return xs.empty() ? 0 : xs.size() - 1; }
    // 诱导网格的行数（相邻横线之间的单元行数），至少为 0。
    std::size_t rows() const { return ys.empty() ? 0 : ys.size() - 1; }
};

// 归一化切割线集合：对 xs / ys 分别去重、升序排序，并裁剪到 [0, width] /
// [0, height]（对应终稿 §6“切割线超出图像边界时自动裁剪”），并保证包含边界值。
// 实现见 src/engine/cut_line.cpp。
void normalizeCutLines(CutLineSet& lines, int width, int height);

} // namespace idc::engine
