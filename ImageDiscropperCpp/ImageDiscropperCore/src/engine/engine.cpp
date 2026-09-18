// ============================================================================
// 文件：src/engine/engine.cpp
// 作用：Grid-Selection-Emit 流水线的顶层编排入口 runEngine（终稿 §10.2 概念流程）。
//       一次调用串起：① 切割线 → ② 诱导网格 → ③ 选择集 → ④ 极性 → split →
//       Sequence → ⑤ 合成，产出 EngineResult（保留集 + 合成描述 + 坍缩可行性）。
// 分块依据：六个阶段的函数定义分散在各阶段文件（cut_line/grid/split/selection/
//       composition/export），本文件只做“编排 + 校验 + 降级决策”，避免上帝文件；
//       runEngine 是三层模式（L1/L2/L3）共用的同一代码路径，模式仅为参数预设（NFR-0）。
// 说明：runEngine 只“计算”不落盘；落盘由 exportImage 单独完成（分离计算与 I/O）。
// ============================================================================
#include "engine/engine.h"

#include <vector>

namespace idc::engine {
namespace {

// 依据切割配置与诱导网格自动推导“选择区” S 的单元序号（L1/L2 无显式选择集时用）。
// 语义（终稿 §4.3）：
//   RECT + KEEP    → S = 中心矩形单元；RECT + REMOVE → S = 十字带单元（行带 ∪ 列带），
//                     剔除十字即删整行 + 整列，四角可坍缩（§5.2 / §5.4）。
//   HORIZONTAL_LINE→ S = 横带单元（y ∈ [y1,y2)）；VERTICAL_LINE → S = 竖带单元（x ∈ [x1,x2)）。
//   MULTI_RECT     → REMOVE：S = ⋃ 各矩形十字带（方案 A，§4.3.6）；KEEP：S = ⋃ 各矩形单元。
//   GRID           → 无显式选择时默认全选（配合极性；L3 通常由 selectedCells 覆盖）。
std::vector<int> deriveSelection(const CutConfig& cut, const Grid& grid) {
    std::vector<int> S;
    const bool remove = cut.polarity == Polarity::REMOVE;

    // 单元 x 跨度是否落在 [x1,x2) 内（列带判定）；y 跨度是否落在 [y1,y2) 内（行带判定）。
    const auto inColBand = [](const Cell& c, const RectRegion& r) {
        return c.area.left >= r.left && c.area.right <= r.right && r.left < r.right;
    };
    const auto inRowBand = [](const Cell& c, const RectRegion& r) {
        return c.area.top >= r.top && c.area.bottom <= r.bottom && r.top < r.bottom;
    };

    switch (cut.generator) {
        case CutGenerator::RECT: {
            const RectRegion& r = cut.rect;
            for (const Cell& c : grid.cells()) {
                const bool col = inColBand(c, r), row = inRowBand(c, r);
                if (remove) { if (col || row) S.push_back(c.index); } // 十字带。
                else        { if (col && row) S.push_back(c.index); } // 中心矩形。
            }
            break;
        }
        case CutGenerator::HORIZONTAL_LINE: {
            const RectRegion& r = cut.rect;
            for (const Cell& c : grid.cells())
                if (inRowBand(c, r)) S.push_back(c.index); // 横带。
            break;
        }
        case CutGenerator::VERTICAL_LINE: {
            const RectRegion& r = cut.rect;
            for (const Cell& c : grid.cells())
                if (inColBand(c, r)) S.push_back(c.index); // 竖带。
            break;
        }
        case CutGenerator::MULTI_RECT: {
            for (const Cell& c : grid.cells()) {
                bool hit = false;
                for (const RectRegion& r : cut.rects) {
                    const bool col = inColBand(c, r), row = inRowBand(c, r);
                    if (remove) { if (col || row) { hit = true; break; } } // ⋃ 十字带。
                    else        { if (col && row) { hit = true; break; } } // ⋃ 矩形。
                }
                if (hit) S.push_back(c.index);
            }
            break;
        }
        case CutGenerator::GRID: {
            for (const Cell& c : grid.cells()) S.push_back(c.index); // 默认全选。
            break;
        }
    }
    return S;
}

} // namespace

// 依据配置对图像跑通完整 Grid-Selection-Emit 流水线，返回保留集与合成描述（不落盘）。
EngineResult runEngine(const core::Image& image, const EngineConfig& config) {
    EngineResult result;

    // --- 前置校验：源图像（E-8 之外的输入合法性）。---
    if (image.empty()) {
        result.error = "源图像为空，无法切割";
        return result;
    }

    // --- FR-1：切割前先应用预处理流水线（旋转/翻转/灰度/反色/色道分离），其产出图像
    //     才是切割引擎的真正输入（终稿 §4.1）。无步骤时直接复用原图，避免对大图做
    //     多余深拷贝（NFR-8）。---
    const bool hasPreprocess = !config.preprocess.steps().empty();
    const core::Image preprocessed =
        hasPreprocess ? config.preprocess.apply(image) : core::Image{};
    const core::Image& work = hasPreprocess ? preprocessed : image;
    if (work.empty()) {
        result.error = "预处理后图像为空，无法切割";
        return result;
    }

    // 切割输入尺寸：预处理可能改变尺寸（如旋转 90° 宽高互换），此时必须以产出图像为准；
    // 无预处理时沿用 config.source，未显式给出（<=0）则回退到图像实际尺寸。
    SourceInfo source = config.source;
    if (hasPreprocess) {
        source.width = work.width();
        source.height = work.height();
    } else {
        if (source.width <= 0) source.width = work.width();
        if (source.height <= 0) source.height = work.height();
    }

    // --- E-1：选框/矩形退化校验（贯穿全图的切割线需有效区间）。---
    if (config.cut.generator == CutGenerator::RECT &&
        config.cut.rect.width() <= 0 && config.cut.rect.height() <= 0) {
        result.error = "选框退化（E-1）：矩形宽高均非正，无法生成切割线";
        return result;
    }
    if (config.cut.generator == CutGenerator::MULTI_RECT && config.cut.rects.empty()) {
        result.error = "多矩形为空（E-1）：无矩形可用于切割";
        return result;
    }

    // --- ① 切割线 + ② 诱导网格。GRID 生成器走 Grid::build（可处理余量策略）。---
    Grid grid;
    if (config.cut.generator == CutGenerator::GRID) {
        grid.build(config.cut.grid, source.width, source.height);
    } else {
        const CutLineSet lines = generateCutLines(config.cut, source);
        grid = induceGrid(lines, source);
    }
    if (grid.cellCount() == 0) {
        result.error = "切割未产生有效网格（E-1/E-5）：参数非法或单元尺寸越界";
        return result;
    }
    const int cellCount = static_cast<int>(grid.cellCount());

    // --- ③ 构建选择集：显式 selectedCells 优先，否则按生成器几何自动推导。---
    Selection selection;
    selection.resize(grid.cellCount());
    selection.setPolarity(config.cut.polarity);
    if (!config.selectedCells.empty()) {
        for (const int idx : config.selectedCells)
            if (idx >= 0 && idx < cellCount) selection.select(static_cast<std::size_t>(idx));
    } else {
        for (const int idx : deriveSelection(config.cut, grid))
            if (idx >= 0 && idx < cellCount) selection.select(static_cast<std::size_t>(idx));
    }

    // --- E-6：删除区为空（REMOVE 极性却无单元被选中剔除）。---
    if (config.cut.polarity == Polarity::REMOVE && selection.selectedCount() == 0) {
        result.error = "删除区为空（E-6）：无单元被选中剔除";
        return result;
    }

    // --- split + ④ 极性 → 保留集 R（对预处理后的图像 work 切分）。---
    const RegionSet all = split(work, grid);
    const RegionSet kept = applyPolarity(all, selection);

    // --- E-7 / E-6：保留区为空（删除区覆盖全图或选择集为空）。---
    if (kept.empty()) {
        result.error = "保留集为空（E-7）：删除区覆盖全图或选择集为空";
        return result;
    }

    // --- ⑤ 坍缩可行性（§5.4）+ 序列 + 合成。---
    result.collapsible = isCollapsible(selection, grid);

    Sequence sequence;
    if (config.order.strategy == SortStrategy::CUSTOM && !config.selectedCells.empty()) {
        sequence.setCustom(config.selectedCells); // 自定义序：以显式选择顺序为准（FR-L3.5）。
    } else {
        sequence.build(grid.cellCount(),
                       static_cast<std::size_t>(grid.colCount()),
                       static_cast<std::size_t>(grid.rowCount()),
                       config.order);
    }

    // 布局决策 +「仅 L3 可重排」限制（重排是 L3 网格的专属合成方式，§5.3）。
    CompositionParams emitParams = config.emitParams;
    const bool isL3 = config.cut.tier == Tier::L3;
    const bool merged = emitParams.mode == EmitMode::MERGED;
    // 显式选择重排但非 L3 → 直接报错（不静默改布局）。
    if (merged && emitParams.layout == MergeLayout::REARRANGE && !isL3) {
        result.error = "合并重排仅 L3 网格模式支持：请改用合并坍缩或分离导出";
        return result;
    }
    // MERGED+COLLAPSE 但不可坍缩：L3 降级为重排（§5.4 推论）；L1/L2 无重排可用 → 报错。
    if (merged && emitParams.layout == MergeLayout::COLLAPSE && !result.collapsible) {
        if (!isL3) {
            result.error = "保留集不可坍缩（§5.4），且 L1/L2 不支持合并重排："
                           "请调整切割线使删除区覆盖整行/整列，或改用分离导出";
            return result;
        }
        emitParams.layout = MergeLayout::REARRANGE;
    }

    result.composition = compose(kept, sequence, emitParams);
    result.kept = kept;
    result.ok = true;
    return result;
}

} // namespace idc::engine
