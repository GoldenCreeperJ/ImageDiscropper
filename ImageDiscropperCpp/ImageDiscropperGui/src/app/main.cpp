// ============================================================================
// 文件：app/main.cpp
// 作用：GUI 程序入口。创建 QApplication、装配并显示主窗口 MainWindow，进入事件循环。
// 分块依据：
//   - 入口只负责「Qt 应用生命周期 + 顶层窗口」，不含任何业务逻辑（CONTRIBUTING.md「分层纪律」）；
//     业务编排全在 MainWindow，Core 调用全在 EngineBridge。
// 说明：
//   · 设置应用/组织名，使 QSettings（配置持久化）有稳定归属（两名均为不可译契约，取 texts::ident）。
//   · 语言在 QApplication 构造后、窗口与命令行解析之前装配（译文表登记 + 偏好决议）。
//   · 高 DPI 缩放：Qt 6 默认启用像素密度感知的自动缩放，无需手动设置 AA_* 属性。
//   · 命令行首参若为图像路径，则启动后自动打开，便于从资源管理器关联/拖放启动。
//   · 顶层壳按平台与编译宏选择（双可执行产物）：
//     Android/iOS → MobileShell（画布为主 + 底部工具条/抽屉，SPEC §8.3），单目标 idc_gui；
//     桌面两产物（同一 common，本文件各编一份）——idc_gui 纯桌面（不含移动码、无 --mobile）；
//     idc_gui_mobile 经 IDC_GUI_HAS_MOBILE + IDC_GUI_FORCE_MOBILE 双击即移动骨架（开发预览，
//     无需命令行）。两壳共用同一套接线与业务槽（MobileShell 继承 MainWindow），入口代码同源一致。
//   · 宏由 CMake 目标编排注入（IDC_GUI_MOBILE 开关控制是否产出 idc_gui_mobile）；
//     Android/iOS 构建由 CMake 强制含移动壳（下方 #error 双重兜底）。
// ============================================================================
#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QPalette>
#include <QStyleHints>

#include <memory>

// 静态链接 Qt 时，平台插件不以 DLL/dylib/so 形式加载，需显式导入。
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

#include "app/app_prefs.h"
#include "app/main_window.h"
#include "i18n/gui_i18n.h"
#include "i18n/gui_texts.h"
#include "util/theme.h"
#ifdef IDC_GUI_HAS_MOBILE
#include "mobile/mobile_shell.h"
#endif
#if (defined(Q_OS_ANDROID) || defined(Q_OS_IOS)) && !defined(IDC_GUI_HAS_MOBILE)
#error "Android/iOS 构建必须启用 IDC_GUI_MOBILE（移动形态是唯一形态）"
#endif

int main(int argc, char* argv[]) {
    const QApplication app(argc, argv);   // 入口仅使用静态方法（setApplicationName/setWindowIcon 等）与 const 引用（parser.process/connect context），无需可写实例。
    QApplication::setApplicationName(QString::fromUtf8(idc::gui::texts::ident::kSettingsApp));
    QApplication::setApplicationDisplayName(QStringLiteral("ImageDiscropper"));
    QApplication::setApplicationVersion(QStringLiteral(IDC_GUI_VERSION));
    QApplication::setOrganizationName(QString::fromUtf8(idc::gui::texts::ident::kSettingsOrg));
    // 窗口/任务栏图标（qrc 内嵌 256px；Linux 裸二进制分发的主要载体，Windows 任务栏亦受益）。
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/app_icon.png")));

    // i18n 装配：先登记本层译文表，再按偏好决议当前语言。两者必须早于**任何面对人的文案取用**
    // （命令行帮助描述、顶层窗口构造）。此处只定初始语言；运行期可经外观对话框热切换（选定即
    // 切全局语言 + 原地重放、不重启不重建窗口，见 i18n/gui_i18n.h 与 app/appearance_dialog.cpp）。
    // 表与偏好均无 → setupFromPreference 走「跟随系统」；认不出的语言码自动回退源语言（不报错）。
    idc::gui::i18n::installCatalogs();
    idc::gui::i18n::setupFromPreference(idc::gui::UiPrefs::instance().language());

    // 主题装配：按偏好强制 Fusion + 灌入深/浅调色板（必须早于顶层窗口构造，令窗口以正确风格/palette 建立）。
    // 运行期可经外观对话框即时再应用（选定即 theme::apply + 全量 re-polish，见 app/appearance_dialog.cpp）。
    idc::gui::theme::apply(idc::gui::theme::fromString(idc::gui::UiPrefs::instance().themeMode()));

    // 运行期跟随系统：强制 Fusion + 显式 palette 后，OS 深浅色切换不再由平台 style 自动驱动控件（默认
    // 原生 windows11 style 会），故补一条重放——仅当偏好为 System 且生效深浅**真的翻转**时才 apply，
    // 比对当前 Window 明度与 resolveDark(System) 是否一致，避免个别平台虚发信号导致无谓全量 re-polish 闪烁。
    if (const QStyleHints* hints = QApplication::styleHints()) {
        QObject::connect(hints, &QStyleHints::colorSchemeChanged, &app, [] {
            if (idc::gui::theme::fromString(idc::gui::UiPrefs::instance().themeMode()) != idc::gui::theme::Mode::System)
                return;   // 用户显式选了 Light/Dark：不跟随系统。
            const bool wantDark = idc::gui::theme::resolveDark(idc::gui::theme::Mode::System);
            if (const bool curDark = qApp->palette().color(QPalette::Window).lightness() < 128; wantDark == curDark) return;   // 生效深浅未变：跳过，免闪烁。
            idc::gui::theme::apply(idc::gui::theme::Mode::System);
        });
    }

    // 解析命令行：位置参数视为待打开的图像路径（可选，便于文件关联启动）。
    QCommandLineParser parser;
    parser.setApplicationDescription(IDC_GTR(msg, kCliDescription));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QString::fromUtf8(idc::gui::texts::ident::kCliArgImageName),
                                 IDC_GTR(msg, kCliImageArgHelp),
                                 QString::fromUtf8(idc::gui::texts::ident::kCliArgImageSyntax));
    parser.process(app);

    // 顶层壳：含移动码的构建（Android/iOS 移动构建、idc_gui_mobile 预览产物）恒为
    // MobileShell——双产物结构下无 --mobile 运行时开关（移动与否由构建产物决定，非运行时开关）；
    // 纯桌面 idc_gui 无移动码，恒为桌面形态。两壳堆叠在
    // MainWindow 公共面上（openImageFromPath 同源）。
    // 语言热切换原地只改文本（MainWindow::retranslateUi），不销毁重建窗口——
    // Document/AnnotationBridge/DocHistory 随窗口存活，UI 状态与业务态全程无损。
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
