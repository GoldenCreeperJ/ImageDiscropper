# ============================================================================
# 文件：ImageDiscropperCli/CMakeLists.txt
# 作用：CLI 模块构建脚本。产出一个可被测试复用的静态库 idc_cli_lib（承载除 main
#       外的全部 CLI 逻辑）与最终可执行程序 idc（命令名，guideline §4）。
# 架构定位：终稿 §10.1 的 CLI 层——命令行薄壳，只解析参数、调用 Core、格式化输出、
#           返回退出码（A-0.1）；不含任何切割 / 几何 / 导出 / 排序 / 极性逻辑。
# 分块依据：
#   - 拆出 idc_cli_lib 而非把全部源码塞进 idc 可执行：使集成测试（tests/）能直接
#     链接并调用 cliMain，实现「不启动子进程」的进程内端到端测试（可捕获退出码与输出）。
#   - CLI 头文件暴露 Core 概念类型（RectRegion / GridParams / ExportFormat…），故对
#     image_discropper_core 采用 PUBLIC 链接，使其 PUBLIC include 目录向下游（idc /
#     cli_tests）传播；第三方库（stb/webp/json）由 Core 以 PRIVATE 封装，CLI 无需感知。
#   - 版本号经编译期宏注入：IDC_CLI_VERSION（根工程版本）与 IDC_CORE_VERSION
#     （Core 经 PARENT_SCOPE 回传），供 --version 打印，保证与构建系统一致（§6.2）。
# ============================================================================
cmake_minimum_required(VERSION 3.28)

# 编译选项与 Core 保持一致（A-0.3）：MSVC 用 /W3 /utf-8（中文注释需 UTF-8），其余用 -Wall -Wextra。
if(MSVC)
    add_compile_options(/W3 /utf-8)
else()
    add_compile_options(-Wall -Wextra)
endif()

# 版本回退：正常由根 CMakeLists 提供；独立配置本子目录时给出占位默认，避免空宏。
if(NOT DEFINED IDC_CLI_VERSION)
    set(IDC_CLI_VERSION "1.0.0")
endif()
if(NOT DEFINED IDC_CORE_VERSION)
    set(IDC_CORE_VERSION "1.0.0")
endif()

# ---------------------------------------------------------------------------
# CLI 源文件（按职责分块，严禁上帝文件）。main.cpp 单列，其余进入可复用静态库。
# ---------------------------------------------------------------------------
set(IDC_CLI_LIB_SRC
        src/cli_app.cpp              # 顶层入口 cliMain：全局选项 / help / version / 分派 / 异常兜底
        src/arg_parser.cpp           # 轻量参数解析：token → 选项名到值列表
        src/value_parser.cpp         # 值解析：坐标 / 带 / 网格 / r,c / 画布 / 颜色 / 枚举 / 模板
        src/cli_error.cpp            # 错误报告：按 §5.2 格式写 stderr
        src/help.cpp                 # 帮助与版本文本（§6）
        src/job.cpp                  # 作业执行：读图 → runEngine → 坍缩校验 → exportImage → 退出码
        src/commands/extract.cpp     # L1 标准提取
        src/commands/erase.cpp       # L2 反向剔除（十字 / 横线 / 竖线 / 多矩形并集）
        src/commands/grid.cpp        # L3 网格分割
        src/commands/config.cpp      # 配置文件：--load 执行 / --save-config 序列化
)

add_library(idc_cli_lib STATIC ${IDC_CLI_LIB_SRC})

# CLI 公共头目录（供 idc 与 cli_tests 包含 *.h）。
target_include_directories(idc_cli_lib PUBLIC include)

# 链接 Core：PUBLIC 使 Core 的 include 目录与静态库向下游传播（CLI 头暴露 Core 类型）。
target_link_libraries(idc_cli_lib PUBLIC image_discropper_core)

# 版本号注入（仅本库内部使用，PRIVATE）。
target_compile_definitions(idc_cli_lib PRIVATE
    IDC_CLI_VERSION="${IDC_CLI_VERSION}"
    IDC_CORE_VERSION="${IDC_CORE_VERSION}"
)

# ---------------------------------------------------------------------------
# 可执行程序 idc：仅含 main（转发 argv 到 cliMain），逻辑全部在 idc_cli_lib。
# ---------------------------------------------------------------------------
add_executable(idc src/main.cpp)
target_link_libraries(idc PRIVATE idc_cli_lib)

# ---------------------------------------------------------------------------
# 测试：单元（参数解析 / 值解析）+ 集成（IT-1~IT-18，进程内调用 cliMain）。
# ---------------------------------------------------------------------------
add_executable(cli_tests
        tests/test_main.cpp
        tests/test_arg_parser.cpp
        tests/test_value_parser.cpp
        tests/test_integration.cpp
)
target_link_libraries(cli_tests PRIVATE idc_cli_lib)

add_test(NAME cli_tests COMMAND cli_tests)
