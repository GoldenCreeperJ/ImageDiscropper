// ============================================================================
// 文件：include_error.h
// 作用：定义 CLI 的错误载体 CliError 与统一的错误输出 reportError（guideline §5.2 / A-0.6）。
//       把「退出码 + 简短描述 + 详情 + 提示」打包为一处，确保所有错误以同一格式写 stderr，
//       stdout 只留正常结果与进度（A-0.6）。
// 分块依据：错误「数据结构 + 格式化输出」是横切关注点，独立于参数解析与命令实现；
//       退出码枚举来自 exit_code.h，本文件只负责「怎么描述、怎么打印」。
// ============================================================================
#pragma once

#include <string>

#include "exit_code.h"

namespace idc::cli {

// ---------------------------------------------------------------------------
// CliError：一次错误的完整描述。
//   code   —— 对应退出码（决定 main 返回值）。
//   brief  —— 简短描述（§5.2 首行 "error:" 之后）。
//   detail —— 详情（多为 Core 返回的错误串；可空）。
//   hint   —— 可操作的建议（可空）。
// ---------------------------------------------------------------------------
struct CliError {
    ExitCode code{ExitCode::InternalError};
    std::string brief;
    std::string detail;
    std::string hint;
};

// 便捷构造：按字段拼装 CliError（detail / hint 可省略）。
inline CliError makeError(const ExitCode code, std::string brief,
                          std::string detail = {}, std::string hint = {}) {
    return CliError{code, std::move(brief), std::move(detail), std::move(hint)};
}

// 按 guideline §5.2 的格式把错误写到 stderr：
//   idc: error: <brief>
//     详情: <detail>      （detail 非空时才输出）
//     提示: <hint>        （hint 非空时才输出）
// 命令名前缀固定为 "idc"（与实际可执行名一致）。
void reportError(const CliError& err);

} // namespace idc::cli
