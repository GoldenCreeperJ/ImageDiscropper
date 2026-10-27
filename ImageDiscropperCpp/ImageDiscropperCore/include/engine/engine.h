// ============================================================================
// 文件：include/engine/engine.h
// 作用：定义统一引擎的顶层配置与 Grid-Selection-Export 流水线接口（SPEC §1）。
//       这是三层模式（L1 标准提取 / L2 反向剔除 / L3 网格分割）共用的同一代码路径，
//       模式仅为参数预设（NFR-0）。
// 分块依据：
//   - Tier / CutGenerator：模式层级与切割线生成方式（SPEC §1 表：切割线生成方式×极性×输出）。
//   - SourceInfo / CutConfig / EngineConfig：对应 SPEC §7 的操作配置数据结构。
//   - generateCutLines / induceGrid：桥接聚合配置的流水线阶段①② 声明；其余阶段函数
//     （split / applyPolarity / compose / exportImage）声明位于各自子头，本头聚合 include。
// 说明：本头是统一管线 facade——聚合 include 各阶段子头，并直接声明桥接聚合配置的
//       generateCutLines / induceGrid 与顶层编排 runEngine；split / applyPolarity / compose /
//       exportImage 的声明位于各自阶段头（解耦），定义分散在 src/engine/ 各阶段文件，
//       避免上帝文件。runEngine 定义见 src/engine/engine.cpp。
//   EngineConfig.base（SPEC §7 可选「generate」段）：底图是与图层同构的 canvas::ImageLayer，
//       仅角色为「钉住最底、不可拖 / 不可改落位」；有 recipe 表纯程序图案底图（配方落
//       recipe、transform 恒等），无 recipe 而携 fileName / 内层落位表新建画布附带图片底图（凭 fileName
//       引用 assets + 内层图片落位）；来源态由载荷（recipe / fileName / decodedPixels）派生、无独立枚举字段；位图恒不入配置。base 存在＝由 composeBase 展平得源画布，代替
//       外部输入图。layers 段同理：存在＝由 composeCanvas 展平得工作图，引擎仍只收一张展平图。
// ============================================================================
#pragma once

#include <expected>
#include <optional>
#include <string>
#include <vector>

#include "core/core_error.h"  // core::CoreError（runEngine 错误侧载荷；对齐 Rust GuideLine §3.3）
#include "core/image.h"
#include "canvas/layer.h"          // canvas::ImageLayer（可选「layers」段：多层画布展平后作为引擎输入）
#include "generate/pattern.h"      // generate::PatternParams（底图/图层的图案元素 backdrop 配方字段）
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
//   Standard↔L1、Erase↔L2、Grid↔L3；与 §7 cut.tier token（extract/erase/grid）同源对齐。
// ---------------------------------------------------------------------------
enum class Tier {
    Standard,  // 标准提取模式（兼容基线，极性恒为 keep）
    Erase,     // 反向剔除模式（差异化内核）
    Grid,      // 网格分割模式（完备表达层）
};

// ---------------------------------------------------------------------------
// CutGenerator：切割线生成方式（SPEC §1“切割线生成方式”维度）。
// ---------------------------------------------------------------------------
enum class CutGenerator {
    Rect,           // 单矩形选框诱导十字切割线（L1/L2）
    HorizontalLine, // 仅两条横线（横带保留/剔除）
    VerticalLine,   // 仅两条竖线（竖带保留/剔除）
    MultiRect,      // 多矩形并集剔除（FR-L2.4，方案 A 十字带并集）
    Grid,           // 参数化网格（L3）
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
    Tier tier{Tier::Erase};                         // 模式层级
    CutGenerator generator{CutGenerator::Rect};     // 切割线生成方式
    RectRegion rect;                                // 单矩形选框（Rect/十字切割用）
    std::vector<RectRegion> rects;                  // 多矩形（MultiRect 用）
    GridParams grid;                                // 网格参数（Grid 用）
    Polarity polarity{Polarity::Remove};            // 极性（keep / remove）
};

// ---------------------------------------------------------------------------
// EngineConfig：一次完整作业的配置聚合（对应 SPEC §7 的整份 JSON）。
// base / layers 段的语义详见文件头「说明」。
// ---------------------------------------------------------------------------
struct EngineConfig {
    SourceInfo source;                          // 原图尺寸
    std::optional<canvas::ImageLayer> base;     // SPEC §7 可选「generate」段：底图（语义见文件头「说明」）
    std::optional<std::vector<canvas::ImageLayer>> layers;   // SPEC §7 可选「layers」段：多层画布数组（存在＝composeCanvas 展平得工作图）
    // 资产目录声明（SPEC §7 可选「assetsDir」键，generate 与 layers 之间）：layers 段 file 相对名的
    //   第一查找位（其余按 <配置名>.assets/ → 配置目录的约定链）。空 = 未声明（纯约定查找）。
    //   须为相对路径、不含「..」/绝对路径/盘符（越界拒绝在执行侧，与 file 名同规）。位图不入配置。
    std::string assetsDir;
    preprocess::PreprocessPipeline preprocess;  // 切割前的基础图像处理（FR-1）
    CutConfig cut;                              // 切割配置
    std::vector<int> selectedCells;             // 显式选择集 S（单元序号）；空 = L1/L2 由生成器自动推导
    SequenceParams order;                       // 排序策略（FR-L3.5）
    CompositionParams exportParams;             // 导出/合成配置（SPEC §4）；export 为 C++ 保留关键字，故成员名用 exportParams。
    // 画布级整体不透明度（0..255，255＝不透明）：SPEC §7 顶层可选「opacity」键。作用于合成 +
    //   画布预处理后的整幅工作图（逐像素 alpha 乘子，与 composeCanvas 逐层不透明度同口径），
    //   影响预览与导出；缺省 255（向后兼容）。不入位图、恒在产出边界施加一次，不烘焙进缓存像素。
    int canvasOpacity{255};
};

// ===========================================================================
// Grid-Selection-Export 流水线接口（SPEC §1）
// 原图 → ① 切割线集合 → ② 诱导网格 → ③ 选择集 → ④ 极性 → ⑤ 排布导出
// 各阶段函数声明位于对应子头（解耦：实现文件只依赖自身阶段头，不全量依赖本 facade）：
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
// Sequence::build → compose，一次调用跑通整条 Grid-Selection-Export 管线。
// ===========================================================================

// ---------------------------------------------------------------------------
// EngineResult：一次引擎作业的成功结果（失败侧经 std::expected 的 CoreError 承载）。
//   kept        —— 保留集 R（带单元序号的片段集合）。
//   composition —— 合成/排布描述（画布尺寸 + 各片段落位）。
//   collapsible —— 坍缩可行性判定结果（SPEC §4.4）；false 时不可合并坍缩
//               （L3 由 runEngine 自动改用重排，L1/L2 归为错误 NotCollapsible）。
//   uniformBlocks —— 保留块是否长宽全等（重排准入门限，SPEC §4.3）：false 时重排不可用，
//               引擎按「可坍缩 → 坍缩，否则 → 分离导出」自动回退（GUI 据此置灰重排项）。
// 说明：成功值类型只含上述字段；成功与否由 std::expected 表达，报因由 CoreError 承载。
// ---------------------------------------------------------------------------
struct EngineResult {
    RegionSet kept;
    Composition composition;
    bool collapsible{false};
    bool uniformBlocks{false};
};

// 依据配置对图像跑通完整流水线，返回保留集与合成描述（不落盘）；失败返回 core::CoreError。
// 落盘由 exportImage 单独完成，以分离“计算”与“I/O”（SPEC §1）。
std::expected<EngineResult, core::CoreError> runEngine(const core::Image& image, const EngineConfig& config);

// 仅尺寸入口（预览专用）：直接以给定输出尺寸跑几何流水线（切割线→网格→选择→split→合成）。
// 不接收图像、不应用 config.preprocess（调用方须传“已预处理尺寸”）。产物与
// runEngine(config.preprocess.apply(原图), 空管线cfg) 一致，供 GUI 实时预览脱离全分辨率像素。
std::expected<EngineResult, core::CoreError> runEngineGeometry(const SourceInfo& dims, const EngineConfig& config);

} // namespace idc::engine
