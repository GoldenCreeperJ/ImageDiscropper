// ============================================================================
// 文件：app/main.cpp
// 作用：GUI 程序入口。创建 QApplication、装配并显示主窗口 MainWindow，进入事件循环。
// 分块依据：
//   - 入口只负责「Qt 应用生命周期 + 顶层窗口」，不含任何业务逻辑（CONTRIBUTING.md「分层纪律」）；
//     业务编排全在 MainWindow，Core 调用全在 EngineBridge。
// 说明：
//   · 设置应用/组织名，使 QSettings（后续阶段的配置持久化）有稳定归属。
//   · 高 DPI 缩放：Qt 6 默认启用像素密度感知的自动缩放，无需手动设置 AA_* 属性。
//   · 命令行首参若为图像路径，则启动后自动打开，便于从资源管理器关联/拖放启动。
//   · 顶层壳按平台与编译宏选择（GuideLine 阶段 3，双可执行产物）：
//     Android/iOS → MobileShell（画布为主 + 底部工具条/抽屉，SPEC §8.3），单目标 idc_gui；
//     桌面两产物（同一 common，本文件各编一份）——idc_gui 纯桌面（不含移动码、无 --mobile）；
//     idc_gui_mobile 经 IDC_GUI_HAS_MOBILE + IDC_GUI_FORCE_MOBILE 双击即移动骨架（开发预览，
//     无需命令行）。两壳共用同一套接线与业务槽（MobileShell 继承 MainWindow），入口代码形状不变。
//   · 宏由 CMake 目标编排注入（IDC_GUI_MOBILE 开关控制是否产出 idc_gui_mobile）；
//     Android/iOS 构建由 CMake 强制含移动壳（下方 #error 双重兜底）。
// ============================================================================
#include <QApplication>
#include <QCommandLineParser>

#include <memory>

// 静态链接 Qt 时，平台插件不再以 DLL/dylib/so 形式加载，需显式导入。
#ifdef QT_STATIC
#include <QtPlugin>
#if defined(Q_OS_WIN)
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)
#elif defined(Q_OS_MACOS)
Q_IMPORT_PLUGIN(QCocoaIntegrationPlugin)
#elif defined(Q_OS_LINUX)
Q_IMPORT_PLUGIN(QXcbIntegrationPlugin)
#endif
#endif

#include "app/main_window.h"
#ifdef IDC_GUI_HAS_MOBILE
#include "mobile/mobile_shell.h"
#endif
#if (defined(Q_OS_ANDROID) || defined(Q_OS_IOS)) && !defined(IDC_GUI_HAS_MOBILE)
#error "Android/iOS 构建必须启用 IDC_GUI_MOBILE（移动形态是唯一形态）"
#endif

int main(int argc, char* argv[]) {
    const QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ImageDiscropperGui"));
    QApplication::setApplicationDisplayName(QStringLiteral("ImageDiscropper"));
    QApplication::setApplicationVersion(QStringLiteral(IDC_GUI_VERSION));
    QApplication::setOrganizationName(QStringLiteral("ImageDiscropper"));

    // 解析命令行：位置参数视为待打开的图像路径（可选，便于文件关联启动）。
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("ImageDiscropper 图像切割 GUI"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("image"),
                                 QStringLiteral("可选：启动时自动打开的图像路径。"),
                                 QStringLiteral("[image]"));
    parser.process(app);

    // 顶层壳：含移动码的构建（Android/iOS 移动构建、idc_gui_mobile 预览产物）恒为
    // MobileShell——双产物结构下无 --mobile 运行时开关（旧单 exe 时代的开关已随
    // 该结构废弃，删除）；纯桌面 idc_gui 无移动码，恒为桌面形态。两壳堆叠在
    // MainWindow 公共面上（openImageFromPath 同源）。
    std::unique_ptr<idc::gui::MainWindow> window;
#ifdef IDC_GUI_HAS_MOBILE
    window = std::make_unique<idc::gui::MobileShell>();
#else
    window = std::make_unique<idc::gui::MainWindow>();
#endif
    window->show();

    // 若命令行带图像路径，窗口显示后自动打开（复用菜单/工具栏同一入口逻辑）。
    if (const QStringList args = parser.positionalArguments(); !args.isEmpty()) {
        window->openImageFromPath(args.first());
    }

    return QApplication::exec();
}
