// ============================================================================
// 文件：src/commands/extract.cpp
// 作用：实现 cmdExtract——L1 标准提取模式（终稿 §4.2.1 / guideline §4.2.1）的命令行薄壳。
//       职责仅四件（A-0.1）：解析 --input / (--rect|--hband|--vband) / --output / --format →
//       按 §5.3 顺序校验 → 装配 EngineConfig（极性恒 keep、输出恒为合并单图 collapse）→ 交 job 执行。
//       不含任何切割 / 几何 / 极性判断逻辑，全部委托 Core 的统一引擎（NFR-0：模式即参数预设）。
// 分块依据：一个子命令一个文件（严禁上帝文件）；执行与退出码映射复用 job（同一执行通道），
//       值解析复用 value_parser，报错样板复用 command_support，均不在此重实现（A-0.2）。
// 说明：L1 的保留区恒为「单个中心矩形」或「整条带」，其补集必为若干整行/整列，故必可坍缩为
//       单图；explicitCollapse 恒为 false，本命令不会触发坍缩不可行（退出码 2 仅 L2/config 才可能）。
// ============================================================================
#include "cli/commands.h"

#include <string>
#include <vector>

#include "engine/engine.h" // EngineConfig / Tier / CutGenerator / Polarity / EmitMode / MergeLayout
#include "engine/region.h" // RectRegion / horizontalBand / verticalBand

#include "cli/arg_parser.h"
#include "cli/command_support.h"
#include "cli/job.h"
#include "cli/value_parser.h"

namespace idc::cli {

// L1 标准提取：--input + (--rect|--hband|--vband 三选一) + --output（极性 keep，合并单图）。
int cmdExtract(const std::vector<std::string>& tokens, const GlobalOptions& go) {
    ArgParser args;
    args.parse(tokens);

    // --- §5.3 第 3 步：必填参数（缺失即退出码 1，早于任何 Core 调用）。---
    if (!args.has("--input"))
        return argError("缺少 --input", "请用 --input <file> 指定输入图像");
    if (!args.has("--output"))
        return argError("缺少 --output", "extract 输出单图，请用 --output <file> 指定输出路径");

    // --- §5.3 第 4 步：互斥——几何参数三选一（恰好一个）。---
    const int geoKinds = static_cast<int>(args.has("--rect")) +
                         static_cast<int>(args.has("--hband")) +
                         static_cast<int>(args.has("--vband"));
    if (geoKinds != 1)
        return argError("--rect / --hband / --vband 必须三选一",
                        geoKinds == 0 ? "请给出恰好一个几何参数"
                                      : "不可同时给出多个几何参数");

    // --- 装配 EngineConfig 的固定部分：L1 极性恒 keep、输出恒为合并单图（collapse）。---
    engine::EngineConfig config;
    config.cut.tier = engine::Tier::L1;
    config.cut.polarity = engine::Polarity::KEEP;
    config.emit.mode = engine::EmitMode::MERGED;
    config.emit.layout = engine::MergeLayout::COLLAPSE;

    // --- §5.3 第 5 步：几何格式校验（在调用 Core 之前，错误一律退出码 1）。---
    // 带的两个坐标先暂存，待读取图像尺寸后再构造「贯穿全图」的带矩形（横带宽=W、竖带高=H）。
    std::string err;
    int bandA = 0, bandB = 0;
    if (args.has("--rect")) {
        engine::RectRegion r;
        if (!parseRect(args.get("--rect"), r, err)) return argError(err);
        if (r.width() <= 0 || r.height() <= 0)
            return argError("矩形退化：需满足 x1<x2 且 y1<y2", "请检查 --rect 的坐标顺序");
        config.cut.generator = engine::CutGenerator::RECT;
        config.cut.rect = r;
    } else if (args.has("--hband")) {
        if (!parseBand(args.get("--hband"), bandA, bandB, err)) return argError(err);
        if (bandA >= bandB)
            return argError("水平带退化：需满足 y1<y2", "请检查 --hband 的坐标顺序");
        config.cut.generator = engine::CutGenerator::HORIZONTAL_LINE;
    } else { // --vband
        if (!parseBand(args.get("--vband"), bandA, bandB, err)) return argError(err);
        if (bandA >= bandB)
            return argError("垂直带退化：需满足 x1<x2", "请检查 --vband 的坐标顺序");
        config.cut.generator = engine::CutGenerator::VERTICAL_LINE;
    }
    // 公共导出选项：--format（合并时作为扩展名回退）/ --naming（L1 合并单图用不到，但保持一致）。
    if (!applyFormatNaming(args, config.emit, err)) return argError(err);

    // --- §5.3 第 6 步：输入图像可读（失败退出码 3）。---
    const JobOptions opt = makeJobOptions(go, /*explicitCollapse=*/false);
    core::Image image;
    if (const ExitCode ec = loadInputImage(args.get("--input"), image, opt); ec != ExitCode::Ok)
        return toInt(ec);

    // --- 装配切割几何：带需图像尺寸构造贯穿全图的矩形；并写入 source（供 --save-config 完整）。---
    if (config.cut.generator == engine::CutGenerator::HORIZONTAL_LINE)
        config.cut.rect = engine::horizontalBand(bandA, bandB, image.width());
    else if (config.cut.generator == engine::CutGenerator::VERTICAL_LINE)
        config.cut.rect = engine::verticalBand(bandA, bandB, image.height());
    config.source.width = image.width();
    config.source.height = image.height();

    // --- §5.3 第 7 步：执行作业；若给出 --save-config 则改为序列化配置的 dry-run（不切割）。---
    const JobOutput out{args.get("--output"), /*separate=*/false};
    return toInt(finishJob(config, image, out, go.saveConfig, opt));
}

} // namespace idc::cli
