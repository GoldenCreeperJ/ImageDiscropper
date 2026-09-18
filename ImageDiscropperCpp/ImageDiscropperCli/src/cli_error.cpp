// ============================================================================
// 文件：src/cli_error.cpp
// 作用：实现 cli_error.h 的 reportError——按 Cli README「错误格式」把错误写 stderr。
// 分块依据：错误输出格式是全局约定，集中一处实现，保证各命令 / 执行桥的错误措辞一致；
//       仅依赖标准库 <iostream>，不触碰 Core（CONTRIBUTING.md「分层纪律」：错误只走 stderr）。
// ============================================================================
#include "cli_error.h"

#include <iostream>

namespace idc::cli {

// 按错误格式打印错误到 stderr：
//   idc: error: <brief>
//     详情: <detail>   （非空才输出）
//     提示: <hint>     （非空才输出）
void reportError(const CliError& err) {
    // 命令名前缀固定 "idc"，与可执行名一致。
    std::cerr << "idc: error: " << err.brief << "\n";
    if (!err.detail.empty()) std::cerr << "  详情: " << err.detail << "\n";
    if (!err.hint.empty()) std::cerr << "  提示: " << err.hint << "\n";
    std::cerr.flush();
}

} // namespace idc::cli
