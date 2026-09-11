// ============================================================================
// 文件：src/engine/sequence.cpp
// 作用：序列 Sequence 的“算法型”接口桩实现占位。
// 说明：不实现排序算法，真正实现留待 L3（v2）阶段（终稿 §8）。
// ============================================================================
#include "engine/sequence.h"

namespace idc::engine {

// 依据网格规模与排序参数生成显式序号序列。
// TODO(引擎实现阶段 v2): 按 FR-L3.5 实现 row-major / column-major / custom
//                       以及 reverse / snake 叠加。
void Sequence::build(const std::size_t /*cellCount*/, const std::size_t /*cols*/,
                     const std::size_t /*rows*/, const SequenceParams& /*params*/) {
    order_.clear(); // 桩实现：不生成序列。
}

} // namespace idc::engine
