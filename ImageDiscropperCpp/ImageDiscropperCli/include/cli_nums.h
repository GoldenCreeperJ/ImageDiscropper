// ============================================================================
// 文件：include/cli_nums.h（CLI 常量层 · 数字）
// 作用：把 CLI 里「会被人工调整」的数值收编为命名常量：参数范围上下限、出厂默认值、
//       输出文件名模板相关边界。（帮助文本各命令 optionRow 的选项列宽不在此：改由
//       src/help.cpp 按不可译用法列串长于编译期取 max，避免手数漂移。）
// 分块依据：按来源模块 —— parser（arg_parser / value_parser 的取值范围与步进）、
//       job（作业装配的默认值与校验边界）、commands（各子命令默认）、help（排版常量）。
// 命名：k + 语义名，单位入名（…Px / …Deg / …Pct / …Cols / …Indent）。
// 类型：inline constexpr int / double / std::size_t（CLI 层无 Qt 依赖）。
// 不提取：数学与换算常数（255 / 100 / 1024 / 360）、循环边界 0/1、理论不可达兜底、
//       tests/ 与 examples/ 及 shell 示例脚本里的期望值。
// 现状：P3（CLI 数字上提）已完成 —— parser 组已填，job 与 commands 经盘点无待提取项（理由见组内注释）。
//       help 组原手定的各命令选项列宽已迁至 src/help.cpp（按 hp::kUse* 串长编译期取 max），
//       不再需在增删选项时手改对齐数字。
// ============================================================================
#pragma once

#include <cstddef>

namespace idc::cli::nums {

// ---------------------------------------------------------------------------
// parser：命令行取值解析期的范围（src/value_parser.cpp、src/generate_args.cpp）。
// 注意：本组的上界与 Core 的 core::nums::engine::kQualityMin/Max（编码期在 image_io.cpp 钉到 1..100）
//       同值但**不同义**：CLI 受理用户显式写的 0（交给 Core 钳位），故不共享常量，以免一端改动误伤另一端。
// ---------------------------------------------------------------------------
namespace parser {

inline constexpr int kQualityMin = 0;      // --quality 下界：value_parser.cpp::parseQuality 的范围校验与报因文本同此。
inline constexpr int kQualityMax = 100;    // --quality 上界：超出即退出码 1，不进 Core。

} // namespace parser

// ---------------------------------------------------------------------------
// job：作业装配与校验期（src/job.cpp、src/commands/*.cpp）。
// 盘点结果：本模块无可调数值——仅有 alpha 满值 255 与下界 0（config.cpp 的 clamp，属换算常数）、
//   尺寸上限文本经 Core 的 generate::kMaxDimension / kMaxTotalPixels 取到（不在此重复定义），
//   故本组保持为空，仅作存在性说明（若日后加入 CLI 侧默认值，落此处）。
// ---------------------------------------------------------------------------
namespace job {
} // namespace job

// ---------------------------------------------------------------------------
// help：已清空——各命令 optionRow 的选项列宽不再手定于此，改由 src/help.cpp
//       的 constexpr optWidth() 按各命令实际打印的不可译用法列（texts::help::kUse*）串长
//       编译期取 max（用法列内容不随语言变，故恒为常量；加/减选项自动跟进、零手数 off-by-one）。
// ---------------------------------------------------------------------------
namespace help {
} // namespace help

} // namespace idc::cli::nums
