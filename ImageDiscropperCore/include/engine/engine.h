// ============================================================================
// 文件：include/engine/engine.h
// 作用：定义统一引擎的顶层配置与 Grid-Selection-Emit 流水线接口（终稿 §2 / §10.2）。
//       这是三层模式（L1 标准提取 / L2 反向剔除 / L3 网格分割）共用的同一代码路径，
//       模式仅为参数预设（NFR-0）。
// 分块依据：
//   - Tier / CutGenerator：模式层级与切割线生成方式（§2 表：切割线生成方式×极性×输出）。
//   - SourceInfo / CutConfig / EngineConfig：对应终稿 §9 的操作配置数据结构。
//   - generateCutLines / induceGrid / split / applyPolarity / compose / exportImage：
//     流水线五个阶段的自由函数声明，串联出 §10.2 的概念流程。
// 说明：★ 本文件是新项目的“接口骨架”，全部流水线函数仅声明；桩实现见
//       src/engine/engine.cpp（返回空结果 + TODO），真正实现属于 MVP 及后续阶段，
//       不在本次“适配既有项目”的范围内。
// ============================================================================
#pragma once

#include <string>
#include <vector>

#include "core/image.h"
#include "engine/composition.h"
#include "engine/cut_line.h"
#include "engine/grid.h"
#include "engine/region.h"
#include "engine/region_set.h"
#include "engine/selection.h"
#include "engine/sequence.h"
#include "preprocess/preprocess_pipeline.h"

namespace idc::engine {

// ---------------------------------------------------------------------------
// Tier：模式层级（终稿 §1）。三者在数学上 L1 ⊂ L2 ⊂ L3，产品上并列呈现。
// ---------------------------------------------------------------------------
enum class Tier {
    L1,  // 标准提取模式（兼容基线，极性恒为 keep）
    L2,  // 反向剔除模式（差异化内核）
    L3,  // 网格分割模式（完备表达层）
};

// ---------------------------------------------------------------------------
// CutGenerator：切割线生成方式（§2“切割线生成方式”维度）。
// ---------------------------------------------------------------------------
enum class CutGenerator {
    RECT,             // 单矩形选框诱导十字切割线（L1/L2）
    HORIZONTAL_LINE,  // 仅两条横线（横带保留/剔除）
    VERTICAL_LINE,    // 仅两条竖线（竖带保留/剔除）
    MULTI_RECT,       // 多矩形并集剔除（FR-L2.4，方案 A 十字带并集）
    GRID,             // 参数化网格（L3）
};

// ---------------------------------------------------------------------------
// SourceInfo：原图尺寸信息（§9 JSON 的 source 段）。
// ---------------------------------------------------------------------------
struct SourceInfo {
    int width{0};
    int height{0};
};

// ---------------------------------------------------------------------------
// CutConfig：切割配置（§9 JSON 的 cut 段）。
// 分块依据：一个结构体覆盖三层模式所需的切割输入——层级、生成方式、几何参数、极性。
// ---------------------------------------------------------------------------
struct CutConfig {
    Tier tier{Tier::L2};                          // 模式层级
    CutGenerator generator{CutGenerator::RECT};   // 切割线生成方式
    RectRegion rect;                              // 单矩形选框（RECT/十字切割用）
    std::vector<RectRegion> rects;                // 多矩形（MULTI_RECT 用）
    GridParams grid;                              // 网格参数（GRID 用）
    Polarity polarity{Polarity::REMOVE};          // 极性（keep / remove）
};

// ---------------------------------------------------------------------------
// EngineConfig：一次完整作业的配置聚合（对应终稿 §9 的整份 JSON）。
// ---------------------------------------------------------------------------
struct EngineConfig {
    SourceInfo source;                          // 原图尺寸
    preprocess::PreprocessPipeline preprocess;  // 切割前的基础图像处理（FR-1）
    CutConfig cut;                              // 切割配置
    SequenceParams order;                       // 排序策略（FR-L3.5）
    CompositionParams emit;                     // 导出/合成配置（§5）
};

// ===========================================================================
// Grid-Selection-Emit 流水线接口（终稿 §2）
// 原图 → ① 切割线集合 → ② 诱导网格 → ③ 选择集 → ④ 极性 → ⑤ 排布导出
// 以下函数均为接口骨架，桩实现见 src/engine/engine.cpp，留待 MVP 及后续阶段。
// ===========================================================================

// ① 由切割配置生成贯穿全图的切割线集合。
CutLineSet generateCutLines(const CutConfig& cut, const SourceInfo& source);

// ② 由切割线集合诱导 m × n 网格。
Grid induceGrid(const CutLineSet& lines, const SourceInfo& source);

// split：按网格将图像切分为互不重叠、恰好铺满的区域集合（§10.2 概念流程）。
RegionSet split(const core::Image& image, const Grid& grid);

// ③④ 依据选择集与极性求出最终保留集 R（keep → S；remove → 全集 \ S）。
RegionSet applyPolarity(const RegionSet& all, const Selection& selection);

// ⑤ 依据序列与布局把保留块合成为输出描述（坍缩或重排）。
Composition compose(const RegionSet& kept, const Sequence& sequence,
                    const CompositionParams& params);

// export：把合成结果落盘（分离多图 / 合并单图）；成功返回 true。
bool exportImage(const Composition& composition, const core::Image& source,
                 const std::string& outputPath);

} // namespace idc::engine
