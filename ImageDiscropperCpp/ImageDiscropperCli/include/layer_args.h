// ============================================================================
// 文件：include/layer_args.h
// 作用：把命令行图层选项解析为 Core 的 ImageLayer 列表——config 命令专用的「图层入参装配」薄胶水
//       （SPEC §7 layers 段元素语法在命令行的两种载体，与 annotation_args 同构）。
// 分块依据：
//   - 只做「选项 token → 经 Core 段级解析 → 追加到 out」的收集，不含任何合成 / 落位 / 引擎逻辑
//     （CONTRIBUTING.md「分层纪律」）：JSON 解析全部委托 Core 的 parseImageLayersFromJsonText，
//     CLI 不自持第三方 JSON 库（该依赖由 Core PRIVATE 封装，不经公共头外泄，见 engine_config_json.h）。
//   - 与 annotation_args 分列两文件：解析的是不同 Core 段（layers vs annotations），虽形态对称，
//     但错误文案与追加语义各异，合并会引入无谓分支（严禁上帝文件）。
// 说明：--layer <内联 JSON 对象> 可重复（每次一条或一组图层）；--layers <file.json> 指向
//       「根为图层数组（或单对象）」的 JSON 文件（亦可重复）。二者可混用，按先 --layer（保持出现
//       顺序）后 --layers 文件的顺序累积到 out；调用方据此追加进 config.layers。
// ============================================================================
#pragma once

#include <string>
#include <vector>

#include "arg_parser.h"

namespace idc::canvas { struct ImageLayer; }

namespace idc::cli {

// 是否给出命令行图层选项（--layer / --layers 任一出现）。
bool hasLayerOptions(const ArgParser& args);

// 收集命令行图层并追加到 out（逐条复用 Core parseImageLayersFromJsonText）：
//   --layer <内联 JSON 对象/数组>（可重复）+ --layers <file.json>（根为数组/单对象）。
//   basisWidth/basisHeight 为画布尺寸（图层 transform 的坐标基准）：位置/尺寸写百分比串时按此换算，
//   故须在画布尺寸可知后调用（解图之后；config 的 dry-run 用配置 source）。
//   无图层选项时不改动 out 并返回 true；内联 / 文件 JSON 非法、文件不可读、或某选项有值却解析出
//   0 条有效图层 → 写 err 返回 false（命令层据此转退出码 1）。
bool collectLayers(const ArgParser& args, std::vector<canvas::ImageLayer>& out, std::string& err,
                   int basisWidth, int basisHeight);

// 追加层 zOrder 定序：未给（§7 缺省 0）→ 按「既有最大 zOrder + 1」起自增续尾；显式非 0 尊重原值。
// existing 传既有层（无则空表，追加层从 0 起自增）。
void finalizeAppendedLayers(const std::vector<canvas::ImageLayer>& existing,
                            std::vector<canvas::ImageLayer>& appended);

} // namespace idc::cli
