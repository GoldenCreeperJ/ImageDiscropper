// ============================================================================
// 文件：src/commands/config.cpp
// 作用：实现 cmdConfig——配置文件命令。两条用途：
//       ① config --load <json> --input <img> (--output | --output-dir)：从 SPEC §7 schema 的 JSON
//          还原 EngineConfig 并执行（复用 Core 的 loadEngineConfig，绝不自实现解析，CONTRIBUTING.md「分层纪律」）。
//       ② config --load <json> --save-config <out.json>：加载后再序列化（转换 / 校验的 dry-run，
//          不读图、不切割）。全局 --config <file> 亦由 cliMain 路由到此（同一执行通道，NFR-0）。
// 分块依据：一命令一文件（严禁上帝文件）；JSON 存取复用 Core engine_config_json，执行 / 退出码
//       复用 job，报错样板复用 command_support（CONTRIBUTING.md「分层纪律」）。配置文件不含图像 / 输出路径（SPEC §7 仅有
//       source 尺寸 + cut/select/order/emit），故执行时的 --input 与输出路径须由命令行补齐。
// 说明（退出码判定）：
//   - --load 缺失 / 无值 → 1；文件不存在 / 不可访问 → 3；文件存在但 JSON 解析失败 → 1
//     （配置内容即“参数”，其格式错误归参数错误；文件层面的缺失 / 不可读归输入文件错误）。
//     · 标注段非法（含字段缺失 / 类型不符）时，Core 已逐条拼好报因串，本命令原样透传为详情
//       （而非压成一条通用「无法按配置 JSON schema 解析」），提示相应取标注语法（kHintAnnoSyntax）。
//   - 执行时缺 --input 或缺对应输出（分离 --output-dir / 合并 --output）→ 1。
//   - 读图失败 → 3；引擎业务失败（E-6/E-7 等）或显式坍缩不可行 → 2；导出失败 → 4（均由 job 映射）。
//   - --save-config 写盘失败 → 4。
//   - explicitCollapse：当且仅当 emit 为 MERGED+COLLAPSE 时置真——这正是 IT-18（坍缩不可行 → 2）
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

#include "engine/engine.h"             // EngineConfig / EmitMode / MergeLayout
#include "core/path_utf8.h"            // pathFromUtf8（取代 C++23 已移除的 u8path）
#include "canvas/layer.h"              // canvas::applyAlpha（底图整体不透明度淡出，与 GUI 合成边界同口径）
#include "engine/engine_config_json.h" // loadEngineConfig（复用 Core JSON 模块，CONTRIBUTING.md「分层纪律」）
#include "generate/pattern.h"          // makePattern（配置 generate 段 → 程序生成源画布）
#include "annotation/annotation_set.h" // AnnotationSet（配置 annotations 段的加载容器）

#include "arg_parser.h"
#include "annotation_args.h"
#include "command_support.h"
#include "exit_code.h"
#include "generate_args.h"
#include "job.h"
#include "i18n/cli_texts.h"
#include "i18n/cli_i18n.h"

namespace idc::cli {

// 选项名简写（不可译契约；报错文案走 IDC_CTR）。
namespace opts = texts::opt;

// 配置文件命令：config --load <file> [执行所需 --input / 输出]，或 --save-config <file>（转换 dry-run）。
int cmdConfig(const std::vector<std::string>& tokens, const GlobalOptions& go) {
    ArgParser args;
    args.parse(tokens);

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
    if (!engine::loadEngineConfig(loadPath, config, &annoSet, &annoLoadErr)) {
        const bool annoRejected = !annoLoadErr.empty();
        return fail(ExitCode::ArgError, IDC_CTR(msg, kErrConfigParse),
                    annoRejected ? annoLoadErr : IDC_CTR(msg, kDetParseFailedLead) + loadPath,
                    annoRejected ? IDC_CTR(msg, kHintAnnoSyntax) : IDC_CTR(msg, kHintConfigSchema));
    }

    // --- 命令行标注（--annotation / --annotations）：并入已载入的临时集合（追加在配置自带段之后），---
    //     使 --save-config dry-run 与执行都带上「配置自带 + 命令行」标注（坐标基准＝预处理产出）。
    std::vector<annotation::Annotation> cliAnnos;
    if (std::string annoErr; !collectAnnotations(args, cliAnnos, annoErr))
        return fail(ExitCode::ArgError, IDC_CTR(msg, kErrAnnoCliParse), annoErr, IDC_CTR(msg, kHintAnnoSyntax));
    if (!cliAnnos.empty()) {
        std::vector<annotation::Annotation> merged = annoSet.annotations(); // 拷贝配置自带条目（Annotation 深拷贝 shape）
        for (auto& a : cliAnnos) merged.push_back(std::move(a));
        annoSet.restore(std::move(merged));
    }

    // --- 用途②：--save-config 为“加载后再序列化”的 dry-run（转换 / 校验），不读图、不切割。---
    //     保留 JSON 原有 source 尺寸（无图像可校正），仅完成一次读入 → 写出的往返。
    //     --bundle-assets：序列化成功后把 File 层源图按相对名复制进 <配置名>.assets/（越界 / 缺源报错）。
    if (!go.saveConfig.empty()) {
        const JobOptions opt = makeJobOptions(go, /*explicitCollapse=*/false);
        if (const ExitCode ec = saveConfigToFile(config, go.saveConfig, opt, &annoSet); ec != ExitCode::Ok)
            return toInt(ec);
        if (go.bundleAssets)
            return toInt(bundleLayerAssets(config, loadPath, go.saveConfig, opt));
        return toInt(ExitCode::Ok);
    }

    // --- 用途①：执行配置。SPEC §7 JSON 不含图像 / 输出路径，故须由命令行补齐。
    //     底图（画布基座）来源多选一：--input 读文件 / --size(+--pattern…) 命令行生成 / 配置自带 generate 段生成 /
    //     皆无则空底图（仅背景）。layers 段（可选）为**叠加图层**，与上述任一底图来源**共存**：
    //     命令层先解底图，再把 layers 以 base 为底 composeCanvas 展平。---
    const bool configHasBase = config.base.has_value();
    const bool configHasLayers = config.layers.has_value();
    if (!args.has(opts::kInput) && !hasGenerateOptions(args) && !configHasBase && !configHasLayers)
        return argError(IDC_CTR(msg, kErrConfigNeedsInput), IDC_CTR(msg, kHintConfigInput));
    if (args.has(opts::kInput) && hasGenerateOptions(args))
        return argError(IDC_CTR(msg, kErrInputAndSizeMix), IDC_CTR(msg, kHintPickInputSource));

    // --- 重排前置校验（与 grid --compose 的 --canvas 强制一致）：仅 L3 可重排，且必须显式给定 cols/rows。---
    //     手写/转换来的配置若违反，直接判参数错误（退出码 1），不交 Core 用默认值兜底。
    if (config.emitParams.mode == engine::EmitMode::Merged &&
        config.emitParams.layout == engine::MergeLayout::Rearrange) {
        if (config.cut.tier != engine::Tier::Grid)
            return argError(IDC_CTR(msg, kErrRearrangeNeedsL3), IDC_CTR(msg, kHintRearrangeTier));
        if (!config.emitParams.cols.has_value() || !config.emitParams.rows.has_value())
            return argError(IDC_CTR(msg, kErrRearrangeNeedsDims), IDC_CTR(msg, kHintRearrangeColsRows));
    }

    // 输出目标由 emit.mode 决定：分离 → --output-dir（文件夹）；合并 → --output（单图）。
    const bool separate = config.emitParams.mode == engine::EmitMode::Separate;
    const std::string outKey = separate ? opts::kOutputDir : opts::kOutput;
    if (!args.has(outKey))
        return argError(IDC_CTR(msg, kErrConfigNeedsOutKey) + outKey,
                        separate ? IDC_CTR(msg, kHintConfigSeparate) : IDC_CTR(msg, kHintConfigMerged));

    // 显式坍缩：仅 MERGED+COLLAPSE 时为真——不可坍缩则 job 返回退出码 2（IT-18 触发路径）。
    const bool explicitCollapse = config.emitParams.mode == engine::EmitMode::Merged &&
                                  config.emitParams.layout == engine::MergeLayout::Collapse;
    const JobOptions opt = makeJobOptions(go, explicitCollapse);

    // --- 第 6 步：先解**底图 base**（--input 读文件失败 3 / --size 生成参数非法 1 / 配置 generate 段现算 / 皆无＝空底图），
    //     再取工作图：若有 layers 段，以 base 为底逐层解码 File 叠加层（相对配置目录）注入后 composeCanvas 展平；
    //     若无 layers 段，工作图＝base。---
    core::Image base;
    if (args.has(opts::kInput) || hasGenerateOptions(args)) {
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
        image = std::move(base);   // 无图层：工作图即底图（--input / generate / 空）。若无底图又无图层（仅 layers 缺失且未给输入）本已被上方守卫拦下。
    }

    // 画布级整体不透明度（§7 顶层 opacity 键）：与 GUI 产出边界同口径——底图/图层解出、合成后，进 executeJob
    //   前对整幅工作图统一施加一次淡出（缺省 255 时 applyAlpha 恒等；executeJob 内的 preprocess 与标注烧录
    //   均不再做整体淡出——通道映射/split/invert 不动 alpha，rotate/flip 原样搬运，resize 仅随双线性插值、不另施乘）。
    canvas::applyAlpha(image, static_cast<std::uint8_t>(std::clamp(config.canvasOpacity, 0, 255)));

    // --- 第 7 步：执行（runEngine → 坍缩校验 → exportImage，退出码由 job 映射）。---
    const JobOutput out{args.get(outKey), separate};
    return toInt(executeJob(config, image, out, opt, &annoSet));
}

} // namespace idc::cli
