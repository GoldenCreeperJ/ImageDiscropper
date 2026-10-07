// ============================================================================
// 文件：src/commands/new.cpp
// 作用：实现 cmdNew——「新建图像」的命令行入口（对齐 GUI「新建」对话框）。用 --size 指定尺寸、
//       --bg 指定底色（含 alpha）、--pattern 等指定程序化图案，合成一张全新画布并写盘到 --output。
//       职责仅三件（CONTRIBUTING.md「分层纪律」）：解析并校验生成选项 → 交 generate_args 委托 Core
//       generate::makePattern 合成 → 交 job 复用 exportImage 落盘；不含任何像素 / 图案逻辑。
// 分块依据：一命令一文件（严禁上帝文件）；参数装配复用 generate_args，生成委托 Core，落盘与格式推断
//       复用 job::writeGeneratedImage（同一 I/O 通道），报错样板复用 command_support。
// 说明：new 是「创建」而非「切割」，故拒绝 --input；尺寸非法（<=0 / 超单边上限 / 超总像素上限）、
//       图案名/颜色/方向/周期非法一律退出码 1（早于生成），落盘失败退出码 4。缺省图案为 solid（纯色），
//       底色 alpha<FF 即得透明 / 半透明画布。--pad-color 设定有损格式（JPEG/BMP）压平透明像素所用的
//       背景色（透传 emit.padColor）；--save-config 触发 dry-run：不落像素，而把本次生成配方（source 尺寸
//       + generate 段 + emit）序列化进配置 JSON（此路径不要求 --output），供 config --load 据其复现画布。
// ============================================================================
#include "commands.h"

#include <iostream>
#include <string>

#include "engine/engine.h"      // EngineConfig（--save-config 序列化 generate 配方）
#include "engine/composition.h" // CompositionParams / ExportFormat
#include "generate/pattern.h"   // generate::PatternParams / makePattern

#include "command_support.h"
#include "generate_args.h"
#include "job.h"
#include "value_parser.h"

namespace idc::cli {

// 新建图像：--size WxH [--pattern/--orientation/--bg/--fg/--period/--line-width/--dot-radius/--seed/--noise]
//           [--format/--quality/--pad-color] [--output <file> | --save-config <file>（dry-run）]。
int cmdNew(const std::vector<std::string>& tokens, const GlobalOptions& go) {
    ArgParser args;
    args.parse(tokens);

    // --- 第 3 步：必填与前置互斥（早于任何 Core 调用，一律退出码 1）。---
    if (!args.has("--size"))
        return argError("缺少 --size", "请用 --size WxH 指定生成画布的尺寸（像素，正整数）");
    if (args.has("--input"))
        return argError("new 不接受 --input",
                        "new 用于程序生成新图像；读取并切割已有图像请改用 extract / erase / grid");
    // --save-config 为 dry-run（仅序列化生成配方），此路径不要求 --output；否则必须给 --output。
    const bool dryRun = !go.saveConfig.empty();
    if (!dryRun && !args.has("--output"))
        return argError("缺少 --output", "new 输出单图，请用 --output <file> 指定生成图像的落盘路径");

    // --- 第 5 步：可选导出选项（--format 缺省由扩展名推断；--quality 仅 JPEG/WebP 生效；
    //         --pad-color 设定有损格式压平透明像素的背景色，透传 emit.padColor）。---
    engine::CompositionParams emit;
    std::string err;
    if (args.has("--format") && !parseFormat(args.get("--format"), emit.format, err))
        return argError(err);
    if (args.has("--quality") && !parseQuality(args.get("--quality"), emit.quality, err))
        return argError(err);
    if (args.has("--pad-color") && !parseColor(args.get("--pad-color"), emit.padColor, err))
        return argError(err);

    // --- 生成参数（委托 generate_args 解析；尺寸/图案/颜色/周期等非法在此转退出码 1，含总像素上限）。---
    int width = 0, height = 0;
    generate::PatternParams params;
    if (!parseGenerateParams(args, width, height, params, err))
        return fail(ExitCode::ArgError, "图像生成参数错误", err,
                    "请检查 --size / --pattern / --orientation / --bg / --fg / --period / --seed / --noise 等选项");

    const JobOptions opt = makeJobOptions(go, /*explicitCollapse=*/false);

    // --- dry-run：把生成配方（source 尺寸 + generate 段 + emit）序列化进配置，不落像素。---
    if (dryRun) {
        engine::EngineConfig config;
        config.source.width = width;
        config.source.height = height;
        // 程序生成底图：同构 canvas::ImageLayer（Recipe 底、transform 恒等）——序列化后与旧 generate 段逐字段等价。
        canvas::ImageLayer base;
        base.sourceKind = canvas::LayerSourceKind::Recipe;
        base.recipe = params;
        config.base = base;
        config.emitParams = emit;
        return toInt(saveConfigToFile(config, go.saveConfig, opt));
    }

    // --- 生成画布（委托 Core makePattern；参数已在上面校验，越界再产空图作防御）。---
    const core::Image image = generate::makePattern(width, height, params);
    if (image.empty())
        return fail(ExitCode::ArgError, "图案生成失败（尺寸或参数非法）",
                    "图案工厂返回空图",
                    "请检查 --size / --pattern / --orientation / --bg / --fg / --period / --seed / --noise 等选项");

    // --- 落盘（复用 job 的合并单图通道：扩展名推断格式 / 自动建父目录 / JPEG 按 pad-color 压平）。---
    if (const ExitCode ec = writeGeneratedImage(image, args.get("--output"), emit, opt); ec != ExitCode::Ok)
        return toInt(ec);

    // --- 正常结果输出到 stdout（quiet 时抑制）。---
    if (!opt.quiet)
        std::cout << "已生成图像：" << args.get("--output")
                  << "（" << image.width() << "x" << image.height() << "）\n";
    return toInt(ExitCode::Ok);
}

} // namespace idc::cli
