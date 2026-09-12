// ============================================================================
// 文件：include/engine/composition.h
// 作用：定义导出/合成相关类型——输出模式 EmitMode、合并布局 MergeLayout、
//       导出格式 ExportFormat、合成参数 CompositionParams 与合成结果 Composition，
//       对应 Grid-Selection-Emit 阶段 ⑤（终稿 §2）与导出规格（§5）。
// 分块依据：
//   - EmitMode / MergeLayout / ExportFormat：导出方式的枚举（§5.1/§5.2/§5.3/§5.5）。
//   - CompositionParams：导出配置（分离/合并、坍缩/重排、画布、填充色、命名模板…）。
//   - Placement / Composition：合成结果的数据描述（每个片段落到输出画布的位置）。
//   - isCollapsible：§5.4 坍缩可行性定理的判定接口（定义见 src/engine/composition.cpp）。
//   - compose：阶段⑤ 合成自由函数，把保留集按序列/布局排布为 Composition（声明由 engine.h
//     facade 下沉至此，使 composition.cpp 只依赖本头）。
// 说明：数据结构描述“怎么摆、怎么导”；像素拼接与落盘见 src/engine/export.cpp。
// ============================================================================
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/color.h"
#include "engine/grid.h"
#include "engine/region.h"
#include "engine/region_set.h"
#include "engine/selection.h"
#include "engine/sequence.h"

namespace idc::engine {

// ---------------------------------------------------------------------------
// EmitMode：输出模式（终稿 §2 阶段⑤）。
// ---------------------------------------------------------------------------
enum class EmitMode {
    SEPARATE,  // 多图分离：每个保留单元写入目标文件夹下一张图（§5.1 / FR-L3.6）
    MERGED,    // 合并单图：坍缩或重排为一张画布（§5.2 / §5.3）
};

// ---------------------------------------------------------------------------
// MergeLayout：合并布局。
// ---------------------------------------------------------------------------
enum class MergeLayout {
    COLLAPSE,   // 坍缩式：剩余块按原相对位置紧贴拼接（L1/L2 主用，§5.2）
    REARRANGE,  // 重排式：按序列填入指定 cols × rows 画布（L3 主用，§5.3）
};

// ---------------------------------------------------------------------------
// ExportFormat：导出格式（终稿 §5.5）。
// ---------------------------------------------------------------------------
enum class ExportFormat {
    PNG,   // 支持透明
    JPEG,  // 须指定背景色替代透明
    WEBP,
    BMP,
};

// ---------------------------------------------------------------------------
// CompositionParams：导出/合成参数（对应终稿 §9 JSON 的 emit 段）。
// ---------------------------------------------------------------------------
struct CompositionParams {
    EmitMode mode{EmitMode::MERGED};                 // 分离 or 合并
    MergeLayout layout{MergeLayout::COLLAPSE};       // 坍缩 or 重排
    std::optional<int> cols;                         // 重排画布列数（可选）
    std::optional<int> rows;                         // 重排画布行数（可选）
    std::optional<int> cellWidth;                    // 重排单元宽（可选）
    std::optional<int> cellHeight;                   // 重排单元高（可选）
    core::Color padColor{core::kTransparent};        // 空位/余量填充色（默认透明）
    ExportFormat format{ExportFormat::PNG};          // 导出格式
    std::string naming{"{name}_{index:03d}"};        // 分离导出命名模板（§5.1）
    int quality{90};                                 // 有损格式质量参数
    bool keepMetadata{false};                        // 是否保留元数据（NFR-9）
};

// ---------------------------------------------------------------------------
// Placement：一个片段在输出画布中的落位（源区域 → 目标区域）。
// ---------------------------------------------------------------------------
struct Placement {
    RectRegion source;  // 在原图中的区域
    RectRegion dest;    // 在输出画布中的区域
    int index{-1};      // 序号（用于命名/排序）
};

// ---------------------------------------------------------------------------
// Composition：合成结果描述（输出画布尺寸 + 所有片段落位）。
// ---------------------------------------------------------------------------
struct Composition {
    int canvasWidth{0};
    int canvasHeight{0};
    std::vector<Placement> placements;
    // 导出设置（compose 从 CompositionParams 透传，供 exportImage 落盘时使用）：
    ExportFormat format{ExportFormat::PNG};    // 导出图像格式
    std::string naming{"{name}_{index:03d}"};  // 分离导出命名模板（{name}/{index}/{index:03d}）
    int quality{90};                           // 有损格式（JPEG/WebP）质量参数
};

// 判定保留集能否无空洞、无重叠地坍缩为矩形图（终稿 §5.4 定理）：
// 当且仅当被剔除单元恰好构成诱导网格中的若干整行和/或整列。
// 定义见 src/engine/composition.cpp。
bool isCollapsible(const Selection& selection, const Grid& grid);

// ⑤ 依据序列与布局把保留块合成为输出描述（分离 / 坍缩 / 重排，终稿 §5.1-§5.3）。
// 定义见 src/engine/composition.cpp。
Composition compose(const RegionSet& kept, const Sequence& sequence,
                    const CompositionParams& params);

} // namespace idc::engine
