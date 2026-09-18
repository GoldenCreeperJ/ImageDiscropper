// ============================================================================
// 文件：src/cli_app.cpp
// 作用：实现 cliMain——CLI 唯一的顶层编排点（固定校验顺序的骨架）。
//       负责：解析全局选项 → 处理 --version / --help → 定位子命令 → 分派到命令实现 →
//       未捕获异常兜底（退出码 5）。main.cpp 仅把 argv 转发到此。
// 分块依据：顶层「分派 + 全局横切」与「单命令参数装配」分离——各命令实现（commands/*.cpp）
//       只关心自身参数，全局逻辑集中于此，避免散落到每个命令（严禁上帝文件，亦避免重复）。
// 说明：全局 --config <file> 复用 config 命令的「加载 + 执行」路径（NFR-0：同一执行通道），
//       不另起炉灶；因 --config 无子命令时 splitCommand 会把首个非选项取值（如 --input 的图像
//       路径）误判为命令名，故 --config 路由传入「全部 args」并前置于命令名判定，避免 --input/
//       --output 等选项丢失（详见下方 cliMain 内注释）。
// ============================================================================
#include "commands.h"

#include <exception>
#include <string>
#include <vector>

#include "arg_parser.h"
#include "cli_error.h"
#include "help.h"

namespace idc::cli {
namespace {

// 从 args 中定位子命令名，并把其后的全部 token 写入 tokens。
// 规则：第一个「不以 '-' 开头」的 token 即命令；跳过全局取值选项（--config / --save-config）
// 及其值，避免把它们的值误当作命令名。未找到命令时返回空串。
std::string splitCommand(const std::vector<std::string>& args, std::vector<std::string>& tokens) {
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& t = args[i];
        if (!t.empty() && t.front() == '-') {
            // 全局取值选项（无 '=' 内联值时）会消费下一个 token，一并跳过。
            if (const bool takesValue = t == "--config" || t == "--save-config"; takesValue && t.find('=') == std::string::npos) ++i;
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
        // 无任何参数：显示顶层帮助（友好退出 0）。
        if (args.empty()) { printTopHelp(); return toInt(ExitCode::Ok); }

        // 1) 全局选项（第 2 步）。
        const GlobalOptions go = parseGlobalOptions(args);

        // --version 优先于一切。
        if (go.version) { printVersion(); return toInt(ExitCode::Ok); }

        // 定位子命令（第 1 步）。
        std::vector<std::string> tokens;
        const std::string cmd = splitCommand(args, tokens);

        // --help：有命令 → 该命令帮助；仅 --config（无命令）→ config 帮助；否则顶层帮助。
        if (go.help) {
            if (!cmd.empty()) printCommandHelp(cmd);
            else if (!go.config.empty()) printCommandHelp("config");
            else printTopHelp();
            return toInt(ExitCode::Ok);
        }

        // 全局 --config：以 JSON 配置驱动本次作业（复用 config 命令的加载 + 执行路径，NFR-0）。
        // 关键：传入「全部 args」而非命令后 tokens——因为 --config 无子命令时，splitCommand 会把
        // 第一个非选项取值（如 --input 的 photo.jpg）误判为命令名并据其切分 tokens，导致 --input
        // 等选项丢失；传全量 args 可让 cmdConfig 完整看到 --input / --output（命令名被忽略，一切
        // 以 JSON 配置为准）。此路由须前置于 cmd.empty() 判定，否则「idc --config x.json --input
        // ...」（无子命令）会先落入顶层帮助分支而不执行。
        if (!go.config.empty()) {
            std::vector<std::string> cfgTokens{"--load", go.config};
            cfgTokens.insert(cfgTokens.end(), args.begin(), args.end());
            return cmdConfig(cfgTokens, go);
        }

        // 有 --help / --config 之外的选项但无命令名：显示顶层帮助。
        if (cmd.empty()) { printTopHelp(); return toInt(ExitCode::Ok); }

        // 2) 分派到子命令；各命令内部按 第 3~5 步校验必填 / 互斥 / 格式。
        if (cmd == "extract") return cmdExtract(tokens, go);
        if (cmd == "erase") return cmdErase(tokens, go);
        if (cmd == "grid") return cmdGrid(tokens, go);
        if (cmd == "config") return cmdConfig(tokens, go);

        reportError(makeError(ExitCode::ArgError, "未知命令：'" + cmd + "'", "",
                              "可用命令：extract / erase / grid / config；用 idc --help 查看用法"));
        return toInt(ExitCode::ArgError);
    } catch (const std::exception& e) {
        // 未预期异常统一兜底为内部错误（5），避免向用户抛出裸异常栈。
        reportError(makeError(ExitCode::InternalError, "内部错误（未预期异常）", e.what(),
                              "请上报 bug（附完整命令行）"));
        return toInt(ExitCode::InternalError);
    } catch (...) {
        reportError(makeError(ExitCode::InternalError, "内部错误（未知异常）", "",
                              "请上报 bug（附完整命令行）"));
        return toInt(ExitCode::InternalError);
    }
}

} // namespace idc::cli
