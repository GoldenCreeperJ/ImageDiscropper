// ============================================================================
// 文件：include/command_support.h
// 作用：各子命令（extract / erase / grid / config）共用的极薄样板助手——统一的「报错并
//       返回退出码」、执行期开关装配、以及公共导出选项 --format / --naming 的解析。
//       目的是消除四个命令文件里重复的 reportError+toInt / JobOptions 装配样板（CONTRIBUTING.md「分层纪律」），
//       同时避免把这些样板堆进某个命令文件形成上帝文件（严禁上帝文件）。
// 分块依据：
//   - 这里只放「跨命令复用、且不含任何切割 / 几何 / 排序 / 极性语义」的胶水助手；单个命令
//     特有的参数装配仍留在各自 commands/*.cpp（CONTRIBUTING.md「分层纪律」：CLI 不实现引擎逻辑，本文件也不实现）。
//   - 全部为 inline（与 cli_error.h 的 makeError 同为头内联）——体量小、无独立编译单元价值，不新增 .cpp。
//   - 复用既有模块：cli_error（错误格式）、exit_code（退出码）、arg_parser（选项读取）、
//     value_parser（值解析）、job（JobOptions）与 Core 的 CompositionParams（导出参数）。
// 常量层：本文件是公共头，因此写全限定名 texts::opt::…（不用命名空间别名，以免污染使用者），
//       选项名单一真相源见 include/i18n/cli_texts.h。
// ============================================================================
#pragma once

#include <fstream>
#include <initializer_list>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "core/path_utf8.h"       // core::pathFromUtf8（readFileText 的宽字符路径解析）
#include "engine/composition.h" // CompositionParams / ExportFormat

#include "arg_parser.h"
#include "cli_error.h"
#include "i18n/cli_i18n.h"
#include "i18n/cli_texts.h"
#include "exit_code.h"
#include "job.h"

#include "value_parser.h"

namespace idc::cli {

// 读取整个文本文件到 outText；文件不存在或不可读返回 false。path 按 UTF-8 → 宽字符开
// （中文 / 非 ASCII 路径可读）。供 annotation_args / layer_args 共用（同层文件读入，与 job 读图同依赖层）。
inline bool readFileText(const std::string& path, std::string& outText) {
    const std::ifstream ifs(core::pathFromUtf8(path), std::ios::binary);
    if (!ifs) return false;
    std::ostringstream ss;
    ss << ifs.rdbuf();
    outText = ss.str();
    return true;
}

// 统一「按错误格式打印错误到 stderr + 返回对应退出码整数」，供各命令直接 return。
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

// 由全局选项与「是否显式要求坍缩」装配执行期开关 JobOptions。
// verbose 默认静默（沉默是金），--verbose/-v 打开；
// strict 由伞形 --strict 置位（把图层缺失降级占位→退出码 3、尺寸错配静默裁切→退出码 3、零产出→退出码 2 统一升级为报错，见 job.cpp）。
// explicitCollapse=true 时，若 Core 判定选择集不可坍缩，job 将返回退出码 2。
inline JobOptions makeJobOptions(const GlobalOptions& go, const bool explicitCollapse) {
    JobOptions opt;
    opt.explicitCollapse = explicitCollapse;
    opt.verbose = go.verbose;   // 默认静默（沉默是金）：--verbose/-v 才开诊断日志；结果行恒写 stdout。
    opt.strict = go.strict;
    return opt;   // assetsDir 由各命令经 assetsDirFromArgs 装配（作业级通用选项，见 MixAssets）。
}

// 从命令参数读取 --assets-dir 三态：缺省 nullopt；flag 空串；=path 非空。
inline std::optional<std::string> assetsDirFromArgs(const ArgParser& args) {
    if (!args.has(texts::opt::kAssetsDir)) return std::nullopt;
    return args.get(texts::opt::kAssetsDir);
}

// ---------------------------------------------------------------------------
// 本命令可接受输入的描述（供 rejectUnknownOptions 逐命令校验）：
//   accepted    = 全局选项 ∪ 本命令专属选项（含经 mixin 并入的生成 / 标注 / 导出公共项）；
//   repeatable  = flag（无值）选项 ∪ 本命令合法多值选项（B 据此不报「重复」）；
//   allowPositional = 是否容忍位置参数（当前五命令均不开启，恒为默认 false；参数保留供未来扩展）。
// ---------------------------------------------------------------------------
struct CommandArgs {
    std::set<std::string> accepted;
    std::set<std::string> repeatable;
    bool allowPositional{false};
};

// 选项混入位（mixin）：多个命令共用的成套选项，按用途成组并入 accepted，避免逐命令罗列。
enum OptionMix : unsigned {
    MixNone = 0,
    MixGenerate = 1u << 0,   // 生成源：--size/--pattern/--orientation/--bg/--fg/--period/--line-width/--dot-radius/--seed/--noise。
    MixAnnotation = 1u << 1, // 命令行标注：--annotation/--annotations（同时并入 repeatable，可重复累加）。
    MixExport = 1u << 2,     // 公共导出：--format/--quality/--naming（applyExportOptions 读取）。
    MixLayers = 1u << 3,     // 命令行图层：--layer/--layers（同时并入 repeatable，可重复累加；展平后进引擎，与标注对称）。
    MixAssets = 1u << 4,     // 作业级通用：--save-config（单值）/ --assets-dir（三态 flag；同时并入 repeatable）。
};

// 组装某命令的 CommandArgs：core = 本命令专属选项；mix = 公共成套选项位；
// repeatable = 本命令额外的合法多值项（如 erase 的 --rect、grid 的 --keep/--remove）。
// 全局选项恒并入 accepted；flag 恒并入 repeatable。
inline CommandArgs makeCommandArgs(const std::initializer_list<std::string> core, const unsigned mix = MixNone,
                                   const std::initializer_list<std::string> repeatable = {},
                                   const bool allowPositional = false) {
    namespace o = texts::opt;
    CommandArgs ca;
    // 全局运行修饰选项（任意命令前后皆可出现，见 parseGlobalOptions）。作业级通用（--save-config / --assets-dir）
    // 经 MixAssets 由各命令并入——它们参与作业装配，不在全局扫描之列。
    ca.accepted = {o::kVerbose, o::kShortVerbose, o::kStrict, o::kLang};   // -h/-V 仅命令名前生效（命令后按「不接受」拒绝）。
    // flag（无值）选项：重复无害，一律视作可重复（B 不据此报错）。--compose 属具体命令但形为 flag。
    ca.repeatable = {o::kVerbose, o::kShortVerbose, o::kStrict, o::kCompose};
    for (const std::string& name : core) ca.accepted.insert(name);
    for (const std::string& name : repeatable) { ca.accepted.insert(name); ca.repeatable.insert(name); }
    if (mix & MixGenerate) {
        for (const std::string& g : {o::kSize, o::kPattern, o::kOrientation, o::kBg, o::kFg, o::kPeriod,
                                     o::kLineWidth, o::kDotRadius, o::kSeed, o::kNoise}) ca.accepted.insert(g);
    }
    if (mix & MixAnnotation) {
        for (const std::string& a : {o::kAnnotation, o::kAnnotations}) {
            ca.accepted.insert(a);
            ca.repeatable.insert(a);
        }
    }
    if (mix & MixLayers) {
        for (const std::string& l : {o::kLayer, o::kLayers}) {
            ca.accepted.insert(l);
            ca.repeatable.insert(l);
        }
    }
    if (mix & MixAssets) {
        ca.accepted.insert(o::kSaveConfig);   // 单值：重复给会按 B 报「重复单值」。
        ca.accepted.insert(o::kAssetsDir);
        ca.repeatable.insert(o::kAssetsDir);
    }
    if (mix & MixExport) {
        for (const std::string& e : {o::kFormat, o::kQuality, o::kNaming}) ca.accepted.insert(e);
    }
    ca.allowPositional = allowPositional;
    return ca;
}

// 拒绝本命令不接受的输入（G1 + 跨命令误用 A + 重复单值 B + 游离位置参数 C，五命令通用）：
// 命令解析完 token 后立即调用，任一命中即报参数错误退出码 1，杜绝「你以为传了、其实被静默吞掉」。
// 三类校验依 A→B→C 顺序：
//   A 未知 / 拼错 / 跨命令误用：收集到但不在「全局 ∪ 本命令专属」白名单内的选项；
//   B 重复单值：同一取值选项出现多次且不在「可重复」集（flag ∪ 本命令合法多值项）内；
//   C 游离位置参数：positional() 非空（所有命令一视同仁）。
inline int rejectUnknownOptions(const ArgParser& args, const CommandArgs& ca) {
    auto joinQuoted = [](const std::vector<std::string>& names) {
        std::string joined;
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (i != 0) joined += ", ";
            joined += "'" + names[i] + "'";
        }
        return joined;
    };
    // A：未知 / 拼错 / 跨命令误用的选项。
    if (const std::vector<std::string> unknown = args.optionsNotIn(ca.accepted); !unknown.empty())
        return fail(ExitCode::ArgError, IDC_CTR(msg, kErrUnknownOption),
                    IDC_CTR(msg, kDetUnknownOptionLead) + joinQuoted(unknown),
                    IDC_CTR(msg, kHintUnknownOption));
    // B：重复给出的单值选项。
    if (const std::vector<std::string> dups = args.duplicatedValueOptions(ca.repeatable); !dups.empty())
        return fail(ExitCode::ArgError, IDC_CTR(msg, kErrDuplicateOption),
                    IDC_CTR(msg, kDetDuplicateOptionLead) + joinQuoted(dups),
                    IDC_CTR(msg, kHintDuplicateOption));
    // C：游离位置参数。
    if (!ca.allowPositional && !args.positional().empty())
        return fail(ExitCode::ArgError, IDC_CTR(msg, kErrUnexpectedArg),
                    IDC_CTR(msg, kDetUnexpectedArgLead) + joinQuoted(args.positional()),
                    IDC_CTR(msg, kHintUnexpectedArg));
    return toInt(ExitCode::Ok);
}

// 解析公共导出选项 --format / --naming / --quality 写入 exportParams：
//   --format 存在则解析为 ExportFormat（失败 → false + err）；合并导出时 Core 仍以 --output
//            扩展名优先，此处的 format 作为无有效扩展名时的回退（见 export.cpp: exportMerged）。
//   --naming 存在则经 checkNaming 校验（非空、不含路径分隔符）后写入，用于分离导出命名。
//   --quality 存在则经 parseQuality 校验（0~100 整数，仅 JPEG/WebP 生效）后写入 exportParams.quality。
// 均可省略（省略时沿用 exportParams 既有默认值）。成功返回 true。
inline bool applyExportOptions(const ArgParser& args, engine::CompositionParams& exportParams,
                              std::string& err) {
    if (args.has(texts::opt::kFormat) && !parseFormat(args.get(texts::opt::kFormat), exportParams.format, err)) return false;
    if (args.has(texts::opt::kQuality) && !parseQuality(args.get(texts::opt::kQuality), exportParams.quality, err)) return false;
    if (args.has(texts::opt::kNaming)) {
        const std::string tpl = args.get(texts::opt::kNaming);
        if (!checkNaming(tpl, err)) return false;
        exportParams.naming = tpl;
    }
    return true;
}

} // namespace idc::cli
