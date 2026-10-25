// ============================================================================
// 文件：src/commands/config.cpp
// 作用：实现 cmdConfig——配置文件命令。两条用途：
//       ① config --load <json> --input <img> (--output | --output-dir)：从 SPEC §7 schema 的 JSON
//          还原 EngineConfig 并执行（复用 Core 的 loadEngineConfig，绝不自实现解析，CONTRIBUTING.md「分层纪律」）。
//       ② config --load <json> --save-config <out.json>：加载后再序列化（转换 / 校验的 dry-run，
//          不读图、不切割）。
// 分块依据：一命令一文件（严禁上帝文件）；JSON 存取复用 Core engine_config_json，执行 / 退出码
//       复用 job，报错样板复用 command_support（CONTRIBUTING.md「分层纪律」）。配置文件不含图像 / 输出路径（SPEC §7 仅有
//       source 尺寸 + cut/select/order/export），故执行时的 --input 与输出路径须由命令行补齐。
// 说明（退出码判定）：
//   - --load 缺失 / 无值 → 1；文件不存在 / 不可访问 → 3；文件存在但 JSON 解析失败 → 1
//     （配置内容即“参数”，其格式错误归参数错误；文件层面的缺失 / 不可读归输入文件错误）。
//     · 标注段非法（含字段缺失 / 类型不符）时，Core 已逐条拼好报因串，本命令原样透传为详情
//       （而非压成一条通用「无法按配置 JSON schema 解析」），提示相应取标注语法（kHintAnnoSyntax）。
//   - 执行时缺 --input 或缺对应输出（分离 --output-dir / 合并 --output）→ 1。
//   - 读图失败 → 3；引擎业务失败（E-6/E-7 等）或显式坍缩不可行 → 2；导出失败 → 4（均由 job 映射）。
//   - --save-config 写盘失败 → 4。
//   - explicitCollapse：当且仅当 export 为 MERGED+COLLAPSE 时置真——这正是 IT-18（坍缩不可行 → 2）
//     经手写配置稳定触发的路径（Core 内部会自动降级为 REARRANGE 并返回 ok=true/collapsible=false，
//     job 据 explicitCollapse 显式报错退出码 2）。
// 常量层：选项名取 texts::opt（不可译）、报错文案经门面 IDC_CTR(msg, ...) 取 texts::msg；
//       标注段非法时的详情串来自 Core（IDC_KTR），本层不自行拼接逐条原因；
//       alpha 上限 255 与 clamp 的 0 属换算 / 边界常量，按判据保持就地。
// ============================================================================
#include "commands.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "engine/engine.h"             // EngineConfig / ExportMode / MergeLayout / Tier / Polarity
#include "engine/region.h"             // horizontalBand / verticalBand（--hband/--vband 字段覆盖的带矩形）
#include "core/path_utf8.h"            // pathFromUtf8（取代 C++23 已移除的 u8path）
#include "canvas/layer.h"              // canvas::applyAlpha（底图整体不透明度淡出，与 GUI 合成边界同口径）
#include "preprocess/preprocess_pipeline.h" // geometryOutput（标注百分比坐标的工作图基准）
#include "engine/engine_config_json.h" // loadEngineConfig（复用 Core JSON 模块，CONTRIBUTING.md「分层纪律」）
#include "generate/pattern.h"          // makePattern（配置 generate 段 → 程序生成源画布）
#include "annotation/annotation_set.h" // AnnotationSet（配置 annotations 段的加载容器）

#include "arg_parser.h"
#include "annotation_args.h"
#include "command_support.h"
#include "exit_code.h"
#include "generate_args.h"
#include "job.h"
#include "layer_args.h"
#include "i18n/cli_texts.h"
#include "i18n/cli_i18n.h"

namespace idc::cli {

// 选项名简写（不可译契约；报错文案走 IDC_CTR）。
namespace opts = texts::opt;

// 配置文件命令：config --load <file> [执行所需 --input / 输出]，或 --save-config <file>（转换 dry-run）。
int cmdConfig(const std::vector<std::string>& tokens, const GlobalOptions& go) {
    ArgParser args;
    args.parse(tokens);

    // --- 第 2.5 步：拒绝本命令不接受的输入（G1+A/B/C，退出码 1），杜绝拼错 / 跨命令误用 / 重复单值 /
    //     游离参数静默失效。导出 / 切割 / 层阶选项为「字段覆盖族」（加载后覆盖配置对应字段，见覆盖装配段）。---
    if (const int rc = rejectUnknownOptions(args, makeCommandArgs(
            {opts::kLoad, opts::kInput, opts::kOutput, opts::kOutputDir,
             opts::kTier, opts::kPolarity, opts::kRect, opts::kHband, opts::kVband,
             opts::kExportMode, opts::kExportLayout, opts::kCanvas, opts::kFormat, opts::kQuality, opts::kNaming, opts::kFit},
            MixGenerate | MixAnnotation | MixLayers | MixAssets,
            {opts::kReplaceAnnotations, opts::kReplaceLayers})); rc != 0) return rc;

    // --- 第 3 步：--load 必填且须带路径（config 命令唯一的参数来源）。---
    if (!args.has(opts::kLoad))
        return argError(IDC_CTR(msg, kErrMissingLoad), IDC_CTR(msg, kHintLoadFile));
    const std::string loadPath = args.get(opts::kLoad);
    if (loadPath.empty())
        return argError(IDC_CTR(msg, kErrLoadNoPath), IDC_CTR(msg, kHintLoadPath));

    // --- 第 6 步（文件层面）：配置文件须存在且可访问，否则输入文件错误（3）。loadPath 按 UTF-8 解析为宽字符路径。---
    if (std::error_code ec; !std::filesystem::exists(core::pathFromUtf8(loadPath), ec))
        return fail(ExitCode::InputError, IDC_CTR(msg, kErrConfigMissing), IDC_CTR(msg, kDetPath) + loadPath,
                    IDC_CTR(msg, kHintLoadReadable));

    // --- 复用 Core 解析 SPEC §7 schema；文件存在但内容非法（语法错误 / 根非对象）→ 参数错误（1）。---
    // 可选 annotations 段一并载入临时集合：执行时由 job 烧进工作图，dry-run 时随配置透写不丢。
    // annoErr：Core 对标注段的逐条报因（头注契约：专供 CLI 报因），非空时优先作为详情上抛，
    //   以免退化为一条通用 schema 提示而丢失「第 N 个元素：……」这类定位信息。
    engine::EngineConfig config;
    annotation::AnnotationSet annoSet;
    std::string annoLoadErr;
    core::PercentHints percentHints;   // 配置中以百分比书写的几何字段（--save-config 时原样回写）
    if (const auto loaded = engine::loadEngineConfig(loadPath, config, &annoSet, &annoLoadErr, &percentHints); !loaded) {
        // 三类失败分流：① 标注段逐条报因（annoLoadErr）；② §7 schema 值非法（枚举拼错 / 颜色 / 类型）的
        //   定位报因——Core 严格校验新增，已填入 CoreError.message；③ 文件级通用失败（语法 / 根非对象 / IO），
        //   Core 故意留空 message，由本层自产「无法解析」文案 + 路径。
        if (!annoLoadErr.empty())
            return fail(ExitCode::ArgError, IDC_CTR(msg, kErrConfigParse), annoLoadErr, IDC_CTR(msg, kHintAnnoSyntax));
        if (const std::string& coreMsg = loaded.error().message; !coreMsg.empty())
            return fail(ExitCode::ArgError, IDC_CTR(msg, kErrConfigParse), coreMsg, IDC_CTR(msg, kHintConfigSchema));
        return fail(ExitCode::ArgError, IDC_CTR(msg, kErrConfigParse),
                    IDC_CTR(msg, kDetParseFailedLead) + loadPath, IDC_CTR(msg, kHintConfigSchema));
    }

    // --- 命令行标注（--annotation / --annotations）：默认并入已载入的临时集合（追加在配置自带段之后）；---
    //     --replace-annotations 则改为替换（无命令行标注即清空），使 --save-config dry-run 与执行都带上最终标注。
    // --- 命令行图层：--layer/--layers 默认追加到配置 layers 段之后（与 --annotation 追加 annotations 同构；
    //     zOrder 未给 → 自增续尾，见 finalizeAppendedLayers）。--replace-layers 则改为替换（无命令行图层即移除该段）。---
    // 两者均可写百分比串（几何坐标相对基准尺寸），故收为一次性调用，由各路径在基准可知处触发：
    //   标注基准＝工作图（preprocess 几何步之后），图层基准＝画布；dry-run 用配置 source，执行用解出的图像尺寸。
    const auto mergeCliAnnoLayers = [&](const int canvasW, const int canvasH) -> int {
        const preprocess::PreprocessPipeline::GeometryOut work = config.preprocess.geometryOutput(canvasW, canvasH);
        std::vector<annotation::Annotation> cliAnnos;
        if (std::string annoErr; !collectAnnotations(args, cliAnnos, annoErr, work.width, work.height))
            return fail(ExitCode::ArgError, IDC_CTR(msg, kErrAnnoCliParse), annoErr, IDC_CTR(msg, kHintAnnoSyntax));
        if (args.has(opts::kReplaceAnnotations)) {
            annoSet.restore(std::move(cliAnnos));
            // 替换即编辑：清除标注几何的百分比回写记录（同上防值巧合误判）。
            std::erase_if(percentHints, [](const core::PercentHint& h) {
                return h.pointer.starts_with("/annotations/");
            });
        } else if (!cliAnnos.empty()) {
            std::vector<annotation::Annotation> merged = annoSet.annotations(); // 拷贝配置自带条目（Annotation 深拷贝 shape）
            for (auto& a : cliAnnos) merged.push_back(std::move(a));
            annoSet.restore(std::move(merged));
        }

        std::vector<canvas::ImageLayer> cliLayers;
        if (std::string layerErr; !collectLayers(args, cliLayers, layerErr, canvasW, canvasH))
            return fail(ExitCode::ArgError, IDC_CTR(msg, kErrLayerCliParse), layerErr, IDC_CTR(msg, kHintLayerSyntax));
        if (args.has(opts::kReplaceLayers)) {
            if (cliLayers.empty()) {
                config.layers.reset();
            } else {
                finalizeAppendedLayers({}, cliLayers);
                config.layers = std::move(cliLayers);
            }
            // 替换即编辑：清除图层几何的百分比回写记录（同上防值巧合误判）。
            std::erase_if(percentHints, [](const core::PercentHint& h) {
                return h.pointer.starts_with("/layers/");
            });
        } else if (!cliLayers.empty()) {
            if (!config.layers) config.layers = std::vector<canvas::ImageLayer>{};
            finalizeAppendedLayers(*config.layers, cliLayers);
            for (auto& l : cliLayers) config.layers->push_back(std::move(l));
        }
        return toInt(ExitCode::Ok);
    };

    // --- 字段覆盖（可选）：CLI 值覆盖配置对应字段，--save-config 落盘与执行同样生效。---
    RectCoords cutRectCoords;   // --rect 覆盖值（百分比段待基准可知再换算）
    {
        std::string err;
        if (args.has(opts::kTier) && !parseTier(args.get(opts::kTier), config.cut.tier, err)) return argError(err);
        if (args.has(opts::kPolarity) && !parsePolarity(args.get(opts::kPolarity), config.cut.polarity, err)) return argError(err);
        if (args.has(opts::kExportMode) && !parseExportMode(args.get(opts::kExportMode), config.exportParams.mode, err)) return argError(err);
        if (args.has(opts::kExportLayout) && !parseMerge(args.get(opts::kExportLayout), config.exportParams.layout, err)) return argError(err);
        if (args.has(opts::kCanvas)) {
            int cols = 0, rows = 0;
            if (!parseCanvas(args.get(opts::kCanvas), cols, rows, err)) return argError(err);
            config.exportParams.cols = cols;
            config.exportParams.rows = rows;
        }
        if (!applyExportOptions(args, config.exportParams, err)) return argError(err);   // --format / --quality / --naming
        // 切割几何覆盖：--rect 直接覆盖矩形；--hband/--vband 需图像尺寸转全幅带矩形，仅执行路径可用。
        const int geoKinds = static_cast<int>(args.has(opts::kRect)) +
                             static_cast<int>(args.has(opts::kHband)) +
                             static_cast<int>(args.has(opts::kVband));
        if (geoKinds > 1)
            return argError(IDC_CTR(msg, kErrGeoThreeOne), IDC_CTR(msg, kHintGeoNoMultiple));
        if (args.has(opts::kRect)) {
            if (!parseRectCoords(args.get(opts::kRect), cutRectCoords, err)) return argError(err);
            config.cut.generator = engine::CutGenerator::Rect;
            // 覆盖即编辑：清除切割几何的百分比回写记录（防新值恰与旧换算值相等而被误判为「未编辑」）。
            std::erase_if(percentHints, [](const core::PercentHint& h) {
                return h.pointer.starts_with("/cut/params/");
            });
        }
    }

    // --- 用途②：--save-config 为“加载后再序列化”的 dry-run（转换 / 校验），不读图、不切割。---
    //     保留 JSON 原有 source 尺寸（无图像可校正），仅完成一次读入 → 写出的往返。
    //     --assets-dir 三态：出现（flag 或 =path）→ 序列化成功后把 File 层源图按相对名打包
    //     （flag → <保存配置名>.assets/；=path → 该目录，且目录在保存配置目录之内时写 §7 assetsDir 字段）。
    // --- 重排前置校验（与 grid --compose 的 --canvas 强制一致）：仅 L3 可重排，且必须显式给定 cols/rows。---
    //     覆盖装配之后统一校验（--save-config dry-run 与执行同口径）：手写/转换/覆盖来的配置若违反，
    //     直接判参数错误（退出码 1），不交 Core 用默认值兜底。
    if (config.exportParams.mode == engine::ExportMode::Merged &&
        config.exportParams.layout == engine::MergeLayout::Rearrange) {
        if (config.cut.tier != engine::Tier::Grid)
            return argError(IDC_CTR(msg, kErrRearrangeNeedsL3), IDC_CTR(msg, kHintRearrangeTier));
        if (!config.exportParams.cols.has_value() || !config.exportParams.rows.has_value())
            return argError(IDC_CTR(msg, kErrRearrangeNeedsDims), IDC_CTR(msg, kHintRearrangeColsRows));
    }

    if (const std::string saveConfigPath = args.get(opts::kSaveConfig); !saveConfigPath.empty()) {
        if (args.has(opts::kHband) || args.has(opts::kVband))
            return argError(IDC_CTR(msg, kErrBandNeedsImage), IDC_CTR(msg, kHintBandNeedsImage));
        // --rect 覆盖的百分比段以配置 source 为基准换算（dry-run 不读图，source 即画布尺寸）。
        if (args.has(opts::kRect)) {
            engine::RectRegion r;
            if (std::string rectErr; !resolveRectCoords(cutRectCoords, config.source.width, config.source.height, r, rectErr))
                return argError(rectErr);
            if (r.width() <= 0 || r.height() <= 0)
                return argError(IDC_CTR(msg, kErrRectDegenerate), IDC_CTR(msg, kHintRectOrder));
            config.cut.rect = r;
        }
        // 命令行标注 / 图层：dry-run 不读图，基准取配置 source（标注再经 preprocess 推导工作图）。
        if (const int rc = mergeCliAnnoLayers(config.source.width, config.source.height); rc != 0) return rc;
        JobOptions opt = makeJobOptions(go, /*explicitCollapse=*/false);
        opt.assetsDir = assetsDirFromArgs(args);   // --assets-dir 三态（作业级通用，命令后）
        if (const std::string rel = relativizeAssetsDirForSave(saveConfigPath, opt.assetsDir.value_or("")); !rel.empty())
            config.assetsDir = rel;
        if (const ExitCode ec = saveConfigToFile(config, saveConfigPath, opt, &annoSet, &percentHints); ec != ExitCode::Ok)
            return toInt(ec);
        if (opt.assetsDir.has_value())
            return toInt(bundleLayerAssets(config, loadPath, saveConfigPath, opt));
        return toInt(ExitCode::Ok);
    }

    // --- 用途①：执行配置。SPEC §7 JSON 不含图像 / 输出路径，故须由命令行补齐。
    //     底图（画布基座）来源多选一：--input 读文件 / --size(+--pattern…) 命令行生成 / 配置自带 generate 段生成 /
    //     皆无则空底图（仅背景）。layers 段（可选）为**叠加图层**，与上述任一底图来源**共存**：
    //     命令层先解底图，再把 layers 以 base 为底 composeCanvas 展平。---
    const bool configHasBase = config.base.has_value();
    // 命令行图层也要参与底图守卫（仅 --layer / --size 无 --input 时同样可成画布）；
    //   最终是否走多层合成在解底图后按合并结果重新判定（configHasLayers 下方回写）。
    bool configHasLayers = config.layers.has_value();
    if (!args.has(opts::kInput) && !hasGenerateOptions(args) && !configHasBase && !configHasLayers &&
        !hasLayerOptions(args))
        return argError(IDC_CTR(msg, kErrConfigNeedsInput), IDC_CTR(msg, kHintConfigInput));
    if (!args.has(opts::kSize) && hasPatternOptions(args))
        return argError(IDC_CTR(msg, kErrPatternNeedsSize), IDC_CTR(msg, kHintPatternNeedsSize));
    if (args.has(opts::kFit) && !(args.has(opts::kInput) && hasGenerateOptions(args)))
        return argError(IDC_CTR(msg, kErrFitNeedsBoth));

    // 输出目标由 export.mode 决定：分离 → --output-dir（文件夹）；合并 → --output（单图）。
    const bool separate = config.exportParams.mode == engine::ExportMode::Separate;
    const std::string outKey = separate ? opts::kOutputDir : opts::kOutput;
    if (!args.has(outKey))
        return argError(IDC_CTR(msg, kErrConfigNeedsOutKey) + outKey,
                        separate ? IDC_CTR(msg, kHintConfigSeparate) : IDC_CTR(msg, kHintConfigMerged));
    // 模式级对称守卫：反向的输出选项（分离却带 --output / 合并却带 --output-dir）与 JSON 导出口径相左，
    //   一律退出码 1（--save-config dry-run 已在上方返回，不经过此处）。
    if (args.has(separate ? opts::kOutput : opts::kOutputDir))
        return argError(IDC_CTR(msg, kErrConfigOutKeyMismatch),
                        separate ? IDC_CTR(msg, kHintConfigSeparateOnly) : IDC_CTR(msg, kHintConfigMergedOnly));

    // 显式坍缩：仅 MERGED+COLLAPSE 时为真——不可坍缩则 job 返回退出码 2（IT-18 触发路径）。
    const bool explicitCollapse = config.exportParams.mode == engine::ExportMode::Merged &&
                                  config.exportParams.layout == engine::MergeLayout::Collapse;
    JobOptions opt = makeJobOptions(go, explicitCollapse);
    opt.assetsDir = assetsDirFromArgs(args);

    // --- 第 6 步：先解**底图 base**（--input 读文件失败 3 / --size 生成参数非法 1 / 配置 generate 段现算 / 皆无＝空底图），
    //     再取工作图：若有 layers 段，以 base 为底逐层解码 File 叠加层（相对配置目录）注入后 composeCanvas 展平；
    //     若无 layers 段，工作图＝base。---
    core::Image base;
    if (args.has(opts::kInput) && hasGenerateOptions(args)) {
        // --input 与 --size 同给：画布尺寸由 --size 定（含图案 / 底色），位图按配置落位声明摆入
        //   （无落位声明则居中铺入）；config.source 同步为画布尺寸。
        core::Image bmp;
        if (const ExitCode lec = loadInputImage(args.get(opts::kInput), bmp, opt); lec != ExitCode::Ok)
            return toInt(lec);
        if (std::string err; !buildGeneratedImage(args, base, err))
            return fail(ExitCode::ArgError, IDC_CTR(msg, kErrGenArgs), err, IDC_CTR(msg, kHintGenArgs));
        config.source.width = base.width();
        config.source.height = base.height();
        canvas::ImageLayer overlay;
        if (configHasBase && config.base->hasImagePlacement) {
            overlay = *config.base;   // 配置落位声明（transform 落位 + 内层 fit/anchor/pivot/x/y/backdrop）
        } else if (args.has(opts::kFit)) {
            overlay.hasImagePlacement = true;   // 无配置落位时由 --fit 构造：框恒=画布、锚点 center。
            overlay.boxWidth = base.width();
            overlay.boxHeight = base.height();
        }
        if (args.has(opts::kFit)) {
            if (std::string err; !parseFit(args.get(opts::kFit), overlay.imageFit, err)) return argError(err);
        }
        overlay.decodedPixels = std::move(bmp);   // 位图源按落位（或居中）摆入画布
        overlay.zOrder = 0;
        const core::Image composed = canvas::composeCanvas({overlay}, base.width(), base.height(), base);
        if (composed.empty())
            return fail(ExitCode::RuntimeError, IDC_CTR(msg, kErrLayerCanvasEmpty),
                        IDC_CTRF(msg, kDetSourceSize, std::to_string(base.width()) + "x" + std::to_string(base.height())),
                        IDC_CTR(msg, kHintSourceBounds));
        base = composed;
    } else if (args.has(opts::kInput) || hasGenerateOptions(args)) {
        if (const ExitCode lec = resolveInputImage(args, base, opt); lec != ExitCode::Ok)
            return toInt(lec);
        // 配置带底图内层落位（路径不入配置）：把已解析的外部输入图按落位摆入 source 尺寸画布。
        //   无落位声明则保持恒等底图（--input 铺满画布）——与既有外部用法一致。
        if (configHasBase && config.base->hasImagePlacement) {
            if (const ExitCode pec = placeInputIntoBase(config, base, opt); pec != ExitCode::Ok)
                return toInt(pec);
        }
    } else if (configHasBase) {
        // 无外部输入：仅纯图案底图（Recipe）可按 source 尺寸现算；
        //   带落位声明者需 --input 提供源图，buildBaseCanvas 内部报因（退出码 3）。
        if (const ExitCode ec = buildBaseCanvas(config, base, opt); ec != ExitCode::Ok)
            return toInt(ec);
    }
    // 底图整体不透明度（§7 generate 段 transform.opacity）：与 GUI 合成边界同口径——base 解出后、进入图层合成前
    //   统一施加一次淡出（composeBase 已不烘焙 opacity，故此为唯一应用点、无双淡）；无 generate 段则不淡。
    if (configHasBase)
        canvas::applyAlpha(base, static_cast<std::uint8_t>(std::clamp(config.base->transform.opacity, 0, 255)));
    core::Image image;
    if (configHasLayers) {
        // 多层画布：以 base 为底、逐层解码 File 叠加层（相对配置目录）注入 → composeCanvas 展平为 config.source 尺寸的画布。
        if (const ExitCode ec = buildLayerCanvas(config, loadPath, base, image, opt); ec != ExitCode::Ok)
            return toInt(ec);
    } else {
        image = std::move(base);   // 无图层：工作图即底图（--input / generate / 空）。仅 layers 缺失且未给输入的情形在上方守卫处即退出。
    }

    // 画布级整体不透明度（§7 顶层 opacity 键）：与 GUI 产出边界同口径——底图/图层解出、合成后，进 executeJob
    //   前对整幅工作图统一施加一次淡出（缺省 255 时 applyAlpha 恒等；executeJob 内的 preprocess 与标注烧录
    //   均不施加整体淡出——通道映射/split/invert 不动 alpha，rotate/flip 原样搬运，resize 仅随双线性插值、不另施乘）。
    canvas::applyAlpha(image, static_cast<std::uint8_t>(std::clamp(config.canvasOpacity, 0, 255)));

    // 命令行标注 / 图层：画布尺寸已定（解出的图像），按此换算百分比坐标后并入（标注基准＝工作图）。
    if (const int rc = mergeCliAnnoLayers(image.width(), image.height()); rc != 0) return rc;
    configHasLayers = config.layers.has_value();   // 合并后回写：命令行图层使无段配置转为多层画布，--replace-layers 空值则移除

    // 字段覆盖的 --rect（百分比段按工作图宽 / 高换算）：在进 executeJob 前写回 config.cut。
    if (args.has(opts::kRect)) {
        engine::RectRegion r;
        if (std::string rectErr; !resolveRectCoords(cutRectCoords, image.width(), image.height(), r, rectErr)) return argError(rectErr);
        if (r.width() <= 0 || r.height() <= 0)
            return argError(IDC_CTR(msg, kErrRectDegenerate), IDC_CTR(msg, kHintRectOrder));
        config.cut.rect = r;
    }

    // 字段覆盖的 --hband/--vband（需图像尺寸）：带坐标转「贯穿全图」的带矩形（横带宽=W、竖带高=H），
    //   在进 executeJob 前写回 config.cut（与 extract 的带构造同口径）。
    if (args.has(opts::kHband) || args.has(opts::kVband)) {
        int bandA = 0, bandB = 0;
        std::string bandErr;
        if (args.has(opts::kHband)) {
            BandCoords bandCoords;
            if (!parseBandCoords(args.get(opts::kHband), bandCoords, bandErr)) return argError(bandErr);
            if (!resolveBandCoords(bandCoords, image.width(), bandA, bandB, bandErr)) return argError(bandErr);
            if (bandA >= bandB)
                return argError(IDC_CTR(msg, kErrHbandDegenerate), IDC_CTR(msg, kHintHbandOrder));
            config.cut.generator = engine::CutGenerator::HorizontalLine;
            config.cut.rect = engine::horizontalBand(bandA, bandB, image.width());
        } else {
            BandCoords bandCoords;
            if (!parseBandCoords(args.get(opts::kVband), bandCoords, bandErr)) return argError(bandErr);
            if (!resolveBandCoords(bandCoords, image.height(), bandA, bandB, bandErr)) return argError(bandErr);
            if (bandA >= bandB)
                return argError(IDC_CTR(msg, kErrVbandDegenerate), IDC_CTR(msg, kHintVbandOrder));
            config.cut.generator = engine::CutGenerator::VerticalLine;
            config.cut.rect = engine::verticalBand(bandA, bandB, image.height());
        }
    }

    // --- 第 7 步：执行（runEngine → 坍缩校验 → exportImage，退出码由 job 映射）。---
    const JobOutput out{args.get(outKey), separate};
    return toInt(executeJob(config, image, out, opt, &annoSet));
}

} // namespace idc::cli
