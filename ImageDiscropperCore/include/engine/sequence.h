// ============================================================================
// 文件：include/engine/sequence.h
// 作用：定义排序策略 SortStrategy 与序列 Sequence——L3 独有的“第三个自由度”
//       （终稿 §4.4.1：顺序），承载 FR-L3.5 的显式排序与自定义序号。
// 分块依据：
//   - SortStrategy / SequenceParams：排序策略配置（row-major / column-major /
//     custom，可叠加 reverse、snake）。
//   - Sequence：排序结果——一个单元格序号的显式序列；build() 为“按策略生成序号”
//     逻辑，属于待实现算法，仅声明，桩实现见 src/engine/sequence.cpp。
// 说明：接口骨架，不实现排序算法。
// ============================================================================
#pragma once

#include <cstddef>
#include <utility>
#include <vector>

namespace idc::engine {

// ---------------------------------------------------------------------------
// SortStrategy：排序策略（FR-L3.5）。
// ---------------------------------------------------------------------------
enum class SortStrategy {
    ROW_MAJOR,     // 横优先：逐行编号
    COLUMN_MAJOR,  // 竖优先：逐列编号
    CUSTOM,        // 自定义：点选顺序即序号，支持拖拽调序
};

// ---------------------------------------------------------------------------
// SequenceParams：排序参数。reverse / snake 可叠加在 ROW_MAJOR / COLUMN_MAJOR 上。
// ---------------------------------------------------------------------------
struct SequenceParams {
    SortStrategy strategy{SortStrategy::ROW_MAJOR};
    bool reverse{false};  // 是否整体逆序
    bool snake{false};    // 是否蛇形（隔行/隔列反向）
};

// ---------------------------------------------------------------------------
// Sequence：显式的单元序号序列。order_[k] = 第 k 个输出位对应的单元序号。
// ---------------------------------------------------------------------------
class Sequence {
public:
    // 依据网格规模与排序参数生成序列（FR-L3.5）。
    // 声明占位，桩实现见 src/engine/sequence.cpp，真正实现留待 L3（v2）阶段。
    void build(const std::size_t cellCount, const std::size_t cols,
               const std::size_t rows, const SequenceParams& params);

    // 直接设置自定义序列（CUSTOM 策略 / 拖拽调序结果）。
    void setCustom(std::vector<int> order) { order_ = std::move(order); }

    // 只读访问序列。
    const std::vector<int>& order() const { return order_; }
    // 序列长度。
    std::size_t size() const { return order_.size(); }
    // 序列是否为空。
    bool empty() const { return order_.empty(); }

private:
    std::vector<int> order_;
};

} // namespace idc::engine
