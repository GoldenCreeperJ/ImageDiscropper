# ============================================================================
# 文件：CMakeLists.txt（本实现根）
# 作用：ImageDiscropper C++ 实现的顶层构建脚本，以「并列多模块」方式纳入
#       Core（纯逻辑静态库）、CLI（命令行薄壳）与 GUI（Qt6 桌面前端）三层结构。
# 分块依据：本文件只做「模块编排 + 全局约定」——统一 C++ 标准、按依赖顺序引入
#       子工程、开放测试注册；各模块的目标、依赖与编译选项由其自身 CMakeLists 定义。
# 说明：Core 必须先于 CLI / GUI 引入——上层链接 image_discropper_core 静态库目标，
#       该目标须在子目录处理前已存在。第三方库（Stb / WebP / nlohmann_json）由 Core
#       以 PRIVATE 方式接入，CLI / GUI 经 Core 公共头间接使用，无需感知。
# ============================================================================
cmake_minimum_required(VERSION 3.28)
project(ImageDiscropper LANGUAGES CXX VERSION 1.0.3)

# ---------------------------------------------------------------------------
# 全局 C++ 约定（子工程亦各自显式设置，双保险）。
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 本项目不使用 C++ 模块：显式关闭模块依赖扫描，避免部分工具链（Android clang）
# 缺失 clang-scan-deps 时扫描步骤失败。
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# vcpkg 静态 triplet 要求全部目标使用静态 CRT (/MT)，否则与预编译的 Qt / WebP
# 产生 RuntimeLibrary 不匹配链接错误（LNK2019 __imp_*）。
# 注：Windows 静态三元组按本项目命名约定为 x64-windows-rel（内容等同社区 x64-windows-static）。
if(DEFINED VCPKG_TARGET_TRIPLET AND (VCPKG_TARGET_TRIPLET MATCHES "static" OR VCPKG_TARGET_TRIPLET STREQUAL "x64-windows-rel"))
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ---------------------------------------------------------------------------
# 构建选项：测试与演示目标（Core 的 demo / unit_tests、CLI 的 cli_tests 均据此门控）。
# 所有构建类型默认开启（CI 即在 Release 下构建并运行测试）；关闭用 -DIDC_BUILD_TESTS=OFF。
# ---------------------------------------------------------------------------
option(IDC_BUILD_TESTS "Build test and demo targets" ON)
if(IDC_BUILD_TESTS)
    enable_testing()
endif()

# 可执行文件与库输出集中到构建目录下的 bin/ lib/，便于示例脚本与测试定位。
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ---------------------------------------------------------------------------
# 版本单一来源：发版只改上方 project() 的 VERSION，向 Core / CLI / GUI 三层透传。
# IDC_CORE_VERSION 默认同产品版本；如需 Core 库版本独立演进，仅改本行。
# 各子工程单独配置时自带占位回退，脱离根也能构建。
# ---------------------------------------------------------------------------
set(IDC_CLI_VERSION  "${PROJECT_VERSION}")
set(IDC_GUI_VERSION  "${PROJECT_VERSION}")
set(IDC_CORE_VERSION "${PROJECT_VERSION}")

# ---------------------------------------------------------------------------
# 子工程引入（顺序即依赖顺序）。
# ---------------------------------------------------------------------------
add_subdirectory(ImageDiscropperCore)
add_subdirectory(ImageDiscropperCli)

# GUI：Qt 6 桌面应用 idc_gui，依赖 vcpkg 的 qtbase（Widgets / Gui / Core）。
# 无需 Qt 的环境可用 -DIDC_BUILD_GUI=OFF 跳过。
option(IDC_BUILD_GUI "Build the Qt 6 GUI (idc_gui)" ON)
if(IDC_BUILD_GUI)
    add_subdirectory(ImageDiscropperGui)
endif()
