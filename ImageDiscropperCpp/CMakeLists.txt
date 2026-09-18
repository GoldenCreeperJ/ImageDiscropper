# ============================================================================
# 文件：CMakeLists.txt（仓库根）
# 作用：ImageDiscropper 工程的顶层构建脚本。以「并列多模块」方式同时纳入
#       Core（纯逻辑静态库）、CLI（命令行薄壳）与 GUI（Qt6 桌面前端），对应终稿 §10.1 的
#       project/{core, cli, gui} 三层结构（三者均已落地）。
# 架构定位：本文件只做「模块编排 + 全局约定」——统一 C++ 标准、按依赖顺序引入子工程、
#           开放测试注册；各模块的具体目标、依赖与编译选项由其自身 CMakeLists 定义。
# 分块依据：
#   - Core 必须先于 CLI 引入：CLI 链接 image_discropper_core 静态库目标，
#     该目标须在 CLI 子目录处理前已存在（CMake 目标按引入顺序对后续兄弟目录可见）。
#   - Core 以 PARENT_SCOPE 向本作用域回传 IDC_CORE_VERSION，供 CLI 的 --version
#     打印 "(core <core-version>)"（guideline §6.2）。
# 说明：使用 CLion / 命令行时，应以本仓库根目录为 CMake 源目录重新加载工程
#       （此前 Core 单独作为顶层构建；引入 CLI 后统一由本文件编排）。
#       vcpkg 工具链（Stb / WebP / nlohmann_json）仍由 Core 以 PRIVATE 方式接入，
#       CLI 只经 Core 公共头间接使用，无需感知第三方库。
# ============================================================================
cmake_minimum_required(VERSION 3.28)
project(ImageDiscropper LANGUAGES CXX VERSION 1.0.0)

# ---------------------------------------------------------------------------
# 全局 C++ 约定（子工程亦各自显式设置，双保险）。
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# vcpkg 静态 triplet（x64-windows-static）要求所有目标使用静态 CRT (/MT)，
# 否则与预编译的 Qt/WebP 等库产生 RuntimeLibrary 不匹配链接错误。
if(DEFINED VCPKG_TARGET_TRIPLET AND VCPKG_TARGET_TRIPLET MATCHES "static")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ---------------------------------------------------------------------------
# 构建选项：Release 发布时只产出最终可执行程序——检测到 Release 构建类型即自动
# 关闭测试与演示目标（Core 的 demo/unit_tests、CLI 的 cli_tests 均据此门控）。
# 其余配置（Debug 等）默认开启，可由 -DIDC_BUILD_TESTS=OFF 覆盖。
# 说明：Release 分支用 FORCE 写缓存，使旧版本配置遗留的缓存默认值（恒 ON）也能
#       自动刷新为 OFF；代价是 Release 下不支持 -D 覆盖（如需在 Release 跑测试，
#       改此处判断，或使用 Debug 构建目录）。
# ---------------------------------------------------------------------------
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(IDC_BUILD_TESTS OFF CACHE BOOL "Build test and demo targets (auto-off in Release)" FORCE)
else()
    option(IDC_BUILD_TESTS "Build test and demo targets (auto-off in Release)" ON)
endif()

# 默认把可执行文件与库输出集中到构建目录下的 bin/ lib/，便于示例脚本与测试定位。
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ---------------------------------------------------------------------------
# 子工程引入（顺序即依赖顺序）。
# ---------------------------------------------------------------------------
# Core：纯逻辑静态库 image_discropper_core（终稿 §10.1 Core 层）。
add_subdirectory(ImageDiscropperCore)

# CLI 版本 = 根工程版本；Core 版本由上方 add_subdirectory 经 PARENT_SCOPE 回传。
set(IDC_CLI_VERSION "${PROJECT_VERSION}")

# 在顶层开启测试，使 ctest 能同时发现 Core 与 CLI 注册的用例。
if(IDC_BUILD_TESTS)
    enable_testing()
endif()

# CLI：命令行薄壳 idc（guideline §0 A-0.1，终稿 §10.1 CLI 层）。
add_subdirectory(ImageDiscropperCli)

# GUI：Qt 6 桌面应用 idc_gui（guideline §2，终稿 §10.1 GUI 层）。
# 依赖 vcpkg 的 qtbase（Widgets/Gui/Core）；链接 image_discropper_core 静态库。
# 若当前环境未安装 Qt6，可注释本行以仅构建 Core 与 CLI。
add_subdirectory(ImageDiscropperGui)
