// ============================================================================
// 文件：include/engine/grid.h
// 作用：定义诱导网格相关类型——网格参数 GridParams、单轴定义 GridAxisSpec、单元格 Cell、
//       网格 Grid，以及余量策略 RemainderPolicy，对应 Grid-Selection-Export 阶段 ②（SPEC §1）
//       与 L3 网格分割模式（SPEC §3.4）。
// 分块依据：
//   - GridAxisSpec / GridParams：单轴与整网格定义（FR-L3.1）。单轴三态并列——周期（基准点+单元尺寸
//     +余量策略）、分割数（等分）、比例（权重），三者都展开为贯穿全图的切割线。
//   - Cell：一个单元格 = 一条竖带与一条横带的交集（SPEC §2.2），带行列号与序号。
//   - Grid：由 GridParams 与图像尺寸铺出的单元格集合；build()（参数化）与
//     buildFromLines()（线诱导）为“铺网格”逻辑，实现见 src/engine/grid.cpp。
// 说明：Grid 提供两条铺设入口——build（参数化网格，L3）与 buildFromLines（切割线诱导，
//       L1/L2/多矩形），二者产出统一的 cells_，下游 split/选择/合成只面向 Grid。
// ============================================================================
#pragma once

#include <optional>
#include <vector>

#include "engine/region.h"

namespace idc::engine {

// ---------------------------------------------------------------------------
// RemainderPolicy：余量策略（FR-L3.2）。
// 网格以基准点为相位锚双向周期铺满全图，图像边缘往往不足一个完整单元；
// 该策略决定如何处理这些跨越图像边界的残缺单元。
// ---------------------------------------------------------------------------
enum class RemainderPolicy {
    Discard,      // 丢弃：跨越图像边界的残缺单元直接舍弃（仅保留完整单元）
    KeepPartial,  // 保留残缺单元：把跨界单元裁剪到图像内（[0,W]/[0,H]）后保留
    Pad,          // 补白：保留残缺单元的完整尺寸（越过图像边界），导出时以填充色补齐
};

// ---------------------------------------------------------------------------
// GridAxisMode：单轴网格定义形态（FR-L3.1 / SPEC §3.4.3），三态并列：
//   Pitch   —— 周期：以基准点为相位锚、单元尺寸为周期，双向铺满全图；
//              跨界残缺单元按余量策略处理（FR-L3.2）。
//   Count   —— 分割数：基准点恒 0，把该轴等分成 count 份；宽度尽量平均，
//              末格吸收全部取整误差，恒恰好铺满、不产生残缺单元。
//   Weights —— 比例：基准点恒 0，按权重分配该轴；同样末格吸收取整误差。
// 三态的产物都是贯穿全图的切割线；Count / Weights 无需余量策略（不存在残缺单元）。
// ---------------------------------------------------------------------------
enum class GridAxisMode {
    Pitch,    // 基准点 + 单元尺寸 + 余量策略
    Count,    // 分割数（第 3 竖 / 4 横…）
    Weights,  // 比例（1:2:1…）
};

// ---------------------------------------------------------------------------
// GridAxisSpec：单轴定义。仅 mode 对应的字段有效（其余保持默认值）：
//   Pitch   → origin / cell / remainder
//   Count   → count（≥1）
//   Weights → weights（≥1 个正权重；整数比与百分比等价，只取相对比例）
// ---------------------------------------------------------------------------
struct GridAxisSpec {
    GridAxisMode mode{GridAxisMode::Pitch};
    int origin{0};       // Pitch：基准点（切割线相位锚，可负）
    int cell{1};         // Pitch：单元尺寸（相邻切割线的周期距离，须为正）
    RemainderPolicy remainder{RemainderPolicy::Discard}; // Pitch：余量策略
    int count{1};        // Count：份数（≥1，且不得超过该方向像素数）
    std::vector<double> weights;   // Weights：权重（正数；长度即份数）
};

// 单轴定义不可展开的原因（供上层本地化报因；None＝可展开）。
enum class GridAxisError {
    None,
    CountNotPositive,    // 分割数 < 1
    CountExceedsLimit,   // 分割数 > 该方向像素数（会产出零宽单元）
    WeightNotPositive,   // 权重为空或含非正数
    CellUnderflow,       // 周期非正，或比例分不出正宽单元（权重个数 > 像素数）
};

// ---------------------------------------------------------------------------
// GridParams：网格定义参数（FR-L3.1 / SPEC §3.4.3）。
// 每个轴独立定义：默认走「基准点 + 单元尺寸 + 余量策略」（origin/cell/remainder 三个平铺字段）；
// axisX / axisY 非空时接管该轴（可为 Pitch / Count / Weights 任一形态），从而支持
// 「横向等分 + 纵向固定高度」这类混合。两轴都恒生成贯穿全图的切割线。
// ---------------------------------------------------------------------------
struct GridParams {
    int originX{0};       // 基准点 x0（切割线相位锚，恒落在某条竖切割线上）
    int originY{0};       // 基准点 y0（切割线相位锚，恒落在某条横切割线上）
    int cellWidth{1};     // 单元宽 cw（相邻竖切割线的周期距离）
    int cellHeight{1};    // 单元高 ch（相邻横切割线的周期距离）
    RemainderPolicy remainder{RemainderPolicy::Discard}; // 余量策略（FR-L3.2；仅周期轴生效）
    std::optional<GridAxisSpec> axisX;  // 非空＝横轴改用显式定义（忽略 originX/cellWidth）
    std::optional<GridAxisSpec> axisY;  // 非空＝纵轴改用显式定义（忽略 originY/cellHeight）
};

// 取某轴的有效定义：axisX/axisY 优先，否则由平铺字段合成周期定义。
GridAxisSpec effectiveAxis(const GridParams& params, bool horizontal);

// 校验单轴定义能否在 [0, limit] 上展开为 ≥1 个正宽单元。
GridAxisError checkAxis(const GridAxisSpec& axis, int limit);

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
    // 依据网格参数与图像尺寸铺设单元格（FR-L3.1 / FR-L3.2 / SPEC §3.4.3）。
    // 每轴按其有效定义（周期 / 分割数 / 比例）独立展开成切割线，再做笛卡尔积：
    // 周期轴自虚拟起点 (-cw,0]×(-ch,0] 双向铺满全图，残缺单元按余量策略处理；
    // 分割数 / 比例轴恒恰好铺满 [0, W] × [0, H]，末格吸收取整误差、不产生残缺单元。
    void build(const GridParams& params, int imageWidth, int imageHeight);

    // 由切割线集合（xs / ys）诱导铺设 m×n 单元（SPEC §1 阶段②）。
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
