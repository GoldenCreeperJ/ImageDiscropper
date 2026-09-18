// ============================================================================
// 文件：tests/test_main.cpp
// 作用：CLI 测试入口——依次调度单元（参数解析 / 值解析）与集成（IT-1~IT-21）测试段。
//       使用共享 CHECK 宏，不依赖第三方框架（与 Core tests/test_main.cpp 同构，CONTRIBUTING.md「分层纪律」）。
// 分块依据：main 只做“调度”，各测试段逻辑分散在 test_arg_parser / test_value_parser /
//       test_integration 三个 .cpp（经 cli_test_harness.h 声明），避免 God File。
// 说明：集成测试写入系统临时目录并在结束时清理，不污染仓库、不依赖网络。
// ============================================================================
#include <iostream>

#include "cli_test_harness.h"

// main：依次执行全部测试段；任一 CHECK 失败即以非零码退出（ctest 判定失败）。
int main() {
    testArgParser();    // 单元：ArgParser + parseGlobalOptions
    testValueParser();  // 单元：value_parser 全部选项解析
    testIntegration();  // 集成：IT-1~IT-21
    std::cout << "all CLI tests passed.\n";
    return 0;
}
