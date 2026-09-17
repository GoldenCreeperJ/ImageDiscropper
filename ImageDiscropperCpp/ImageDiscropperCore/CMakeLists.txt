# ============================================================================
# 文件：CMakeLists.txt
# 作用：ImageDiscropperCore 工程顶层构建脚本。定义 C++ 标准、Core 静态库目标、
#       演示可执行程序与极简自测程序。
# 架构定位：本库即终稿 §10.1 所述 Core 层（纯逻辑、无 GUI 依赖），
#           供上层 CLI / GUI 以静态库方式引用。
# 分块依据：源文件按功能模块分组（core / geometry / pixel_ops / annotation /
#           preprocess / history / engine），与 include、src 目录一一对应。
# ============================================================================
cmake_minimum_required(VERSION 3.28)
project(ImageDiscropperCore LANGUAGES CXX VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W3 /utf-8)
else()
    add_compile_options(-Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# 第三方依赖（均由 vcpkg 提供，且全部以 PRIVATE 方式接入——不经公共头暴露给下游）：
#   · stb           header-only 图像编码库，仅 image_io.cpp 内含 <stb_image_write.h> 并定义实现宏。
#   · libwebp       WebP 编码库，仅 image_io.cpp 内含 <webp/encode.h>/<webp/decode.h>（补齐 stb 不支持的 WebP）。
#   · nlohmann_json 单头 JSON 库，仅 engine_config_json.cpp 内含 <nlohmann/json.hpp>（配置文件存取）。
# 三者均只在各自 .cpp 内部使用，公共头不出现其类型，故对下游零依赖扩散（依赖单向）。
# ---------------------------------------------------------------------------
find_package(Stb REQUIRED)
find_package(WebP CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# ---------------------------------------------------------------------------
# 收集所有源文件（按目录分组，便于阅读与后续维护）
# ---------------------------------------------------------------------------
# core：基础数据类型（Color / Point / Image）
set(IDC_CORE_SRC
        src/core/color.cpp
        src/core/point.cpp
        src/core/image.cpp
)

# geometry：标注图形几何（ShapeType / Shape / Path）
set(IDC_GEOMETRY_SRC
        src/geometry/shapes.cpp
        src/geometry/path.cpp
)

# pixel_ops：像素处理算法（颜色/通道、几何变换）
set(IDC_PIXEL_OPS_SRC
        src/pixel_ops/color_ops.cpp
        src/pixel_ops/geometric_ops.cpp
)

# annotation：标注层（FR-1.3），由原 drawing 模块重命名而来
set(IDC_ANNOTATION_SRC
        src/annotation/shape_factory.cpp
        src/annotation/rasterizer.cpp
        src/annotation/annotation_layer.cpp
        src/annotation/view_transform.cpp
)

# preprocess：切割前的基础图像处理流水线（FR-1），由原 operation 模块重命名而来
set(IDC_PREPROCESS_SRC
        src/preprocess/preprocess_config.cpp
        src/preprocess/preprocess_pipeline.cpp
)

# history：撤销 / 重做栈（FR-1.5 / NFR-4）
set(IDC_HISTORY_SRC
        src/history/history_manager.cpp
)

# engine：Grid-Selection-Emit 统一引擎（终稿 §2 / §10.2）——切割线 / 网格 / 切分 /
#         选择极性 / 排序 / 合成 / 导出 / 图像编码(stb+libwebp) / JSON 配置(nlohmann) / 顶层编排。
set(IDC_ENGINE_SRC
        src/engine/cut_line.cpp
        src/engine/grid.cpp
        src/engine/split.cpp
        src/engine/selection.cpp
        src/engine/sequence.cpp
        src/engine/composition.cpp
        src/engine/export.cpp
        src/engine/image_io.cpp
        src/engine/engine_config_json.cpp
        src/engine/engine.cpp
)

add_library(image_discropper_core STATIC
    ${IDC_CORE_SRC}
    ${IDC_GEOMETRY_SRC}
    ${IDC_PIXEL_OPS_SRC}
    ${IDC_ANNOTATION_SRC}
    ${IDC_PREPROCESS_SRC}
    ${IDC_HISTORY_SRC}
    ${IDC_ENGINE_SRC}
)

target_include_directories(image_discropper_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# stb 头文件路径（PRIVATE）：仅 image_io.cpp 编译时需要，下游引用本库无需感知 stb。
target_include_directories(image_discropper_core PRIVATE ${Stb_INCLUDE_DIR})

# libwebp（WebP 编码）+ nlohmann/json（配置存取）以 PRIVATE 链接：仅本库 .cpp 内部使用，
# 二者的头文件路径与符号均不经公共头暴露，下游链接 image_discropper_core 无需感知。
target_link_libraries(image_discropper_core PRIVATE WebP::webp nlohmann_json::nlohmann_json)

# ---------------------------------------------------------------------------
# 演示程序与自测程序
# ---------------------------------------------------------------------------
add_executable(demo examples/demo_main.cpp)
target_link_libraries(demo PRIVATE image_discropper_core)

add_executable(unit_tests
        tests/test_main.cpp
        tests/test_engine_pipeline.cpp
        tests/test_engine_export.cpp
        tests/test_engine_boundary.cpp
)
target_link_libraries(unit_tests PRIVATE image_discropper_core)

enable_testing()
add_test(NAME unit_tests COMMAND unit_tests)

# ---------------------------------------------------------------------------
# 作为子工程被根 CMakeLists 引入时，向父作用域回传 Core 版本号，供 CLI 的
# --version 输出 "idc <version> (core <core-version>)"（guideline §6.2）。
# 独立构建（Core 即顶层）时跳过——此时不存在父作用域。
# ---------------------------------------------------------------------------
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(IDC_CORE_VERSION "${PROJECT_VERSION}" PARENT_SCOPE)
endif()
