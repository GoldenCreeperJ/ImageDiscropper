// ============================================================================
// 文件：app/menus/main_window_menus.cpp
// 作用：实现 MainWindowUi::buildMenus 的「文件 / 编辑」域与总装配，并提供菜单装配绑定助手
//       （bindAddAction / bindAddMenu，声明见 main_window_menus_internal.h，四个菜单 TU 共用）。
// 分块依据：buildMenus 按菜单域拆为四个 TU——本文件（文件/编辑 + 总装配 + 共享助手）、
//       main_window_menus_mode.cpp（模式/预处理）、main_window_menus_canvas.cpp（图片/标注）、
//       main_window_menus_view.cpp（视图/帮助，含帮助/关于正文）；与中央区/工具栏/状态栏装配及
//       syncUi 无共享状态，独立成件使 main_window_ui.cpp 保持「装配 + 同步」的单一关注点
//       （CONTRIBUTING.md「分层纪律」；严禁上帝文件）。
// 说明：所有 connect 的接收者/上下文均为 w（friend 权限连接私有槽/控制器成员）；lambda 捕获 w 而非 this。
// ============================================================================
#include "app/main_window_ui.h"

#include <QAction>
#include <QActionGroup>
#include <QFileInfo>
#include <QMenu>
#include <QMenuBar>
#include <QWidget>

#include "app/controllers/canvas_coordinator.h"
#include "app/controllers/config_controller.h"
#include "app/controllers/export_controller.h"
#include "app/controllers/session_controller.h"
#include "app/keybindings.h"
#include "app/app_prefs.h"
#include "app/main_window.h"
#include "panels/export_panel.h"
#include "i18n/gui_texts.h"
#include "i18n/gui_i18n.h"
#include "i18n/retranslate.h"

#include "main_window_menus_internal.h" // bindAddAction / bindAddMenu 声明（四个菜单 TU 共用）

namespace idc::gui {

// 菜单装配绑定助手（包 MainWindow::retranslator_）：把一次 addAction/addMenu 变为可重放登记，
// 收敛逐站点改动。文字入参一律用 IDC_GT(...) 产出的 TextRef（constexpr、key+fallback）。
// 声明见 main_window_menus_internal.h（四个菜单 TU 共用），定义在本 TU 单份。
QAction* bindAddAction(i18n::Retranslator& r, QMenu* m, const i18n::TextRef& ref) {
    QAction* a = m->addAction(i18n::qs(ref));
    r.add([a, ref] { a->setText(i18n::qs(ref)); });
    return a;
}
QAction* bindAddAction(i18n::Retranslator& r, QActionGroup* g, const i18n::TextRef& ref) {
    QAction* a = g->addAction(i18n::qs(ref));
    r.add([a, ref] { a->setText(i18n::qs(ref)); });
    return a;
}
QMenu* bindAddMenu(i18n::Retranslator& r, QMenu* parent, const i18n::TextRef& ref) {
    QMenu* m = parent->addMenu(i18n::qs(ref));
    r.add([m, ref] { m->setTitle(i18n::qs(ref)); });
    return m;
}
QMenu* bindAddMenu(i18n::Retranslator& r, QMenuBar* parent, const i18n::TextRef& ref) {
    QMenu* m = parent->addMenu(i18n::qs(ref));
    r.add([m, ref] { m->setTitle(i18n::qs(ref)); });
    return m;
}

// 文件菜单域。
void MainWindowUi::buildFileMenu(MainWindow* w, QMenuBar* bar) {
    // ---- 文件 ----
    QMenu* mFile = bindAddMenu(w->retranslator_, bar, IDC_GT(menu, kMenuFile));
    QAction* aNew = bindAddAction(w->retranslator_, mFile, IDC_GT(menu, kFileNewCanvas));
    KeyBindings::instance().registerAction(CommandId::New, aNew);   // 默认键回落注册表；用户自定义后以此处生效。
    i18n::bindToolTip(w->retranslator_, aNew, IDC_GT(tooltip, kNewCanvas));
    QObject::connect(aNew, &QAction::triggered, w, [w] { w->session_->onNew(); });
    QAction* aOpen = bindAddAction(w->retranslator_, mFile, IDC_GT(menu, kFileOpenImage));
    KeyBindings::instance().registerAction(CommandId::Open, aOpen);
    QObject::connect(aOpen, &QAction::triggered, w, [w] { w->session_->onOpen(); });
    // 最近打开（MRU）：UiPrefs（QSettings ui/ 组、应用级、非 §7、不入撤销）；aboutToShow 懒重建，无需信号。
    // 菜单项文字在 aboutToShow 时才取 IDC_GTR（每次展开重评），切语言后下次展开即新语言，无需绑定。
    QMenu* mRecent = bindAddMenu(w->retranslator_, mFile, IDC_GT(menu, kFileRecent));
    QObject::connect(mRecent, &QMenu::aboutToShow, w, [w, mRecent] {
        mRecent->clear();
        const QStringList recents = UiPrefs::instance().recentFiles();
        if (recents.isEmpty()) {
            QAction* none = mRecent->addAction(IDC_GTR(menu, kFileRecentEmpty));
            none->setEnabled(false);
            return;
        }
        for (const QString& f : recents) {
            QAction* a = mRecent->addAction(QFileInfo(f).fileName());
            a->setToolTip(f);
            a->setStatusTip(f);
            QObject::connect(a, &QAction::triggered, w, [w, f] { w->openImageFromPath(f); });
        }
        mRecent->addSeparator();
        const QAction* clear = mRecent->addAction(IDC_GTR(menu, kFileRecentClear));
        QObject::connect(clear, &QAction::triggered, w, [] { UiPrefs::instance().clearRecentFiles(); });
    });
    QAction* aExport = bindAddAction(w->retranslator_, mFile, IDC_GT(menu, kFileExport));
    KeyBindings::instance().registerAction(CommandId::Export, aExport);
    QObject::connect(aExport, &QAction::triggered, w, [w] { w->export_->onExport(); });
    // 导出设置（输出方式/格式/烧录开关）：与导出面板组合框/复选框镜像——面板为唯一真相源，
    // 菜单经 selectExportMode/selectFormat/setBurnInChecked 走与用户点击面板同一路径（含重排 auto-grid）。
    QMenu* mExportSet = bindAddMenu(w->retranslator_, mFile, IDC_GT(menu, kFileExportSettings));
    auto* exportGroup = new QActionGroup(w);
    exportGroup->setExclusive(true);
    w->aExportSep_ = bindAddAction(w->retranslator_, exportGroup, IDC_GT(menu, kExportSeparate));
    w->aExportCol_ = bindAddAction(w->retranslator_, exportGroup, IDC_GT(menu, kExportCollapse));
    w->aExportRear_ = bindAddAction(w->retranslator_, exportGroup, IDC_GT(menu, kExportRearrange));
    for (QAction* a : {w->aExportSep_, w->aExportCol_, w->aExportRear_}) {
        a->setCheckable(true);
        mExportSet->addAction(a);
    }
    i18n::bindToolTip(w->retranslator_, w->aExportRear_, IDC_GT(tooltip, kExportRearrange));
    QObject::connect(w->aExportSep_, &QAction::triggered, w, [w] { w->exportPanel_->selectExportMode(0); });
    QObject::connect(w->aExportCol_, &QAction::triggered, w, [w] { w->exportPanel_->selectExportMode(1); });
    QObject::connect(w->aExportRear_, &QAction::triggered, w, [w] { w->exportPanel_->selectExportMode(2); });
    mExportSet->addSeparator();
    auto* fmtGroup = new QActionGroup(w);
    fmtGroup->setExclusive(true);
    w->aFmtPng_ = fmtGroup->addAction(QStringLiteral("PNG"));
    w->aFmtJpg_ = fmtGroup->addAction(QStringLiteral("JPG"));
    w->aFmtWebp_ = fmtGroup->addAction(QStringLiteral("WebP"));
    w->aFmtBmp_ = fmtGroup->addAction(QStringLiteral("BMP"));
    for (QAction* a : {w->aFmtPng_, w->aFmtJpg_, w->aFmtWebp_, w->aFmtBmp_}) {
        a->setCheckable(true);
        mExportSet->addAction(a);
    }
    QObject::connect(w->aFmtPng_, &QAction::triggered, w, [w] { w->exportPanel_->selectFormat(0); });
    QObject::connect(w->aFmtJpg_, &QAction::triggered, w, [w] { w->exportPanel_->selectFormat(1); });
    QObject::connect(w->aFmtWebp_, &QAction::triggered, w, [w] { w->exportPanel_->selectFormat(2); });
    QObject::connect(w->aFmtBmp_, &QAction::triggered, w, [w] { w->exportPanel_->selectFormat(3); });
    mExportSet->addSeparator();
    w->aBurnAnno_ = bindAddAction(w->retranslator_, mExportSet, IDC_GT(menu, kBurnAnnotation));
    w->aBurnAnno_->setCheckable(true);
    i18n::bindToolTip(w->retranslator_, w->aBurnAnno_, IDC_GT(tooltip, kBurnAnnotation));
    QObject::connect(w->aBurnAnno_, &QAction::triggered, w, [w](const bool on) { w->exportPanel_->setBurnInChecked(on); });
    w->aBurnLayers_ = bindAddAction(w->retranslator_, mExportSet, IDC_GT(menu, kBurnImageLayers));
    w->aBurnLayers_->setCheckable(true);
    i18n::bindToolTip(w->retranslator_, w->aBurnLayers_, IDC_GT(tooltip, kBurnImageLayers));
    QObject::connect(w->aBurnLayers_, &QAction::triggered, w, [w](const bool on) { w->exportPanel_->setLayersBurnInChecked(on); });
    // 「重排前二次确认」「未烧录时导出前提醒」两开关已迁至「视图→外观/交互→其他→确认与吸附」，本菜单不再设入口（偏好仍存 UiPrefs，onExport/onSaveConfig 照常读取）。
    mFile->addSeparator();
    // 配置文件加载/保存（FR-L3.8）：复用 Core loadEngineConfig/saveEngineConfig（SPEC §7 schema）。
    QAction* aLoadCfg = bindAddAction(w->retranslator_, mFile, IDC_GT(menu, kFileLoadConfig));
    i18n::bindToolTip(w->retranslator_, aLoadCfg, IDC_GT(tooltip, kLoadConfig));
    QObject::connect(aLoadCfg, &QAction::triggered, w, [w] { w->config_->onLoadConfig(); });
    QAction* aSaveCfg = bindAddAction(w->retranslator_, mFile, IDC_GT(menu, kFileSaveConfig));
    i18n::bindToolTip(w->retranslator_, aSaveCfg, IDC_GT(tooltip, kSaveConfig));
    QObject::connect(aSaveCfg, &QAction::triggered, w, [w] { w->config_->onSaveConfig(); });
    mFile->addSeparator();
    const QAction* aExit = bindAddAction(w->retranslator_, mFile, IDC_GT(menu, kFileExit));
    QObject::connect(aExit, &QAction::triggered, w, &QWidget::close);

}

void MainWindowUi::buildEditMenu(MainWindow* w, QMenuBar* bar) {
    // ---- 编辑（全局撤销/重做：上下文路由，复用 Core HistoryManager）----
    QMenu* mEdit = bindAddMenu(w->retranslator_, bar, IDC_GT(menu, kMenuEdit));
    w->aUndo_ = bindAddAction(w->retranslator_, mEdit, IDC_GT(menu, kEditUndo));
    KeyBindings::instance().registerAction(CommandId::Undo, w->aUndo_);
    i18n::bindToolTip(w->retranslator_, w->aUndo_, IDC_GT(tooltip, kUndo));
    QObject::connect(w->aUndo_, &QAction::triggered, w, &MainWindow::onUndo);
    w->aRedo_ = bindAddAction(w->retranslator_, mEdit, IDC_GT(menu, kEditRedo));
    KeyBindings::instance().registerAction(CommandId::Redo, w->aRedo_);
    i18n::bindToolTip(w->retranslator_, w->aRedo_, IDC_GT(tooltip, kRedo));
    QObject::connect(w->aRedo_, &QAction::triggered, w, &MainWindow::onRedo);
    mEdit->addSeparator();
    // Esc 不注册为动作快捷键（会与画布绘制态「Esc 收笔」在快捷键表互相抢占），由 MainWindow::keyPressEvent 分派。
    QAction* aClear = bindAddAction(w->retranslator_, mEdit, IDC_GT(menu, kEditClearSel));
    i18n::bindToolTip(w->retranslator_, aClear, IDC_GT(tooltip, kClearSelection));
    QObject::connect(aClear, &QAction::triggered, w, [w] { w->canvas_->onClearCut(); });
    // L3 选择集快捷操作（与参数面板「全选/反选/清空」按钮同一 Document 入口；syncPanels 仅在 L3 模式启用）。
    w->aCellsAll_ = bindAddAction(w->retranslator_, mEdit, IDC_GT(menu, kEditCellsAll));
    QObject::connect(w->aCellsAll_, &QAction::triggered, w, [w] { w->canvas_->onCellsSelectAll(); });
    w->aCellsInvert_ = bindAddAction(w->retranslator_, mEdit, IDC_GT(menu, kEditCellsInvert));
    QObject::connect(w->aCellsInvert_, &QAction::triggered, w, [w] { w->canvas_->onCellsInvert(); });
    w->aCellsClear_ = bindAddAction(w->retranslator_, mEdit, IDC_GT(menu, kEditCellsClear));
    QObject::connect(w->aCellsClear_, &QAction::triggered, w, [w] { w->canvas_->onCellsClear(); });
    mEdit->addSeparator();
    // 一键重置全部：清图层（回底图）+全部标注+全部预处理+选区，仅保留底图。
    QAction* aDestroyAll = bindAddAction(w->retranslator_, mEdit, IDC_GT(menu, kEditResetAll));
    i18n::bindToolTip(w->retranslator_, aDestroyAll, IDC_GT(tooltip, kResetAll));
    QObject::connect(aDestroyAll, &QAction::triggered, w, [w] { w->session_->onDestroyAll(); });

}

// 总装配：按菜单域依次调用四个 TU 的域 helper（与 buildToolbar 等并列；域内控件/连接自持）。
// 菜单项文字、悬停说明与帮助正文均取 i18n/gui_texts.h（texts::menu / texts::tooltip / texts::msg）——
// 桌面菜单与移动「更多」菜单同源，改一条即两处同时变。
void MainWindowUi::buildMenus(MainWindow* w) {
    QMenuBar* bar = w->menuBar();
    buildFileMenu(w, bar);
    buildEditMenu(w, bar);
    buildModeMenu(w, bar);
    buildPreprocessMenu(w, bar);
    buildLayerMenu(w, bar);
    buildAnnoMenu(w, bar);
    buildViewMenu(w, bar);
    buildHelpMenu(w, bar);
}

} // namespace idc::gui
