// ============================================================================
// 文件：src/commands/extract.cpp
// 作用：实现 cmdExtract——L1 标准提取模式（SPEC §3.2）的命令行薄壳。
//       职责仅四件（CONTRIBUTING.md「分层纪律」）：解析 --input / (--rect|--hband|--vband) / --output / --format →
//       按固定顺序校验 → 装配 EngineConfig（极性恒 keep、输出恒为合并单图 collapse）→ 交 job 执行。
//       不含任何切割 / 几何 / 极性判断逻辑，全部委托 Core 的统一引擎（NFR-0：模式即参数预设）。
// 分块依据：一个子命令一个文件（严禁上帝文件）；执行与退出码映射复用 job（同一执行通道），
//       值解析复用 value_parser，报错样板复用 command_support，均不在此重实现（CONTRIBUTING.md「分层纪律」）。
// 常量层：选项名取 texts::opt（不可译）、报错文案经门面 IDC_CTR(msg, ...) 取 texts::msg；
//       跨命令重复的同值文案（缺输入 / 矩形退化 / 带退化 …）由 msg 归一，不就地重复。
// 说明：L1 的保留区恒为「单个中心矩形」或「整条带」，其补集必为若干整行/整列，故必可坍缩为
//       单图；explicitCollapse 恒为 false，本命令不会触发坍缩不可行（退出码 2 仅 L2/config 才可能）。
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

// L1 标准提取：--input + (--rect|--hband|--vband 三选一) + --output（极性 keep，合并单图）。
int cmdExtract(const std::vector<std::string>& tokens, const GlobalOptions& go) {
    ArgParser args;
    args.parse(tokens);

    // --- 第 2.5 步：拒绝本命令不接受的输入（G1+A/B/C，退出码 1），杜绝拼错 / 跨命令误用 / 重复单值 / 游离参数静默失效。---
    //     --naming 不并入（输出恒为单图，命名模板无消费点，按「不静默吞」原则拒绝）。---
    if (const int rc = rejectUnknownOptions(args, makeCommandArgs(
            {opts::kInput, opts::kRect, opts::kHband, opts::kVband, opts::kOutput, opts::kFormat, opts::kQuality, opts::kFit},
            MixGenerate | MixAnnotation | MixLayers | MixAssets)); rc != 0) return rc;

    // --- 第 3 步：必填参数（缺失即退出码 1，早于任何 Core 调用）。---
    // 输入来源二选一：--input 读文件，或 --size(+--pattern…) 生成画布。
    if (!args.has(opts::kInput) && !hasGenerateOptions(args))
        return argError(IDC_CTR(msg, kErrMissingInput), IDC_CTR(msg, kHintInputOrSize));
    if (!args.has(opts::kSize) && hasPatternOptions(args))
        return argError(IDC_CTR(msg, kErrPatternNeedsSize), IDC_CTR(msg, kHintPatternNeedsSize));
    if (!args.has(opts::kSaveConfig) && !args.has(opts::kOutput))
        return argError(IDC_CTR(msg, kErrMissingOutput), IDC_CTR(msg, kHintExtractOutput));

    // --- 第 4 步：互斥——几何参数三选一（恰好一个）。---
    const int geoKinds = static_cast<int>(args.has(opts::kRect)) +
                         static_cast<int>(args.has(opts::kHband)) +
                         static_cast<int>(args.has(opts::kVband));
    if (geoKinds != 1)
        return argError(IDC_CTR(msg, kErrGeoThreeOne),
                        geoKinds == 0 ? IDC_CTR(msg, kHintGeoExactlyOne)
                                      : IDC_CTR(msg, kHintGeoNoMultiple));

    // --- 装配 EngineConfig 的固定部分：L1 极性恒 keep、输出恒为合并单图（collapse）。---
    engine::EngineConfig config;
    config.cut.tier = engine::Tier::Standard;
    config.cut.polarity = engine::Polarity::Keep;
    config.exportParams.mode = engine::ExportMode::Merged;
    config.exportParams.layout = engine::MergeLayout::Collapse;

    // --- 第 5 步：几何形态校验（在调用 Core 之前，错误一律退出码 1）。---
    //     只判段数与每段形态（整数像素或百分比串）；百分比需图像尺寸作基准，故换算与退化校验
    //     留到第 6 步解图之后。
    std::string err;
    RectCoords rectCoords;
    BandCoords bandCoords;
    if (args.has(opts::kRect)) {
        if (!parseRectCoords(args.get(opts::kRect), rectCoords, err)) return argError(err);
        config.cut.generator = engine::CutGenerator::Rect;
    } else if (args.has(opts::kHband)) {
        if (!parseBandCoords(args.get(opts::kHband), bandCoords, err)) return argError(err);
        config.cut.generator = engine::CutGenerator::HorizontalLine;
    } else { // --vband
        if (!parseBandCoords(args.get(opts::kVband), bandCoords, err)) return argError(err);
        config.cut.generator = engine::CutGenerator::VerticalLine;
    }
    // 公共导出选项：--format（合并时作为扩展名回退）/ --quality（JPEG/WebP）。
    if (!applyExportOptions(args, config.exportParams, err)) return argError(err);

    // --- 第 6 步：获取输入图像（--input 读文件失败退出码 3；--size 生成参数非法退出码 1）。---
    JobOptions opt = makeJobOptions(go, /*explicitCollapse=*/false);
    core::Image image;
    if (const ExitCode ec = resolveInputImage(args, image, opt); ec != ExitCode::Ok)
        return toInt(ec);

    // --- 装配切割几何：百分比段按图像宽 / 高换算成像素（横向坐标用宽、纵向用高）并做退化校验；
    //     带需图像尺寸构造贯穿全图的矩形；并写入 source（供 --save-config 完整）。---
    int bandA = 0, bandB = 0;
    if (config.cut.generator == engine::CutGenerator::Rect) {
        if (!resolveRectCoords(rectCoords, image.width(), image.height(), config.cut.rect, err))
            return argError(err);
        if (config.cut.rect.width() <= 0 || config.cut.rect.height() <= 0)
            return argError(IDC_CTR(msg, kErrRectDegenerate), IDC_CTR(msg, kHintRectOrder));
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

    // --- 第 7 步：命令行图层（--layer/--layers）装配：作为叠加层展平进引擎（与 --annotation 对称，
    //     资产按当前工作目录解析，无 <配置名>.assets/ 层级；zOrder 未给自增续尾）。---
    std::vector<canvas::ImageLayer> cliLayers;
    if (!collectLayers(args, cliLayers, err, image.width(), image.height())) return argError(err);
    if (!cliLayers.empty()) {
        finalizeAppendedLayers({}, cliLayers);
        config.layers = std::move(cliLayers);
    }

    // --- 第 7 步：执行作业；若给出 --save-config 则走序列化配置的 dry-run（不切割）。---
    const JobOutput out{args.get(opts::kOutput), /*separate=*/false};
    opt.assetsDir = assetsDirFromArgs(args);   // --assets-dir 三态（作业级通用，命令后）
    return toInt(finishJob(config, image, out, args.get(opts::kSaveConfig), opt, annoPtr));
}

} // namespace idc::cli
