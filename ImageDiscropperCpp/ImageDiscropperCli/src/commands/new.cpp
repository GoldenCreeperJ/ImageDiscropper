// ============================================================================
// 文件：src/commands/new.cpp
// 作用：实现 cmdNew——「新建图像」的命令行入口（对齐 GUI「新建」对话框）。用 --size 指定尺寸、
//       --bg 指定底色（含 alpha）、--pattern 等指定程序化图案，合成一张全新画布并写盘到 --output。
//       可选 --annotation/--layer 在生成画布上烧入标注 / 叠加图层（与切割命令同口径：图层先合成、
//       标注恒在最上；坐标基准＝生成画布尺寸）。职责仅三件（CONTRIBUTING.md「分层纪律」）：解析并校验
//       生成选项 → 交 generate_args 委托 Core generate::makePattern 合成 → 交 job 复用 exportImage 落盘；
//       不含任何像素 / 图案逻辑。
// 分块依据：一命令一文件（严禁上帝文件）；参数装配复用 generate_args，生成委托 Core，落盘与格式推断
//       复用 job::writeGeneratedImage（同一 I/O 通道），报错样板复用 command_support。
// 说明：new 是「创建」而非「切割」，故拒绝 --input；尺寸非法（<=0 / 超单边上限 / 超总像素上限）、
//       图案名/颜色/方向/周期非法一律退出码 1（早于生成），落盘失败退出码 4。缺省图案为 solid（纯色），
//       底色 alpha<FF 即得透明 / 半透明画布。--pad-color 设定有损格式（JPEG/BMP）压平透明像素所用的
//       背景色（透传 export.padColor）；--save-config 触发 dry-run：不落像素，而把本次配方（source 尺寸
//       + generate 段 + annotations/layers 段 + 全幅 keep 切割段 + export）序列化进配置 JSON（此路径不要求
//       --output），config --load 可直接复现画布。
//       图层资产按当前工作目录解析（无配置文件锚点）；--assets-dir 三态与其余命令同规（=path 亦写 assetsDir 字段）。
// 常量层：选项名取 texts::opt（不可译）、报错文案与日志经门面 IDC_CTR(msg, ...) 取 texts::msg。
// ============================================================================
#include "commands.h"

#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "engine/engine.h"      // EngineConfig / Tier / CutGenerator / Polarity（--save-config 序列化配方）
#include "engine/composition.h" // CompositionParams / ExportFormat
#include "engine/region.h"      // RectRegion（全幅保留切割段）
#include "generate/pattern.h"   // generate::PatternParams / makePattern
#include "annotation/annotation_set.h" // AnnotationSet（命令行标注烧录容器）
#include "canvas/layer.h"       // ImageLayer（命令行图层装配 / 生成底图）

#include "annotation_args.h"
#include "command_support.h"
#include "generate_args.h"
#include "job.h"
#include "layer_args.h"
#include "value_parser.h"
#include "i18n/cli_texts.h"
#include "i18n/cli_i18n.h"

namespace idc::cli {

// 选项名简写（不可译契约；报错文案走 IDC_CTR）。
namespace opts = texts::opt;

// 新建图像：--size WxH [--pattern/--orientation/--bg/--fg/--period/--line-width/--dot-radius/--seed/--noise]
//           [--format/--quality/--pad-color] [--output <file> | --save-config <file>（dry-run）]。
int cmdNew(const std::vector<std::string>& tokens, const GlobalOptions& go) {
    ArgParser args;
    args.parse(tokens);

    // --- 第 2.5 步：拒绝本命令不接受的输入（G1+A/B/C，退出码 1）。--input 列入白名单，
    //     以便第 3 步给出“new 不接受 --input”的专属报因（而非笼统的未知选项）。---
    if (const int rc = rejectUnknownOptions(args, makeCommandArgs(
            {opts::kInput, opts::kOutput, opts::kFormat, opts::kQuality, opts::kPadColor},
            MixGenerate | MixAnnotation | MixLayers | MixAssets)); rc != 0) return rc;

    // --- 第 3 步：必填与前置互斥（早于任何 Core 调用，一律退出码 1）。---
    if (!args.has(opts::kSize))
        return argError(IDC_CTR(msg, kErrMissingSize), IDC_CTR(msg, kHintSizeForm));
    if (args.has(opts::kInput))
        return argError(IDC_CTR(msg, kErrNewNoInput), IDC_CTR(msg, kHintNewPurpose));
    // --save-config 为 dry-run（仅序列化配方），此路径不要求 --output；否则必须给 --output。
    const std::string saveConfigPath = args.get(opts::kSaveConfig);
    const bool dryRun = !saveConfigPath.empty();
    if (!dryRun && !args.has(opts::kOutput))
        return argError(IDC_CTR(msg, kErrMissingOutput), IDC_CTR(msg, kHintNewOutput));

    // --- 第 5 步：可选导出选项（--format 缺省由扩展名推断；--quality 仅 JPEG/WebP 生效；
    //         --pad-color 设定有损格式压平透明像素的背景色，透传 export.padColor）。---
    engine::CompositionParams exportParams;
    std::string err;
    if (args.has(opts::kFormat) && !parseFormat(args.get(opts::kFormat), exportParams.format, err))
        return argError(err);
    if (args.has(opts::kQuality) && !parseQuality(args.get(opts::kQuality), exportParams.quality, err))
        return argError(err);
    if (args.has(opts::kPadColor) && !parseColor(args.get(opts::kPadColor), exportParams.padColor, err))
        return argError(err);

    // --- 生成参数（委托 generate_args 解析；尺寸/图案/颜色/周期等非法在此转退出码 1，含总像素上限）。---
    int width = 0, height = 0;
    generate::PatternParams params;
    if (!parseGenerateParams(args, width, height, params, err))
        return fail(ExitCode::ArgError, IDC_CTR(msg, kErrGenArgs), err, IDC_CTR(msg, kHintGenArgs));

    const JobOptions opt = [&] {
        JobOptions o = makeJobOptions(go, /*explicitCollapse=*/false);
        o.assetsDir = assetsDirFromArgs(args);   // --assets-dir 三态（作业级通用，命令后）
        return o;
    }();

    // --- 装配 EngineConfig（dry-run 序列化与执行共用同一份）：生成底图 + 命令行标注 / 图层。---
    engine::EngineConfig config;
    config.source.width = width;
    config.source.height = height;
    // 程序生成底图：同构 canvas::ImageLayer（图案元素、无图片元素、transform 恒等），序列化后即为配置的 generate 段。
    canvas::ImageLayer base;
    base.backdrop = params;
    config.base = base;
    config.exportParams = exportParams;
    // 全幅保留的切割段：使配置经 config --load 可直接复现画布（全幅保留＝整图输出，语义无损）。
    config.cut.tier = engine::Tier::Standard;
    config.cut.generator = engine::CutGenerator::Rect;
    config.cut.polarity = engine::Polarity::Keep;
    config.cut.rect = engine::RectRegion(0, 0, width, height);

    // --- 命令行标注：执行时烧入生成画布（坐标基准＝画布尺寸，几何坐标可写百分比串）；dry-run 随配置透写 annotations 段。---
    std::vector<annotation::Annotation> annos;
    if (!collectAnnotations(args, annos, err, width, height)) return argError(err);
    annotation::AnnotationSet annoSet;
    const annotation::AnnotationSet* annoPtr = nullptr;
    if (!annos.empty()) { annoSet.restore(std::move(annos)); annoPtr = &annoSet; }

    // --- 命令行图层：以生成画布为底叠加展平（与 --annotation 对称；资产按当前工作目录解析，
    //     zOrder 未给自增续尾）。---
    std::vector<canvas::ImageLayer> cliLayers;
    if (!collectLayers(args, cliLayers, err, width, height)) return argError(err);
    if (!cliLayers.empty()) {
        finalizeAppendedLayers({}, cliLayers);
        config.layers = std::move(cliLayers);
    }

    // --- dry-run：把配方（source 尺寸 + generate 段 + annotations/layers 段 + export）序列化进配置，
    //     不落像素。--assets-dir 三态：出现 → 序列化成功后把 File 层源图按相对名打包
    //     （flag → <保存配置名>.assets/；=path → 该目录，且目录在保存配置目录之内时写 assetsDir 字段）。---
    if (dryRun) {
        if (const std::string rel = relativizeAssetsDirForSave(saveConfigPath, opt.assetsDir.value_or("")); !rel.empty())
            config.assetsDir = rel;
        if (const ExitCode ec = saveConfigToFile(config, saveConfigPath, opt, annoPtr); ec != ExitCode::Ok) return toInt(ec);
        if (opt.assetsDir.has_value())
            return toInt(bundleLayerAssets(config, /*configPath=*/"", saveConfigPath, opt));
        return toInt(ExitCode::Ok);
    }

    // --- 生成画布（委托 Core makePattern；参数已在上面校验，越界再产空图作防御）。---
    core::Image image = generate::makePattern(width, height, params);
    if (image.empty())
        return fail(ExitCode::ArgError, IDC_CTR(msg, kErrPatternGenFailed),
                    IDC_CTR(msg, kDetFactoryEmpty),
                    IDC_CTR(msg, kHintGenArgs));

    // --- 命令行图层叠加：以生成画布为底 composeCanvas 展平（缺资产默认降级占位；--strict 报退出码 3）。---
    if (config.layers.has_value() && !config.layers->empty()) {
        core::Image composed;
        if (const ExitCode ec = buildLayerCanvas(config, /*configPath=*/"", image, composed, opt); ec != ExitCode::Ok)
            return toInt(ec);
        image = std::move(composed);
    }

    // --- 命令行标注烧入（层序与切割命令同口径：图层合成之后、落盘之前——标注恒在最上）。---
    if (annoPtr != nullptr) {
        core::Image burned = annoPtr->burnIn(image);
        image = std::move(burned);
    }

    // --- 落盘（复用 job 的合并单图通道：扩展名推断格式 / 自动建父目录 / JPEG 按 pad-color 压平）。---
    if (const ExitCode ec = writeGeneratedImage(image, args.get(opts::kOutput), exportParams, opt); ec != ExitCode::Ok)
        return toInt(ec);

    // --- 正常结果输出到 stdout（数据行，恒写；沉默是金只约束诊断日志）。---
    std::cout << IDC_CTR(msg, kLogImageGenerated) << args.get(opts::kOutput)
              << IDC_CTR(msg, kLogParenOpen) << image.width() << "x" << image.height() << IDC_CTR(msg, kLogParenClose);
    return toInt(ExitCode::Ok);
}

} // namespace idc::cli
