// ============================================================================
// 文件：src/commands/grid.cpp
// 作用：实现 cmdGrid——L3 网格分割模式（SPEC §3.4）的命令行薄壳。
//       网格定义有三种并列形态（SPEC §3.4.3，可整幅给、也可按轴分别给）：
//         · 周期（--grid x0,y0,cw,ch / 单轴 --grid-x、--grid-y）：基准点 + 单元尺寸 + 余量策略；
//         · 分割数（--divide ColxRow / 单轴 --divide-x、--divide-y）：基准点恒 0，尽量均分，末份吸收取整误差；
//         · 比例（--weights-x、--weights-y）：基准点恒 0，按权重分配，末份吸收取整误差。
//       三者均展开为贯穿全图的切割线，行列数与单元位置尺寸由图像边界自动推导。职责仅四件
//       （CONTRIBUTING.md「分层纪律」）：解析参数 → 按固定顺序校验 → 用 Core 的 Grid 把 (r,c)
//       映射为单元序号并装配 EngineConfig → 交 job 执行；不含任何切割 / 几何 / 排序逻辑，
//       全部委托 Core 的统一引擎（NFR-0）。
// 分块依据：一命令一文件（严禁上帝文件）；(r,c)→单元序号的映射复用 Core Grid::build/cellAt
//       （CONTRIBUTING.md「分层纪律」：不在 CLI 重算几何——value_parser.h 已注明该步在命令层用 Core Grid 完成）；
//       执行 / 退出码复用 job（同一执行通道），值解析复用 value_parser，报错样板复用 command_support。
// 说明：
//   - 三种“选择来源”互斥：--sort custom 用 --order（点选顺序即保留序，极性 keep）；否则
//     --keep（极性 keep）与 --remove（极性 remove）二选一（重要约束）。
//   - --compose 恒为重排合并（REARRANGE，按 --canvas ColxRow 落位）；缺省为分离导出到文件夹。
//     网格保留集通常是任意单元子集，不满足坍缩定理（SPEC §4.4），故 grid 不提供 collapse，
//     explicitCollapse 恒为 false，本命令不会触发退出码 2。
//   - 越界的 (r,c) 属参数错误（退出码 1），但其判定依赖图像尺寸才能铺出网格，故只能在读图
//     （第 6 步，退出码 3）之后检查——这是固定校验顺序在“几何依赖图像尺寸”时的固有例外。
//   - --pad-color：写入 export.padColor；Core 的 compose 已把 padColor 透传进 Composition，
//     exportMerged 以之填充合并画布空位 / 余量（并对 JPEG/BMP 作压平背景），故对合并导出生效。
// 常量层：选项名取 texts::opt（不可译）、报错文案经门面 IDC_CTR(msg, ...) 取 texts::msg。
// ============================================================================
#include "commands.h"

#include <string>
#include <utility>
#include <vector>

#include "core/color.h"      // core::Color（--pad-color）
#include "engine/engine.h"   // EngineConfig / Tier / CutGenerator / Polarity / ExportMode / MergeLayout
#include "engine/grid.h"     // Grid / GridParams / Cell / RemainderPolicy
#include "engine/sequence.h" // SortStrategy
#include "annotation/annotation_set.h" // AnnotationSet / Annotation（命令行标注烧录容器）

#include "annotation_args.h"
#include "arg_parser.h"
#include "command_support.h"
#include "generate_args.h"
#include "job.h"
#include "layer_args.h"   // collectLayers / finalizeAppendedLayers（命令行图层，--layer/--layers）
#include "value_parser.h"
#include "i18n/cli_texts.h"
#include "i18n/cli_i18n.h"

namespace idc::cli {

// 选项名简写（不可译契约；报错文案走 IDC_CTR）。
namespace opts = texts::opt;

// L3 网格分割：--input + --grid + (--keep|--remove|--order) + 排序/余量/合成等。
int cmdGrid(const std::vector<std::string>& tokens, const GlobalOptions& go) {
    ArgParser args;
    args.parse(tokens);

    // --- 第 2.5 步：拒绝本命令不接受的输入（G1+A/B/C，退出码 1）；--keep/--remove 可重复，列入可重复集。---
    if (const int rc = rejectUnknownOptions(args, makeCommandArgs(
            {opts::kInput, opts::kGrid, opts::kGridX, opts::kGridY, opts::kDivide, opts::kDivideX, opts::kDivideY,
             opts::kWeightsX, opts::kWeightsY, opts::kSort, opts::kKeep, opts::kRemove, opts::kOrder, opts::kDecorate,
             opts::kMargin, opts::kCompose, opts::kCanvas, opts::kMergeSort, opts::kMergeDecorate,
             opts::kOutput, opts::kOutputDir, opts::kPadColor, opts::kFit},
            MixGenerate | MixAnnotation | MixExport | MixLayers | MixAssets,
            {opts::kKeep, opts::kRemove})); rc != 0) return rc;

    // --- 第 3 步：必填。（输入来源二选一：--input 读文件 或 --size 生成画布。）---
    if (!args.has(opts::kInput) && !hasGenerateOptions(args))
        return argError(IDC_CTR(msg, kErrMissingInput), IDC_CTR(msg, kHintInputOrSize));
    if (!args.has(opts::kSize) && hasPatternOptions(args))
        return argError(IDC_CTR(msg, kErrPatternNeedsSize), IDC_CTR(msg, kHintPatternNeedsSize));
    // --- 第 3 步：网格定义必须给出，且整幅定义与按轴定义互斥、同一轴只能一种形态。---
    const bool wholeGrid = args.has(opts::kGrid);
    const bool wholeDivide = args.has(opts::kDivide);
    const bool axisX = args.has(opts::kGridX) || args.has(opts::kDivideX) || args.has(opts::kWeightsX);
    const bool axisY = args.has(opts::kGridY) || args.has(opts::kDivideY) || args.has(opts::kWeightsY);
    if (!wholeGrid && !wholeDivide && !axisX && !axisY)
        return argError(IDC_CTR(msg, kErrMissingGrid), IDC_CTR(msg, kHintGridGeo));
    if ((wholeGrid && wholeDivide) || ((wholeGrid || wholeDivide) && (axisX || axisY)))
        return argError(IDC_CTR(msg, kErrGridDefineMix), IDC_CTR(msg, kHintGridDefineMix));
    if (const auto forms = [](const bool a, const bool b, const bool c) { return (a ? 1 : 0) + (b ? 1 : 0) + (c ? 1 : 0); };
        forms(args.has(opts::kGridX), args.has(opts::kDivideX), args.has(opts::kWeightsX)) > 1 ||
        forms(args.has(opts::kGridY), args.has(opts::kDivideY), args.has(opts::kWeightsY)) > 1)
        return argError(IDC_CTR(msg, kErrGridDefineMix), IDC_CTR(msg, kHintGridDefineMix));

    // --- 装配固定部分：L3 恒为参数化网格生成器。---
    engine::EngineConfig config;
    config.cut.tier = engine::Tier::Grid;
    config.cut.generator = engine::CutGenerator::Grid;

    std::string err;

    // --- 第 5 步（提前解析 --sort：第 4 步的选择来源互斥判定依赖它）。---
    auto strategy = engine::SortStrategy::RowMajor;
    if (args.has(opts::kSort) && !parseSort(args.get(opts::kSort), strategy, err)) return argError(err);
    config.order.strategy = strategy;
    const bool custom = strategy == engine::SortStrategy::Custom;

    // --- 第 4 步：选择来源互斥（--keep / --remove / --order 三者的合法组合）。---
    const bool hasKeep = args.has(opts::kKeep);
    const bool hasRemove = args.has(opts::kRemove);
    if (hasKeep && hasRemove)
        return argError(IDC_CTR(msg, kErrKeepRemoveMix), IDC_CTR(msg, kHintKeepOrRemove));
    if (custom) {
        // 自定义序：--order 必填，且不可再用 --keep/--remove/--decorate（点选顺序即保留集与输出序）。
        if (!args.has(opts::kOrder))
            return argError(IDC_CTR(msg, kErrCustomNeedsOrder), IDC_CTR(msg, kHintOrderSyntax));
        if (hasKeep || hasRemove)
            return argError(IDC_CTR(msg, kErrCustomNoKeepRemove), IDC_CTR(msg, kHintOrderIsSelection));
        if (args.has(opts::kDecorate))
            return argError(IDC_CTR(msg, kErrCustomNoDecorate), IDC_CTR(msg, kHintCustomNoDecorate));
        config.cut.polarity = engine::Polarity::Keep;
    } else {
        // 非自定义：必须恰好给出 --keep 或 --remove；--order 仅在 custom 下有效。
        if (args.has(opts::kOrder))
            return argError(IDC_CTR(msg, kErrOrderNeedsCustom), IDC_CTR(msg, kHintOrderNeedsCustom));
        if (!hasKeep && !hasRemove)
            return argError(IDC_CTR(msg, kErrNoCellChoice), IDC_CTR(msg, kHintKeepRemoveCells));
        config.cut.polarity = hasRemove ? engine::Polarity::Remove : engine::Polarity::Keep;
    }

    // --- 第 5 步：网格定义格式校验与余量策略。整幅 / 单轴周期含百分比段，换算需图像尺寸，
    //     故此处只判形态，换算与「单元尺寸为正」留到第 6 步解图之后。---
    engine::GridParams gp;
    GridCoords gridCoords;                        // --grid：整幅周期（x0,y0,cw,ch）
    AxisPitchCoords axisXCoords, axisYCoords;     // --grid-x / --grid-y：单轴周期（origin,cell）
    std::vector<double> weightsX, weightsY;       // --weights-x / --weights-y：比例
    int divideX = 0, divideY = 0;                 // --divide-x / --divide-y：单轴分割数
    int divideCols = 0, divideRows = 0;           // --divide：两轴分割数
    if (args.has(opts::kMargin) && !parseMargin(args.get(opts::kMargin), gp.remainder, err))
        return argError(err);
    // 余量策略只对周期轴有意义：两轴皆为分割数 / 比例时不存在跨界残缺单元。
    if (args.has(opts::kMargin) && !args.has(opts::kGrid) && !args.has(opts::kGridX) && !args.has(opts::kGridY))
        return argError(IDC_CTR(msg, kErrMarginNeedsPitch), IDC_CTR(msg, kHintMarginNeedsPitch));
    if (args.has(opts::kGrid) && !parseGridCoords(args.get(opts::kGrid), gridCoords, err)) return argError(err);
    if (args.has(opts::kDivide) && !parseDivide(args.get(opts::kDivide), divideCols, divideRows, err)) return argError(err);
    if (args.has(opts::kDivideX) && !parsePositiveCount(args.get(opts::kDivideX), divideX, err)) return argError(err);
    if (args.has(opts::kDivideY) && !parsePositiveCount(args.get(opts::kDivideY), divideY, err)) return argError(err);
    if (args.has(opts::kGridX) && !parseAxisPitchCoords(args.get(opts::kGridX), axisXCoords, err)) return argError(err);
    if (args.has(opts::kGridY) && !parseAxisPitchCoords(args.get(opts::kGridY), axisYCoords, err)) return argError(err);
    if (args.has(opts::kWeightsX) && !parseWeightList(args.get(opts::kWeightsX), weightsX, err)) return argError(err);
    if (args.has(opts::kWeightsY) && !parseWeightList(args.get(opts::kWeightsY), weightsY, err)) return argError(err);

    // --- 第 5 步：排序修饰（reverse / snake，仅非 custom）。---
    if (!custom && args.has(opts::kDecorate)) {
        bool reverse = false, snake = false;
        if (!parseDecorate(args.get(opts::kDecorate), reverse, snake, err)) return argError(err);
        config.order.reverse = reverse;
        config.order.snake = snake;
    }

    // --- 第 4/5 步：输出模式（--compose 重排合并 / 缺省分离）与画布 ColxRow。---
    const bool separate = !args.has(opts::kCompose);
    if (!separate) { // --compose：合并为单图，恒 REARRANGE（网格保留集一般不可坍缩）。
        if (args.has(opts::kOutputDir))
            return argError(IDC_CTR(msg, kErrComposeWithOutputDir), IDC_CTR(msg, kHintComposeOutput));
        if (!args.has(opts::kCanvas))
            return argError(IDC_CTR(msg, kErrComposeNeedsCanvas), IDC_CTR(msg, kHintCanvasColxRow));
        if (!args.has(opts::kSaveConfig) && !args.has(opts::kOutput))
            return argError(IDC_CTR(msg, kErrComposeNeedsOutput), IDC_CTR(msg, kHintMergeOutputPath));
        int cols = 0, rows = 0;
        if (!parseCanvas(args.get(opts::kCanvas), cols, rows, err)) return argError(err);
        config.exportParams.mode = engine::ExportMode::Merged;
        config.exportParams.layout = engine::MergeLayout::Rearrange;
        config.exportParams.cols = cols; // 画布列数（单元数，非像素）
        config.exportParams.rows = rows; // 画布行数（单元数，非像素）
        // 重排填充顺序（与选择排序 --sort 正交）：--merge-sort 行/列优先，--merge-decorate 蛇形/倒序。
        // --sort 决定「块的先后列表」，--merge-sort/--merge-decorate 决定「该列表如何铺进输出画布」。
        auto msort = engine::SortStrategy::RowMajor;
        if (args.has(opts::kMergeSort)) {
            if (!parseSort(args.get(opts::kMergeSort), msort, err)) return argError(err);
            if (msort == engine::SortStrategy::Custom)
                return argError(IDC_CTR(msg, kErrMergeSortNoCustom), IDC_CTR(msg, kHintMergeSortNoCustom));
        }
        config.exportParams.mergeOrder.strategy = msort;
        if (args.has(opts::kMergeDecorate)) {
            bool mrev = false, msnk = false;
            if (!parseDecorate(args.get(opts::kMergeDecorate), mrev, msnk, err)) return argError(err);
            config.exportParams.mergeOrder.reverse = mrev;
            config.exportParams.mergeOrder.snake = msnk;
        }
    } else { // 缺省：分离导出到文件夹（不存在会自动创建）。
        // 模式级对称守卫：合并专属选项只在 --compose 分支被读取；未加 --compose 却给出它们属误用，
        //   一律退出码 1，杜绝「传了却不生效」。
        if (args.has(opts::kOutput) || args.has(opts::kCanvas) ||
            args.has(opts::kMergeSort) || args.has(opts::kMergeDecorate))
            return argError(IDC_CTR(msg, kErrSeparateWithMergeOnly), IDC_CTR(msg, kHintSeparateMergeOnly));
        if (!args.has(opts::kSaveConfig) && !args.has(opts::kOutputDir))
            return argError(IDC_CTR(msg, kErrMissingOutputDir), IDC_CTR(msg, kHintOutputDirOrCompose));
        config.exportParams.mode = engine::ExportMode::Separate;
    }

    // --- 第 5 步：填充色与公共导出选项 --format / --quality / --naming。---
    if (args.has(opts::kPadColor)) {
        core::Color pad;
        if (!parseColor(args.get(opts::kPadColor), pad, err)) return argError(err);
        config.exportParams.padColor = pad; // Core 已透传至合并画布填充色（见文件头说明）。
    }
    if (!applyExportOptions(args, config.exportParams, err)) return argError(err);

    // --- 第 5 步：选择单元 (r,c) 的格式解析（映射为序号需网格，故读图后再映射）。---
    std::vector<std::pair<int, int>> chosen; // 保持出现顺序（custom 下即输出序）。
    if (custom) {
        if (!parseOrderCells(args.get(opts::kOrder), chosen, err)) return argError(err);
        // 自定义序列中单元应唯一：重复会使同一单元在重排画布上落位两次（防御性校验）。
        for (std::size_t i = 0; i < chosen.size(); ++i)
            for (std::size_t j = i + 1; j < chosen.size(); ++j)
                if (chosen[i] == chosen[j])
                    return argError(IDC_CTR(msg, kErrOrderDupCell) + std::to_string(chosen[i].first) + "," +
                                        std::to_string(chosen[i].second) + ")",
                                    IDC_CTR(msg, kHintOrderUnique));
    } else {
        // 集合语义：每个 --keep/--remove 值可为单个 r,c 或分号列表 r,c;r,c（与 --order 同形解析，累加去重交 Core）。
        for (const std::string& s : args.getAll(hasKeep ? opts::kKeep : opts::kRemove)) {
            std::vector<std::pair<int, int>> cells;
            if (!parseCellList(s, cells, err)) return argError(err);
            for (auto& cell : cells) chosen.push_back(std::move(cell));
        }
    }

    // --- 第 6 步：获取输入图像（--input 读文件失败 3；--size 生成参数非法 1）。---
    JobOptions opt = makeJobOptions(go, /*explicitCollapse=*/false);
    core::Image image;
    if (const ExitCode ec = resolveInputImage(args, image, opt); ec != ExitCode::Ok)
        return toInt(ec);

    // --- 换算网格几何（百分比段按图像宽 / 高）并交 Core Grid 铺网格（复用引擎几何，CONTRIBUTING.md「分层纪律」），
    //     据此把 (r,c) 映射为单元序号。---
    const int imgW = image.width();
    const int imgH = image.height();
    const auto countSpec = [](const int n) {
        engine::GridAxisSpec s;
        s.mode = engine::GridAxisMode::Count;
        s.count = n;
        return s;
    };
    const auto weightSpec = [](const std::vector<double>& w) {
        engine::GridAxisSpec s;
        s.mode = engine::GridAxisMode::Weights;
        s.weights = w;
        return s;
    };
    if (args.has(opts::kGrid)) {
        // 整幅周期：保持平铺形态（平铺四键），余量策略落在网格级 remainder。
        if (!resolveGridCoords(gridCoords, imgW, imgH, gp, err)) return argError(err);
    } else if (args.has(opts::kDivide)) {
        gp.axisX = countSpec(divideCols);
        gp.axisY = countSpec(divideRows);
    } else {
        // 按轴定义：每轴从周期 / 分割数 / 比例中取一种；未定义的轴取「整轴 1 份」
        //（不可沿用平铺默认 cell=1——那会把该轴切成像素行）。
        if (args.has(opts::kGridX)) {
            engine::GridAxisSpec s;
            if (!resolveAxisPitchCoords(axisXCoords, imgW, s, err)) return argError(err);
            gp.axisX = s;
        } else if (args.has(opts::kDivideX)) {
            gp.axisX = countSpec(divideX);
        } else if (args.has(opts::kWeightsX)) {
            gp.axisX = weightSpec(weightsX);
        }
        if (args.has(opts::kGridY)) {
            engine::GridAxisSpec s;
            if (!resolveAxisPitchCoords(axisYCoords, imgH, s, err)) return argError(err);
            gp.axisY = s;
        } else if (args.has(opts::kDivideY)) {
            gp.axisY = countSpec(divideY);
        } else if (args.has(opts::kWeightsY)) {
            gp.axisY = weightSpec(weightsY);
        }
        if (!gp.axisX.has_value()) gp.axisX = countSpec(1);
        if (!gp.axisY.has_value()) gp.axisY = countSpec(1);
        // 余量策略只作用于周期轴（分割数 / 比例恒恰好铺满，不存在残缺单元）。
        if (gp.axisX->mode == engine::GridAxisMode::Pitch) gp.axisX->remainder = gp.remainder;
        if (gp.axisY->mode == engine::GridAxisMode::Pitch) gp.axisY->remainder = gp.remainder;
    }
    config.cut.grid = gp;
    engine::Grid grid;
    grid.build(gp, imgW, imgH);
    if (grid.cellCount() == 0)
        return argError(IDC_CTR(msg, kErrGridNoCells), IDC_CTR(msg, kHintGridNoCells));
    for (const auto&[fst, snd] : chosen) {
        const engine::Cell* cell = grid.cellAt(fst, snd);
        if (cell == nullptr)
            return argError(IDC_CTR(msg, kErrCellOutOfRange) + std::to_string(fst) + "," + std::to_string(snd) +
                                IDC_CTR(msg, kErrCellOutOfRangeTail),
                            IDC_CTRF(msg, kHintGridShape, std::to_string(grid.rowCount()), std::to_string(grid.colCount())));
        config.selectedCells.push_back(cell->index);
    }
    config.source.width = imgW;
    config.source.height = imgH;

    // --- 第 7 步：命令行标注（SPEC §7 annotations 元素语法）装配到临时集合，交 job 烧录 / 随 --save-config 透写。---
    //     坐标基准＝工作图尺寸（本命令无预处理选项，工作图即输入图）；几何坐标可写百分比串（源输入图宽高）。
    //     无标注时传 nullptr（不烧录、不写段）。---
    std::vector<annotation::Annotation> annos;
    if (!collectAnnotations(args, annos, err, image.width(), image.height())) return argError(err);
    annotation::AnnotationSet annoSet;
    const annotation::AnnotationSet* annoPtr = nullptr;
    if (!annos.empty()) { annoSet.restore(std::move(annos)); annoPtr = &annoSet; }

    // --- 第 7 步：命令行图层（--layer/--layers）装配：作为叠加层展平进引擎（资产按当前工作目录解析）。---
    std::vector<canvas::ImageLayer> cliLayers;
    if (!collectLayers(args, cliLayers, err, image.width(), image.height())) return argError(err);
    if (!cliLayers.empty()) {
        finalizeAppendedLayers({}, cliLayers);
        config.layers = std::move(cliLayers);
    }

    // --- 第 7 步：执行 / dry-run（--save-config）。分离取 --output-dir，合并取 --output。---
    const JobOutput out{separate ? args.get(opts::kOutputDir) : args.get(opts::kOutput), separate};
    opt.assetsDir = assetsDirFromArgs(args);   // --assets-dir 三态（作业级通用，命令后）
    return toInt(finishJob(config, image, out, args.get(opts::kSaveConfig), opt, annoPtr));
}

} // namespace idc::cli
