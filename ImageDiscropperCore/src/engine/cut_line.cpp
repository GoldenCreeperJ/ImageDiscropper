// ============================================================================
// 文件：src/engine/cut_line.cpp
// 作用：切割线相关“算法型”接口的桩实现占位。
// 说明：本次仅适配既有项目、搭建接口骨架，不实现切割线归一化算法；
//       真正实现留待 MVP 阶段（终稿 §8）。
// ============================================================================
#include "engine/cut_line.h"

namespace idc::engine {

// 归一化切割线集合（去重 / 升序 / 裁剪到边界）。
// TODO(引擎实现阶段 MVP): 按终稿 §2 阶段① 与 §6 边界约定实现。
void normalizeCutLines(CutLineSet& /*lines*/, const int /*width*/, const int /*height*/) {
    // 桩实现：暂不处理。
}

} // namespace idc::engine
