// ============================================================================
// 文件：include/command_support.h
// 作用：各子命令（extract / erase / grid / config）共用的极薄样板助手——统一的「报错并
//       返回退出码」、执行期开关装配、以及公共导出选项 --format / --naming 的解析。
//       目的是消除四个命令文件里重复的 reportError+toInt / JobOptions 装配样板（A-0.2），
//       同时避免把这些样板堆进某个命令文件形成上帝文件（严禁上帝文件）。
// 分块依据：
//   - 这里只放「跨命令复用、且不含任何切割 / 几何 / 排序 / 极性语义」的胶水助手；单个命令
//     特有的参数装配仍留在各自 commands/*.cpp（A-0.1：CLI 不实现引擎逻辑，本文件也不实现）。
//   - 全部为 inline（与 cli_error.h 的 makeError 同为头内联）——体量小、无独立编译单元价值，
//     故不新增 .cpp，也就不必改动 CMake 源清单（降低构建接入风险）。
//   - 复用既有模块：cli_error（§5.2 错误格式）、exit_code（§5.1 退出码）、arg_parser（选项读取）、
//     value_parser（值解析）、job（JobOptions）与 Core 的 CompositionParams（导出参数）。
// ============================================================================
#pragma once

#include <string>

#include "engine/composition.h" // CompositionParams / ExportFormat

#include "arg_parser.h"
#include "cli_error.h"
#include "exit_code.h"
#include "job.h"
#include "value_parser.h"

namespace idc::cli {

// 统一「按 §5.2 打印错误到 stderr + 返回对应退出码整数」，供各命令直接 return。
// detail / hint 可省略（省略时错误输出不含对应行，见 cli_error.cpp）。
inline int fail(const ExitCode code, const std::string& brief,
                const std::string& detail = {}, const std::string& hint = {}) {
    reportError(makeError(code, brief, detail, hint));
    return toInt(code);
}

// 参数错误（退出码 1）快捷方式：detail 恒空，仅给简短描述与可操作提示。
inline int argError(const std::string& brief, const std::string& hint = {}) {
    return fail(ExitCode::ArgError, brief, {}, hint);
}

// 由全局选项与「是否显式要求坍缩」装配执行期开关 JobOptions（verbose / quiet 透传）。
// explicitCollapse=true 时，若 Core 判定选择集不可坍缩，job 将返回退出码 2（§4.2.2.4）。
inline JobOptions makeJobOptions(const GlobalOptions& go, const bool explicitCollapse) {
    JobOptions opt;
    opt.explicitCollapse = explicitCollapse;
    opt.verbose = go.verbose;
    opt.quiet = go.quiet;
    return opt;
}

// 解析公共导出选项 --format / --naming 写入 emit：
//   --format 存在则解析为 ExportFormat（失败 → false + err）；合并导出时 Core 仍以 --output
//            扩展名优先，此处的 format 作为无有效扩展名时的回退（见 export.cpp: exportMerged）。
//   --naming 存在则经 checkNaming 校验（非空、不含路径分隔符）后写入，用于分离导出命名。
// 二者均可省略（省略时沿用 emit 既有默认值）。成功返回 true。
inline bool applyFormatNaming(const ArgParser& args, engine::CompositionParams& emit,
                              std::string& err) {
    if (args.has("--format") && !parseFormat(args.get("--format"), emit.format, err)) return false;
    if (args.has("--naming")) {
        const std::string tpl = args.get("--naming");
        if (!checkNaming(tpl, err)) return false;
        emit.naming = tpl;
    }
    return true;
}

} // namespace idc::cli
