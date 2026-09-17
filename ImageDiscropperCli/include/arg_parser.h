// ============================================================================
// 文件：include/arg_parser.h
// 作用：定义轻量命令行解析——GlobalOptions（全局选项，guideline §4.1）与 ArgParser
//       （子命令 token → 「选项名到值列表」的收集器）。这是 CLI「解析参数」职责的落点。
// 分块依据：
//   - 本文件只做「语法层」解析：把 token 归类为选项 / 值 / 位置参数，不理解语义
//     （坐标、网格、枚举等「值 → 类型」的转换见 value_parser.h）。
//   - 不引入任何第三方 CLI 库（A-0.2/A-0.3：CLI 依赖须与 Core 一致，Core 未 vendor
//     命令行库，故自持极简解析器，仅用标准库）。
// 说明：可重复选项（--rect / --keep / --remove）保留全部值及其出现顺序；
//       flag 型选项（无值）以空串记录其「出现过」。解析不因未知选项报错，
//       「哪些选项合法、是否必填、是否互斥」由各命令按 §5.3 顺序校验。
// ============================================================================
#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace idc::cli {

// ---------------------------------------------------------------------------
// GlobalOptions：全局选项（guideline §4.1）。可出现在子命令前后任意位置。
// ---------------------------------------------------------------------------
struct GlobalOptions {
    bool help{false};        // --help / -h：显示帮助。
    bool version{false};     // --version / -v：显示版本。
    bool verbose{false};     // --verbose：详细日志到 stderr。
    bool quiet{false};       // --quiet：静默，仅输出错误。
    std::string config;      // --config <file>：从 JSON 配置读取全部参数（§9）。
    std::string saveConfig;  // --save-config <file>：把当前参数序列化为 JSON（dry-run）。
};

// ---------------------------------------------------------------------------
// ArgParser：把子命令之后的 token 收集为「选项名 → 值列表」。
// 约定：
//   - 选项以 '-' 开头；支持 "--name value" 与 "--name=value" 两种写法。
//   - 已知 flag（无值选项，如 --compose）记录一个空串值表示「出现过」。
//   - 其余选项：若紧随其后的 token 不以 '-' 开头，则作为其值消费；否则视为缺值
//     （记录空串，交由命令层判定是否报错）。本 CLI 的合法值均非负、不以 '-' 开头。
//   - 不以 '-' 开头且未被前一选项消费的 token 归入位置参数。
// ---------------------------------------------------------------------------
class ArgParser {
public:
    // 解析 token 列表（不含程序名与子命令名）。
    void parse(const std::vector<std::string>& tokens);

    // 某选项是否出现过（含 flag）。name 形如 "--input"。
    bool has(const std::string& name) const;

    // 某选项出现次数（可重复选项据此判断单次 / 多次语义）。
    std::size_t count(const std::string& name) const;

    // 取某选项的首个值；不存在或无值时返回 fallback。
    std::string get(const std::string& name, const std::string& fallback = {}) const;

    // 取某选项的全部值（可重复选项，保持出现顺序）；不存在返回空向量。
    std::vector<std::string> getAll(const std::string& name) const;

    // 位置参数（非选项、未被消费的 token）。
    const std::vector<std::string>& positional() const { return positional_; }

private:
    // 选项名 → 值列表（flag 以空串占位）。用 vector 保序，unordered_map 快速定位。
    std::unordered_map<std::string, std::vector<std::string>> opts_;
    std::vector<std::string> positional_;
};

// 从完整 argv（不含程序名）扫描出全局选项。识别 --help/-h、--version/-v、
// --verbose、--quiet、--config <file>、--save-config <file>。
GlobalOptions parseGlobalOptions(const std::vector<std::string>& args);

} // namespace idc::cli
