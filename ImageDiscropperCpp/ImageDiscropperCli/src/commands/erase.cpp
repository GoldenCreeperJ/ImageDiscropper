// ============================================================================
// 文件：src/commands/erase.cpp
// 作用：实现 cmdErase——L2 反向剔除模式（SPEC §3.3）的命令行薄壳。
//       支持四种剔除：单矩形十字切割、横线带、竖线带、多矩形并集（--rect 多次）。极性恒 remove；
//       缺省分离导出到文件夹，给出 --merge collapse 时合并为单图（重排 rearrange 仅 L3 grid 支持，
//       本命令拒绝）。职责仅四件（CONTRIBUTING.md「分层纪律」）：
//       解析参数 → 按固定顺序校验 → 装配 EngineConfig → 交 job 执行；不含任何切割/几何/极性逻辑。
// 分块依据：一命令一文件（严禁上帝文件）；执行/退出码复用 job（NFR-0 同一通道），值解析复用
//       value_parser，报错样板复用 command_support（CONTRIBUTING.md「分层纪律」）。分离/合并输出目标的判定在本命令内完成
//       （与 grid 的差异：erase 无 --compose/--canvas，合并方式只 collapse——重排为 L3 专属）。
// 常量层：选项名取 texts::opt（不可译）、报错文案经门面 IDC_CTR(msg, ...) 取 texts::msg。
// 说明：显式 --merge collapse 但选择集不可坍缩时由 job 返回退出码 2。四种剔除在合法
//       参数下删除区恒为若干整行/整列（多矩形并集亦然），故通常可坍缩；该分支主要为
//       防御性对齐规范，真正稳定触发退出码 2 的场景见 config 命令的手写配置（IT-18）。
// ============================================================================
#include "commands.h"

#include <string>
#include <utility>
#include <vector>

#include "engine/engine.h" // EngineConfig / Tier / CutGenerator / Polarity / ExportMode / MergeLayout
#include "engine/region.h" // RectRegion / horizontalBand / verticalBand
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

// L2 反向剔除：--input + (--rect 可重复|--hband|--vband) + [--merge] + 输出（极性 remove）。
int cmdErase(const std::vector<std::string>& tokens, const GlobalOptions& go) {
    ArgParser args;
    args.parse(tokens);

    // --- 第 2.5 步：拒绝本命令不接受的输入（G1+A/B/C，退出码 1）；--rect 在 L2 可重复做并集，列入可重复集。---
    if (const int rc = rejectUnknownOptions(args, makeCommandArgs(
            {opts::kInput, opts::kRect, opts::kHband, opts::kVband, opts::kMerge, opts::kOutput, opts::kOutputDir, opts::kFit},
            MixGenerate | MixAnnotation | MixExport | MixLayers | MixAssets,
            {opts::kRect})); rc != 0) return rc;

    // --- 第 3 步：必填。（输入来源二选一：--input 读文件 或 --size 生成画布。）---
    if (!args.has(opts::kInput) && !hasGenerateOptions(args))
        return argError(IDC_CTR(msg, kErrMissingInput), IDC_CTR(msg, kHintInputOrSize));
    if (!args.has(opts::kSize) && hasPatternOptions(args))
        return argError(IDC_CTR(msg, kErrPatternNeedsSize), IDC_CTR(msg, kHintPatternNeedsSize));

    // --- 第 4 步：互斥——几何三选一（--rect 可重复，整体算作一类）。---
    const bool hasRect = args.has(opts::kRect);
    const bool hasH = args.has(opts::kHband);
    const bool hasV = args.has(opts::kVband);
    if (const int geoKinds = static_cast<int>(hasRect) + static_cast<int>(hasH) + static_cast<int>(hasV); geoKinds != 1)
        return argError(IDC_CTR(msg, kErrGeoThreeOne),
                        geoKinds == 0 ? IDC_CTR(msg, kHintGeoOneKindRect)
                                      : IDC_CTR(msg, kHintGeoNoMix));

    // --- 第 4 步：--merge 与 --output-dir 互斥；据此定「分离 / 合并」输出目标。---
    const bool hasMerge = args.has(opts::kMerge);
    if (hasMerge && args.has(opts::kOutputDir))
        return argError(IDC_CTR(msg, kErrMergeWithOutputDir), IDC_CTR(msg, kHintMergeVsSeparate));

    // --- 装配 EngineConfig 固定部分：L2 极性恒 remove；输出模式由 --merge 决定。---
    const bool separate = !hasMerge; // 缺省分离导出。
    bool explicitCollapse = false;   // 仅显式 --merge collapse 时为真（不可坍缩 → 退出码 2）。
    engine::EngineConfig config;
    config.cut.tier = engine::Tier::Erase;
    config.cut.polarity = engine::Polarity::Remove;

    std::string err;
    if (hasMerge) {
        auto layout = engine::MergeLayout::Collapse;
        if (!parseMerge(args.get(opts::kMerge), layout, err)) return argError(err);
        // 仅 L3 可重排（Core runEngine 硬约束）：erase 为 L2，显式 rearrange 直接拒绝。
        if (layout == engine::MergeLayout::Rearrange)
            return argError(IDC_CTR(msg, kErrEraseNoRearrange), IDC_CTR(msg, kHintRearrangeL3Only));
        config.exportParams.mode = engine::ExportMode::Merged;
        config.exportParams.layout = layout;
        explicitCollapse = layout == engine::MergeLayout::Collapse;
        if (!args.has(opts::kSaveConfig) && !args.has(opts::kOutput))
            return argError(IDC_CTR(msg, kErrMissingMergeOutput), IDC_CTR(msg, kHintMergeOutputPath));
    } else {
        // 模式级对称守卫（与上方 --merge 拒 --output-dir 反向）：--output 属合并目标，
        //   未加 --merge 却给出属误用，一律退出码 1。
        if (args.has(opts::kOutput))
            return argError(IDC_CTR(msg, kErrSeparateWithOutput), IDC_CTR(msg, kHintMergeVsSeparate));
        config.exportParams.mode = engine::ExportMode::Separate;
        if (!args.has(opts::kSaveConfig) && !args.has(opts::kOutputDir))
            return argError(IDC_CTR(msg, kErrMissingOutputDir), IDC_CTR(msg, kHintOutputDirAutoCreate));
    }

    // --- 第 5 步：几何形态校验（整数像素或百分比段）；百分比需图像尺寸作基准，故换算与退化校验
    //     留到第 6 步解图之后。---
    std::vector<std::string> rectStrs;   // --rect 原值（多矩形退化时按条回指具体那一个）
    std::vector<RectCoords> rectCoords;
    BandCoords bandCoords;
    if (hasRect) {
        rectStrs = args.getAll(opts::kRect);
        // 单矩形 → 十字切割（保留四角）；多矩形 → 并集剔除（FR-L2.4）。
        config.cut.generator = rectStrs.size() == 1 ? engine::CutGenerator::Rect : engine::CutGenerator::MultiRect;
        for (const std::string& s : rectStrs) {
            RectCoords rc;
            if (!parseRectCoords(s, rc, err)) return argError(err);
            rectCoords.push_back(rc);
        }
    } else if (hasH) {
        if (!parseBandCoords(args.get(opts::kHband), bandCoords, err)) return argError(err);
        config.cut.generator = engine::CutGenerator::HorizontalLine;
    } else { // hasV
        if (!parseBandCoords(args.get(opts::kVband), bandCoords, err)) return argError(err);
        config.cut.generator = engine::CutGenerator::VerticalLine;
    }
    // 公共导出选项：--format（分离时决定扩展名 / 合并时作扩展名回退）/ --quality（JPEG/WebP）/ --naming（分离命名模板）。
    if (!applyExportOptions(args, config.exportParams, err)) return argError(err);

    // --- 第 6 步：获取输入图像（--input 读文件失败 3；--size 生成参数非法 1）。---
    JobOptions opt = makeJobOptions(go, explicitCollapse);
    core::Image image;
    if (const ExitCode ec = resolveInputImage(args, image, opt); ec != ExitCode::Ok)
        return toInt(ec);

    // --- 装配切割几何：百分比段按图像宽 / 高换算成像素并做退化校验；带需图像尺寸构造贯穿全图矩形；
    //     写入 source（供 --save-config 完整）。---
    int bandA = 0, bandB = 0;
    if (!rectCoords.empty()) {
        const bool multi = config.cut.generator == engine::CutGenerator::MultiRect;
        for (std::size_t i = 0; i < rectCoords.size(); ++i) {
            engine::RectRegion r;
            if (!resolveRectCoords(rectCoords[i], image.width(), image.height(), r, err)) return argError(err);
            if (r.width() <= 0 || r.height() <= 0) {
                const std::string hint = multi ? IDC_CTR(msg, kHintRectOrderLead) + rectStrs[i] + IDC_CTR(msg, kHintRectOrderTail)
                                               : IDC_CTR(msg, kHintRectOrder);
                return argError(IDC_CTR(msg, kErrRectDegenerate), hint);
            }
            if (multi) config.cut.rects.push_back(r);
            else config.cut.rect = r;
        }
    } else if (config.cut.generator == engine::CutGenerator::HorizontalLine) {
        if (!resolveBandCoords(bandCoords, image.width(), bandA, bandB, err)) return argError(err);
        if (bandA >= bandB)
            return argError(IDC_CTR(msg, kErrHbandDegenerate), IDC_CTR(msg, kHintHbandOrder));
        config.cut.rect = engine::horizontalBand(bandA, bandB, image.width());
    } else {
        if (!resolveBandCoords(bandCoords, image.height(), bandA, bandB, err)) return argError(err);
        if (bandA >= bandB)
            return argError(IDC_CTR(msg, kErrVbandDegenerate), IDC_CTR(msg, kHintVbandOrder));
        config.cut.rect = engine::verticalBand(bandA, bandB, image.height());
    }
    config.source.width = image.width();
    config.source.height = image.height();

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
