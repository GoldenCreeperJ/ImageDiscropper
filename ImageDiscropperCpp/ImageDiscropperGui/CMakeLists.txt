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

# ---------------------------------------------------------------------------
# Android 工具链方言补齐（集中于此，勿再散落）：
# Qt 的安卓宏默认下列变量由 NDK 自带工具链文件设置，而本工程工具链是 vcpkg.cmake
# ——两个设置者都不在场，缺省时 androiddeployqt 拿空值拼路径、APK 打包失败。
# 各项均镜像 NDK 工具链同款逻辑补齐，且必须在 find_package(Qt6) 之前：
#   ① ANDROID_NDK_HOST_SYSTEM_NAME —— 部署设置 json 的 ndk-host 字段与 prebuilt
#      目录探测；缺省 → prebuilt//bin/llvm-readobj 不存在、libc++_shared.so 找不到。
#   ② CMAKE_SYSROOT —— Qt 安卓宏 stdcpp-path 取 ${CMAKE_SYSROOT}/usr/lib/；
#      CMake 官方模块检测到统一 Clang 工具链时主动跳过 sysroot 选择
#      （改走 CMAKE_FIND_ROOT_PATH）；缺省 → stdcpp-path 写进 json 为
#      /usr/lib//aarch64-linux-android/...。
#   ③ ANDROID_PLATFORM —— 唯一消费方是 Qt6CoreConfigExtras 的**加载门控**
#      （if(ANDROID_PLATFORM) 才 include 整个 Qt6AndroidMacros），值本身无人读；
#      按 CMAKE_SYSTEM_VERSION 回填即可——预设只保留 CMake 核心方言，不双写 API 版本。
# ---------------------------------------------------------------------------
if(ANDROID)
    if(NOT DEFINED ANDROID_NDK_HOST_SYSTEM_NAME)
        if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
            set(ANDROID_NDK_HOST_SYSTEM_NAME "windows-x86_64")
        elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
            set(ANDROID_NDK_HOST_SYSTEM_NAME "darwin-x86_64")
        elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
            set(ANDROID_NDK_HOST_SYSTEM_NAME "linux-x86_64")
        endif()
    endif ()
    if(NOT CMAKE_SYSROOT)
        set(CMAKE_SYSROOT "${CMAKE_ANDROID_NDK}/toolchains/llvm/prebuilt/${ANDROID_NDK_HOST_SYSTEM_NAME}/sysroot")
    endif()
    if(NOT ANDROID_PLATFORM)
        set(ANDROID_PLATFORM "${CMAKE_SYSTEM_VERSION}")
    endif()
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

# canvas：QGraphicsView/Scene 画布，图层化渲染底图/遮罩/切割线/网格线/选区/标注矢量叠加；
#         交互经输入适配器分层（GuideLine 阶段 2/3）：ICanvasInputAdapter 接口 + 桌面实现 DesktopInputAdapter
#         + 触控实现 TouchInputAdapter（阶段 3，Android 构造时换装，桌面路径零改动）。
set(IDC_GUI_CANVAS_SRC
        include/canvas/z_order.h
        include/canvas/canvas_input_adapter.h
        include/canvas/canvas_desktop_input_adapter.h
        include/canvas/canvas_touch_input_adapter.h
        include/canvas/canvas_scene.h
        include/canvas/canvas_view.h
        include/canvas/mask_layer.h
        include/canvas/grid_layer.h
        include/canvas/cell_picker_item.h
        include/canvas/selection_rect_item.h
        include/canvas/annotation_item.h
        src/canvas/canvas_desktop_input_adapter.cpp
        src/canvas/canvas_touch_input_adapter.cpp
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
        include/panels/accordion_panel.h
        include/panels/left_panel.h
        include/panels/param_panel.h
        include/panels/export_panel.h
        include/panels/image_panel.h
        include/panels/tool_panel.h
        include/panels/layer_panel.h
        include/panels/annotation_prop_panel.h
        src/panels/accordion_panel.cpp
        src/panels/left_panel.cpp
        src/panels/param_panel.cpp
        src/panels/export_panel.cpp
        src/panels/image_panel.cpp
        src/panels/tool_panel.cpp
        src/panels/layer_panel.cpp
        src/panels/annotation_prop_panel.cpp
)

# app：主窗口装配壳 + 四个控制器（预览/预处理/标注/历史）+ 状态栏 + 装配建造者；
#      程序入口 main.cpp 不入 common（两产物各自编译一份，按宏选壳）。
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
)

# mobile：移动端形态（GuideLine 阶段 3，与 app/ 并列）——MobileShell 壳 + 无键盘补偿控件；
#         复用 model/canvas/panels 既有件，零复刻引擎/编排逻辑（SPEC §8.3 行为同构）。
#         是否编入由 IDC_GUI_MOBILE 配置项控制（见下）。
set(IDC_GUI_MOBILE_SRC
        include/mobile/mobile_shell.h
        include/mobile/mobile_shell_ui.h
        include/mobile/mobile_step_pad.h
        src/mobile/mobile_shell.cpp
        src/mobile/mobile_shell_ui.cpp
        src/mobile/mobile_step_pad.cpp
)

# ---------------------------------------------------------------------------
# 目标编排（双可执行）：
#   · idc_gui_common（OBJECT 库）：除程序入口外的全部共享源；include 根、Qt/Core 链接
#     与版本宏经 PUBLIC 传递给各可执行目标（CMake 3.12+ 直接 link OBJECT 库即继承对象与要求）。
#   · 桌面产出**两个可执行**（同一 common，main.cpp 各编一份）：
#       - idc_gui：纯桌面产物，不含任何移动形态代码（无 --mobile 选项）；
#       - idc_gui_mobile：移动形态产物（受 IDC_GUI_MOBILE 控制，默认 ON）——编译宏
#         IDC_GUI_HAS_MOBILE + IDC_GUI_FORCE_MOBILE，双击即移动骨架，无需命令行开关。
#   · Android/iOS 构建只出**单目标 idc_gui**（移动形态是唯一形态，common+mobile 直接合体，
#     不读 IDC_GUI_MOBILE 开关）：既有 CD/打包脚本按 idc_gui.* 文件名取物，零改动。
#     双产物分支下 idc_gui 在 Android 上仍叫 idc_gui，阶段 1 的 APK 链无需适配。
#   注：触控输入适配器（canvas/TouchInputAdapter）属阶段 2/3 适配器分层，始终在 common 中
#   参与编译（仅 Android 运行时换装），不随本开关剔除。
# ---------------------------------------------------------------------------
option(IDC_GUI_MOBILE "桌面构建额外产出移动形态可执行 idc_gui_mobile（Android/iOS 不读本开关，恒为单目标移动形态）" ON)

add_library(idc_gui_common OBJECT
        ${IDC_GUI_UTIL_SRC}
        ${IDC_GUI_MODEL_SRC}
        ${IDC_GUI_CANVAS_SRC}
        ${IDC_GUI_PANELS_SRC}
        ${IDC_GUI_APP_SRC}
)
target_include_directories(idc_gui_common PUBLIC include)
# 链接：Qt6 Widgets（含 Gui/Core 传递）+ Core 静态库；PUBLIC 传递给每个可执行。
target_link_libraries(idc_gui_common PUBLIC
        Qt6::Widgets
        Qt6::Gui
        Qt6::Core
        image_discropper_core
)
target_compile_definitions(idc_gui_common PUBLIC
        IDC_GUI_VERSION="${IDC_GUI_VERSION}"
        IDC_CORE_VERSION="${IDC_CORE_VERSION}"
)

# 参与构建的可执行目标清单（供链接/bundle 属性/插件部署循环复用）。
set(_idc_gui_exes idc_gui)

if(ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
    # 移动构建：单目标＝桌面 common + 移动壳；选壳由 Q_OS_ANDROID 在 main.cpp 内固定。
    qt_add_executable(idc_gui MANUAL_FINALIZATION WIN32
            src/app/main.cpp
            ${IDC_GUI_MOBILE_SRC}
    )
    target_compile_definitions(idc_gui PRIVATE IDC_GUI_HAS_MOBILE=1)
    # Android 安装界面/桌面图标显示名：默认取目标名 idc_gui，改为产品全名
    #（iOS 忽略该属性；mac 的 bundle 名已由 MACOSX_BUNDLE_* 属性覆盖）。
    # 包名：默认是 Qt 模板占位 org.qtproject.example.<目标名>，正式化
    #（与 mac bundle 标识符 com.imagediscropper.gui 一致；改包名后旧安装需先卸载）。
    set_target_properties(idc_gui PROPERTIES
        QT_ANDROID_APP_NAME "ImageDiscropper"
        QT_ANDROID_PACKAGE_NAME "com.imagediscropper.gui"
    )
else()
    # WIN32：按 GUI 子系统链接（MSVC /SUBSYSTEM:WINDOWS），双击启动时不弹出控制台窗口；
    # 入口点 WinMain 由下方 qt_finalize_target 链接 Qt6::EntryPointImplementation 提供。
    qt_add_executable(idc_gui MANUAL_FINALIZATION WIN32 src/app/main.cpp)
    if(IDC_GUI_MOBILE)
        qt_add_executable(idc_gui_mobile MANUAL_FINALIZATION WIN32
                src/app/main.cpp
                ${IDC_GUI_MOBILE_SRC}
        )
        target_compile_definitions(idc_gui_mobile PRIVATE
                IDC_GUI_HAS_MOBILE=1
                IDC_GUI_FORCE_MOBILE=1   # 双击即移动骨架（不走 --mobile）。
        )
        list(APPEND _idc_gui_exes idc_gui_mobile)
    endif()
endif()

foreach(_idc_exe ${_idc_gui_exes})
    target_link_libraries(${_idc_exe} PRIVATE idc_gui_common)
endforeach()

# macOS 发布形态：各可执行以 .app 捆绑包输出（CD 打包为 zip 分发，按名取 idc_gui.app）。
# 非 Apple 平台忽略该属性；idc_gui_mobile 同样 bundle（mac 预览免弹终端窗），
# bundle 标识符按目标名区分（com.imagediscropper.idc_gui / .idc_gui_mobile），互不冲突。
# 无自定义图标（缺省为系统默认图标，正式分发如需图标后续补 icns）。
foreach(_idc_exe ${_idc_gui_exes})
    set_target_properties(${_idc_exe} PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_BUNDLE_NAME "ImageDiscropper"
            MACOSX_BUNDLE_GUI_IDENTIFIER "com.imagediscropper.${_idc_exe}"
            MACOSX_BUNDLE_BUNDLE_VERSION "${IDC_GUI_VERSION}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${IDC_GUI_VERSION}"
    )
endforeach()

# include 根、版本宏与 Qt/Core 链接均已由 idc_gui_common 的 PUBLIC 要求传递（见上方目标编排）。

# ---------------------------------------------------------------------------
# Qt 运行时插件部署（仅动态链接时需要）：vcpkg 的 VCPKG_APPLOCAL_DEPS 只把 Qt 的 DLL 复制到 exe 旁，
# 不复制 Qt 插件目录。缺 platforms 插件时应用启动即报
# “no Qt platform plugin could be initialized”，故构建后把插件复制到 exe 旁。
# 静态链接时插件已编译进 exe，无需复制。守卫**按目录内容判断**：动态构建的
# Qt6/plugins/platforms 含共享库（.so/.dylib/.dll），静态构建只有 .a 与
# objects-Release/*.cpp.o（静态插件初始化对象）——后者若拷进 .app 会被 codesign
# 拒绝（"code object is not signed at all"）。不依赖 triplet 名（本项目三元组名
# 已无 static 字样，名字匹配不可靠）。Android 同理无需复制：插件随 APK 由
# androiddeployqt（qt_finalize_target 自动挂钩）打包。
# 注意：Linux 的 vcpkg qtbase 即使使用动态 triplet 也构建为静态（无 styles 插件
# 目录）——内容判断下该腿自然跳过，与静态 mac/ios 一致。
# ---------------------------------------------------------------------------
if(NOT ANDROID
        AND DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    file(GLOB _idc_platform_dyn
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/Qt6/plugins/platforms/*${CMAKE_SHARED_LIBRARY_SUFFIX}"
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/Qt6/plugins/platforms/*${CMAKE_SHARED_LIBRARY_SUFFIX}")
    if(_idc_platform_dyn)
        if(CMAKE_BUILD_TYPE AND NOT CMAKE_BUILD_TYPE STREQUAL "")
            # 单配置生成器（Ninja 预设）：构建类型配置期已知，按目录是否存在决定是否拷贝。
            if(CMAKE_BUILD_TYPE STREQUAL "Debug")
                set(_idc_platforms_src "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/Qt6/plugins/platforms")
                set(_idc_styles_src "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/Qt6/plugins/styles")
            else()
                set(_idc_platforms_src "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/Qt6/plugins/platforms")
                set(_idc_styles_src "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/Qt6/plugins/styles")
            endif()
            foreach(_idc_gui_target ${_idc_gui_exes})
                set(_idc_copy_commands)
                if(EXISTS "${_idc_platforms_src}")
                    list(APPEND _idc_copy_commands
                            COMMAND ${CMAKE_COMMAND} -E copy_directory
                            "${_idc_platforms_src}" "$<TARGET_FILE_DIR:${_idc_gui_target}>/platforms")
                endif()
                if(EXISTS "${_idc_styles_src}")
                    list(APPEND _idc_copy_commands
                            COMMAND ${CMAKE_COMMAND} -E copy_directory
                            "${_idc_styles_src}" "$<TARGET_FILE_DIR:${_idc_gui_target}>/styles")
                endif()
                if(_idc_copy_commands)
                    add_custom_command(TARGET ${_idc_gui_target} POST_BUILD ${_idc_copy_commands}
                            COMMENT "Copying Qt plugins (platforms/styles) next to ${_idc_gui_target}"
                            VERBATIM)
                endif()
            endforeach()
        else()
            foreach(_idc_gui_target ${_idc_gui_exes})
                add_custom_command(TARGET ${_idc_gui_target} POST_BUILD
                        COMMAND ${CMAKE_COMMAND} -E copy_directory
                        "$<$<CONFIG:Debug>:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/Qt6/plugins/platforms>$<$<NOT:$<CONFIG:Debug>>:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/Qt6/plugins/platforms>"
                        "$<TARGET_FILE_DIR:${_idc_gui_target}>/platforms"
                        COMMAND ${CMAKE_COMMAND} -E copy_directory
                        "$<$<CONFIG:Debug>:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/Qt6/plugins/styles>$<$<NOT:$<CONFIG:Debug>>:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/Qt6/plugins/styles>"
                        "$<TARGET_FILE_DIR:${_idc_gui_target}>/styles"
                        COMMENT "Copying Qt plugins (platforms/styles) next to ${_idc_gui_target}"
                        VERBATIM)
            endforeach()
        endif()
    endif()
endif()

# Qt 6.3+ 推荐：最终化各可执行目标（处理 WIN32 入口、部署等）。低版本自动跳过。
# 各可执行必须以 qt_add_executable 创建（见目标编排）：Android 的最终化
# 终结器要挂部署设置与 APK 目标，只认 qt_add_executable 留下的模块标记，
# 普通 add_executable 报 "QT_ANDROID_GENERATE_DEPLOYMENT_SETTINGS only works on
# Module targets"。MANUAL_FINALIZATION 关闭 Qt 的自动最终化，本循环是唯一最终化点。
if(COMMAND qt_finalize_target)
    foreach(_idc_exe ${_idc_gui_exes})
        qt_finalize_target(${_idc_exe})
    endforeach()
endif()
