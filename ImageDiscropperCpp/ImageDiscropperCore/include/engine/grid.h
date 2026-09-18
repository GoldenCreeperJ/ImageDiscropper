// ============================================================================
// 文件：include/engine/grid.h
// 作用：定义诱导网格相关类型——网格参数 GridParams、单元格 Cell、网格 Grid，
//       以及余量策略 RemainderPolicy，对应 Grid-Selection-Emit 阶段 ②（终稿 §2）
//       与 L3 网格分割模式（§4.4）。
// 分块依据：
//   - GridParams：网格定义参数（FR-L3.1）与余量策略（FR-L3.2），纯配置数据。
//   - Cell：一个单元格 = 一条竖带与一条横带的交集（§3.2），带行列号与序号。
//   - Grid：由 GridParams 与图像尺寸铺出的单元格集合；build()（参数化）与
//     buildFromLines()（线诱导）为“铺网格”逻辑，实现见 src/engine/grid.cpp。
// 说明：Grid 提供两条铺设入口——build（参数化网格，L3）与 buildFromLines（切割线诱导，
//       L1/L2/多矩形），二者产出统一的 cells_，下游 split/选择/合成只面向 Grid。
// ============================================================================
#pragma once

#include <vector>

#include "engine/region.h"

namespace idc::engine {

// ---------------------------------------------------------------------------
// RemainderPolicy：余量策略（FR-L3.2）。
// 网格以基准点为相位锚双向周期铺满全图，图像边缘往往不足一个完整单元；
// 该策略决定如何处理这些跨越图像边界的残缺单元。
// ---------------------------------------------------------------------------
enum class RemainderPolicy {
    DISCARD,      // 丢弃：跨越图像边界的残缺单元直接舍弃（仅保留完整单元）
    KEEP_PARTIAL, // 保留残缺单元：把跨界单元裁剪到图像内（[0,W]/[0,H]）后保留
    PAD,          // 补白：保留残缺单元的完整尺寸（越过图像边界），导出时以填充色补齐
};

// ---------------------------------------------------------------------------
// GridParams：网格定义参数（FR-L3.1 / 终稿 §4.4.4）。
// 网格仅由「基准点 + 单元尺寸」定义：以基准点为相位锚、以单元尺寸为周期，双向生成
// 贯穿全图的切割线；行数、列数与单元的位置尺寸均由图像边界自动推导，不作为输入参数
// （切割线是贯穿全图的直线，故不存在“间距”“行列数”这类局部参数）。
// ---------------------------------------------------------------------------
struct GridParams {
    int originX{0};       // 基准点 x0（切割线相位锚，恒落在某条竖切割线上）
    int originY{0};       // 基准点 y0（切割线相位锚，恒落在某条横切割线上）
    int cellWidth{1};     // 单元宽 cw（相邻竖切割线的周期距离）
    int cellHeight{1};    // 单元高 ch（相邻横切割线的周期距离）
    RemainderPolicy remainder{RemainderPolicy::DISCARD}; // 余量策略（FR-L3.2）
};

// ---------------------------------------------------------------------------
// Cell：网格单元格。
// ---------------------------------------------------------------------------
struct Cell {
    int row{0};                 // 行号（从 0 起）
    int col{0};                 // 列号（从 0 起）
    int index{0};               // 线性序号（默认 row-major：row * cols + col）
    RectRegion area;            // 单元格覆盖的矩形区域
    bool partial{false};        // 是否为余量产生的残缺单元
};

// ---------------------------------------------------------------------------
// Grid：诱导网格，持有一组按行主序排列的单元格。
// ---------------------------------------------------------------------------
class Grid {
public:
    // 依据网格参数与图像尺寸铺设单元格（FR-L3.1 / FR-L3.2 / 终稿 §4.4.4）。
    // 以基准点为相位锚、单元尺寸为周期，自虚拟起点 (-cw,0]×(-ch,0] 双向铺满全图，
    // 行列数由图像边界自动推导；跨越边界的残缺单元按余量策略处理。
    // origin=0 时等价于自左/上边界单向周期铺满（与旧行为一致）。
    void build(const GridParams& params, int imageWidth, int imageHeight);

    // 由切割线集合（xs / ys）诱导铺设 m×n 单元（终稿 §2 阶段②）。
    // 单元 C[i][j] = [xs[i], xs[i+1]) × [ys[j], ys[j+1])，互不重叠且恰好铺满。
    void buildFromLines(const std::vector<int>& xs, const std::vector<int>& ys);

    // 只读访问网格参数。
    const GridParams& params() const { return params_; }
    // 只读访问全部单元格。
    const std::vector<Cell>& cells() const { return cells_; }
    // 单元格总数。
    std::size_t cellCount() const { return cells_.size(); }
    // 解析后的列数 / 行数（build / buildFromLines 调用后有效；由图像边界自动推导）。
    int colCount() const { return cols_; }
    int rowCount() const { return rows_; }

    // 按行列号取单元格；越界返回 nullptr。
    const Cell* cellAt(const int row, const int col) const {
        for (const auto& c : cells_) {
            if (c.row == row && c.col == col) return &c;
        }
        return nullptr;
    }

private:
    GridParams params_;
    std::vector<Cell> cells_;
    int cols_{0};  // 派生列数：由 build / buildFromLines 依图像边界计算（非输入参数）
    int rows_{0};  // 派生行数：同上
};

} // namespace idc::engine
