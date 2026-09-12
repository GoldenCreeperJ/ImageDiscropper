// ============================================================================
// 文件：src/main.cpp
// 作用：可执行程序 idc 的入口。仅把 argv 转换为 token 列表并转发给 cliMain，
//       返回其退出码——不含任何业务逻辑（逻辑全部在 idc_cli_lib，便于集成测试复用）。
// 分块依据：入口点单列一文件，与顶层编排（cli_app.cpp）解耦；这样 tests/ 可直接链接
//       idc_cli_lib 调用 cliMain 做进程内端到端测试，无需启动子进程（guideline §9.3）。
// 说明（Windows 中文控制台乱码修复）：源码以 /utf-8 编译，字符串字面量与 std::cout 输出的
//       均为 UTF-8 字节；但 Windows 控制台缺省用 OEM 代码页（中文系统为 GBK/936）解释这些
//       字节，导致帮助 / 版本 / 错误文本显示为乱码。故在入口处把控制台输入 / 输出代码页切到
//       UTF-8（65001），使控制台按 UTF-8 渲染。注意：不转换 argv——原生窄字符参数仍保持系统
//       ANSI 代码页，与底层 stb_image 的 fopen（按 ANSI 解释路径）一致，避免中文路径打不开；
//       输出被重定向到文件 / 管道时无控制台，SetConsole*CP 静默失败，UTF-8 字节原样写入（符合预期，
//       亦不影响集成测试用 rdbuf 捕获）。
// ============================================================================
#include <string>
#include <vector>

#include "cli/commands.h"

#ifdef _WIN32
// 仅在 Windows 下需要显式切换控制台代码页；其他平台的终端通常默认 UTF-8。
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// 把控制台输入 / 输出代码页设为 UTF-8，使 std::cout 写出的 UTF-8 中文正确显示。
static void enableUtf8Console() {
    SetConsoleOutputCP(CP_UTF8); // 输出：帮助 / 版本 / 错误文本（stdout / stderr）
    SetConsoleCP(CP_UTF8);       // 输入：交互式读取时的中文（本工具虽不读 stdin，一并设置更稳妥）
}
#endif

// 程序入口：argv[1..] → std::vector<std::string> → cliMain。
int main(int argc, char** argv) {
#ifdef _WIN32
    enableUtf8Console(); // 必须先于任何输出，避免首行中文乱码。
#endif
    std::vector<std::string> args;
    args.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0);
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
    return idc::cli::cliMain(args);
}
