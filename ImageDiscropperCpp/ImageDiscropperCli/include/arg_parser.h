// ============================================================================
// 文件：include/arg_parser.h
// 作用：定义轻量命令行解析——GlobalOptions（全局选项）与 ArgParser
//       （子命令 token → 「选项名到值列表」的收集器）。这是 CLI「解析参数」职责的落点。
// 分块依据：
//   - 本文件只做「语法层」解析：把 token 归类为选项 / 值 / 位置参数，不理解语义
//     （坐标、网格、枚举等「值 → 类型」的转换见 value_parser.h）。
//   - 不引入任何第三方 CLI 库（CONTRIBUTING.md「分层纪律」：CLI 依赖须与 Core 一致，Core 未 vendor
//     命令行库，故自持极简解析器，仅用标准库）。
// 说明：可重复选项（--rect / --keep / --remove）保留全部值及其出现顺序；
//       flag 型选项（无值）以空串记录其「出现过」。解析本身不因未知选项报错（纯语法归类），
//       但命令层会经 ArgParser::optionsNotIn / duplicatedValueOptions（按本命令白名单）拒绝拼错 / 跨命令误用 /
//       重复的单值选项，并拒绝游离位置参数（退出码 1），
//       「哪些选项合法、是否必填、是否互斥」仍由各命令按固定顺序校验。
// ============================================================================
#pragma once

#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace idc::cli {

// ---------------------------------------------------------------------------
// GlobalOptions：全局选项（布局见本目录 README「全局选项」）。可出现在子命令前后任意位置。
// 注：成员**顺序与个数被 cli_tests 的结构化绑定断言依赖**（tests/test_arg_parser.cpp），
//     增删成员或调序必须同步该用例——这一耦合是故意的：漏同步会当场编译失败，
//     而不是让新选项静默逃过默认值断言。
// ---------------------------------------------------------------------------
struct GlobalOptions {
    bool help{false};        // --help / -h：显示帮助。
    bool version{false};     // --version / -v：显示版本。
    bool verbose{false};     // --verbose / -v：打开进度诊断日志（stderr；默认静默——沉默是金，结果行恒写 stdout）。
    bool strict{false};       // --strict：伞形严格模式——把图层资源缺失降级占位 / 尺寸错配静默裁切 / 零产出统一升级为报错（见 makeJobOptions）。
    std::string lang;        // --lang <code>：帮助 / 报错文本的语言；空串 = 未显式指定（转走环境变量链，见 cli_i18n.h）。
};
// 注：--save-config / --assets-dir 为**作业级通用选项**（写在子命令之后，经 command_support.h 的
//   MixAssets 由各命令装配、从 ArgParser 读取），不在此处——它们参与作业装配（assets-dir 可写进
//   config.assetsDir 字段），与「运行修饰」不是一类。

// ---------------------------------------------------------------------------
// ArgParser：把子命令之后的 token 收集为「选项名 → 值列表」。
// 约定：
//   - 选项以 '-' 开头；支持 "--name value" 与 "--name=value" 两种写法。
//   - 单独的 "--" 为分隔符：其后的 token 一律归入位置参数，不再解析为选项。
//   - 已知 flag（无值选项，如 --compose）记录一个空串值表示「出现过」。
//   - 其余选项：若紧随其后的 token 不以 '-' 开头，或为「负数值」（如 -5、-5,-7,10,20），
//     则作为其值消费；否则视为缺值（记录空串，交由命令层判定是否报错）。
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

    // 返回本命令 token 里出现、但**不在给定可接受选项集**内的选项名，去重并按字典序排列。
    // 供命令层按「本命令专属 + 全局」白名单拒绝未知 / 拼错 / 跨命令误用的选项（退出码 1）。
    // 值以 '-' 内联形式出现（如 --foo=bar）按选项名 '--foo' 判定；负数值已被当作上游选项的值消费，不在此列。
    std::vector<std::string> optionsNotIn(const std::set<std::string>& accepted) const;

    // 返回出现次数 >1、但不在给定「可重复」集合内的选项名（去重、字典序）。
    // 供命令层拒绝「重复给出的单值选项」（如 --input 两次只取第一个、第二个静默失效，退出码 1）。
    // 「可重复」集合由调用方并入 flag（无值）选项与合法多值选项（--rect/--keep/--remove/--annotation 等），
    // 使 flag 与既定的可重复选项不被误报。
    std::vector<std::string> duplicatedValueOptions(const std::set<std::string>& allowedRepeat) const;

private:
    // 选项名 → 值列表（flag 以空串占位）。用 vector 保序，unordered_map 快速定位。
    std::unordered_map<std::string, std::vector<std::string>> opts_;
    std::vector<std::string> positional_;
};

// 从完整 argv（不含程序名）扫出全局运行修饰选项（位置无关）。识别 --help/-h、--version/-v、
// --verbose、--strict、--lang <code>。作业级通用与命令专属选项由各命令经 ArgParser 读取。
GlobalOptions parseGlobalOptions(const std::vector<std::string>& args);

} // namespace idc::cli
