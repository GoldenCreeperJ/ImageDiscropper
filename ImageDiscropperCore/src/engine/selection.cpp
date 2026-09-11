// ============================================================================
// 文件：src/engine/selection.cpp
// 作用：选择集 Selection 的“算法型”接口桩实现占位。
// 说明：不实现极性解析算法，真正实现留待 MVP 阶段（终稿 §8）。
// ============================================================================
#include "engine/selection.h"

namespace idc::engine {

// 按极性解析出最终保留的单元序号集合。
// TODO(引擎实现阶段 MVP): KEEP 返回选中项、REMOVE 返回其补集（§2 阶段④）。
std::vector<std::size_t> Selection::resolve() const {
    return {}; // 桩实现：返回空集。
}

} // namespace idc::engine
