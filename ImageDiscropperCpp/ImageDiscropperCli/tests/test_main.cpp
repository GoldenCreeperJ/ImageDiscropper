// ============================================================================
// 文件：tests/test_main.cpp
// 作用：CLI 测试入口——依次调度单元（参数解析 / 值解析）与集成（IT-1~IT-53）测试段。
//       使用共享 CHECK 宏，不依赖第三方框架（与 Core tests/test_main.cpp 同构，CONTRIBUTING.md「分层纪律」）。
// 分块依据：main 只做“调度”，各测试段逻辑分散在 test_arg_parser / test_value_parser /
//       test_integration 三个 .cpp（经 cli_test_harness.h 声明），避免 God File。
// 说明：集成测试写入系统临时目录并在结束时清理，不污染仓库、不依赖网络。
// ============================================================================
#include <iostream>
#include <cstdlib>   // 清语言 env：_putenv_s（MSVC）/ unsetenv（POSIX）

#include "cli_test_harness.h"

// 将 CLI 语言 env 链（IDC_LANG > LC_ALL > LANG）清空，令进程内直调的 cliMain 恒回落源语言 zh-CN。
// 集成测试继承 CI runner 的 LANG（macOS 常为 en_US.UTF-8）会把输出翻成英文，使断言中文文案
// （坍缩 / 标注 / 图案…）的用例（IT-18 / IT-52 / IT-53…）在部分平台翻车；此处锁定默认语言，跨平台确定。
// 显式 --lang 的用例不受影响（applyLanguage 走命令行分支，不读 env）。
static void pinDefaultLanguage() {
#ifdef _MSC_VER
    _putenv_s("IDC_LANG", "");
    _putenv_s("LC_ALL", "");
    _putenv_s("LANG", "");
#else
    unsetenv("IDC_LANG");
    unsetenv("LC_ALL");
    unsetenv("LANG");
#endif
}

// main：依次执行全部测试段；任一 CHECK 失败即以非零码退出（ctest 判定失败）。
int main() {
    pinDefaultLanguage();
    testArgParser();    // 单元：ArgParser + parseGlobalOptions
    testValueParser();  // 单元：value_parser 全部选项解析
    testIntegration();  // 集成：IT-1~IT-53
    std::cout << "all CLI tests passed.\n";
    return 0;
}
