// ============================================================================
// 文件：include/engine/engine.h
// 作用：定义统一引擎的顶层配置与 Grid-Selection-Emit 流水线接口（SPEC §1）。
//       这是三层模式（L1 标准提取 / L2 反向剔除 / L3 网格分割）共用的同一代码路径，
//       模式仅为参数预设（NFR-0）。
// 分块依据：
//   - Tier / CutGenerator：模式层级与切割线生成方式（SPEC §1 表：切割线生成方式×极性×输出）。
//   - SourceInfo / CutConfig / EngineConfig：对应 SPEC §7 的操作配置数据结构。
//   - generateCutLines / induceGrid：桥接聚合配置的流水线阶段①② 声明；其余阶段函数
//     （split / applyPolarity / compose / exportImage）声明已下沉到各自子头，本头聚合 include。
// 说明：本头是统一管线 facade——聚合 include 各阶段子头，并直接声明桥接聚合配置的
//       generateCutLines / induceGrid 与顶层编排 runEngine；split / applyPolarity / compose /
//       exportImage 的声明已下沉到各自阶段头（解耦），定义仍分散在 src/engine/ 各阶段文件，
//       避免上帝文件。runEngine 定义见 src/engine/engine.cpp。
// ============================================================================
#pragma once

#include <expected>
#include <optional>
#include <string>
#include <vector>

#include "core/core_error.h"  // core::CoreError（runEngine 错误侧载荷；对齐 Rust GuideLine §3.3）
#include "core/image.h"
#include "canvas/layer.h"          // canvas::ImageLayer（可选「layers」段：多层画布展平后作为引擎输入）
#include "generate/pattern.h"      // generate::PatternParams（底图/图层的 recipe、boxBackdrop 图案配方字段）
#include "engine/composition.h"
#include "engine/cut_line.h"
#include "engine/export.h"
#include "engine/grid.h"
#include "engine/region.h"
#include "engine/region_set.h"
#include "engine/selection.h"
#include "engine/sequence.h"
#include "engine/split.h"
#include "preprocess/preprocess_pipeline.h"

namespace idc::engine {

// ---------------------------------------------------------------------------
// Tier：模式层级（SPEC §1）。三者在数学上 L1 ⊂ L2 ⊂ L3，产品上并列呈现。
// ---------------------------------------------------------------------------
enum class Tier {
    L1,  // 标准提取模式（兼容基线，极性恒为 keep）
    L2,  // 反向剔除模式（差异化内核）
    L3,  // 网格分割模式（完备表达层）
};

// ---------------------------------------------------------------------------
// CutGenerator：切割线生成方式（SPEC §1“切割线生成方式”维度）。
// ---------------------------------------------------------------------------
enum class CutGenerator {
    RECT,             // 单矩形选框诱导十字切割线（L1/L2）
    HORIZONTAL_LINE,  // 仅两条横线（横带保留/剔除）
    VERTICAL_LINE,    // 仅两条竖线（竖带保留/剔除）
    MULTI_RECT,       // 多矩形并集剔除（FR-L2.4，方案 A 十字带并集）
    GRID,             // 参数化网格（L3）
};

// ---------------------------------------------------------------------------
// SourceInfo：原图尺寸信息（SPEC §7 JSON 的 source 段）。
// ---------------------------------------------------------------------------
struct SourceInfo {
    int width{0};
    int height{0};
};

// ---------------------------------------------------------------------------
// CutConfig：切割配置（SPEC §7 JSON 的 cut 段）。
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
// EngineConfig：一次完整作业的配置聚合（对应 SPEC §7 的整份 JSON）。
// ---------------------------------------------------------------------------
struct EngineConfig {
    SourceInfo source;                          // 原图尺寸
    std::optional<canvas::ImageLayer> base;     // SPEC §7 可选「generate」段：底图＝与图层同构的 canvas::ImageLayer，仅角色为「钉住最底、不可拖/不可改落位」。sourceKind==Recipe＝纯程序图案底图（配方落 recipe、transform 恒等，序列化后与旧 generate 逐字段等价）；sourceKind==File＝新建画布附带图片底图（凭 fileName 引用 assets + 内层图片落位）。位图恒不入配置。存在＝由 composeBase 展平得源画布，代替外部输入图。
    std::optional<std::vector<canvas::ImageLayer>> layers;   // SPEC §7 可选「layers」段：多层画布直接以数组承载（存在＝由 composeCanvas 展平得工作图，引擎仍只收一张展平图）
    preprocess::PreprocessPipeline preprocess;  // 切割前的基础图像处理（FR-1）
    CutConfig cut;                              // 切割配置
    std::vector<int> selectedCells;             // 显式选择集 S（单元序号）；空 = L1/L2 由生成器自动推导
    SequenceParams order;                       // 排序策略（FR-L3.5）
    CompositionParams emitParams;               // 导出/合成配置（SPEC §4；成员名避开 Qt emit 关键字宏）
    // 画布级整体不透明度（0..255，255＝不透明）：SPEC §7 顶层可选「opacity」键。作用于合成 + 画布预处理后
    //   的整幅工作图（逐像素 alpha 乘子，与 composeCanvas 逐层不透明度同口径），影响预览与导出；缺省 255（向后兼容）。
    //   不入位图、恒在产出边界（applyAlpha）施加一次，不烘焙进缓存像素。
    int canvasOpacity{255};
};

// ===========================================================================
// Grid-Selection-Emit 流水线接口（SPEC §1）
// 原图 → ① 切割线集合 → ② 诱导网格 → ③ 选择集 → ④ 极性 → ⑤ 排布导出
// 各阶段函数声明已下沉到对应子头（解耦：实现文件只依赖自身阶段头，不再全量依赖本 facade）：
//   split → engine/split.h；applyPolarity → engine/selection.h；
//   compose → engine/composition.h；exportImage → engine/export.h。
// 仅 generateCutLines / induceGrid 因桥接聚合配置（CutConfig / SourceInfo）留在本 facade，
// 以避免 cut_line ↔ grid ↔ 配置头的循环依赖。本头已聚合 include 上述子头，下游只需
// include engine/engine.h 即可获得完整流水线 API（向后兼容）。
// ===========================================================================

// ① 由切割配置生成贯穿全图的切割线集合（桥接 CutConfig，定义见 src/engine/cut_line.cpp）。
CutLineSet generateCutLines(const CutConfig& cut, const SourceInfo& source);

// ② 由切割线集合诱导 m × n 网格（桥接 CutLineSet，定义见 src/engine/grid.cpp）。
Grid induceGrid(const CutLineSet& lines, const SourceInfo& source);

// ===========================================================================
// 顶层编排入口（SPEC §1 概念流程）
// generateCutLines → induceGrid → 构建选择集 → split → applyPolarity →
// Sequence::build → compose，一次调用跑通整条 Grid-Selection-Emit 管线。
// ===========================================================================

// ---------------------------------------------------------------------------
// EngineResult：一次引擎作业的成功结果（失败侧经 std::expected 的 CoreError 承载）。
//   kept        —— 保留集 R（带单元序号的片段集合）。
//   composition —— 合成/排布描述（画布尺寸 + 各片段落位）。
//   collapsible —— 坍缩可行性判定结果（SPEC §4.4）；false 时不可合并坍缩
//               （L3 由 runEngine 自动改用重排，L1/L2 归为错误 NotCollapsible）。
// 说明：成功值类型不再携带 ok / error——成功与否由 std::expected 表达，报因由 CoreError 承载。
// ---------------------------------------------------------------------------
struct EngineResult {
    RegionSet kept;
    Composition composition;
    bool collapsible{false};
};

// 依据配置对图像跑通完整流水线，返回保留集与合成描述（不落盘）；失败返回 core::CoreError。
// 落盘由 exportImage 单独完成，以分离“计算”与“I/O”（SPEC §1）。
std::expected<EngineResult, core::CoreError> runEngine(const core::Image& image, const EngineConfig& config);

// 仅尺寸入口（预览专用）：直接以给定输出尺寸跑几何流水线（切割线→网格→选择→split→合成）。
// 不接收图像、不应用 config.preprocess（调用方须传“已预处理尺寸”）。产物与
// runEngine(config.preprocess.apply(原图), 空管线cfg) 一致，供 GUI 实时预览脱离全分辨率像素。
std::expected<EngineResult, core::CoreError> runEngineGeometry(const SourceInfo& dims, const EngineConfig& config);

} // namespace idc::engine
