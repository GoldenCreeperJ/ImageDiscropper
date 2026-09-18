// ============================================================================
// 文件：app/main.cpp
// 作用：GUI 程序入口。创建 QApplication、装配并显示主窗口 MainWindow，进入事件循环。
// 分块依据：
//   - 入口只负责「Qt 应用生命周期 + 顶层窗口」，不含任何业务逻辑（A-0.1）；
//     业务编排全在 MainWindow，Core 调用全在 EngineBridge。
// 说明：
//   · 设置应用/组织名，使 QSettings（后续阶段的配置持久化）有稳定归属。
//   · 高 DPI 缩放：Qt 6 默认启用像素密度感知的自动缩放，无需手动设置 AA_* 属性。
//   · 命令行首参若为图像路径，则启动后自动打开，便于从资源管理器关联/拖放启动。
// ============================================================================
#include <QApplication>
#include <QCommandLineParser>

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

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
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

    idc::gui::MainWindow window;
    window.show();

    // 若命令行带图像路径，窗口显示后自动打开（复用菜单/工具栏同一入口逻辑）。
    const QStringList args = parser.positionalArguments();
    if (!args.isEmpty()) {
        window.openImageFromPath(args.first());
    }

    return QApplication::exec();
}
