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
// 说明：显式 --merge collapse 但选择集不可坍缩时由 job 返回退出码 2。四种剔除在合法
//       参数下删除区恒为若干整行/整列（多矩形并集亦然），故通常可坍缩；该分支主要为
//       防御性对齐规范，真正稳定触发退出码 2 的场景见 config 命令的手写配置（IT-18）。
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

namespace idc::cli {

// L2 反向剔除：--input + (--rect 可重复|--hband|--vband) + [--merge] + 输出（极性 remove）。
int cmdErase(const std::vector<std::string>& tokens, const GlobalOptions& go) {
    ArgParser args;
    args.parse(tokens);

    // --- 第 3 步：必填。（输入来源二选一：--input 读文件 或 --size 生成画布。）---
    if (!args.has("--input") && !hasGenerateOptions(args))
        return argError("缺少输入图像", "请用 --input <file> 指定输入图像，或用 --size WxH（可配 --pattern）生成画布作为输入");
    if (args.has("--input") && hasGenerateOptions(args))
        return argError("--input 与 --size 不可同时使用", "读取文件用 --input；程序生成画布用 --size（二者择一）");

    // --- 第 4 步：互斥——几何三选一（--rect 可重复，整体算作一类）。---
    const bool hasRect = args.has("--rect");
    const bool hasH = args.has("--hband");
    const bool hasV = args.has("--vband");
    if (const int geoKinds = static_cast<int>(hasRect) + static_cast<int>(hasH) + static_cast<int>(hasV); geoKinds != 1)
        return argError("--rect / --hband / --vband 必须三选一",
                        geoKinds == 0 ? "请给出恰好一种几何参数（--rect 可重复以做多矩形并集剔除）"
                                      : "不可混用多种几何参数");

    // --- 第 4 步：--merge 与 --output-dir 互斥；据此定「分离 / 合并」输出目标。---
    const bool hasMerge = args.has("--merge");
    if (hasMerge && args.has("--output-dir"))
        return argError("--merge 与 --output-dir 不得同时出现",
                        "合并请用 --output <file>；分离请用 --output-dir <dir>（二者择一）");

    // --- 装配 EngineConfig 固定部分：L2 极性恒 remove；输出模式由 --merge 决定。---
    const bool separate = !hasMerge; // 缺省分离导出。
    bool explicitCollapse = false;   // 仅显式 --merge collapse 时为真（不可坍缩 → 退出码 2）。
    engine::EngineConfig config;
    config.cut.tier = engine::Tier::L2;
    config.cut.polarity = engine::Polarity::REMOVE;

    std::string err;
    if (hasMerge) {
        auto layout = engine::MergeLayout::COLLAPSE;
        if (!parseMerge(args.get("--merge"), layout, err)) return argError(err);
        // 仅 L3 可重排（Core runEngine 硬约束）：erase 为 L2，显式 rearrange 直接拒绝。
        if (layout == engine::MergeLayout::REARRANGE)
            return argError("erase（L2）不支持 --merge rearrange",
                            "合并重排仅 L3 grid 命令支持；L2 请用 --merge collapse，或去掉 --merge 分离导出");
        config.emitParams.mode = engine::EmitMode::MERGED;
        config.emitParams.layout = layout;
        explicitCollapse = layout == engine::MergeLayout::COLLAPSE;
        if (!args.has("--output"))
            return argError("合并导出缺少 --output", "请用 --output <file> 指定合并输出路径");
    } else {
        config.emitParams.mode = engine::EmitMode::SEPARATE;
        if (!args.has("--output-dir"))
            return argError("分离导出缺少 --output-dir",
                            "请用 --output-dir <dir> 指定输出文件夹（不存在会自动创建）");
    }

    // --- 第 5 步：几何格式校验。带坐标先暂存，读图后再构造贯穿全图的带矩形。---
    int bandA = 0, bandB = 0;
    if (hasRect) {
        if (const std::vector<std::string> rectStrs = args.getAll("--rect"); rectStrs.size() == 1) {
            // 单矩形 → 十字切割（保留四角）。
            engine::RectRegion r;
            if (!parseRect(rectStrs[0], r, err)) return argError(err);
            if (r.width() <= 0 || r.height() <= 0)
                return argError("矩形退化：需满足 x1<x2 且 y1<y2", "请检查 --rect 的坐标顺序；只需一条切割线时请改用 --hband / --vband");
            config.cut.generator = engine::CutGenerator::RECT;
            config.cut.rect = r;
        } else {
            // 多矩形并集剔除（FR-L2.4）：各矩形诱导十字带，删除区为并集（始终满足坍缩条件）。
            config.cut.generator = engine::CutGenerator::MULTI_RECT;
            for (const std::string& s : rectStrs) {
                engine::RectRegion r;
                if (!parseRect(s, r, err)) return argError(err);
                if (r.width() <= 0 || r.height() <= 0)
                    return argError("矩形退化：需满足 x1<x2 且 y1<y2",
                                    "请检查 --rect '" + s + "' 的坐标顺序；只需一条切割线时请改用 --hband / --vband");
                config.cut.rects.push_back(r);
            }
        }
    } else if (hasH) {
        if (!parseBand(args.get("--hband"), bandA, bandB, err)) return argError(err);
        if (bandA >= bandB)
            return argError("水平带退化：需满足 y1<y2", "请检查 --hband 的坐标顺序");
        config.cut.generator = engine::CutGenerator::HORIZONTAL_LINE;
    } else { // hasV
        if (!parseBand(args.get("--vband"), bandA, bandB, err)) return argError(err);
        if (bandA >= bandB)
            return argError("垂直带退化：需满足 x1<x2", "请检查 --vband 的坐标顺序");
        config.cut.generator = engine::CutGenerator::VERTICAL_LINE;
    }
    // 公共导出选项：--format（分离时决定扩展名 / 合并时作扩展名回退）/ --quality（JPEG/WebP）/ --naming（分离命名模板）。
    if (!applyExportOptions(args, config.emitParams, err)) return argError(err);

    // --- 第 6 步：获取输入图像（--input 读文件失败 3；--size 生成参数非法 1）。---
    const JobOptions opt = makeJobOptions(go, explicitCollapse);
    core::Image image;
    if (const ExitCode ec = resolveInputImage(args, image, opt); ec != ExitCode::Ok)
        return toInt(ec);

    // --- 装配切割几何：带需图像尺寸构造贯穿全图矩形；写入 source（供 --save-config 完整）。---
    if (config.cut.generator == engine::CutGenerator::HORIZONTAL_LINE)
        config.cut.rect = engine::horizontalBand(bandA, bandB, image.width());
    else if (config.cut.generator == engine::CutGenerator::VERTICAL_LINE)
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

    // --- 第 7 步：执行 / dry-run（--save-config）。分离取 --output-dir，合并取 --output。---
    const JobOutput out{separate ? args.get("--output-dir") : args.get("--output"), separate};
    return toInt(finishJob(config, image, out, go.saveConfig, opt, annoPtr));
}

} // namespace idc::cli
