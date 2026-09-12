// ============================================================================
// 文件：tests/cli_test_harness.h
// 作用：CLI 测试的极简骨架——统一的 CHECK 断言宏 + 跨文件测试段声明。供 test_main.cpp
//       与各 test_*.cpp 共享，避免每个测试文件重复定义宏，也避免把所有测试塞进单一
//       God File（guideline §10.3：严禁上帝文件）。与 Core 的 tests/test_harness.h 同构，
//       保持两个模块测试风格一致（A-0.3）。
// 分块依据：CHECK 宏是“断言机制”，测试段声明是“跨编译单元的连接点”，二者集中于此；
//       具体测试逻辑分散在 test_arg_parser / test_value_parser / test_integration 三个 .cpp，
//       由 test_main.cpp 的 main() 统一调度（单元 → 集成，先易后难）。
// 说明：不引入第三方测试框架（A-0.2/A-0.3：CLI 依赖须与 Core 一致，仅用标准库）；
//       CHECK 失败即打印文件 / 行号 / 表达式并以非零码退出，使 ctest 能捕获失败。
// ============================================================================
#pragma once

#include <cstdlib>
#include <iostream>

// 简易断言宏：失败时打印文件 / 行号 / 表达式并以非零码退出。
// 说明：宏内含逗号表达式时用 CHECK((a==b)) 双括号包裹，避免逗号被当作宏参数分隔符。
#define CHECK(expr)                                                        \
    do {                                                                   \
        if (!(expr)) {                                                     \
            std::cerr << "CHECK failed: " #expr " @ " << __FILE__ << ":"   \
                      << __LINE__ << "\n";                                 \
            std::exit(1);                                                  \
        }                                                                  \
    } while (0)

// ---------------------------------------------------------------------------
// CLI 测试段（定义于独立 .cpp，由 test_main.cpp 的 main() 调用）。
//   testArgParser   —— 单元：ArgParser 语法归类 + parseGlobalOptions 全局选项（§9.1 参数解析）。
//   testValueParser —— 单元：value_parser 全部“字符串 → 类型”解析与格式校验（§9.1）。
//   testIntegration —— 集成：IT-1~IT-18，进程内调用 cliMain，校验退出码 / 输出尺寸 / 像素顺序（§9.2）。
// ---------------------------------------------------------------------------
void testArgParser();
void testValueParser();
void testIntegration();
