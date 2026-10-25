// ============================================================================
// 文件：src/cli_app.cpp
// 作用：实现 cliMain——CLI 唯一的顶层编排点（固定校验顺序的骨架）。
//       负责：登记本层译文表并决议语言 → 解析全局选项 → 处理 --version / --help → 定位子命令 →
//       分派到命令实现 → 未捕获异常兜底（退出码 5）。main.cpp 仅把 argv 转发到此。
// 分块依据：顶层「分派 + 全局横切」与「单命令参数装配」分离——各命令实现（commands/*.cpp）
//       只关心自身参数，全局逻辑集中于此，避免散落到每个命令（严禁上帝文件，亦避免重复）。
// 常量层：选项名取 texts::opt、子命令名取 texts::cmd、错误文案取 texts::msg（均见 include/i18n/cli_texts.h）；
//       别名只落在本 .cpp 内（命令名别名写作 cmds，以免与局部变量 cmd 同名）。
// ============================================================================
#include "commands.h"

#include <exception>
#include <string>
#include <vector>

#include "arg_parser.h"
#include "cli_error.h"
#include "i18n/cli_i18n.h"
#include "i18n/cli_texts.h"
#include "help.h"

namespace idc::cli {

// 本文件常量简写：opts＝选项名、cmds＝子命令名、msgs＝错误文案。
namespace opts = texts::opt;
namespace cmds = texts::cmd;

namespace {

// 从 args 中定位子命令名，并把其后的全部 token 写入 tokens。
// 规则：第一个「不以 '-' 开头」的 token 即命令；跳过全局运行修饰的取值项（仅 --lang）
// 及其值，避免把语言码误当作命令名。未找到命令时返回空串。
std::string splitCommand(const std::vector<std::string>& args, std::vector<std::string>& tokens) {
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& t = args[i];
        if (!t.empty() && t.front() == '-') {
            // 全局取值项（无 '=' 内联值时）会消费下一个 token，一并跳过。
            // 漏进这个集合会让「idc --lang en extract …」把 "en" 误当命令名。
            if (const bool takesValue = t == opts::kLang;
                takesValue && !t.contains('=')) ++i;
            continue;
        }
        tokens.assign(args.begin() + static_cast<std::ptrdiff_t>(i + 1), args.end());
        return t;
    }
    return {};
}

} // namespace

// CLI 顶层入口。返回退出码整数（已含退出码映射）。
int cliMain(const std::vector<std::string>& args) {
    try {
        // 0) i18n 装配：先登记本层译文表（纯静态表登记，零成本），再决议语言。
        //    面向人的输出（顶层帮助 / 错误三段式）全在其后，故必须先装表与定语言。
        i18n::installCatalogs();

        // 无任何参数：显示顶层帮助（友好退出 0）——无命令行输入可解析，语言只取环境变量链。
        if (args.empty()) {
            i18n::applyLanguage();
            printTopHelp();
            return toInt(ExitCode::Ok);
        }

        // 1) 全局选项（第 2 步）。
        const GlobalOptions go = parseGlobalOptions(args);

        // --version 优先于一切（版本行形态由 IT-13 钉住，不含可译文，故无需先定语言）。
        if (go.version) { printVersion(); return toInt(ExitCode::Ok); }

        // --lang 校验与切换：显式写了却认不出 → 参数错误（1）且不静默回退（否则拼错看起来像生效了）。
        // 报因本身用当前语言（此时尚未切换），故恒为源语言中文——与其余参数错误同形。
        if (!i18n::applyLanguage(go.lang)) {
            reportError(makeError(ExitCode::ArgError,
                                  IDC_CTR(msg, kErrBadLang) + go.lang + "'", "",
                                  IDC_CTR(msg, kHintLangCodes) + i18n::availableLanguageCodes()));
            return toInt(ExitCode::ArgError);
        }

        // 定位子命令（第 1 步）。
        std::vector<std::string> tokens;
        const std::string cmd = splitCommand(args, tokens);

        // --help [目标]：目标＝命令名 → 该命令帮助；目标＝主题名 → 主题帮助页；无目标 → 顶层帮助；
        // 都不是 → 参数错误（1），提示命令与主题两清单。
        if (go.help) {
            if (cmd.empty()) { printTopHelp(); return toInt(ExitCode::Ok); }
            if (isKnownCommand(cmd)) { printCommandHelp(cmd); return toInt(ExitCode::Ok); }
            if (isKnownTopic(cmd)) { printTopicHelp(cmd); return toInt(ExitCode::Ok); }
            reportError(makeError(ExitCode::ArgError, IDC_CTR(msg, kErrUnknownHelpTarget) + cmd + "'",
                                  "", IDC_CTR(msg, kHintKnownHelpTargets)));
            return toInt(ExitCode::ArgError);
        }

        // 有选项但无命令名：显示顶层帮助。
        if (cmd.empty()) { printTopHelp(); return toInt(ExitCode::Ok); }

        // 2) 分派到子命令；各命令内部按 第 3~5 步校验必填 / 互斥 / 格式。
        if (cmd == cmds::kExtract) return cmdExtract(tokens, go);
        if (cmd == cmds::kErase) return cmdErase(tokens, go);
        if (cmd == cmds::kGrid) return cmdGrid(tokens, go);
        if (cmd == cmds::kConfig) return cmdConfig(tokens, go);
        if (cmd == cmds::kNew) return cmdNew(tokens, go);

        reportError(makeError(ExitCode::ArgError, IDC_CTR(msg, kErrUnknownCmd) + cmd + "'", "",
                              IDC_CTR(msg, kHintKnownCmds)));
        return toInt(ExitCode::ArgError);
    } catch (const std::exception& e) {
        // 未预期异常统一兜底为内部错误（5），避免向用户抛出裸异常栈。
        reportError(makeError(ExitCode::InternalError, IDC_CTR(msg, kErrInternalUnexpected), e.what(),
                              IDC_CTR(msg, kHintReportBug)));
        return toInt(ExitCode::InternalError);
    } catch (...) {
        reportError(makeError(ExitCode::InternalError, IDC_CTR(msg, kErrInternalUnknown), "",
                              IDC_CTR(msg, kHintReportBug)));
        return toInt(ExitCode::InternalError);
    }
}

} // namespace idc::cli
