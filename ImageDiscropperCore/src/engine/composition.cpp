// ============================================================================
// 文件：src/engine/composition.cpp
// 作用：合成/导出相关“算法型”接口的桩实现占位。
// 说明：不实现坍缩可行性判定，真正实现留待 MVP 阶段（终稿 §8）。
// ============================================================================
#include "engine/composition.h"

namespace idc::engine {

// 判定保留集能否坍缩为矩形图（终稿 §5.4 定理）。
// TODO(引擎实现阶段 MVP): 校验被剔除单元是否恰好构成若干整行/整列。
bool isCollapsible(const Selection& /*selection*/, const Grid& /*grid*/) {
    return false; // 桩实现：保守判定为不可坍缩。
}

} // namespace idc::engine
