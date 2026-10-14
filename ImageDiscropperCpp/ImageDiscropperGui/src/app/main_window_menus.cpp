// ============================================================================
// 文件：app/main_window_menus.cpp
// 作用：实现 MainWindowUi::buildMenus（自 main_window_ui.cpp 拆出独立翻译单元）。
//       菜单栏装配：文件 / 编辑 / 模式 / 图像 / 图层 / 标注 / 视图 / 帮助（含帮助/关于正文）。
// 分块依据：buildMenus 独占约 400 行且与中央区/工具栏/状态栏装配及 syncUi 无共享状态，
//           独立成件使 main_window_ui.cpp 保持「装配 + 同步」的单一关注点（CONTRIBUTING.md「分层纪律」）。
// 说明：所有 connect 的接收者/上下文均为 w（friend 权限连接私有槽/控制器成员）；lambda 捕获 w 而非 this。
// ============================================================================
#include "app/main_window_ui.h"

#include <QAction>
#include <QActionGroup>
#include <QFileInfo>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QWidget>

#include "app/controllers/annotation_coordinator.h"
#include "app/appearance_dialog.h"
#include "app/canvas_prefs.h"
#include "app/keybindings.h"
#include "app/app_prefs.h"
#include "app/controllers/canvas_coordinator.h"
#include "app/controllers/config_controller.h"
#include "app/controllers/export_controller.h"
#include "app/controllers/preprocess_controller.h"
#include "app/controllers/session_controller.h"
#include "app/main_window.h"
#include "canvas/canvas_mode.h"
#include "canvas/canvas_view.h"
#include "canvas/layer.h"
#include "generate/pattern.h"
#include "panels/export_panel.h"
#include "panels/layer_panel.h"
#include "panels/tool_panel.h"
#include "util/scrollable_dialog.h"   // showScrollableMessage：使用说明 / 关于长文本改可滚动信息框（item 4）
#include "i18n/gui_texts.h"
#include "i18n/gui_i18n.h"
#include "i18n/retranslate.h"

// 版本号经 CMake 编译期宏注入；独立配置本目录时回退占位版本（与构建说明一致）。
#ifndef IDC_GUI_VERSION
#define IDC_GUI_VERSION "dev"
#endif
#ifndef IDC_CORE_VERSION
#define IDC_CORE_VERSION "dev"
#endif

namespace idc::gui {

namespace {
// 菜单装配绑定助手（包 MainWindow::retranslator_）：把一次 addAction/addMenu 变为可重放登记，
// 收敛逐站点改动。文字入参一律用 IDC_GT(...) 产出的 TextRef（constexpr、key+fallback）。
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
} // namespace

// 装配菜单栏：文件 / 编辑 / 模式 / 图像 / 图层 / 标注 / 视图 / 帮助。
// 菜单项文字、悬停说明与帮助正文均取 i18n/gui_texts.h（texts::menu / texts::tooltip / texts::msg）——
// 桌面菜单与移动「更多」菜单同源，改一条即两处同时变。

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
    // 菜单经 selectEmitMode/selectFormat/setBurnInChecked 走与用户点击面板同一路径（含重排 auto-grid）。
    QMenu* mExportSet = bindAddMenu(w->retranslator_, mFile, IDC_GT(menu, kFileExportSettings));
    auto* emitGroup = new QActionGroup(w);
    emitGroup->setExclusive(true);
    w->aEmitSep_ = bindAddAction(w->retranslator_, emitGroup, IDC_GT(menu, kEmitSeparate));
    w->aEmitCol_ = bindAddAction(w->retranslator_, emitGroup, IDC_GT(menu, kEmitCollapse));
    w->aEmitRear_ = bindAddAction(w->retranslator_, emitGroup, IDC_GT(menu, kEmitRearrange));
    for (QAction* a : {w->aEmitSep_, w->aEmitCol_, w->aEmitRear_}) {
        a->setCheckable(true);
        mExportSet->addAction(a);
    }
    i18n::bindToolTip(w->retranslator_, w->aEmitRear_, IDC_GT(tooltip, kEmitRearrange));
    QObject::connect(w->aEmitSep_, &QAction::triggered, w, [w] { w->exportPanel_->selectEmitMode(0); });
    QObject::connect(w->aEmitCol_, &QAction::triggered, w, [w] { w->exportPanel_->selectEmitMode(1); });
    QObject::connect(w->aEmitRear_, &QAction::triggered, w, [w] { w->exportPanel_->selectEmitMode(2); });
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
    // 重排前二次确认：开关存在 UiPrefs（应用级、跨会话记住），控制 onExport 遇风险时是否弹模态确认（默认开）。
    QAction* aConfirmRearrange = bindAddAction(w->retranslator_, mExportSet, IDC_GT(menu, kConfirmRearrange));
    aConfirmRearrange->setCheckable(true);
    aConfirmRearrange->setChecked(UiPrefs::instance().confirmRearrangeRisk());
    i18n::bindToolTip(w->retranslator_, aConfirmRearrange, IDC_GT(tooltip, kConfirmRearrange));
    QObject::connect(aConfirmRearrange, &QAction::toggled, w, [](const bool on) { UiPrefs::instance().setConfirmRearrangeRisk(on); });
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
    // Esc 不注册为动作快捷键（会与画布绘制态「Esc 收笔」在快捷键表互相抢占），改由 MainWindow::keyPressEvent 分派。
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

void MainWindowUi::buildModeMenu(MainWindow* w, QMenuBar* bar) {
    // ---- 模式（L1/L2/L3 + 极性）：菜单为全部功能入口；动作与工具栏共用同一批 QAction
    //      （QActionGroup 互斥、syncPanels 从 Document 回同步选中态），无第二份状态。----
    QMenu* mMode = bindAddMenu(w->retranslator_, bar, IDC_GT(menu, kMenuMode));
    auto* modeGroup = new QActionGroup(w);
    modeGroup->setExclusive(true);
    w->modeActionL1_ = bindAddAction(w->retranslator_, modeGroup, IDC_GT(menu, kModeL1));
    w->modeActionL2_ = bindAddAction(w->retranslator_, modeGroup, IDC_GT(menu, kModeL2));
    w->modeActionL3_ = bindAddAction(w->retranslator_, modeGroup, IDC_GT(menu, kModeL3));
    for (QAction* a : {w->modeActionL1_, w->modeActionL2_, w->modeActionL3_}) {
        a->setCheckable(true);
        mMode->addAction(a);
    }
    w->modeActionL1_->setChecked(true);
    i18n::bindToolTip(w->retranslator_, w->modeActionL3_, IDC_GT(tooltip, kModeL3));
    QObject::connect(w->modeActionL1_, &QAction::triggered, w, [w] { w->onModeAction(1); });
    QObject::connect(w->modeActionL2_, &QAction::triggered, w, [w] { w->onModeAction(2); });
    QObject::connect(w->modeActionL3_, &QAction::triggered, w, [w] { w->onModeAction(3); });
    mMode->addSeparator();
    // 极性（保留/删除框内）：L1/L2 下与模式一一对应（Document::setPolarity 联动切模式），L3 独立。
    auto* polGroup = new QActionGroup(w);
    polGroup->setExclusive(true);
    w->aPolKeep_ = bindAddAction(w->retranslator_, polGroup, IDC_GT(menu, kPolarityKeep));
    w->aPolRemove_ = bindAddAction(w->retranslator_, polGroup, IDC_GT(menu, kPolarityRemove));
    w->aPolKeep_->setCheckable(true);
    w->aPolRemove_->setCheckable(true);
    w->aPolKeep_->setChecked(true);
    i18n::bindToolTip(w->retranslator_, w->aPolKeep_, IDC_GT(tooltip, kPolarityKeep));
    i18n::bindToolTip(w->retranslator_, w->aPolRemove_, IDC_GT(tooltip, kPolarityRemove));
    mMode->addAction(w->aPolKeep_);
    mMode->addAction(w->aPolRemove_);
    QObject::connect(w->aPolKeep_, &QAction::triggered, w, [w] { w->onPolarityShortcut(false); });
    QObject::connect(w->aPolRemove_, &QAction::triggered, w, [w] { w->onPolarityShortcut(true); });
    mMode->addSeparator();
    // 画布四态互斥模式（与工具面板顶行按钮镜像）：经工具面板 setMode 走同一 modeChanged → applyCanvasMode 路径。
    QMenu* mCanvas = bindAddMenu(w->retranslator_, mMode, IDC_GT(menu, kCanvasMode));
    auto* canvasGroup = new QActionGroup(w);
    canvasGroup->setExclusive(true);
    w->aCanvasMarquee_ = bindAddAction(w->retranslator_, canvasGroup, IDC_GT(menu, kCanvasMarquee));
    w->aCanvasImage_ = bindAddAction(w->retranslator_, canvasGroup, IDC_GT(menu, kCanvasImagePick));
    w->aCanvasAnnoSel_ = bindAddAction(w->retranslator_, canvasGroup, IDC_GT(menu, kCanvasAnnoSelect));
    w->aCanvasAnnoCreate_ = bindAddAction(w->retranslator_, canvasGroup, IDC_GT(menu, kCanvasAnnoCreate));
    for (QAction* a : {w->aCanvasMarquee_, w->aCanvasImage_, w->aCanvasAnnoSel_, w->aCanvasAnnoCreate_}) {
        a->setCheckable(true);
        mCanvas->addAction(a);
    }
    i18n::bindToolTip(w->retranslator_, w->aCanvasMarquee_, IDC_GT(tooltip, kCanvasMarquee));
    i18n::bindToolTip(w->retranslator_, w->aCanvasImage_, IDC_GT(tooltip, kCanvasImagePick));
    i18n::bindToolTip(w->retranslator_, w->aCanvasAnnoSel_, IDC_GT(tooltip, kCanvasAnnoSelect));
    i18n::bindToolTip(w->retranslator_, w->aCanvasAnnoCreate_, IDC_GT(tooltip, kCanvasAnnoCreate));
    QObject::connect(w->aCanvasMarquee_, &QAction::triggered, w, [w] { w->toolPanel_->setMode(CanvasMode::Marquee); });
    QObject::connect(w->aCanvasImage_, &QAction::triggered, w, [w] { w->toolPanel_->setMode(CanvasMode::ImagePick); });
    QObject::connect(w->aCanvasAnnoSel_, &QAction::triggered, w, [w] { w->toolPanel_->setMode(CanvasMode::AnnoSelect); });
    QObject::connect(w->aCanvasAnnoCreate_, &QAction::triggered, w, [w] { w->toolPanel_->setMode(CanvasMode::AnnoCreate); });
    mMode->addSeparator();
    // 切割参数（与右侧「参数」页镜像，仅当前切割模式启用；syncPanels 回同步勾选态）：
    // L1 形状 / L2 方式 / 转网格（L2）/ L3 排序。
    QMenu* mL1 = bindAddMenu(w->retranslator_, mMode, IDC_GT(menu, kL1ShapeSub));
    w->mL1Shape_ = mL1;
    auto* l1Group = new QActionGroup(w);
    l1Group->setExclusive(true);
    w->aL1Rect_ = bindAddAction(w->retranslator_, l1Group, IDC_GT(menu, kL1Rect));
    w->aL1HBand_ = bindAddAction(w->retranslator_, l1Group, IDC_GT(menu, kL1HBand));
    w->aL1VBand_ = bindAddAction(w->retranslator_, l1Group, IDC_GT(menu, kL1VBand));
    for (QAction* a : {w->aL1Rect_, w->aL1HBand_, w->aL1VBand_}) {
        a->setCheckable(true);
        mL1->addAction(a);
    }
    QObject::connect(w->aL1Rect_, &QAction::triggered, w, [w] { w->doc_.setL1Shape(L1Shape::RECT); });
    QObject::connect(w->aL1HBand_, &QAction::triggered, w, [w] { w->doc_.setL1Shape(L1Shape::HBAND); });
    QObject::connect(w->aL1VBand_, &QAction::triggered, w, [w] { w->doc_.setL1Shape(L1Shape::VBAND); });
    QMenu* mL2 = bindAddMenu(w->retranslator_, mMode, IDC_GT(menu, kL2MethodSub));
    w->mL2Sub_ = mL2;
    auto* l2Group = new QActionGroup(w);
    l2Group->setExclusive(true);
    w->aL2Cross_ = bindAddAction(w->retranslator_, l2Group, IDC_GT(menu, kL2Cross));
    w->aL2HLine_ = bindAddAction(w->retranslator_, l2Group, IDC_GT(menu, kL2HLine));
    w->aL2VLine_ = bindAddAction(w->retranslator_, l2Group, IDC_GT(menu, kL2VLine));
    w->aL2Multi_ = bindAddAction(w->retranslator_, l2Group, IDC_GT(menu, kL2MultiRect));
    for (QAction* a : {w->aL2Cross_, w->aL2HLine_, w->aL2VLine_, w->aL2Multi_}) {
        a->setCheckable(true);
        mL2->addAction(a);
    }
    QObject::connect(w->aL2Cross_, &QAction::triggered, w, [w] { w->doc_.setL2Sub(L2Sub::CROSS); });
    QObject::connect(w->aL2HLine_, &QAction::triggered, w, [w] { w->doc_.setL2Sub(L2Sub::HLINE); });
    QObject::connect(w->aL2VLine_, &QAction::triggered, w, [w] { w->doc_.setL2Sub(L2Sub::VLINE); });
    QObject::connect(w->aL2Multi_, &QAction::triggered, w, [w] { w->doc_.setL2Sub(L2Sub::MULTI_RECT); });
    QMenu* mSort = bindAddMenu(w->retranslator_, mMode, IDC_GT(menu, kL3SortSub));
    w->mSort_ = mSort;
    auto* sortGroup = new QActionGroup(w);
    sortGroup->setExclusive(true);
    w->aSortRow_ = bindAddAction(w->retranslator_, sortGroup, IDC_GT(menu, kSortRowMajor));
    w->aSortCol_ = bindAddAction(w->retranslator_, sortGroup, IDC_GT(menu, kSortColMajor));
    w->aSortCustom_ = bindAddAction(w->retranslator_, sortGroup, IDC_GT(menu, kSortCustom));
    for (QAction* a : {w->aSortRow_, w->aSortCol_, w->aSortCustom_}) {
        a->setCheckable(true);
        mSort->addAction(a);
    }
    QObject::connect(w->aSortRow_, &QAction::triggered, w, [w] { w->doc_.setSortStrategy(engine::SortStrategy::ROW_MAJOR); });
    QObject::connect(w->aSortCol_, &QAction::triggered, w, [w] { w->doc_.setSortStrategy(engine::SortStrategy::COLUMN_MAJOR); });
    QObject::connect(w->aSortCustom_, &QAction::triggered, w, [w] { w->doc_.setSortStrategy(engine::SortStrategy::CUSTOM); });
    mSort->addSeparator();
    w->aSortReverse_ = bindAddAction(w->retranslator_, mSort, IDC_GT(menu, kSortReverse));
    w->aSortReverse_->setCheckable(true);
    i18n::bindToolTip(w->retranslator_, w->aSortReverse_, IDC_GT(tooltip, kSortReverse));
    QObject::connect(w->aSortReverse_, &QAction::triggered, w, [w](const bool on) { w->doc_.setSortReverse(on); });
    w->aSortSnake_ = bindAddAction(w->retranslator_, mSort, IDC_GT(menu, kSortSnake));
    w->aSortSnake_->setCheckable(true);
    i18n::bindToolTip(w->retranslator_, w->aSortSnake_, IDC_GT(tooltip, kSortSnake));
    QObject::connect(w->aSortSnake_, &QAction::triggered, w, [w](const bool on) { w->doc_.setSortSnake(on); });
    // L3 余量策略（与「参数」页余量下拉镜像，面板为真相源）：仅 L3 启用，syncPanels 回同步勾选态。
    QMenu* mRemainder = bindAddMenu(w->retranslator_, mMode, IDC_GT(menu, kL3RemainderSub));
    w->mRemainder_ = mRemainder;
    auto* remGroup = new QActionGroup(w);
    remGroup->setExclusive(true);
    const struct { engine::RemainderPolicy policy; i18n::TextRef label; } kRems[] = {
        { engine::RemainderPolicy::DISCARD, IDC_GT(menu, kRemainderDiscard) },
        { engine::RemainderPolicy::KEEP_PARTIAL, IDC_GT(menu, kRemainderKeep) },
        { engine::RemainderPolicy::PAD, IDC_GT(menu, kRemainderPad) },
    };
    w->remainderActions_.resize(3, nullptr);
    for (const auto&[policy, label] : kRems) {
        QAction* a = bindAddAction(w->retranslator_, remGroup, label);
        a->setCheckable(true);
        mRemainder->addAction(a);
        w->remainderActions_[static_cast<int>(policy)] = a;
        QObject::connect(a, &QAction::triggered, w, [w, policy = policy] { w->doc_.setRemainder(policy); });
    }

}

void MainWindowUi::buildImageMenu(MainWindow* w, QMenuBar* bar) {
    // ---- 图像（预处理 FR-1：旋转/翻转/黑白/反色/重置；完整控件见右侧「预处理」页）----
    QMenu* mImage = bindAddMenu(w->retranslator_, bar, IDC_GT(menu, kMenuPreprocess));
    QAction* aRotL = bindAddAction(w->retranslator_, mImage, IDC_GT(menu, kRotLeft90));
    QObject::connect(aRotL, &QAction::triggered, w, [w] { w->preprocess_->onRotate(-90); });
    QAction* aRotR = bindAddAction(w->retranslator_, mImage, IDC_GT(menu, kRotRight90));
    QObject::connect(aRotR, &QAction::triggered, w, [w] { w->preprocess_->onRotate(90); });
    QAction* aRot180 = bindAddAction(w->retranslator_, mImage, IDC_GT(menu, kRot180));
    QObject::connect(aRot180, &QAction::triggered, w, [w] { w->preprocess_->onRotate(180); });
    mImage->addSeparator();
    QAction* aFlipH = bindAddAction(w->retranslator_, mImage, IDC_GT(menu, kFlipHorizontal));
    QObject::connect(aFlipH, &QAction::triggered, w, [w] { w->preprocess_->onFlip(true); });
    QAction* aFlipV = bindAddAction(w->retranslator_, mImage, IDC_GT(menu, kFlipVertical));
    QObject::connect(aFlipV, &QAction::triggered, w, [w] { w->preprocess_->onFlip(false); });
    mImage->addSeparator();
    w->aGrayMenu_ = bindAddAction(w->retranslator_, mImage, IDC_GT(menu, kGray));
    QObject::connect(w->aGrayMenu_, &QAction::triggered, w, [w] { w->preprocess_->onGray(); });
    // 按通道反色（7 种常用组合入菜单，任意组合去「预处理」面板复选框）：子菜单项随 preprocessActions_
    // 按有无图像逐项置灰，入口本身另存 mInvert_ 由 syncPanels 整入口置灰（与「色道分离」同款；
    // 灰度图仍可反色，不受灰度智能约束）。
    QMenu* mInvert = bindAddMenu(w->retranslator_, mImage, IDC_GT(menu, kInvert));
    w->mInvert_ = mInvert;
    struct InvertEntry { i18n::TextRef label; bool r, g, b; };
    const InvertEntry kInverts[] = {
        { IDC_GT(menu, kInvertAll), true, true, true },
        { IDC_GT(menu, kInvertRG), true, true, false },
        { IDC_GT(menu, kInvertRB), true, false, true },
        { IDC_GT(menu, kInvertGB), false, true, true },
        { IDC_GT(menu, kInvertR), true, false, false },
        { IDC_GT(menu, kInvertG), false, true, false },
        { IDC_GT(menu, kInvertB), false, false, true },
    };
    for (const InvertEntry& inv : kInverts) {
        QAction* a = bindAddAction(w->retranslator_, mInvert, inv.label);
        QObject::connect(a, &QAction::triggered, w, [w, inv] { w->preprocess_->onInvert(inv.r, inv.g, inv.b); });
        w->preprocessActions_.push_back(a);
    }
    // 色道分离（保留掩码 '1' 保留 / '0' 置零）：常用组合入菜单，任意通道组合去「预处理」面板复选框；
    // 与「黑白」同源置灰（syncPanels：当前预处理目标会话已产出灰度→灰度图无通道可分）。
    QMenu* mSplit = bindAddMenu(w->retranslator_, mImage, IDC_GT(menu, kChannelSplit));
    w->mSplit_ = mSplit;
    struct SplitEntry { i18n::TextRef label; bool r, g, b; };
    const SplitEntry kSplits[] = {
        { IDC_GT(menu, kSplitKeepR), true, false, false },
        { IDC_GT(menu, kSplitKeepG), false, true, false },
        { IDC_GT(menu, kSplitKeepB), false, false, true },
        { IDC_GT(menu, kSplitKeepRG), true, true, false },
        { IDC_GT(menu, kSplitKeepRB), true, false, true },
        { IDC_GT(menu, kSplitKeepGB), false, true, true },
    };
    for (const SplitEntry& s : kSplits) {
        QAction* a = bindAddAction(w->retranslator_, mSplit, s.label);
        QObject::connect(a, &QAction::triggered, w, [w, s] { w->preprocess_->onSplit(s.r, s.g, s.b); });
        w->preprocessActions_.push_back(a);
    }
    mImage->addSeparator();
    QAction* aResetPre = bindAddAction(w->retranslator_, mImage, IDC_GT(menu, kResetPreprocess));
    QObject::connect(aResetPre, &QAction::triggered, w, [w] { w->preprocess_->onResetPreprocess(); });
    // 预处理动作集合：无图像时统一置灰（syncPanels 逐项同步；反色/分离子菜单项已逐项入列，
    // 两子菜单入口另由 mInvert_/mSplit_ 整入口置灰；「黑白」「色道分离」另有灰度智能置灰）。
    for (QAction* a : {aRotL, aRotR, aRot180, aFlipH, aFlipV, w->aGrayMenu_, aResetPre})
        w->preprocessActions_.push_back(a);
}

void MainWindowUi::buildLayerMenu(MainWindow* w, QMenuBar* bar) {
    // ---- 图层（多层画布，与预处理拆分——图层栈 CRUD 独立成菜单；显隐开关在视图菜单）----
    QMenu* mLayers = bindAddMenu(w->retranslator_, bar, IDC_GT(menu, kMenuImage));
    QAction* aAddImageLayer = bindAddAction(w->retranslator_, mLayers, IDC_GT(menu, kAddImageLayer));
    i18n::bindToolTip(w->retranslator_, aAddImageLayer, IDC_GT(tooltip, kAddImageLayer));
    QObject::connect(aAddImageLayer, &QAction::triggered, w, [w] { w->session_->onAddImageLayer(); });
    QAction* aClearLayers = bindAddAction(w->retranslator_, mLayers, IDC_GT(menu, kClearAllImageLayers));
    i18n::bindToolTip(w->retranslator_, aClearLayers, IDC_GT(tooltip, kClearAllImageLayers));
    QObject::connect(aClearLayers, &QAction::triggered, w, [w] { w->session_->onClearAllLayers(); });
    mLayers->addSeparator();
    // 层序/删除：作用画布当前拾取层（「图片拾取」态点选图层即拾取）；无拾取层时 syncPanels 置灰。
    w->aLayerUp_ = bindAddAction(w->retranslator_, mLayers, IDC_GT(menu, kLayerUp));
    i18n::bindToolTip(w->retranslator_, w->aLayerUp_, IDC_GT(tooltip, kLayerUp));
    QObject::connect(w->aLayerUp_, &QAction::triggered, w, [w] {
        if (w->doc_.selectedLayerId()) w->doc_.moveLayerZOrder(*w->doc_.selectedLayerId(), true);
    });
    w->aLayerDown_ = bindAddAction(w->retranslator_, mLayers, IDC_GT(menu, kLayerDown));
    i18n::bindToolTip(w->retranslator_, w->aLayerDown_, IDC_GT(tooltip, kLayerDown));
    QObject::connect(w->aLayerDown_, &QAction::triggered, w, [w] {
        if (w->doc_.selectedLayerId()) w->doc_.moveLayerZOrder(*w->doc_.selectedLayerId(), false);
    });
    w->aLayerTop_ = bindAddAction(w->retranslator_, mLayers, IDC_GT(menu, kLayerToTop));
    QObject::connect(w->aLayerTop_, &QAction::triggered, w, [w] {
        if (w->doc_.selectedLayerId()) w->doc_.moveLayerToEdge(*w->doc_.selectedLayerId(), true);
    });
    w->aLayerBottom_ = bindAddAction(w->retranslator_, mLayers, IDC_GT(menu, kLayerToBottom));
    QObject::connect(w->aLayerBottom_, &QAction::triggered, w, [w] {
        if (w->doc_.selectedLayerId()) w->doc_.moveLayerToEdge(*w->doc_.selectedLayerId(), false);
    });
    w->aLayerRemove_ = bindAddAction(w->retranslator_, mLayers, IDC_GT(menu, kLayerRemove));
    i18n::bindToolTip(w->retranslator_, w->aLayerRemove_, IDC_GT(tooltip, kLayerRemove));
    QObject::connect(w->aLayerRemove_, &QAction::triggered, w, [w] {
        if (w->doc_.selectedLayerId()) w->doc_.removeLayer(*w->doc_.selectedLayerId());
    });
    mLayers->addSeparator();
    // 锚点/枢轴（作用当前拾取层落位，9 向枚举镜像「图层编辑」面板两组下拉框；面板为真相源，
    // syncPanels 随拾取层回同步勾选态、无拾取层置灰）。
    const struct { generate::ImageAnchor anchor; i18n::TextRef label; } kAnchors[] = {
        { generate::ImageAnchor::TopLeft, IDC_GT(label, kDirTopLeft) },
        { generate::ImageAnchor::TopCenter, IDC_GT(label, kDirTopCenter) },
        { generate::ImageAnchor::TopRight, IDC_GT(label, kDirTopRight) },
        { generate::ImageAnchor::MiddleLeft, IDC_GT(label, kDirMiddleLeft) },
        { generate::ImageAnchor::Center, IDC_GT(label, kDirCenter) },
        { generate::ImageAnchor::MiddleRight, IDC_GT(label, kDirMiddleRight) },
        { generate::ImageAnchor::BottomLeft, IDC_GT(label, kDirBottomLeft) },
        { generate::ImageAnchor::BottomCenter, IDC_GT(label, kDirBottomCenter) },
        { generate::ImageAnchor::BottomRight, IDC_GT(label, kDirBottomRight) },
    };
    w->layerAnchorActions_.resize(9, nullptr);
    w->layerPivotActions_.resize(9, nullptr);
    QMenu* mAnchor = bindAddMenu(w->retranslator_, mLayers, IDC_GT(menu, kAnchor));
    w->mLayerAnchor_ = mAnchor;
    auto* anchorGroup = new QActionGroup(w);
    anchorGroup->setExclusive(true);
    QMenu* mPivot = bindAddMenu(w->retranslator_, mLayers, IDC_GT(menu, kPivot));
    w->mLayerPivot_ = mPivot;
    auto* pivotGroup = new QActionGroup(w);
    pivotGroup->setExclusive(true);
    for (const auto&[anchor, label] : kAnchors) {
        QAction* ac = bindAddAction(w->retranslator_, anchorGroup, label);
        ac->setCheckable(true);
        mAnchor->addAction(ac);
        w->layerAnchorActions_[static_cast<int>(anchor)] = ac;
        QObject::connect(ac, &QAction::triggered, w, [w, anchor = anchor] {
            if (w->doc_.selectedLayerId()) {
                if (const canvas::ImageLayer* layer = w->doc_.layerById(*w->doc_.selectedLayerId())) {
                    canvas::ImageLayer updated = *layer;
                    updated.transform.inCanvasAnchor = anchor;
                    w->doc_.updateLayer(*w->doc_.selectedLayerId(), updated);
                }
            }
        });
        QAction* pv = bindAddAction(w->retranslator_, pivotGroup, label);
        pv->setCheckable(true);
        mPivot->addAction(pv);
        w->layerPivotActions_[static_cast<int>(anchor)] = pv;
        QObject::connect(pv, &QAction::triggered, w, [w, anchor = anchor] {
            if (w->doc_.selectedLayerId()) {
                if (const canvas::ImageLayer* layer = w->doc_.layerById(*w->doc_.selectedLayerId())) {
                    canvas::ImageLayer updated = *layer;
                    updated.transform.layerPivot = anchor;
                    w->doc_.updateLayer(*w->doc_.selectedLayerId(), updated);
                }
            }
        });
    }

}

void MainWindowUi::buildAnnoMenu(MainWindow* w, QMenuBar* bar) {
    // ---- 标注：撤销/重做/删除选中/清除全部/属性定位 ----
    // 标注撤销/重做复用 AnnotationEditor 内建条目级会话态；快捷键 Ctrl+Z/Y 已统一交给编辑菜单
    // （上下文路由：标注上下文时自动转发到此处），故本菜单项不再绑定快捷键，避免冲突。
    QMenu* mAnno = bindAddMenu(w->retranslator_, bar, IDC_GT(menu, kMenuAnnotation));
    const QAction* aAnnoUndo = bindAddAction(w->retranslator_, mAnno, IDC_GT(menu, kAnnoUndo));
    QObject::connect(aAnnoUndo, &QAction::triggered, w, [w] { w->annotation_->onAnnoUndo(); });
    const QAction* aAnnoRedo = bindAddAction(w->retranslator_, mAnno, IDC_GT(menu, kAnnoRedo));
    QObject::connect(aAnnoRedo, &QAction::triggered, w, [w] { w->annotation_->onAnnoRedo(); });
    mAnno->addSeparator();
    QAction* aAnnoDel = bindAddAction(w->retranslator_, mAnno, IDC_GT(menu, kAnnoDeleteSelected));
    i18n::bindToolTip(w->retranslator_, aAnnoDel, IDC_GT(tooltip, kAnnoDeleteSelected));
    QObject::connect(aAnnoDel, &QAction::triggered, w, [w] { w->annotation_->onAnnoDeleteSelected(); });
    const QAction* aAnnoClear = bindAddAction(w->retranslator_, mAnno, IDC_GT(menu, kAnnoClearAll));
    QObject::connect(aAnnoClear, &QAction::triggered, w, [w] { w->annotation_->onAnnoClearAll(); });
    // 叠放次序（与标注属性页“叠放次序”区同一入口，作用选中项；无选中时 syncAnnoSelActions 置灰）。
    QMenu* mZ = bindAddMenu(w->retranslator_, mAnno, IDC_GT(menu, kAnnoStacking));
    w->aZRaise_ = bindAddAction(w->retranslator_, mZ, IDC_GT(menu, kLayerUp));
    QObject::connect(w->aZRaise_, &QAction::triggered, w, [w] { w->annotation_->onAnnoRaise(); });
    w->aZLower_ = bindAddAction(w->retranslator_, mZ, IDC_GT(menu, kLayerDown));
    QObject::connect(w->aZLower_, &QAction::triggered, w, [w] { w->annotation_->onAnnoLower(); });
    w->aZFront_ = bindAddAction(w->retranslator_, mZ, IDC_GT(menu, kAnnoToFront));
    QObject::connect(w->aZFront_, &QAction::triggered, w, [w] { w->annotation_->onAnnoBringToFront(); });
    w->aZBack_ = bindAddAction(w->retranslator_, mZ, IDC_GT(menu, kAnnoToBack));
    QObject::connect(w->aZBack_, &QAction::triggered, w, [w] { w->annotation_->onAnnoSendToBack(); });
    // 填充开关（勾选态随选中项/默认值回同步；无选中时设置下次绘制的默认值，与属性面板同语义）。
    w->aFill_ = bindAddAction(w->retranslator_, mAnno, IDC_GT(menu, kAnnoFill));
    w->aFill_->setCheckable(true);
    i18n::bindToolTip(w->retranslator_, w->aFill_, IDC_GT(tooltip, kAnnoFill));
    QObject::connect(w->aFill_, &QAction::triggered, w, [w](const bool on) { w->annotation_->onAnnoFillChanged(on); });
    mAnno->addSeparator();
    // 工具子菜单：与工具面板形状行镜像（面板为真相源、syncToolActions 回同步勾选态）；
    // 「选择」工具＝「标注选取」画布模式（在模式菜单）。绘制工具须处于「标注绘制」态，
    // 故非绘制态下先切画布模式（与用户在面板上先切模式再选工具的流程一致）。
    QMenu* mTools = bindAddMenu(w->retranslator_, mAnno, IDC_GT(menu, kAnnoTools));
    struct MenuToolEntry { AnnoTool tool; i18n::TextRef label; };
    const MenuToolEntry kMenuTools[] = {
        { AnnoTool::RECT, IDC_GT(label, kToolRect) },
        { AnnoTool::SQUARE, IDC_GT(label, kToolSquare) },
        { AnnoTool::RHOMBUS, IDC_GT(label, kToolRhombus) },
        { AnnoTool::CIRCLE, IDC_GT(label, kToolCircle) },
        { AnnoTool::ELLIPSE, IDC_GT(label, kToolEllipse) },
        { AnnoTool::ISOS_TRI, IDC_GT(label, kToolIsoTriangle) },
        { AnnoTool::EQU_TRI, IDC_GT(label, kToolEquTriangle) },
        { AnnoTool::RECT_TRI, IDC_GT(label, kToolRightTriangle) },
        { AnnoTool::EQU_RECT_TRI, IDC_GT(label, kToolIsoRightTri) },
        { AnnoTool::LINE, IDC_GT(label, kToolLine) },
        { AnnoTool::POLYLINE, IDC_GT(label, kToolPolyline) },
        { AnnoTool::TEXT, IDC_GT(label, kToolText) },
        { AnnoTool::BRUSH, IDC_GT(label, kToolBrush) },
    };
    w->toolActions_.resize(14, nullptr);   // 按 AnnoTool 枚举序索引（SELECT=0 无菜单项）。
    auto* toolGroup = new QActionGroup(w);
    toolGroup->setExclusive(true);
    for (const auto&[tool, label] : kMenuTools) {
        QAction* a = bindAddAction(w->retranslator_, mTools, label);
        a->setCheckable(true);
        toolGroup->addAction(a);
        w->toolActions_[static_cast<int>(tool)] = a;
        QObject::connect(a, &QAction::triggered, w, [w, tool = tool] {
            if (w->view_ && w->view_->canvasMode() != CanvasMode::AnnoCreate)
                w->toolPanel_->setMode(CanvasMode::AnnoCreate);
            w->annotation_->onToolSelected(tool);
        });
    }

}

void MainWindowUi::buildViewMenu(MainWindow* w, QMenuBar* bar) {
    // ---- 视图 ----
    QMenu* mView = bindAddMenu(w->retranslator_, bar, IDC_GT(menu, kMenuView));
    QAction* aZoomIn = bindAddAction(w->retranslator_, mView, IDC_GT(menu, kZoomIn));
    KeyBindings::instance().registerAction(CommandId::ZoomIn, aZoomIn);
    QObject::connect(aZoomIn, &QAction::triggered, w, [w] { w->view_->zoomIn(); });
    QAction* aZoomOut = bindAddAction(w->retranslator_, mView, IDC_GT(menu, kZoomOut));
    KeyBindings::instance().registerAction(CommandId::ZoomOut, aZoomOut);
    QObject::connect(aZoomOut, &QAction::triggered, w, [w] { w->view_->zoomOut(); });
    QAction* aFit = bindAddAction(w->retranslator_, mView, IDC_GT(menu, kFitToWindow));
    KeyBindings::instance().registerAction(CommandId::Fit, aFit);
    QObject::connect(aFit, &QAction::triggered, w, [w] { w->view_->fitToWindow(); });
    const QAction* aReset = bindAddAction(w->retranslator_, mView, IDC_GT(menu, kResetView));
    QObject::connect(aReset, &QAction::triggered, w, [w] { w->view_->resetZoom(); });
    mView->addSeparator();
    // 图层显隐（与左侧「图层」面板逐行双向镜像）：面板复选框是唯一真相源——菜单只翻转复选框，
    // 落地链（面板信号 → AnnotationCoordinator → 场景）保持不变；面板/右键改动反向回同步勾选态
    // （setChecked 不触发 triggered，无回环）。默认勾选与面板初始态一致（全部可见）。
    const auto addViewToggle = [w, mView](const i18n::TextRef& ref,
                                          void (LayerPanel::*setter)(bool) const,
                                          void (LayerPanel::*signal)(bool)) {
        QAction* a = bindAddAction(w->retranslator_, mView, ref);
        a->setCheckable(true);
        a->setChecked(true);
        QObject::connect(a, &QAction::triggered, w, [w, setter](const bool on) { (w->layerPanel_->*setter)(on); });
        QObject::connect(w->layerPanel_, signal, a, [a](const bool on) { a->setChecked(on); });
    };
    addViewToggle(IDC_GT(menu, kShowBase), &LayerPanel::setBaseVisible, &LayerPanel::baseVisibilityChanged);
    addViewToggle(IDC_GT(menu, kShowImageLayer), &LayerPanel::setImageVisible, &LayerPanel::imageLayerVisibilityChanged);
    addViewToggle(IDC_GT(menu, kShowAnnotation), &LayerPanel::setAnnotationVisible, &LayerPanel::annotationVisibilityChanged);
    addViewToggle(IDC_GT(menu, kShowMask), &LayerPanel::setMaskVisible, &LayerPanel::maskVisibilityChanged);
    addViewToggle(IDC_GT(menu, kShowGrid), &LayerPanel::setGridVisible, &LayerPanel::gridVisibilityChanged);
    addViewToggle(IDC_GT(menu, kShowCutLine), &LayerPanel::setCutLineVisible, &LayerPanel::cutLineVisibilityChanged);
    addViewToggle(IDC_GT(menu, kShowSelection), &LayerPanel::setSelectionVisible, &LayerPanel::selectionVisibilityChanged);
    addViewToggle(IDC_GT(menu, kShowCellNumber), &LayerPanel::setNumberVisible, &LayerPanel::numberVisibilityChanged);

    mView->addSeparator();
    // 选区吸附（运行时开关）：拖动选区/边时是否自动吸附到图像边界与中线；状态走 CanvasPrefs（QSettings 应用级）。
    // 切换即时经 CanvasPrefs::changed 联动 CanvasView 回设已存在选区图元，无需重建。
    QAction* aSnap = bindAddAction(w->retranslator_, mView, IDC_GT(menu, kSnapToggle));
    aSnap->setCheckable(true);
    aSnap->setChecked(CanvasPrefs::instance().snapEnabled());
    i18n::bindToolTip(w->retranslator_, aSnap, IDC_GT(tooltip, kSnap));
    QObject::connect(aSnap, &QAction::toggled, w, [](const bool on) { CanvasPrefs::instance().setSnapEnabled(on); });
    // 外观/交互：上区六项核心语义色与填充 alpha（Palette 单例），下区画布交互偏好（手柄/边缘余量/缩放/新建默认，CanvasPrefs）；
    // 均 QSettings 应用级持久化，改动即时生效（选区吸附不在此——上面已作可勾选运行时开关）。
    QAction* aAppearance = bindAddAction(w->retranslator_, mView, IDC_GT(menu, kAppearance));
    i18n::bindToolTip(w->retranslator_, aAppearance, IDC_GT(tooltip, kAppearance));
    QObject::connect(aAppearance, &QAction::triggered, w, [w] {
        AppearanceDialog dlg(w);
        // 对话框内选语言即时热切换：本窗换文由对话框自完，主窗重放经 languageApplied 回接（不关面板、不销毁窗口）。
        QObject::connect(&dlg, &AppearanceDialog::languageApplied, w, [w] { w->retranslateUi(); });
        // 选主题即时应用：theme::apply 已全量 re-polish chrome，本回接仅作兜底（触发主窗重绘自定义绘制）。
        QObject::connect(&dlg, &AppearanceDialog::themeApplied, w, [w] { w->update(); });
        dlg.exec();
        // 关闭后兜底：若尚未应用的语言变更（理论上对话框已即时应用）再原地重放一次，保证一致。
        if (i18n::needsRetranslate(UiPrefs::instance().language()))
            w->retranslateUi();
    });

}

void MainWindowUi::buildHelpMenu(MainWindow* w, QMenuBar* bar) {
    // ---- 帮助 ----
    QMenu* mHelp = bindAddMenu(w->retranslator_, bar, IDC_GT(menu, kMenuHelp));
    const QAction* aUsage = bindAddAction(w->retranslator_, mHelp, IDC_GT(menu, kUsageGuide));
    QObject::connect(aUsage, &QAction::triggered, w, [w] {
        showScrollableMessage(w, IDC_GTR(menu, kUsageGuide),
            IDC_GTR(msg, kHelpUsageBody), IDC_GTR(label, kDialogOkButton));
    });
    const QAction* aAbout = bindAddAction(w->retranslator_, mHelp, IDC_GT(menu, kAbout));
    QObject::connect(aAbout, &QAction::triggered, w, [w] {
        showScrollableMessage(w, IDC_GTR(menu, kAbout),
            IDC_GTR(msg, kAboutBody)
                .arg(QStringLiteral(IDC_GUI_VERSION), QStringLiteral(IDC_CORE_VERSION)),
            IDC_GTR(label, kDialogOkButton));
    });
}



// 装配菜单栏：按菜单域拆为文件内 helper（与 buildToolbar 等并列；域内控件/连接自持）。
void MainWindowUi::buildMenus(MainWindow* w) {
    QMenuBar* bar = w->menuBar();
    buildFileMenu(w, bar);
    buildEditMenu(w, bar);
    buildModeMenu(w, bar);
    buildImageMenu(w, bar);
    buildLayerMenu(w, bar);
    buildAnnoMenu(w, bar);
    buildViewMenu(w, bar);
    buildHelpMenu(w, bar);
}

} // namespace idc::gui
