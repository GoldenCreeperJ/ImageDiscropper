# ============================================================================
# 文件：CMakeLists.txt（仓库根）
# 作用：ImageDiscropper 工程的顶层构建脚本。以「并列多模块」方式同时纳入
#       Core（纯逻辑静态库）、CLI（命令行薄壳）与 GUI（Qt6 桌面前端），即 Core / CLI / GUI
#       三层结构（见本目录 README「工程组成」）。
# 架构定位：本文件只做「模块编排 + 全局约定」——统一 C++ 标准、按依赖顺序引入子工程、
#           开放测试注册；各模块的具体目标、依赖与编译选项由其自身 CMakeLists 定义。
# 分块依据：
#   - Core 必须先于 CLI 引入：CLI 链接 image_discropper_core 静态库目标，
#     该目标须在 CLI 子目录处理前已存在（CMake 目标按引入顺序对后续兄弟目录可见）。
#   - Core 以 PARENT_SCOPE 向本作用域回传 IDC_CORE_VERSION，供 CLI 的 --version
#     打印 "(core <core-version>)"。
# 说明：使用 CLion / 命令行时，应以本目录为 CMake 源目录加载工程（CLion 会识别
#       CMakePresets.json 中的预设）。vcpkg 工具链（Stb / WebP / nlohmann_json）
#       由 Core 以 PRIVATE 方式接入，CLI/GUI 只经 Core 公共头间接使用，无需感知第三方库。
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
# 构建选项：测试与演示目标（Core 的 demo/unit_tests、CLI 的 cli_tests 均据此门控）。
# 所有构建类型默认开启——CI 即在 Release 下构建并运行测试（与发布产物同构）；
# 如需关闭：-DIDC_BUILD_TESTS=OFF。
# ---------------------------------------------------------------------------
option(IDC_BUILD_TESTS "Build test and demo targets" ON)

# 默认把可执行文件与库输出集中到构建目录下的 bin/ lib/，便于示例脚本与测试定位。
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ---------------------------------------------------------------------------
# 子工程引入（顺序即依赖顺序）。
# ---------------------------------------------------------------------------
# Core：纯逻辑静态库 image_discropper_core。
add_subdirectory(ImageDiscropperCore)

# CLI / GUI 版本 = 根工程版本（产品版本单一来源：根 project() 一处维护，
# 发版时只改这里）；Core 版本由上方 add_subdirectory 经 PARENT_SCOPE 回传（库版本独立）。
set(IDC_CLI_VERSION "${PROJECT_VERSION}")
set(IDC_GUI_VERSION "${PROJECT_VERSION}")

# 在顶层开启测试，使 ctest 能同时发现 Core 与 CLI 注册的用例。
if(IDC_BUILD_TESTS)
    enable_testing()
endif()

# CLI：命令行薄壳 idc。
add_subdirectory(ImageDiscropperCli)

# GUI：Qt 6 桌面应用 idc_gui。
# 依赖 vcpkg 的 qtbase（Widgets/Gui/Core）；链接 image_discropper_core 静态库。
# 无需 Qt 的环境（或只想构建 Core/CLI）可用 -DIDC_BUILD_GUI=OFF 跳过。
option(IDC_BUILD_GUI "Build the Qt 6 GUI (idc_gui)" ON)
if(IDC_BUILD_GUI)
    add_subdirectory(ImageDiscropperGui)
endif()
