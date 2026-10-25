// ============================================================================
// 文件：src/arg_parser.cpp
// 作用：实现 arg_parser.h——ArgParser::parse（token 归类）与 parseGlobalOptions（全局选项扫描）。
// 分块依据：只处理「语法层」token 归类，不理解值语义（值转换见 value_parser.cpp）；
//       纯标准库实现，无第三方依赖（CONTRIBUTING.md「分层纪律」）。
// 常量层：选项名一律取 texts::opt（include/i18n/cli_texts.h），不写字面量——flag 表、全局扫描与
//       命令层取值共用同一串，改名时不会漏改某一处。
// ============================================================================
#include "arg_parser.h"

#include <algorithm>
#include <cctype>
#include <ranges>
#include <set>

#include "i18n/cli_texts.h"

namespace idc::cli {

// 选项名简写：本 TU 内统一走 texts::opt（别名只落在 .cpp，不进公共头）。
namespace opt = texts::opt;

namespace {

// 已知的「无值 flag」集合：这些选项后不消费 value，仅记录其出现过。
// 其余以 '-' 开头的选项一律按「取值选项」处理（消费紧随其后、不以 '-' 开头的 token）。
// 三处同步登记（新增 flag 必须三处齐改）：① opt 常量（cli_texts_base.h）；② 本集合；
// ③ makeCommandArgs 的 repeatable 集（command_support.h / 各命令）。
bool isFlagOption(const std::string& name) {
    return name == opt::kCompose || name == opt::kVerbose || name == opt::kShortVerbose ||
           name == opt::kHelp || name == opt::kShortHelp || name == opt::kVersion ||
           name == opt::kShortVersion || name == opt::kStrict ||
           name == opt::kAssetsDir || name == opt::kReplaceAnnotations || name == opt::kReplaceLayers;
}

// 短选项簇展开（GNU 惯例 -abc ≡ -a -b -c）：整簇都是已知 flag 短写（h/v/V）时拆成独立短选项，
// 否则原样保留（交后续按未知选项报错）。
std::vector<std::string> expandShortClusters(const std::vector<std::string>& args) {
    std::vector<std::string> out;
    for (const std::string& t : args) {
        if (t.size() > 2 && t[0] == '-' && t[1] != '-') {
            const std::string known = "hvV";
            bool allKnown = true;
            for (std::size_t i = 1; i < t.size(); ++i)
                if (known.find(t[i]) == std::string::npos) { allKnown = false; break; }
            if (allKnown) {
                for (std::size_t i = 1; i < t.size(); ++i) out.push_back(std::string("-") + t[i]);
                continue;
            }
        }
        out.push_back(t);
    }
    return out;
}

// 判断 token 是否以 '-' 开头（即选项 / flag，而非值 / 位置参数）。
bool startsWithDash(const std::string& t) { return !t.empty() && t.front() == '-'; }

// 判断 token 是否为「负数值」形式（如 -5 或 -5,-7,10,20）：'-' 后紧跟数字。
// 负值合法（如负网格基准点），可作为取值选项的值被消费；其余 '-' 开头 token 仍视为选项。
bool isNegativeNumberToken(const std::string& t) {
    return t.size() >= 2 && t.front() == '-' &&
           std::isdigit(static_cast<unsigned char>(t[1])) != 0;
}

// 把一个「取值选项」的值追加到 opts_[name]（保持出现顺序，支持可重复选项）。
void record(std::unordered_map<std::string, std::vector<std::string>>& opts,
            const std::string& name, const std::string& value) {
    opts[name].push_back(value);
}

} // namespace

// 解析 token 列表：逐个归类为选项（含值）/ flag / 位置参数。
void ArgParser::parse(const std::vector<std::string>& tokens) {
    bool onlyPositional = false; // 遇到 "--" 之后全部视为位置参数。
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const std::string& t = tokens[i];

        // "--" 分隔符：其后不再解析选项。
        if (!onlyPositional && t == "--") { onlyPositional = true; continue; }

        // 位置参数：不以 '-' 开头，或已进入 "--" 之后。
        if (onlyPositional || !startsWithDash(t)) { positional_.push_back(t); continue; }

        // "--name=value" 形式：在首个 '=' 处切分。
        if (const std::size_t eq = t.find('='); eq != std::string::npos) {
            record(opts_, t.substr(0, eq), t.substr(eq + 1));
            continue;
        }

        // flag：仅记录出现过（空串占位）。
        if (isFlagOption(t)) { record(opts_, t, ""); continue; }

        // 取值选项：消费紧随其后、不以 '-' 开头的 token（或负数值 token）作为值；否则记为空值（缺值）。
        if (i + 1 < tokens.size() &&
            (!startsWithDash(tokens[i + 1]) || isNegativeNumberToken(tokens[i + 1]))) {
            record(opts_, t, tokens[i + 1]);
            ++i; // 跳过已被消费的值。
        } else {
            record(opts_, t, "");
        }
    }
}

bool ArgParser::has(const std::string& name) const { return opts_.contains(name); }

std::size_t ArgParser::count(const std::string& name) const {
    const auto it = opts_.find(name);
    return it == opts_.end() ? 0 : it->second.size();
}

std::string ArgParser::get(const std::string& name, const std::string& fallback) const {
    const auto it = opts_.find(name);
    if (it == opts_.end() || it->second.empty()) return fallback;
    return it->second.front();
}

std::vector<std::string> ArgParser::getAll(const std::string& name) const {
    const auto it = opts_.find(name);
    return it == opts_.end() ? std::vector<std::string>{} : it->second;
}

// 不在给定可接受集内的选项名 = 本命令收集、但调用方未列入白名单的键（去重）。
//  opts_ 键已去重，排序仅为输出稳定。负数值已被上游当作选项值消费，不在 opts_ 键中。
std::vector<std::string> ArgParser::optionsNotIn(const std::set<std::string>& accepted) const {
    std::vector<std::string> unknown;
    for (const auto &name: opts_ | std::views::keys) {
        if (!accepted.contains(name)) unknown.push_back(name);
    }
    std::ranges::sort(unknown);
    return unknown;
}

// 重复的单值选项 = 出现次数 >1、且不在「可重复」集内的键（去重、字典序）。
//  opts_ 每个键的 vector 长度即出现次数；调用方把 flag 与合法多值选项并入 allowedRepeat 以免误报。
std::vector<std::string> ArgParser::duplicatedValueOptions(const std::set<std::string>& allowedRepeat) const {
    std::vector<std::string> dups;
    for (const auto& [name, values] : opts_) {
        if (values.size() > 1 && !allowedRepeat.contains(name)) dups.push_back(name);
    }
    std::ranges::sort(dups);
    return dups;
}

// 扫描完整 argv 提取全局选项。--save-config / --lang 取其后的值（或 "=value" 形式）。
GlobalOptions parseGlobalOptions(const std::vector<std::string>& args) {
    GlobalOptions go;
    bool seenCommand = false;   // 已越过命令名：其后 -h/-V/--help/--version 不再生效（命令后拒绝，交命令层报「不接受」）。
    const std::vector<std::string> expanded = expandShortClusters(args);
    for (std::size_t i = 0; i < expanded.size(); ++i) {
        const std::string& t = expanded[i];
        if (!t.empty() && t.front() != '-') seenCommand = true;   // 第一个非选项 token 即命令名（与 splitCommand 同规）。

        // "=value" 形式的全局取值选项。
        const std::size_t eq = t.find('=');
        const std::string name = eq == std::string::npos ? t : t.substr(0, eq);
        const bool hasInline = eq != std::string::npos;
        const std::string inlineVal = hasInline ? t.substr(eq + 1) : std::string();

        if ((name == opt::kHelp || name == opt::kShortHelp) && !seenCommand) { go.help = true; continue; }
        if ((name == opt::kVersion || name == opt::kShortVersion) && !seenCommand) { go.version = true; continue; }
        if (name == opt::kVerbose || name == opt::kShortVerbose) { go.verbose = true; continue; }
        if (name == opt::kStrict) { go.strict = true; continue; }

        // --lang <code>：取原值不校验（语言码是否可用属语义层，见 cli_i18n.cpp::applyLanguage）。
        // 下一 token 以 '-' 开头（另一个选项）则不消费——记空串＝未指定（与 ArgParser 同规，不吞选项）。
        if (name == opt::kLang) {
            if (hasInline) go.lang = inlineVal;
            else if (i + 1 < expanded.size() && !expanded[i + 1].empty() && expanded[i + 1].front() != '-')
                go.lang = expanded[++i];
        }
    }
    return go;
}

} // namespace idc::cli
