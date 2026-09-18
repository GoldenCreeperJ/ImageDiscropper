// ============================================================================
// 文件：src/engine/sequence.cpp
// 作用：实现排序阶段——Sequence::build 依据网格规模与排序参数生成显式单元序号序列
//       （SPEC §3.4.1“顺序”自由度 / FR-L3.5）。
// 分块依据：排序是 L3 独有的第三自由度；本文件只生成“序号顺序”，不触碰像素与区域，
//       合成阶段（composition.cpp）据此把保留块填入画布。
// ============================================================================
#include "engine/sequence.h"

#include <algorithm>

namespace idc::engine {

// 依据网格规模与排序参数生成显式序号序列（FR-L3.5）。
// order_[k] = 第 k 个输出位对应的单元序号；支持 row-major / column-major，
// 叠加 reverse（整体逆序）与 snake（隔行/隔列反向）；custom 由 setCustom 提供。
void Sequence::build(const std::size_t cellCount, const std::size_t cols,
                     const std::size_t rows, const SequenceParams& params) {
    // CUSTOM：序号由 setCustom 显式提供（点选顺序 / 拖拽调序），build 不覆盖。
    if (params.strategy == SortStrategy::CUSTOM) {
        return;
    }

    order_.clear();
    if (cellCount == 0 || cols == 0 || rows == 0) return;
    order_.reserve(cellCount);

    if (params.strategy == SortStrategy::ROW_MAJOR) {
        // 横优先：逐行输出；snake 时奇数行行内反向。
        for (std::size_t r = 0; r < rows; ++r) {
            const bool reverseRow = params.snake && r % 2 == 1;
            for (std::size_t c = 0; c < cols; ++c) {
                const std::size_t col = reverseRow ? cols - 1 - c : c;
                if (const std::size_t index = r * cols + col; index < cellCount) order_.push_back(static_cast<int>(index));
            }
        }
    } else {
        // 竖优先（COLUMN_MAJOR）：逐列输出；snake 时奇数列列内反向。
        for (std::size_t c = 0; c < cols; ++c) {
            const bool reverseCol = params.snake && c % 2 == 1;
            for (std::size_t r = 0; r < rows; ++r) {
                const std::size_t row = reverseCol ? rows - 1 - r : r;
                if (const std::size_t index = row * cols + c; index < cellCount) order_.push_back(static_cast<int>(index));
            }
        }
    }

    // reverse：整体逆序，叠加在 row/column-major 与 snake 之上。
    if (params.reverse) {
        std::reverse(order_.begin(), order_.end());
    }
}

} // namespace idc::engine
