// ============================================================================
// 文件：include/engine/export.h
// 作用：声明导出阶段 exportImage——把合成结果 Composition 落盘（终稿 §5.1/§5.2/§5.5）。
// 分块依据：export 是独立的流水线阶段，声明从 engine.h facade 下沉至此，使 export.cpp 只
//       依赖本阶段头（Composition + core::Image）与 image_io，不再全量 include engine.h（解耦）。
// 说明：分离模式（SEPARATE）把各片段写入目标文件夹（不存在则自动创建，E-8）；合并模式
//       （MERGED）构建画布 blit 后写单图。像素搬运在此发生。定义见 src/engine/export.cpp。
// ============================================================================
#pragma once

#include <string>

#include "core/image.h"
#include "engine/composition.h"

namespace idc::engine {

// 把合成结果落盘（分离多图 / 合并单图）；成功返回 true。
// SEPARATE：outputPath 为目标文件夹（不存在则自动创建，含多级父目录；创建失败返回 false，E-8）。
// MERGED：outputPath 为单图文件路径，格式优先由扩展名推断，无有效扩展名时回退 Composition::format。
bool exportImage(const Composition& composition, const core::Image& source,
                 const std::string& outputPath);

} // namespace idc::engine
