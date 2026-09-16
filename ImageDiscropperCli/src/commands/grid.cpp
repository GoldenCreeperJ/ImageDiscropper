// ============================================================================
// 文件：src/commands/grid.cpp
// 作用：实现 cmdGrid——L3 网格分割模式（终稿 §4.4 / guideline §4.2.3）的命令行薄壳。
//       网格仅由「基准点 + 单元尺寸」定义（终稿 §4.4.4：切割线贯穿全图，行列数与单元位置尺寸
//       由图像边界自动推导，不存在“行数/列数/间距”这类输入参数）。职责仅四件（A-0.1）：
//       解析参数 → 按 §5.3 校验 → 用 Core 的 Grid 把 (r,c) 映射为单元序号并装配 EngineConfig →
//       交 job 执行；不含任何切割 / 几何 / 排序逻辑，全部委托 Core 的统一引擎（NFR-0）。
// 分块依据：一命令一文件（严禁上帝文件）；(r,c)→单元序号的映射复用 Core Grid::build/cellAt
//       （A-0.2：不在 CLI 重算几何——value_parser.h 已注明该步在命令层用 Core Grid 完成）；
//       执行 / 退出码复用 job（同一执行通道），值解析复用 value_parser，报错样板复用 command_support。
// 说明：
//   - 三种“选择来源”互斥：--sort custom 用 --order（点选顺序即保留序，极性 keep）；否则
//     --keep（极性 keep）与 --remove（极性 remove）二选一（§4.2.3 重要约束）。
//   - --compose 恒为重排合并（REARRANGE，按 --canvas ColxRow 落位）；缺省为分离导出到文件夹。
//     网格保留集通常是任意单元子集，不满足坍缩定理（§5.4），故 grid 不提供 collapse，
//     explicitCollapse 恒为 false，本命令不会触发退出码 2。
//   - 越界的 (r,c) 属参数错误（退出码 1），但其判定依赖图像尺寸才能铺出网格，故只能在读图
//     （第 6 步，退出码 3）之后检查——这是 §5.3 顺序在“几何依赖图像尺寸”时的固有例外。
//   - --pad-color：写入 emit.padColor；Core 的 compose 已把 padColor 透传进 Composition，
//     exportMerged 以之填充合并画布空位 / 余量（并对 JPEG/BMP 作压平背景），故对合并导出生效。
// ============================================================================
#include "cli/commands.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "core/color.h"      // core::Color（--pad-color）
#include "engine/engine.h"   // EngineConfig / Tier / CutGenerator / Polarity / EmitMode / MergeLayout
#include "engine/grid.h"     // Grid / GridParams / Cell / RemainderPolicy
#include "engine/sequence.h" // SortStrategy

#include "cli/arg_parser.h"
#include "cli/command_support.h"
#include "cli/job.h"
#include "cli/value_parser.h"

namespace idc::cli {

// L3 网格分割：--input + --grid + (--keep|--remove|--order) + 排序/余量/合成等。
int cmdGrid(const std::vector<std::string>& tokens, const GlobalOptions& go) {
    ArgParser args;
    args.parse(tokens);

    // --- §5.3 第 3 步：必填。---
    if (!args.has("--input"))
        return argError("缺少 --input", "请用 --input <file> 指定输入图像");
    if (!args.has("--grid"))
        return argError("缺少 --grid", "请用 --grid x0,y0,cw,ch 指定基准点与单元尺寸");

    // --- 装配固定部分：L3 恒为参数化网格生成器。---
    engine::EngineConfig config;
    config.cut.tier = engine::Tier::L3;
    config.cut.generator = engine::CutGenerator::GRID;

    std::string err;

    // --- §5.3 第 5 步（提前解析 --sort：第 4 步的选择来源互斥判定依赖它）。---
    engine::SortStrategy strategy = engine::SortStrategy::ROW_MAJOR;
    if (args.has("--sort") && !parseSort(args.get("--sort"), strategy, err)) return argError(err);
    config.order.strategy = strategy;
    const bool custom = (strategy == engine::SortStrategy::CUSTOM);

    // --- §5.3 第 4 步：选择来源互斥（--keep / --remove / --order 三者的合法组合）。---
    const bool hasKeep = args.has("--keep");
    const bool hasRemove = args.has("--remove");
    if (hasKeep && hasRemove)
        return argError("--keep 与 --remove 不得同时出现",
                        "请只用其一：保留单元用 --keep，剔除单元用 --remove");
    if (custom) {
        // 自定义序：--order 必填，且不可再用 --keep/--remove/--decorate（点选顺序即保留集与输出序）。
        if (!args.has("--order"))
            return argError("--sort custom 需要 --order", "请用 --order r,c;r,c 指定自定义序列");
        if (hasKeep || hasRemove)
            return argError("--sort custom 下不可再用 --keep / --remove",
                            "自定义序列 --order 本身即保留集与输出顺序");
        if (args.has("--decorate"))
            return argError("--sort custom 下不可用 --decorate", "自定义序已显式给定，无需排序修饰");
        config.cut.polarity = engine::Polarity::KEEP;
    } else {
        // 非自定义：必须恰好给出 --keep 或 --remove；--order 仅在 custom 下有效。
        if (args.has("--order"))
            return argError("--order 仅在 --sort custom 下有效",
                            "请改用 --sort custom，或用 --keep / --remove 选择单元");
        if (!hasKeep && !hasRemove)
            return argError("缺少单元选择", "请用 --keep r,c 或 --remove r,c 指定单元格（均可重复）");
        config.cut.polarity = hasRemove ? engine::Polarity::REMOVE : engine::Polarity::KEEP;
    }

    // --- §5.3 第 5 步：网格几何（基准点 + 单元尺寸）与余量策略。---
    engine::GridParams gp;
    int x0 = 0, y0 = 0, cw = 0, ch = 0;
    if (!parseGridGeo(args.get("--grid"), x0, y0, cw, ch, err)) return argError(err);
    gp.originX = x0;
    gp.originY = y0;
    gp.cellWidth = cw;
    gp.cellHeight = ch;
    if (args.has("--margin") && !parseMargin(args.get("--margin"), gp.remainder, err))
        return argError(err);
    config.cut.grid = gp;

    // --- §5.3 第 5 步：排序修饰（reverse / snake，仅非 custom）。---
    if (!custom && args.has("--decorate")) {
        bool reverse = false, snake = false;
        if (!parseDecorate(args.get("--decorate"), reverse, snake, err)) return argError(err);
        config.order.reverse = reverse;
        config.order.snake = snake;
    }

    // --- §5.3 第 4/5 步：输出模式（--compose 重排合并 / 缺省分离）与画布 ColxRow。---
    const bool separate = !args.has("--compose");
    if (!separate) { // --compose：合并为单图，恒 REARRANGE（网格保留集一般不可坍缩）。
        if (args.has("--output-dir"))
            return argError("--compose 与 --output-dir 不得同时出现",
                            "合并请用 --output <file>；分离请去掉 --compose 并用 --output-dir <dir>");
        if (!args.has("--canvas"))
            return argError("--compose 需要 --canvas", "请用 --canvas ColxRow 指定重排画布（如 8x6）");
        if (!args.has("--output"))
            return argError("--compose 需要 --output", "请用 --output <file> 指定合并输出路径");
        int cols = 0, rows = 0;
        if (!parseCanvas(args.get("--canvas"), cols, rows, err)) return argError(err);
        config.emitParams.mode = engine::EmitMode::MERGED;
        config.emitParams.layout = engine::MergeLayout::REARRANGE;
        config.emitParams.cols = cols; // 画布列数（单元数，非像素）
        config.emitParams.rows = rows; // 画布行数（单元数，非像素）
    } else { // 缺省：分离导出到文件夹（不存在会自动创建）。
        if (!args.has("--output-dir"))
            return argError("分离导出缺少 --output-dir",
                            "请用 --output-dir <dir> 指定输出文件夹，或加 --compose + --canvas + --output 合并为单图");
        config.emitParams.mode = engine::EmitMode::SEPARATE;
    }

    // --- §5.3 第 5 步：填充色与公共导出选项 --format / --naming。---
    if (args.has("--pad-color")) {
        core::Color pad;
        if (!parseColor(args.get("--pad-color"), pad, err)) return argError(err);
        config.emitParams.padColor = pad; // Core 已透传至合并画布填充色（见文件头说明）。
    }
    if (!applyFormatNaming(args, config.emitParams, err)) return argError(err);

    // --- §5.3 第 5 步：选择单元 (r,c) 的格式解析（映射为序号需网格，故读图后再映射）。---
    std::vector<std::pair<int, int>> chosen; // 保持出现顺序（custom 下即输出序）。
    if (custom) {
        if (!parseOrderCells(args.get("--order"), chosen, err)) return argError(err);
        // 自定义序列中单元应唯一：重复会使同一单元在重排画布上落位两次（防御性校验）。
        for (std::size_t i = 0; i < chosen.size(); ++i)
            for (std::size_t j = i + 1; j < chosen.size(); ++j)
                if (chosen[i] == chosen[j])
                    return argError("--order 含重复单元 (" + std::to_string(chosen[i].first) + "," +
                                        std::to_string(chosen[i].second) + ")",
                                    "自定义序列中每个单元应唯一");
    } else {
        for (const std::string& s : args.getAll(hasKeep ? "--keep" : "--remove")) {
            int r = 0, c = 0;
            if (!parseCell(s, r, c, err)) return argError(err);
            chosen.emplace_back(r, c);
        }
    }

    // --- §5.3 第 6 步：读图（失败退出码 3）。---
    const JobOptions opt = makeJobOptions(go, /*explicitCollapse=*/false);
    core::Image image;
    if (const ExitCode ec = loadInputImage(args.get("--input"), image, opt); ec != ExitCode::Ok)
        return toInt(ec);

    // --- 用 Core Grid 铺网格（复用引擎几何，A-0.2），据此把 (r,c) 映射为单元序号。---
    engine::Grid grid;
    grid.build(gp, image.width(), image.height());
    if (grid.cellCount() == 0)
        return argError("网格未产生任何单元",
                        "请减小 --grid 的单元尺寸，或用 --margin keep-partial/pad 保留跨界的残缺单元");
    for (const auto& rc : chosen) {
        const engine::Cell* cell = grid.cellAt(rc.first, rc.second);
        if (cell == nullptr)
            return argError("单元格 (" + std::to_string(rc.first) + "," + std::to_string(rc.second) +
                                ") 越界",
                            "当前网格为 " + std::to_string(grid.rowCount()) + " 行 × " +
                                std::to_string(grid.colCount()) + " 列（行列号从 0 起）");
        config.selectedCells.push_back(cell->index);
    }
    config.source.width = image.width();
    config.source.height = image.height();

    // --- §5.3 第 7 步：执行 / dry-run（--save-config）。分离取 --output-dir，合并取 --output。---
    const JobOutput out{separate ? args.get("--output-dir") : args.get("--output"), separate};
    return toInt(finishJob(config, image, out, go.saveConfig, opt));
}

} // namespace idc::cli
