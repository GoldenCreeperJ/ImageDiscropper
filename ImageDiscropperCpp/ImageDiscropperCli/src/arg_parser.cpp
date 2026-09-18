// ============================================================================
// 文件：src/arg_parser.cpp
// 作用：实现 arg_parser.h——ArgParser::parse（token 归类）与 parseGlobalOptions（全局选项扫描）。
// 分块依据：只处理「语法层」token 归类，不理解值语义（值转换见 value_parser.cpp）；
//       纯标准库实现，无第三方依赖（A-0.2/A-0.3）。
// ============================================================================
#include "arg_parser.h"

namespace idc::cli {
namespace {

// 已知的「无值 flag」集合：这些选项后不消费 value，仅记录其出现过。
// 其余以 '-' 开头的选项一律按「取值选项」处理（消费紧随其后、不以 '-' 开头的 token）。
bool isFlagOption(const std::string& name) {
    return name == "--compose" || name == "--verbose" || name == "--quiet" ||
           name == "--help" || name == "-h" || name == "--version" || name == "-v";
}

// 判断 token 是否以 '-' 开头（即选项 / flag，而非值 / 位置参数）。
bool startsWithDash(const std::string& t) { return !t.empty() && t.front() == '-'; }

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

        // 取值选项：消费紧随其后、不以 '-' 开头的 token 作为值；否则记为空值（缺值）。
        if (i + 1 < tokens.size() && !startsWithDash(tokens[i + 1])) {
            record(opts_, t, tokens[i + 1]);
            ++i; // 跳过已被消费的值。
        } else {
            record(opts_, t, "");
        }
    }
}

bool ArgParser::has(const std::string& name) const { return opts_.find(name) != opts_.end(); }

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

// 扫描完整 argv 提取全局选项。--config / --save-config 取其后的值（或 "=value" 形式）。
GlobalOptions parseGlobalOptions(const std::vector<std::string>& args) {
    GlobalOptions go;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& t = args[i];

        // "=value" 形式的全局取值选项。
        const std::size_t eq = t.find('=');
        const std::string name = eq == std::string::npos ? t : t.substr(0, eq);
        const bool hasInline = eq != std::string::npos;
        const std::string inlineVal = hasInline ? t.substr(eq + 1) : std::string();

        if (name == "--help" || name == "-h") { go.help = true; continue; }
        if (name == "--version" || name == "-v") { go.version = true; continue; }
        if (name == "--verbose") { go.verbose = true; continue; }
        if (name == "--quiet") { go.quiet = true; continue; }

        if (name == "--config") {
            if (hasInline) go.config = inlineVal;
            else if (i + 1 < args.size()) go.config = args[++i];
            continue;
        }
        if (name == "--save-config") {
            if (hasInline) go.saveConfig = inlineVal;
            else if (i + 1 < args.size()) go.saveConfig = args[++i];
        }
    }
    return go;
}

} // namespace idc::cli
