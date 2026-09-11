# ============================================================================
# 文件：CMakeLists.txt
# 作用：ImageDiscropperCore 工程顶层构建脚本。定义 C++ 标准、Core 静态库目标、
#       演示可执行程序与极简自测程序。
# 架构定位：本库即终稿 §10.1 所述 Core 层（纯逻辑、无 GUI 依赖），
#           供上层 CLI / GUI 以静态库方式引用。
# 分块依据：源文件按功能模块分组（core / geometry / processing / annotation /
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

# processing：像素处理算法（颜色/通道、几何变换）
set(IDC_PROCESSING_SRC
    src/processing/color_ops.cpp
    src/processing/geometric_ops.cpp
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

# engine：Grid-Selection-Emit 引擎接口骨架（终稿 §2 / §10.2），当前为数据结构 + 桩实现
set(IDC_ENGINE_SRC
    src/engine/cut_line.cpp
    src/engine/grid.cpp
    src/engine/selection.cpp
    src/engine/sequence.cpp
    src/engine/composition.cpp
    src/engine/engine.cpp
)

add_library(image_discropper_core STATIC
    ${IDC_CORE_SRC}
    ${IDC_GEOMETRY_SRC}
    ${IDC_PROCESSING_SRC}
    ${IDC_ANNOTATION_SRC}
    ${IDC_PREPROCESS_SRC}
    ${IDC_HISTORY_SRC}
    ${IDC_ENGINE_SRC}
)

target_include_directories(image_discropper_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# ---------------------------------------------------------------------------
# 演示程序与自测程序
# ---------------------------------------------------------------------------
add_executable(demo examples/demo_main.cpp)
target_link_libraries(demo PRIVATE image_discropper_core)

add_executable(unit_tests tests/test_main.cpp)
target_link_libraries(unit_tests PRIVATE image_discropper_core)

enable_testing()
add_test(NAME unit_tests COMMAND unit_tests)
