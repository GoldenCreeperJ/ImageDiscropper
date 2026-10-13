// ============================================================================
// 文件：app/main_window_ui.cpp
// 作用：实现 MainWindowUi 的中央区/工具栏/状态栏装配与 UI 状态回同步（见同名头文件说明；
//       菜单栏装配 buildMenus 已拆至同目录 main_window_menus.cpp）。
// 分块依据：
//   - buildCentral：部件创建 + 注入 + 布局（左滚动容器 | 画布 | 右选项卡）；
//   - buildToolbar：动作创建 + 连接（常用功能快速入口，动作触发连接到控制器槽）；
//   - buildStatus：StatusBar 创建 + 初始模式名；
//   - syncUi/3 个勾选态同步：面板与菜单/工具栏动作 ← Document/标注桥（原 MainWindow::syncPanels 全体）。
// 说明：所有 connect 的接收者/上下文均为 w（friend 权限连接私有槽）；lambda 捕获 w 而非 this。
// ============================================================================
#include "app/main_window_ui.h"

#include <algorithm>

#include <QAction>
#include <QMenu>
#include <QScrollArea>
#include <QSplitter>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

#include "app/controllers/config_controller.h"
#include "app/controllers/export_controller.h"
#include "app/main_window.h"
#include "app/controllers/session_controller.h"
#include "app/status_bar.h"
#include "canvas/canvas_scene.h"
#include "canvas/canvas_view.h"
#include "canvas/layer.h"
#include "panels/accordion_panel.h"
#include "panels/annotation_prop_panel.h"
#include "panels/export_panel.h"
#include "panels/image_panel.h"
#include "panels/layer_panel.h"
#include "panels/layer_edit_panel.h"
#include "panels/left_panel.h"
#include "panels/param_panel.h"
#include "panels/tool_panel.h"
#include "util/gui_nums.h"
#include "i18n/gui_texts.h"
#include "i18n/gui_i18n.h"
#include "i18n/retranslate.h"

namespace idc::gui {

// 装配中央区：左面板 | 画布 | 右侧折叠面板（预处理 / 图片 / 标注 / 参数 / 导出）。
void MainWindowUi::buildCentral(MainWindow* w) {
    w->scene_ = new CanvasScene(w);
    w->view_ = new CanvasView(w->scene_, w);

    w->left_ = new LeftPanel(w);
    w->param_ = new ParamPanel(w);
    w->exportPanel_ = new ExportPanel(w);
    w->imagePanel_ = new ImagePanel(w);
    w->toolPanel_ = new ToolPanel(w);
    w->layerPanel_ = new LayerPanel(w);
    w->layerEditPanel_ = new LayerEditPanel(w);
    w->annoPropPanel_ = new AnnotationPropPanel(w);
    w->left_->setDocument(&w->doc_);
    w->param_->setDocument(&w->doc_);
    w->exportPanel_->setDocument(&w->doc_);
    w->imagePanel_->setDocument(&w->doc_);
    w->layerEditPanel_->setDocument(&w->doc_);
    w->toolPanel_->setModel(&w->annoBridge_);
    w->layerPanel_->setModel(&w->annoBridge_);
    w->annoPropPanel_->setModel(&w->annoBridge_);

    // 右侧 AccordionPanel 折叠式：多节**可同时展开**，整栏外套 QScrollArea 滚动触达；
    // 页面即面板本体（栏内布局自适应宽度，横向不出滚动条）。
    // 移动端抽屉另用 QTabWidget 选择式（见 mobile/mobile_shell.cpp）。
    auto* accordion = new AccordionPanel(w);
    // 折叠节标题：初次按当前语言 addSection，并登记按新语言 retitle 的重放闭包（只换文字、不动展开态）。
    const auto addI18nSection = [w, accordion](QWidget* content, const i18n::TextRef& ref) {
        accordion->addSection(content, i18n::qs(ref));
        w->retranslator_.add([accordion, content, ref] { accordion->retitle(content, i18n::qs(ref)); });
    };
    addI18nSection(w->layerEditPanel_, IDC_GT(label, kSecImage));
    addI18nSection(w->imagePanel_, IDC_GT(label, kSecPreprocess));
    addI18nSection(w->annoPropPanel_, IDC_GT(label, kSecAnnotation));
    addI18nSection(w->param_, IDC_GT(label, kSecParam));
    addI18nSection(w->exportPanel_, IDC_GT(label, kExportBoxTitle));
    w->rightTabs_ = accordion;   // 控制器/菜单经双型分发访问（成员多态 QWidget*）。
    w->accordion_ = accordion;   // 折叠节标题重放与右栏宽度重测取此指针。
    auto* rightScroll = new QScrollArea(w);
    rightScroll->setWidgetResizable(true);
    rightScroll->setFrameShape(QFrame::NoFrame);
    rightScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    rightScroll->setWidget(accordion);
    w->rightScroll_ = rightScroll;

    // 左侧容器：模式/极性(LeftPanel) + 标注工具(ToolPanel) + 图层(LayerPanel) 竖排，可滚动避免拥挤。
    // （图层编辑已移到右侧折叠栏「图片」节。）
    auto* leftContainer = new QWidget(w);
    w->leftContainer_ = leftContainer;   // 左栏内容容器（其 sizeHint 驱动左滚动区最小宽）。
    auto* leftLayout = new QVBoxLayout(leftContainer);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(nums::panels::kPanelSpacingPx);
    leftLayout->addWidget(w->left_);
    leftLayout->addWidget(w->toolPanel_);
    leftLayout->addWidget(w->layerPanel_);
    leftLayout->addStretch(1);
    auto* leftScroll = new QScrollArea(w);
    leftScroll->setWidgetResizable(true);
    leftScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    leftScroll->setWidget(leftContainer);
    w->leftScroll_ = leftScroll;

    auto* split = new QSplitter(Qt::Horizontal, w);
    w->split_ = split;                   // 三栏分隔器（热切换重测栏宽后 setSizes 保持初始宽）。
    split->addWidget(leftScroll);
    split->addWidget(w->view_);
    split->addWidget(rightScroll);
    split->setStretchFactor(0, 0);   // 左面板：窗口整体缩放时不抢空间
    split->setStretchFactor(1, 1);   // 画布：占据剩余空间
    split->setStretchFactor(2, 0);   // 右面板：窗口整体缩放时不抢空间
    split->setChildrenCollapsible(false); // 禁止把面板拖到 0 而完全折叠消失
    split->setHandleWidth(nums::app::kSplitHandleWidthPx);   // 稍宽的分隔条，拖拽手感更明显
    split->setSizes({nums::app::kLeftPanelWidthPx, nums::app::kCanvasAreaWidthPx,   // 设计默认宽（左面板预留容纳标注工具网格；此后自由拖拽）
                     nums::app::kRightPanelWidthPx});
    w->setCentralWidget(split);
    // i18n 自适应宽度：硬编码下限按中文排版标定，英文等更长的译文会把栏内内容撑过下限而横向截断
    //（两栏均 ScrollBarAlwaysOff）。以内容在当前语言下的 sizeHint 实测宽抬高下限（max(设计下限, 自然宽)）；
    // 与语言热切换重放共用同一函数（见 MainWindow::applyPanelMinWidths）。
    w->applyPanelMinWidths();
}

// 装配工具栏：新建/打开/导出、加载/保存配置、撤销/重做、缩放、模式切换、重置全部
// （常用功能快速入口；遮罩等图层显隐开关已迁入左侧「图层」面板，视图菜单与画布右键仍保留快捷切换）。
void MainWindowUi::buildToolbar(MainWindow* w) {
    QToolBar* tb = w->addToolBar(i18n::qs(IDC_GT(label, kMainToolBarTitle)));
    tb->setMovable(false);
    // 工具栏动作文字：tbAdd 设初值 + 登记按新语言 setText 的重放闭包（入 MainWindow::retranslator_）。
    const auto tbAdd = [w, tb](const i18n::TextRef& ref) {
        QAction* a = tb->addAction(i18n::qs(ref));
        w->retranslator_.add([a, ref] { a->setText(i18n::qs(ref)); });
        return a;
    };

    const QAction* aNew = tbAdd(IDC_GT(label, kTbNew));
    QObject::connect(aNew, &QAction::triggered, w, [w] { w->session_->onNew(); });
    const QAction* aOpen = tbAdd(IDC_GT(label, kTbOpen));
    QObject::connect(aOpen, &QAction::triggered, w, [w] { w->session_->onOpen(); });
    const QAction* aExport = tbAdd(IDC_GT(label, kExportBoxTitle));
    QObject::connect(aExport, &QAction::triggered, w, [w] { w->export_->onExport(); });
    // 配置存取（与文件菜单同源，FR-L3.8）：整套参数可保存复用/加载还原。
    const QAction* aLoadCfg = tbAdd(IDC_GT(label, kTbLoadConfig));
    QObject::connect(aLoadCfg, &QAction::triggered, w, [w] { w->config_->onLoadConfig(); });
    const QAction* aSaveCfg = tbAdd(IDC_GT(label, kTbSaveConfig));
    QObject::connect(aSaveCfg, &QAction::triggered, w, [w] { w->config_->onSaveConfig(); });
    tb->addSeparator();
    // 撤销/重做（工具栏按钮）：复用编辑菜单同一 QAction，共享 Ctrl+Z/Y 快捷键与动态启用态。
    if (w->aUndo_) tb->addAction(w->aUndo_);
    if (w->aRedo_) tb->addAction(w->aRedo_);
    tb->addSeparator();

    const QAction* aZoomIn = tbAdd(IDC_GT(menu, kZoomIn));
    QObject::connect(aZoomIn, &QAction::triggered, w, [w] { w->view_->zoomIn(); });
    const QAction* aZoomOut = tbAdd(IDC_GT(menu, kZoomOut));
    QObject::connect(aZoomOut, &QAction::triggered, w, [w] { w->view_->zoomOut(); });
    const QAction* aFit = tbAdd(IDC_GT(label, kTbFit));
    QObject::connect(aFit, &QAction::triggered, w, [w] { w->view_->fitToWindow(); });
    tb->addSeparator();

    // 一键重置全部（与编辑菜单同槽）。
    QAction* aDestroyAllTb = tbAdd(IDC_GT(label, kTbResetAll));
    i18n::bindToolTip(w->retranslator_, aDestroyAllTb, IDC_GT(tooltip, kTbResetAllTip));
    QObject::connect(aDestroyAllTb, &QAction::triggered, w, [w] { w->session_->onDestroyAll(); });
}

// 装配状态栏：StatusBar 组件（光标坐标 / 像素颜色 / 缩放倍数 / 提示信息；模式名并入提示线）。
void MainWindowUi::buildStatus(MainWindow* w) {
    w->status_ = new StatusBar(w);
    w->setStatusBar(w->status_);
}

// ============================ UI 状态回同步（自 MainWindow::syncPanels 全体迁入）============================

// 全量同步：左侧面板与右侧「参数/导出/图像」页 + 工具栏/菜单动作到 Document（原 MainWindow::syncPanels）。
// const 引用入参：成员均为指针，改动直达所指对象（QAction 勾选/启停、面板回填）。
void MainWindowUi::syncUi(const MainWindow& w) {
    w.left_->syncFromDocument();
    w.param_->syncFromDocument();
    w.exportPanel_->syncFromDocument();
    w.imagePanel_->syncFromDocument();
    w.layerEditPanel_->syncFromDocument();
    w.layerPanel_->syncFromDocument();   // 图片图层行：随图层栈增删 / 逐层 visible 回同步（blockSignals 防回环）。
    const bool has = w.doc_.hasImage();
    // 图像菜单预处理动作：无图像时统一置灰（原仅「黑白」置灰，其余静默无操作）。
    for (QAction* a : w.preprocessActions_) a->setEnabled(has);
    // 灰度启停智能：**当前预处理目标**会话已产出灰度时置灰「黑白」与「色道分离」
    //（与预处理面板 activeSession().producesGray() 判定同源；选中图层/底图时按对应会话判定，不再只看画布会话）。
    const bool grayBlocked = has && w.doc_.activeSession().producesGray();
    if (w.aGrayMenu_) w.aGrayMenu_->setEnabled(has && !grayBlocked);
    if (w.mSplit_) w.mSplit_->setEnabled(has && !grayBlocked);
    // 「反色」子菜单入口：仅随有无图像整入口置灰（灰度图仍可反色，不吃灰度智能门；与 mSplit_ 同款补齐）。
    if (w.mInvert_) w.mInvert_->setEnabled(has);
    // 模式/极性动作选中态（setChecked 不触发 triggered，无回环）。
    if (w.modeActionL1_) w.modeActionL1_->setChecked(w.doc_.mode() == engine::Tier::L1);
    if (w.modeActionL2_) w.modeActionL2_->setChecked(w.doc_.mode() == engine::Tier::L2);
    if (w.modeActionL3_) w.modeActionL3_->setChecked(w.doc_.mode() == engine::Tier::L3);
    if (w.aPolKeep_) w.aPolKeep_->setChecked(w.doc_.polarity() == engine::Polarity::KEEP);
    if (w.aPolRemove_) w.aPolRemove_->setChecked(w.doc_.polarity() == engine::Polarity::REMOVE);
    // 画布四态与标注工具/叠放/填充菜单勾选态（另有变更信号即时回同步；此处兜底初始与 doc 变化）。
    syncCanvasModeActions(w);
    syncToolActions(w);
    syncAnnoSelActions(w);
    // 切割参数：子菜单按当前切割模式启停 + 勾选态回同步。
    const bool l1 = has && w.doc_.mode() == engine::Tier::L1;
    const bool l2 = has && w.doc_.mode() == engine::Tier::L2;
    const bool l3 = has && w.doc_.mode() == engine::Tier::L3;
    if (w.mL1Shape_) w.mL1Shape_->setEnabled(l1);
    if (w.aL1Rect_) w.aL1Rect_->setChecked(w.doc_.l1Shape() == L1Shape::RECT);
    if (w.aL1HBand_) w.aL1HBand_->setChecked(w.doc_.l1Shape() == L1Shape::HBAND);
    if (w.aL1VBand_) w.aL1VBand_->setChecked(w.doc_.l1Shape() == L1Shape::VBAND);
    if (w.mL2Sub_) w.mL2Sub_->setEnabled(l2);
    if (w.aL2Cross_) w.aL2Cross_->setChecked(w.doc_.l2Sub() == L2Sub::CROSS);
    if (w.aL2HLine_) w.aL2HLine_->setChecked(w.doc_.l2Sub() == L2Sub::HLINE);
    if (w.aL2VLine_) w.aL2VLine_->setChecked(w.doc_.l2Sub() == L2Sub::VLINE);
    if (w.aL2Multi_) w.aL2Multi_->setChecked(w.doc_.l2Sub() == L2Sub::MULTI_RECT);
    if (w.aToGrid_) w.aToGrid_->setEnabled(l2 && (w.doc_.hasRect() || !w.doc_.rects().empty()));
    if (w.mSort_) w.mSort_->setEnabled(l3);
    if (w.aSortRow_) w.aSortRow_->setChecked(w.doc_.order().strategy == engine::SortStrategy::ROW_MAJOR);
    if (w.aSortCol_) w.aSortCol_->setChecked(w.doc_.order().strategy == engine::SortStrategy::COLUMN_MAJOR);
    if (w.aSortCustom_) w.aSortCustom_->setChecked(w.doc_.order().strategy == engine::SortStrategy::CUSTOM);
    if (w.aSortReverse_) w.aSortReverse_->setChecked(w.doc_.order().reverse);
    if (w.aSortSnake_) w.aSortSnake_->setChecked(w.doc_.order().snake);
    // L3 余量策略：仅 L3 启用 + 勾选态回同步（「参数」页余量下拉为真相源，菜单仅镜像）。
    if (w.mRemainder_) w.mRemainder_->setEnabled(l3);
    for (std::size_t i = 0; i < w.remainderActions_.size(); ++i)
        if (w.remainderActions_[i])
            w.remainderActions_[i]->setChecked(
                static_cast<int>(i) == static_cast<int>(w.doc_.gridParams().remainder));
    // 导出设置：输出方式/格式/烧录开关勾选态（面板为真相源，菜单仅镜像）。
    // 置灰规则与导出面板 refreshModeItemStates 一致：坍缩依 Core 坍缩可行性，重排仅 L3（Core 硬约束）。
    if (w.aEmitSep_) w.aEmitSep_->setChecked(w.doc_.emitMode() == engine::EmitMode::SEPARATE);
    if (w.aEmitCol_) {
        w.aEmitCol_->setChecked(w.doc_.emitMode() == engine::EmitMode::MERGED
                                && w.doc_.layout() == engine::MergeLayout::COLLAPSE);
        w.aEmitCol_->setEnabled(w.exportPanel_->collapsible());
    }
    if (w.aEmitRear_) {
        w.aEmitRear_->setChecked(w.doc_.emitMode() == engine::EmitMode::MERGED
                                 && w.doc_.layout() == engine::MergeLayout::REARRANGE);
        w.aEmitRear_->setEnabled(w.doc_.mode() == engine::Tier::L3);
    }
    if (w.aFmtPng_) w.aFmtPng_->setChecked(w.doc_.format() == engine::ExportFormat::PNG);
    if (w.aFmtJpg_) w.aFmtJpg_->setChecked(w.doc_.format() == engine::ExportFormat::JPEG);
    if (w.aFmtWebp_) w.aFmtWebp_->setChecked(w.doc_.format() == engine::ExportFormat::WEBP);
    if (w.aFmtBmp_) w.aFmtBmp_->setChecked(w.doc_.format() == engine::ExportFormat::BMP);
    if (w.aBurnAnno_) w.aBurnAnno_->setChecked(w.annoBridge_.burnInEnabled());
    if (w.aBurnLayers_) w.aBurnLayers_->setChecked(w.doc_.layersBurnIn());
    // 图层菜单动作：作用画布当前拾取层，无拾取层时置灰。
    const bool layerSel = w.doc_.selectedLayerId().has_value();
    if (w.aLayerUp_) w.aLayerUp_->setEnabled(layerSel);
    if (w.aLayerDown_) w.aLayerDown_->setEnabled(layerSel);
    if (w.aLayerTop_) w.aLayerTop_->setEnabled(layerSel);
    if (w.aLayerBottom_) w.aLayerBottom_->setEnabled(layerSel);
    if (w.aLayerRemove_) w.aLayerRemove_->setEnabled(layerSel);
    // 锚点/枢轴子菜单：随拾取层启停 + 勾选态回同步（「图层编辑」面板两组下拉框为真相源，菜单仅镜像）。
    if (w.mLayerAnchor_) w.mLayerAnchor_->setEnabled(layerSel);
    if (w.mLayerPivot_) w.mLayerPivot_->setEnabled(layerSel);
    if (const canvas::ImageLayer* picked = layerSel ? w.doc_.layerById(*w.doc_.selectedLayerId()) : nullptr) {
        for (std::size_t i = 0; i < w.layerAnchorActions_.size(); ++i)
            if (w.layerAnchorActions_[i])
                w.layerAnchorActions_[i]->setChecked(
                    static_cast<int>(i) == static_cast<int>(picked->transform.inCanvasAnchor));
        for (std::size_t i = 0; i < w.layerPivotActions_.size(); ++i)
            if (w.layerPivotActions_[i])
                w.layerPivotActions_[i]->setChecked(
                    static_cast<int>(i) == static_cast<int>(picked->transform.layerPivot));
    }
    // 编辑菜单 L3 选择集动作：仅 L3 模式可用。
    if (w.aCellsAll_) w.aCellsAll_->setEnabled(l3);
    if (w.aCellsInvert_) w.aCellsInvert_->setEnabled(l3);
    if (w.aCellsClear_) w.aCellsClear_->setEnabled(l3);
}

// 画布四态菜单勾选态回同步（读视图当前模式；applyCanvasMode 落地后由 MainWindow::connectAll 第二条连接调用）。
void MainWindowUi::syncCanvasModeActions(const MainWindow& w) {
    if (!w.view_) return;
    const CanvasMode m = w.view_->canvasMode();
    if (w.aCanvasMarquee_) w.aCanvasMarquee_->setChecked(m == CanvasMode::Marquee);
    if (w.aCanvasImage_) w.aCanvasImage_->setChecked(m == CanvasMode::ImagePick);
    if (w.aCanvasAnnoSel_) w.aCanvasAnnoSel_->setChecked(m == CanvasMode::AnnoSelect);
    if (w.aCanvasAnnoCreate_) w.aCanvasAnnoCreate_->setChecked(m == CanvasMode::AnnoCreate);
}

// 标注工具菜单勾选态回同步（读桥当前工具；toolChanged 与 syncUi 调用）。
void MainWindowUi::syncToolActions(const MainWindow& w) {
    const int idx = static_cast<int>(w.annoBridge_.currentTool());
    for (std::size_t i = 0; i < w.toolActions_.size(); ++i)
        if (w.toolActions_[i]) w.toolActions_[i]->setChecked(static_cast<int>(i) == idx);
}

// 标注叠放次序动作启用态（依赖选中项）与「填充」勾选态回同步
// （AnnotationBridge::selectionChanged/changed 与 syncUi 调用）。
void MainWindowUi::syncAnnoSelActions(const MainWindow& w) {
    const bool sel = w.annoBridge_.selectedIndex().has_value();
    if (w.aZRaise_) w.aZRaise_->setEnabled(sel);
    if (w.aZLower_) w.aZLower_->setEnabled(sel);
    if (w.aZFront_) w.aZFront_->setEnabled(sel);
    if (w.aZBack_) w.aZBack_->setEnabled(sel);
    if (w.aFill_) w.aFill_->setChecked(w.annoBridge_.displayFill());
}

} // namespace idc::gui
