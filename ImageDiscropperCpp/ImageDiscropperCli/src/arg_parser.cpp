// ============================================================================
// 文件：src/arg_parser.cpp
// 作用：实现 arg_parser.h——ArgParser::parse（token 归类）与 parseGlobalOptions（全局选项扫描）。
// 分块依据：只处理「语法层」token 归类，不理解值语义（值转换见 value_parser.cpp）；
//       纯标准库实现，无第三方依赖（CONTRIBUTING.md「分层纪律」）。
// 常量层：选项名一律取 texts::opt（include/i18n/cli_texts.h），不写字面量——flag 表、全局扫描与
//       命令层取值共用同一串，改名时不会漏改某一处。
// ============================================================================
#include "arg_parser.h"

#include <cctype>

#include "i18n/cli_texts.h"

namespace idc::cli {

// 选项名简写：本 TU 内统一走 texts::opt（别名只落在 .cpp，不进公共头）。
namespace opt = texts::opt;

namespace {

// 已知的「无值 flag」集合：这些选项后不消费 value，仅记录其出现过。
// 其余以 '-' 开头的选项一律按「取值选项」处理（消费紧随其后、不以 '-' 开头的 token）。
bool isFlagOption(const std::string& name) {
    return name == opt::kCompose || name == opt::kVerbose || name == opt::kQuiet ||
           name == opt::kHelp || name == opt::kShortHelp || name == opt::kVersion ||
           name == opt::kShortVersion;
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

// 扫描完整 argv 提取全局选项。--config / --save-config / --lang 取其后的值（或 "=value" 形式）。
GlobalOptions parseGlobalOptions(const std::vector<std::string>& args) {
    GlobalOptions go;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& t = args[i];

        // "=value" 形式的全局取值选项。
        const std::size_t eq = t.find('=');
        const std::string name = eq == std::string::npos ? t : t.substr(0, eq);
        const bool hasInline = eq != std::string::npos;
        const std::string inlineVal = hasInline ? t.substr(eq + 1) : std::string();

        if (name == opt::kHelp || name == opt::kShortHelp) { go.help = true; continue; }
        if (name == opt::kVersion || name == opt::kShortVersion) { go.version = true; continue; }
        if (name == opt::kVerbose) { go.verbose = true; continue; }
        if (name == opt::kQuiet) { go.quiet = true; continue; }
        if (name == opt::kBundleAssets) { go.bundleAssets = true; continue; }

        if (name == opt::kConfig) {
            if (hasInline) go.config = inlineVal;
            else if (i + 1 < args.size()) go.config = args[++i];
            continue;
        }
        if (name == opt::kSaveConfig) {
            if (hasInline) go.saveConfig = inlineVal;
            else if (i + 1 < args.size()) go.saveConfig = args[++i];
            continue;
        }
        // --lang <code>：取原值不校验（语言码是否可用属语义层，见 cli_i18n.cpp::applyLanguage）。
        if (name == opt::kLang) {
            if (hasInline) go.lang = inlineVal;
            else if (i + 1 < args.size()) go.lang = args[++i];
        }
    }
    return go;
}

} // namespace idc::cli
