// ============================================================================
// 文件：include/engine/grid.h
// 作用：定义诱导网格相关类型——网格参数 GridParams、单元格 Cell、网格 Grid，
//       以及余量策略 RemainderPolicy，对应 Grid-Selection-Emit 阶段 ②（终稿 §2）
//       与 L3 网格分割模式（§4.4）。
// 分块依据：
//   - GridParams：网格定义参数（FR-L3.1）与余量策略（FR-L3.2），纯配置数据。
//   - Cell：一个单元格 = 一条竖带与一条横带的交集（§3.2），带行列号与序号。
//   - Grid：由 GridParams 与图像尺寸铺出的单元格集合；build() 为“铺网格”逻辑，
//     属于待实现算法，故仅声明，桩实现见 src/engine/grid.cpp。
// 说明：接口骨架，只定义数据结构与访问器，不实现网格铺设算法。
// ============================================================================
#pragma once

#include <cstddef>
#include <vector>

#include "engine/region.h"

namespace idc::engine {

// ---------------------------------------------------------------------------
// RemainderPolicy：余量策略（FR-L3.2）。
// 当 W - x0（或 H - y0）不是单元尺寸的整数倍时，如何处理残缺部分。
// ---------------------------------------------------------------------------
enum class RemainderPolicy {
    DISCARD,      // 丢弃：不足一个完整单元的余量直接舍弃
    KEEP_PARTIAL, // 保留残缺单元：把余量作为一个较小的单元保留
    PAD,          // 补白：把余量补齐到完整单元尺寸（导出时以填充色补）
};

// ---------------------------------------------------------------------------
// GridParams：网格定义参数（FR-L3.1）。
// ---------------------------------------------------------------------------
struct GridParams {
    int originX{0};       // 基准点 x0
    int originY{0};       // 基准点 y0
    int cellWidth{1};     // 单元宽 cw
    int cellHeight{1};    // 单元高 ch
    int gapX{0};          // 单元水平间距 gx（默认 0）
    int gapY{0};          // 单元垂直间距 gy（默认 0）
    int cols{0};          // 列数；<= 0 表示自动铺满至右边界
    int rows{0};          // 行数；<= 0 表示自动铺满至下边界
    RemainderPolicy remainder{RemainderPolicy::DISCARD}; // 余量策略
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
    // 依据网格参数与图像尺寸铺设单元格（FR-L3.1 / FR-L3.2）。
    // 声明占位，桩实现见 src/engine/grid.cpp，真正实现留待 L3（v2）阶段。
    void build(const GridParams& params, int imageWidth, int imageHeight);

    // 只读访问网格参数。
    const GridParams& params() const { return params_; }
    // 只读访问全部单元格。
    const std::vector<Cell>& cells() const { return cells_; }
    // 单元格总数。
    std::size_t cellCount() const { return cells_.size(); }

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
};

} // namespace idc::engine
