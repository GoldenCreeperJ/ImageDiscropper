// ============================================================================
// 文件：include/annotation_args.h
// 作用：把命令行标注选项解析为 Core 的标注对象列表——三层命令（extract/erase/grid）与 config
//       命令共用的「标注入参装配」薄胶水（SPEC §7 annotations 段元素语法在命令行的两种载体）。
// 分块依据：
//   - 只做「选项 token → 经 Core 段级解析 → 追加到 out」的收集，不含任何几何 / 烧录 / 引擎逻辑
//     （CONTRIBUTING.md「分层纪律」）：JSON 解析全部委托 Core 的 parseAnnotationsFromJsonText，
//     CLI 不自持第三方 JSON 库（该依赖由 Core PRIVATE 封装，不经公共头外泄，见 engine_config_json.h）。
//   - 与 value_parser 分离：value_parser 是「字符串 → 强类型」的纯转换且无文件 I/O；本模块需读文件 +
//     调 Core 段解析，职责与依赖均不同，单列一文件以免污染 value_parser 的纯函数定位（严禁上帝文件）。
// 说明：--annotation <内联 JSON 对象> 可重复（每次一条标注）；--annotations <file.json> 指向
//       「根为标注数组（或单对象）」的 JSON 文件（亦可重复）。二者可混用，按先 --annotation（保持出现
//       顺序）后 --annotations 文件的顺序累积到 out；调用方据此装配 AnnotationSet 交 job 烧录。
// ============================================================================
#pragma once

#include <string>
#include <vector>

#include "arg_parser.h"

namespace idc::annotation { struct Annotation; }

namespace idc::cli {

// 是否给出命令行标注选项（--annotation / --annotations 任一出现）。
bool hasAnnotationOptions(const ArgParser& args);

// 收集命令行标注并追加到 out（逐条复用 Core parseAnnotationsFromJsonText）：
//   --annotation <内联 JSON 对象>（可重复）+ --annotations <file.json>（根为数组＝多条）。
//   无标注选项时不改动 out 并返回 true；内联 / 文件 JSON 非法、文件不可读、或某选项有值却解析出
//   0 条有效标注 → 写 err 返回 false（命令层据此转退出码 1）。
bool collectAnnotations(const ArgParser& args, std::vector<annotation::Annotation>& out,
                        std::string& err);

} // namespace idc::cli
