# ============================================================================
# 文件：CMakeLists.txt
# 作用：ImageDiscropperGui 工程构建脚本。定义 Qt 6 GUI 可执行目标 idc_gui，
#       并链接 Core 静态库 image_discropper_core。
# 架构定位：GUI 是 Core 的纯消费者——只采集交互、翻译为
#           Core 参数、调用 Core、渲染结果；切割/几何/排序/极性/导出逻辑全在 Core。
# 分块依据：源文件按职责分组（app / model / canvas / panels / util），与本目录
#           的子目录一一对应，避免上帝文件。
# 说明：
#   · Qt 6 由 vcpkg 的 qtbase 提供，仅链接 Widgets / Gui / Core 三个组件；
#     qtbase 的可选驱动目标（Windows 的 DB2/PPS/OCI/Libb2 等）或依赖链
#     （Linux/macOS 上为构建 qtbase 而编译的 sql 驱动等）本 GUI 均不链接。
#   · Core 的第三方依赖（stb / WebP / nlohmann_json）在 Core 侧以 PRIVATE 接入，
#     GUI 仅经 Core 公共头间接使用，无需感知（依赖单向）。
# ============================================================================
cmake_minimum_required(VERSION 3.28)
project(ImageDiscropperGui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译选项与 Core/CLI 保持一致：MSVC 用 /W3 /utf-8（中文注释需 UTF-8），其余用 -Wall -Wextra。
if(MSVC)
    add_compile_options(/W3 /utf-8)
else()
    add_compile_options(-Wall -Wextra)
endif()

# 版本注入（与 CLI 同一形式）：IDC_GUI_VERSION 由根 CMakeLists 提供（产品版本单一来源）；
# IDC_CORE_VERSION 正常由根经 PARENT_SCOPE 回传。独立配置本子目录时给出占位默认，避免空宏。
if(NOT DEFINED IDC_GUI_VERSION)
    set(IDC_GUI_VERSION "1.0.0")
endif()
if(NOT DEFINED IDC_CORE_VERSION)
    set(IDC_CORE_VERSION "1.0.0")
endif()

# Android NDK 宿主平台名：Qt 的安卓宏（部署设置 json 的 ndk-host 字段、prebuilt 目录探测）
# 读 ANDROID_NDK_HOST_SYSTEM_NAME——该变量由 NDK 自带工具链文件设置，本工程工具链是
# vcpkg.cmake 不会设置（与 ANDROID_SDK_ROOT 同类的方言缺口）。此处镜像 NDK 工具链的
# 推导逻辑补齐；缺省时 androiddeployqt 拿空值拼路径（prebuilt//bin/llvm-readobj 不存在、
# libc++_shared.so 找不到），APK 打包步骤失败。
if(ANDROID AND NOT DEFINED ANDROID_NDK_HOST_SYSTEM_NAME)
    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
        set(ANDROID_NDK_HOST_SYSTEM_NAME "windows-x86_64")
    elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
        set(ANDROID_NDK_HOST_SYSTEM_NAME "darwin-x86_64")
    elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
        set(ANDROID_NDK_HOST_SYSTEM_NAME "linux-x86_64")
    endif()
endif()

# NDK 统一 sysroot：Qt 安卓宏的 stdcpp-path 取 ${CMAKE_SYSROOT}/usr/lib/（Qt6AndroidMacros）。
# CMAKE_SYSROOT 本由 NDK 工具链文件设置；CMake 官方模块检测到统一 Clang 工具链时
# 主动跳过 sysroot 选择（改走 CMAKE_FIND_ROOT_PATH），本工程工具链又是 vcpkg.cmake——
# 两头无人设置，stdcpp-path 写进部署 json 为空（/usr/lib//aarch64-linux-android/...），
# androiddeployqt 找不到 libc++_shared.so 打包失败。此处按 NDK 工具链同款路径补齐。
if(ANDROID AND NOT CMAKE_SYSROOT)
    set(CMAKE_SYSROOT "${CMAKE_ANDROID_NDK}/toolchains/llvm/prebuilt/${ANDROID_NDK_HOST_SYSTEM_NAME}/sysroot")
endif()

# ---------------------------------------------------------------------------
# Qt 6（Widgets + QGraphicsView）。AUTOMOC 处理 Q_OBJECT 头文件的元对象编译。
# ---------------------------------------------------------------------------
find_package(Qt6 CONFIG REQUIRED COMPONENTS Widgets Gui Core)

# Qt 6.3+ 标准工程初始化：开启 AUTOMOC/AUTOUIC/AUTORCC（清单基线已锁 Qt 6.11）。
qt_standard_project_setup()

# ---------------------------------------------------------------------------
# 源文件清单（按模块分组）。与 Core/CLI 一致做 include/src 分离：
#   · 头文件在 include/<模块>/（对外可见的类声明边界）；
#   · 实现在 src/<模块>/（.cpp 与各模块 README.md）。
# .h 一并列入目标，确保 AUTOMOC 扫描到 Q_OBJECT 头。
# ---------------------------------------------------------------------------
# util：core::Image 与 Qt 图像类型的适配、降采样预览、标注矢量绘制与导出输出预览渲染（视图关注点，非引擎逻辑）。
set(IDC_GUI_UTIL_SRC
        include/util/annotation_qt_painter.h
        include/util/image_qt_adapter.h
        include/util/output_preview_renderer.h
        include/util/path_qt_adapter.h
        include/util/preview_scaler.h
        src/util/annotation_qt_painter.cpp
        src/util/image_qt_adapter.cpp
        src/util/output_preview_renderer.cpp
        src/util/path_qt_adapter.cpp
        src/util/preview_scaler.cpp
)

# model：会话状态单一真相源 Document 与唯一触碰 Core 的 EngineBridge。
set(IDC_GUI_MODEL_SRC
        include/model/annotation_bridge.h
        include/model/document.h
        include/model/engine_bridge.h
        src/model/annotation_bridge.cpp
        src/model/document.cpp
        src/model/engine_bridge.cpp
)

# canvas：QGraphicsView/Scene 画布，图层化渲染底图/遮罩/切割线/网格线/选区/标注矢量叠加。
set(IDC_GUI_CANVAS_SRC
        include/canvas/z_order.h
        include/canvas/canvas_scene.h
        include/canvas/canvas_view.h
        include/canvas/mask_layer.h
        include/canvas/grid_layer.h
        include/canvas/cell_picker_item.h
        include/canvas/selection_rect_item.h
        include/canvas/annotation_item.h
        src/canvas/canvas_scene.cpp
        src/canvas/canvas_view.cpp
        src/canvas/mask_layer.cpp
        src/canvas/grid_layer.cpp
        src/canvas/cell_picker_item.cpp
        src/canvas/selection_rect_item.cpp
        src/canvas/annotation_item.cpp
)

# panels：左侧模式/极性面板与标注工具/图层面板、右侧参数/导出/图像/标注属性面板。
set(IDC_GUI_PANELS_SRC
        include/panels/left_panel.h
        include/panels/param_panel.h
        include/panels/export_panel.h
        include/panels/image_panel.h
        include/panels/tool_panel.h
        include/panels/layer_panel.h
        include/panels/annotation_prop_panel.h
        src/panels/left_panel.cpp
        src/panels/param_panel.cpp
        src/panels/export_panel.cpp
        src/panels/image_panel.cpp
        src/panels/tool_panel.cpp
        src/panels/layer_panel.cpp
        src/panels/annotation_prop_panel.cpp
)

# app：主窗口装配壳 + 四个控制器（预览/预处理/标注/历史）+ 状态栏 + 装配建造者 + 程序入口。
set(IDC_GUI_APP_SRC
        include/app/annotation_coordinator.h
        include/app/doc_history.h
        include/app/main_window.h
        include/app/main_window_ui.h
        include/app/preprocess_controller.h
        include/app/preview_controller.h
        include/app/status_bar.h
        src/app/annotation_coordinator.cpp
        src/app/doc_history.cpp
        src/app/main_window.cpp
        src/app/main_window_ui.cpp
        src/app/preprocess_controller.cpp
        src/app/preview_controller.cpp
        src/app/status_bar.cpp
        src/app/main.cpp
)

# WIN32：按 GUI 子系统链接（MSVC /SUBSYSTEM:WINDOWS），双击启动时不弹出控制台窗口；
# 入口点 WinMain 由下方 qt_finalize_target 链接 Qt6::EntryPointImplementation 提供。
qt_add_executable(idc_gui MANUAL_FINALIZATION WIN32
        ${IDC_GUI_UTIL_SRC}
        ${IDC_GUI_MODEL_SRC}
        ${IDC_GUI_CANVAS_SRC}
        ${IDC_GUI_PANELS_SRC}
        ${IDC_GUI_APP_SRC}
)

# macOS 发布形态：idc_gui 以 .app 捆绑包输出（CD 打包为 zip 分发）。
# 非 Apple 平台忽略该属性，无需按平台分支；无自定义图标（缺省为系统默认图标，
# 正式分发如需图标后续补 icns）。
set_target_properties(idc_gui PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "ImageDiscropper"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.imagediscropper.gui"
        MACOSX_BUNDLE_BUNDLE_VERSION "${IDC_GUI_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${IDC_GUI_VERSION}"
)

# include 作为包含根，使源码以 "model/document.h"、"canvas/canvas_scene.h" 形式互相引用
# （与 Core 的 include/、CLI 的 include/ 同一约定：包含根下直接是功能分组子目录）。
target_include_directories(idc_gui PRIVATE include)

target_compile_definitions(idc_gui PRIVATE
        IDC_GUI_VERSION="${IDC_GUI_VERSION}"
        IDC_CORE_VERSION="${IDC_CORE_VERSION}"
)

# 链接：Qt6 Widgets（含 Gui/Core 传递）+ Core 静态库。
target_link_libraries(idc_gui PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        image_discropper_core
)

# ---------------------------------------------------------------------------
# Qt 运行时插件部署（仅动态链接时需要）：vcpkg 的 VCPKG_APPLOCAL_DEPS 只把 Qt 的 DLL 复制到 exe 旁，
# 不复制 Qt 插件目录。缺 platforms 插件时应用启动即报
# “no Qt platform plugin could be initialized”，故构建后把插件复制到 exe 旁。
# 静态链接时（VCPKG_TARGET_TRIPLET 含 "static"）插件已编译进 exe，无需复制。
# 注意：Linux 的 vcpkg qtbase 即使使用动态 triplet 也构建为静态，无 styles 插件目录，
# 故单配置生成器在配置期检查目录存在才拷贝（多配置生成器保持生成器表达式写法）。
# ---------------------------------------------------------------------------
if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET
        AND NOT VCPKG_TARGET_TRIPLET MATCHES "static")
    if(CMAKE_BUILD_TYPE AND NOT CMAKE_BUILD_TYPE STREQUAL "")
        # 单配置生成器（Ninja 预设）：构建类型配置期已知，按目录是否存在决定是否拷贝。
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            set(_idc_platforms_src "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/Qt6/plugins/platforms")
            set(_idc_styles_src "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/Qt6/plugins/styles")
        else()
            set(_idc_platforms_src "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/Qt6/plugins/platforms")
            set(_idc_styles_src "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/Qt6/plugins/styles")
        endif()
        set(_idc_copy_commands)
        if(EXISTS "${_idc_platforms_src}")
            list(APPEND _idc_copy_commands
                    COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${_idc_platforms_src}" "$<TARGET_FILE_DIR:idc_gui>/platforms")
        endif()
        if(EXISTS "${_idc_styles_src}")
            list(APPEND _idc_copy_commands
                    COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${_idc_styles_src}" "$<TARGET_FILE_DIR:idc_gui>/styles")
        endif()
        if(_idc_copy_commands)
            add_custom_command(TARGET idc_gui POST_BUILD ${_idc_copy_commands}
                    COMMENT "Copying Qt plugins (platforms/styles) next to idc_gui"
                    VERBATIM)
        endif()
    else()
        add_custom_command(TARGET idc_gui POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                "$<$<CONFIG:Debug>:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/Qt6/plugins/platforms>$<$<NOT:$<CONFIG:Debug>>:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/Qt6/plugins/platforms>"
                "$<TARGET_FILE_DIR:idc_gui>/platforms"
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                "$<$<CONFIG:Debug>:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/Qt6/plugins/styles>$<$<NOT:$<CONFIG:Debug>>:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/Qt6/plugins/styles>"
                "$<TARGET_FILE_DIR:idc_gui>/styles"
                COMMENT "Copying Qt plugins (platforms/styles) next to idc_gui"
                VERBATIM)
    endif()
endif()

# Qt 6.3+ 推荐：最终化目标（处理 WIN32 入口、部署等）。低版本自动跳过。
if(COMMAND qt_finalize_target)
    qt_finalize_target(idc_gui)
endif()
