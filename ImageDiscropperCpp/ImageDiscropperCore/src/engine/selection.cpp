// ============================================================================
// 文件：src/engine/selection.cpp
// 作用：实现选择/极性阶段（Grid-Selection-Emit 阶段③④）——Selection::resolve 按极性
//       解析保留序号，applyPolarity 据此从全集过滤出保留集 R（终稿 §2 / §4.3 / §4.4）。
// 分块依据：极性（keep/remove）是 L1/L2/L3 三层共用的同一开关；本文件只做“集合取补”，
//       不涉及像素，切分见 split.cpp，合成见 composition.cpp。
// ============================================================================
#include "engine/selection.h"

namespace idc::engine {

// 按当前极性解析出“最终保留”的单元序号集合（§2 阶段④）。
//   KEEP   → 返回所有被选中的序号（R = S）；
//   REMOVE → 返回所有未被选中的序号（R = 全集 \ S，反向剔除内核）。
std::vector<std::size_t> Selection::resolve() const {
    std::vector<std::size_t> kept;
    kept.reserve(selected_.size());
    for (std::size_t i = 0; i < selected_.size(); ++i) {
        // KEEP 保留选中项；REMOVE 保留未选中项（补集）。
        const bool isKept = (polarity_ == Polarity::KEEP) ? selected_[i] : !selected_[i];
        if (isKept) kept.push_back(i);
    }
    return kept;
}

// 依据选择集与极性，从全集中过滤出保留集 R（§2 阶段③④）。
// all 的每个 Fragment 携带单元序号，与 Selection 的位向量按序号对齐。
RegionSet applyPolarity(const RegionSet& all, const Selection& selection) {
    // 将 resolve 结果展开为按序号索引的保留标志表，便于 O(1) 过滤。
    const std::vector<std::size_t> keptIndices = selection.resolve();
    std::vector<bool> keepFlag(selection.cellCount(), false);
    for (const std::size_t idx : keptIndices) {
        if (idx < keepFlag.size()) keepFlag[idx] = true;
    }

    // 逐片段过滤：序号有效且被标记保留者进入结果集（原样保留 index/kind）。
    RegionSet out;
    for (const Fragment& f : all.fragments()) {
        if (f.index >= 0 &&
            static_cast<std::size_t>(f.index) < keepFlag.size() &&
            keepFlag[static_cast<std::size_t>(f.index)]) {
            out.add(f);
        }
    }
    return out;
}

} // namespace idc::engine
