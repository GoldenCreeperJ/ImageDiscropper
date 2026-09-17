// ============================================================================
// 文件：src/commands/config.cpp
// 作用：实现 cmdConfig——配置文件命令（guideline §4.2.4）。两条用途：
//       ① config --load <json> --input <img> (--output | --output-dir)：从 §9 schema 的 JSON
//          还原 EngineConfig 并执行（复用 Core 的 loadEngineConfig，绝不自实现解析，A-0.2）。
//       ② config --load <json> --save-config <out.json>：加载后再序列化（转换 / 校验的 dry-run，
//          不读图、不切割）。全局 --config <file> 亦由 cliMain 路由到此（同一执行通道，NFR-0）。
// 分块依据：一命令一文件（严禁上帝文件）；JSON 存取复用 Core engine_config_json，执行 / 退出码
//       复用 job，报错样板复用 command_support（A-0.2）。配置文件不含图像 / 输出路径（§9 仅有
//       source 尺寸 + cut/select/order/emit），故执行时的 --input 与输出路径须由命令行补齐。
// 说明（退出码判定，§5.1 / §5.3）：
//   - --load 缺失 / 无值 → 1；文件不存在 / 不可访问 → 3；文件存在但 JSON 解析失败 → 1
//     （配置内容即“参数”，其格式错误归参数错误；文件层面的缺失 / 不可读归输入文件错误）。
//   - 执行时缺 --input 或缺对应输出（分离 --output-dir / 合并 --output）→ 1。
//   - 读图失败 → 3；引擎业务失败（E-6/E-7 等）或显式坍缩不可行 → 2；导出失败 → 4（均由 job 映射）。
//   - --save-config 写盘失败 → 4。
//   - explicitCollapse：当且仅当 emit 为 MERGED+COLLAPSE 时置真——这正是 IT-18（坍缩不可行 → 2）
//     经手写配置稳定触发的路径（Core 内部会自动降级为 REARRANGE 并返回 ok=true/collapsible=false，
//     job 据 explicitCollapse 显式报错退出码 2）。
// ============================================================================
#include "commands.h"

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "engine/engine.h"             // EngineConfig / EmitMode / MergeLayout
#include "engine/engine_config_json.h" // loadEngineConfig（复用 Core JSON 模块，A-0.2）

#include "arg_parser.h"
#include "command_support.h"
#include "exit_code.h"
#include "job.h"

namespace idc::cli {

// 配置文件命令：config --load <file> [执行所需 --input / 输出]，或 --save-config <file>（转换 dry-run）。
int cmdConfig(const std::vector<std::string>& tokens, const GlobalOptions& go) {
    ArgParser args;
    args.parse(tokens);

    // --- §5.3 第 3 步：--load 必填且须带路径（config 命令唯一的参数来源）。---
    if (!args.has("--load"))
        return argError("缺少 --load", "请用 --load <file> 指定 §9 schema 的 JSON 配置文件");
    const std::string loadPath = args.get("--load");
    if (loadPath.empty())
        return argError("--load 缺少文件路径", "请用 --load <file> 指定配置文件路径");

    // --- §5.3 第 6 步（文件层面）：配置文件须存在且可访问，否则输入文件错误（3）。---
    std::error_code ec;
    if (!std::filesystem::exists(loadPath, ec))
        return fail(ExitCode::InputError, "配置文件不存在或不可访问", "路径：" + loadPath,
                    "请检查 --load 的路径是否正确、文件是否存在且可读");

    // --- 复用 Core 解析 §9 schema；文件存在但内容非法（语法错误 / 根非对象）→ 参数错误（1）。---
    engine::EngineConfig config;
    if (!engine::loadEngineConfig(loadPath, config))
        return fail(ExitCode::ArgError, "配置文件解析失败",
                    "无法按终稿 §9 schema 解析：" + loadPath,
                    "请确认 JSON 语法正确且根为对象，字段含 source / preprocess / cut / select / order / emit");

    // --- 用途②：--save-config 为“加载后再序列化”的 dry-run（转换 / 校验），不读图、不切割。---
    //     保留 JSON 原有 source 尺寸（无图像可校正），仅完成一次读入 → 写出的往返。
    if (!go.saveConfig.empty())
        return toInt(saveConfigToFile(config, go.saveConfig,
                                      makeJobOptions(go, /*explicitCollapse=*/false)));

    // --- 用途①：执行配置。§9 JSON 不含图像 / 输出路径，故须由命令行补齐。---
    if (!args.has("--input"))
        return argError("执行配置需要 --input",
                        "配置文件（§9）不含图像路径，请用 --input <file> 指定要切割的图像");

    // --- 重排前置校验（与 grid --compose 的 --canvas 强制一致）：仅 L3 可重排，且必须显式给定 cols/rows。---
    //     手写/转换来的配置若违反，直接判参数错误（退出码 1），不交 Core 用默认值兜底。
    if (config.emitParams.mode == engine::EmitMode::MERGED &&
        config.emitParams.layout == engine::MergeLayout::REARRANGE) {
        if (config.cut.tier != engine::Tier::L3)
            return argError("合并重排仅 L3 支持",
                            "配置 emit.merge.layout=rearrange 要求 cut.tier=L3；其他层请改用 collapse 或分离导出");
        if (!config.emitParams.cols.has_value() || !config.emitParams.rows.has_value())
            return argError("合并重排需要 cols 与 rows",
                            "请在配置 emit.merge 中同时给出正的 cols 和 rows（重排画布列/行数）");
    }

    // 输出目标由 emit.mode 决定：分离 → --output-dir（文件夹）；合并 → --output（单图）。
    const bool separate = (config.emitParams.mode == engine::EmitMode::SEPARATE);
    const std::string outKey = separate ? "--output-dir" : "--output";
    if (!args.has(outKey))
        return argError("执行配置需要 " + outKey,
                        separate ? "配置为分离导出，请用 --output-dir <dir> 指定输出文件夹"
                                 : "配置为合并导出，请用 --output <file> 指定输出路径");

    // 显式坍缩：仅 MERGED+COLLAPSE 时为真——不可坍缩则 job 返回退出码 2（IT-18 触发路径）。
    const bool explicitCollapse = (config.emitParams.mode == engine::EmitMode::MERGED &&
                                   config.emitParams.layout == engine::MergeLayout::COLLAPSE);
    const JobOptions opt = makeJobOptions(go, explicitCollapse);

    // --- §5.3 第 6 步：读图（失败退出码 3）。---
    core::Image image;
    if (const ExitCode lec = loadInputImage(args.get("--input"), image, opt); lec != ExitCode::Ok)
        return toInt(lec);

    // --- §5.3 第 7 步：执行（runEngine → 坍缩校验 → exportImage，退出码由 job 映射）。---
    const JobOutput out{args.get(outKey), separate};
    return toInt(executeJob(config, image, out, opt));
}

} // namespace idc::cli
