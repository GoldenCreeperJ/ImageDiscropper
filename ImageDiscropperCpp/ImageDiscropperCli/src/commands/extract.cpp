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

#include "engine/engine.h" // EngineConfig / Tier / CutGenerator / Polarity / EmitMode / MergeLayout
#include "engine/region.h" // RectRegion / horizontalBand / verticalBand
#include "annotation/annotation_set.h" // AnnotationSet / Annotation（命令行标注烧录容器）

#include "annotation_args.h"
#include "arg_parser.h"
#include "command_support.h"
#include "generate_args.h"
#include "job.h"
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

    // --- 第 3 步：必填参数（缺失即退出码 1，早于任何 Core 调用）。---
    // 输入来源二选一：--input 读文件，或 --size(+--pattern…) 生成画布。
    if (!args.has(opts::kInput) && !hasGenerateOptions(args))
        return argError(IDC_CTR(msg, kErrMissingInput), IDC_CTR(msg, kHintInputOrSize));
    if (args.has(opts::kInput) && hasGenerateOptions(args))
        return argError(IDC_CTR(msg, kErrInputAndSizeMix), IDC_CTR(msg, kHintPickInputSource));
    if (!args.has(opts::kOutput))
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
    config.emitParams.mode = engine::EmitMode::Merged;
    config.emitParams.layout = engine::MergeLayout::Collapse;

    // --- 第 5 步：几何格式校验（在调用 Core 之前，错误一律退出码 1）。---
    // 带的两个坐标先暂存，待读取图像尺寸后再构造「贯穿全图」的带矩形（横带宽=W、竖带高=H）。
    std::string err;
    int bandA = 0, bandB = 0;
    if (args.has(opts::kRect)) {
        engine::RectRegion r;
        if (!parseRect(args.get(opts::kRect), r, err)) return argError(err);
        if (r.width() <= 0 || r.height() <= 0)
            return argError(IDC_CTR(msg, kErrRectDegenerate), IDC_CTR(msg, kHintRectOrder));
        config.cut.generator = engine::CutGenerator::Rect;
        config.cut.rect = r;
    } else if (args.has(opts::kHband)) {
        if (!parseBand(args.get(opts::kHband), bandA, bandB, err)) return argError(err);
        if (bandA >= bandB)
            return argError(IDC_CTR(msg, kErrHbandDegenerate), IDC_CTR(msg, kHintHbandOrder));
        config.cut.generator = engine::CutGenerator::HorizontalLine;
    } else { // --vband
        if (!parseBand(args.get(opts::kVband), bandA, bandB, err)) return argError(err);
        if (bandA >= bandB)
            return argError(IDC_CTR(msg, kErrVbandDegenerate), IDC_CTR(msg, kHintVbandOrder));
        config.cut.generator = engine::CutGenerator::VerticalLine;
    }
    // 公共导出选项：--format（合并时作为扩展名回退）/ --quality（JPEG/WebP）/ --naming（L1 合并单图用不到，但保持一致）。
    if (!applyExportOptions(args, config.emitParams, err)) return argError(err);

    // --- 第 6 步：获取输入图像（--input 读文件失败退出码 3；--size 生成参数非法退出码 1）。---
    const JobOptions opt = makeJobOptions(go, /*explicitCollapse=*/false);
    core::Image image;
    if (const ExitCode ec = resolveInputImage(args, image, opt); ec != ExitCode::Ok)
        return toInt(ec);

    // --- 装配切割几何：带需图像尺寸构造贯穿全图的矩形；并写入 source（供 --save-config 完整）。---
    if (config.cut.generator == engine::CutGenerator::HorizontalLine)
        config.cut.rect = engine::horizontalBand(bandA, bandB, image.width());
    else if (config.cut.generator == engine::CutGenerator::VerticalLine)
        config.cut.rect = engine::verticalBand(bandA, bandB, image.height());
    config.source.width = image.width();
    config.source.height = image.height();

    // --- 第 7 步：命令行标注（SPEC §7 annotations 元素语法）装配到临时集合，交 job 烧录 / 随 --save-config 透写。---
    //     坐标基准＝预处理产出（job 内先物化 preprocess 再烧标注）；无标注时传 nullptr（不烧录、不写段）。
    std::vector<annotation::Annotation> annos;
    if (!collectAnnotations(args, annos, err)) return argError(err);
    annotation::AnnotationSet annoSet;
    const annotation::AnnotationSet* annoPtr = nullptr;
    if (!annos.empty()) { annoSet.restore(std::move(annos)); annoPtr = &annoSet; }

    // --- 第 7 步：执行作业；若给出 --save-config 则改为序列化配置的 dry-run（不切割）。---
    const JobOutput out{args.get(opts::kOutput), /*separate=*/false};
    return toInt(finishJob(config, image, out, go.saveConfig, opt, annoPtr));
}

} // namespace idc::cli
