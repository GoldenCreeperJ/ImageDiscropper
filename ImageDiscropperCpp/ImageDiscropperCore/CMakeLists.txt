# ============================================================================
# 文件：CMakeLists.txt
# 作用：ImageDiscropperCore 工程顶层构建脚本。定义 C++ 标准、Core 静态库目标、
#       演示可执行程序与极简自测程序。
# 架构定位：Core 层（纯逻辑、无 GUI 依赖），供上层 CLI / GUI 以静态库方式引用。
# 分块依据：源文件按功能模块分组（core / io / i18n / geometry / annotation /
#           preprocess / generate / canvas / engine），与 include、src 目录一一对应。
# ============================================================================
cmake_minimum_required(VERSION 3.28)

# 版本单一来源：作为子工程时由根 CMakeLists 透传 IDC_CORE_VERSION（发版只改根 project()）；
# 独立配置本子目录时回退占位，避免空 project VERSION。
if(NOT DEFINED IDC_CORE_VERSION)
    set(IDC_CORE_VERSION "1.0.3")   # 独立构建回退（正常发版走根工程，勿在此维护版本）
endif()
project(ImageDiscropperCore LANGUAGES CXX VERSION ${IDC_CORE_VERSION})

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W3 /utf-8)
else()
    add_compile_options(-Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# 第三方依赖（均由 vcpkg 提供，且全部以 PRIVATE 方式接入——不经公共头暴露给下游）：
#   · stb           header-only 图像编码库，仅 src/io/image_io.cpp 内含 <stb_image_write.h> 并定义实现宏。
#   · libwebp       WebP 编码库，仅 src/io/image_io.cpp 内含 <webp/encode.h>/<webp/decode.h>（补齐 stb 不支持的 WebP）。
#   · nlohmann_json 单头 JSON 库，仅配置 / 标注的私有实现内含 <nlohmann/json.hpp>：
#     engine_config_json.cpp（codec）、schema_validate.cpp、percent_coords.cpp（居 src/engine/config_json/，
#     经私有内部头 src/json_common.h / engine_config_json_internal.h 共用助手）与
#     annotation_json.cpp / annotation_json_parse.cpp（经 src/json_common.h、annotation_json_internal.h）。
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

# io：图像编解码（stb + libwebp），与 path_utf8 平级的基础 IO 能力
set(IDC_IO_SRC
        src/io/image_io.cpp
)

# i18n：三层共用的进程级语言状态内核（零 Qt）+ Core 自己的英文表
#       （报因与图案名属 Core 文案，三层共用一份；GUI / CLI 的表仍住各自层，见其 src/i18n/lang_en.cpp）。
#         加一门语言 = 各层新增一份 lang_xx.cpp + 在该层 installCatalogs() 登记一行 +
#         include/i18n/core_i18n.h 补一个语言码，重编即可；无构建期工具链，zh-CN 不建表（走回退）。
set(IDC_CORE_I18N_SRC
        src/i18n/core_i18n.cpp
        src/i18n/lang_en.cpp
)

# geometry：标注图形几何（ShapeType / Shape / Path）
set(IDC_GEOMETRY_SRC
        src/geometry/shapes.cpp
        src/geometry/path.cpp
)

# annotation：标注集合（FR-1.3，纯数据 + zOrder + 光栅合成；编辑会话态属 GUI AnnotationEditor）
#             + 标注 ↔ JSON 序列化（SPEC §7 annotations 段，写侧 annotation_json.cpp / 读侧 annotation_json_parse.cpp）
set(IDC_ANNOTATION_SRC
        src/annotation/shape_factory.cpp
        src/annotation/rasterizer.cpp
        src/annotation/annotation_set.cpp
        src/annotation/annotation_json.cpp
        src/annotation/annotation_json_parse.cpp
        src/annotation/view_transform.cpp
)

# preprocess：切割前的基础图像处理流水线（FR-1）；pixel_kernels 为私有核（src/preprocess/pixel_kernels.h，不入 include/）
set(IDC_PREPROCESS_SRC
        src/preprocess/preprocess_config.cpp
        src/preprocess/preprocess_pipeline.cpp
        src/preprocess/pixel_kernels.cpp
)

# generate：程序化图案生成（「新建画布」画布内容工厂；纯像素生成，无 GUI 依赖）
set(IDC_GENERATE_SRC
        src/generate/pattern.cpp
)

# canvas：多层画布数据模型与合成（ImageLayer/composeCanvas，纯像素零 GUI）
set(IDC_CANVAS_SRC
        src/canvas/layer.cpp
)

# engine：Grid-Selection-Export 统一引擎（SPEC §1）——切割线 / 网格 / 切分 /
#         选择极性 / 排序 / 合成 / 导出 / JSON 配置 / 顶层编排。
set(IDC_ENGINE_SRC
        src/engine/cut_line.cpp
        src/engine/grid.cpp
        src/engine/split.cpp
        src/engine/selection.cpp
        src/engine/sequence.cpp
        src/engine/composition.cpp
        src/engine/export.cpp
        src/engine/config_json/schema_validate.cpp   # §7 schema 严格校验（读侧守卫；validateSchema / validateLayerSeg）
        src/engine/config_json/percent_coords.cpp    # 百分比坐标读侧归一化 + 保存侧回写（resolvePercentCoords / applyPercentHints）
        src/engine/config_json/engine_config_json.cpp
        src/engine/engine.cpp
)

add_library(image_discropper_core STATIC
    ${IDC_CORE_SRC}
    ${IDC_IO_SRC}
    ${IDC_CORE_I18N_SRC}
    ${IDC_GEOMETRY_SRC}
    ${IDC_ANNOTATION_SRC}
    ${IDC_PREPROCESS_SRC}
    ${IDC_GENERATE_SRC}
    ${IDC_CANVAS_SRC}
    ${IDC_ENGINE_SRC}
)

# PIC：静态库对象需可重定位——Android 上 Qt 把应用链接为共享模块（libidc_gui_*.so），
# 无 -fPIC 的对象在 AArch64 上无法重定位（ld.lld: relocation R_AARCH64_* cannot be used...）。
# NDK 工具链默认会开 PIC，但本工程工具链是 vcpkg.cmake（无该默认，静态库目标 PIC 默认关）。
# 桌面平台链接进可执行文件，PIC 无额外成本；MSVC 无 -fPIC 概念，忽略该属性。
set_target_properties(image_discropper_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(image_discropper_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# stb 头文件路径（PRIVATE）：仅 image_io.cpp 编译时需要，下游引用本库无需感知 stb。
target_include_directories(image_discropper_core PRIVATE ${Stb_INCLUDE_DIR})

# libwebp（WebP 编码）+ nlohmann/json（配置存取）以 PRIVATE 链接：仅本库 .cpp 内部使用，
# 二者的头文件路径与符号均不经公共头暴露，下游链接 image_discropper_core 无需感知。
target_link_libraries(image_discropper_core PRIVATE WebP::webp nlohmann_json::nlohmann_json)

# ---------------------------------------------------------------------------
# 演示程序与自测程序（仅在 IDC_BUILD_TESTS=ON 时构建，Release 发布可关闭）
# ---------------------------------------------------------------------------
if(IDC_BUILD_TESTS)
    add_executable(demo examples/demo_main.cpp)
    target_link_libraries(demo PRIVATE image_discropper_core)

    add_executable(unit_tests
            tests/test_main.cpp
            tests/test_engine_pipeline.cpp
            tests/test_engine_export.cpp
            tests/test_engine_boundary.cpp
            tests/test_engine_grid_axis.cpp  # L3 网格单轴三态（分割数 / 比例）：取整律 / 混合轴 / 报因 / JSON 双形态
            tests/test_engine_config_json.cpp
            tests/test_i18n_core.cpp        # i18n 机制内核（零 Qt）：表登记 / 语言码归一 / 缺译回退 / 占位符替换断言
            tests/test_i18n_catalog.cpp     # i18n 目录对账（零 Qt）：孤儿键 / 重复键 / 跨层撞键 / 契约组越界 / %N 占位符一致 + 覆盖率
            # 跨模块直编：GUI 的标注编辑会话（零 Qt 依赖）——供会话语义（撤销/重做/选中跟随）回归。
            ../ImageDiscropperGui/src/model/annotation_editor.cpp
            # 跨模块直编：GUI 的配置资产助手（零 Qt 依赖）——供查找链/解码注入/打包回归。
            ../ImageDiscropperGui/src/model/config_assets.cpp
            # 跨模块直编：GUI 的预处理会话（零 Qt 依赖）——供 geo→color 不变式/归一化/baseDirty/签名回归。
            ../ImageDiscropperGui/src/model/preprocess_session.cpp
            # 跨模块直编：GUI 的图层会话库（零 Qt 依赖）——供每层会话路由/物化签名比对/按 id 生命周期回收回归。
            ../ImageDiscropperGui/src/model/layer_session_store.cpp
    )
    target_link_libraries(unit_tests PRIVATE image_discropper_core)
    # 目录对账要同时读三层的常量层头与译文表，故注入源码根（`/..` 在子工程与独立构建下都是仓库内根）。
    get_filename_component(IDC_I18N_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
    target_compile_definitions(unit_tests PRIVATE IDC_TEXTS_SOURCE_ROOT="${IDC_I18N_ROOT}")
    # 仅为直编 annotation_editor.cpp 与 test_main 引 model/annotation_editor.h 开放 GUI include 根。
    target_include_directories(unit_tests PRIVATE ../ImageDiscropperGui/include)

    add_test(NAME unit_tests COMMAND unit_tests)
endif()
