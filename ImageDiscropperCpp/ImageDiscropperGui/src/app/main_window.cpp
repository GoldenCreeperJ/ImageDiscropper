// ============================================================================
// 文件：app/main_window.cpp
// 作用：实现主窗口的装配、数据流编排与状态栏呈现（见同名头文件说明）。
// 分块依据：
//   - buildCentral/buildMenus/buildToolbar/buildStatus/connectAll 各管一块装配（单键快捷键在 keyPressEvent 处理）；
//   - on* 槽把用户意图落到 Document，再经 refreshPreview 调 EngineBridge（Core）刷新画布；
//   - MainWindow 不含任何切割/几何/导出实现（A-0.1），只做编排与呈现。
// ============================================================================
#include "app/main_window.h"

#include <algorithm>
#include <functional>
#include <string>
#include <utility>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressDialog>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#include "canvas/canvas_scene.h"
#include "canvas/canvas_view.h"
#include "panels/annotation_prop_panel.h"
#include "panels/export_panel.h"
#include "panels/image_panel.h"
#include "panels/layer_panel.h"
#include "panels/left_panel.h"
#include "panels/param_panel.h"
#include "panels/tool_panel.h"
#include "util/image_qt_adapter.h"
#include "util/output_preview_renderer.h"
#include "util/path_qt_adapter.h"

#ifndef IDC_GUI_VERSION
#define IDC_GUI_VERSION "dev"
#endif

namespace idc::gui {

// 输出预览（G-11 / §4.6）尺寸参数：
//   kExportSrcMaxDim   —— 预览专用小源图的最长边上限（缩略图只在此小图上 blit，保证快）。
//   kExportPreviewMaxDim —— 输出缩略图的最长边上限（像素少、仅示意，与面板标签框相区隔）。
constexpr int kExportSrcMaxDim = 512;
constexpr int kExportPreviewMaxDim = 192;

// 构造：装配全部部件并显示首次引导。
MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("ImageDiscropper GUI %1").arg(QStringLiteral(IDC_GUI_VERSION)));
    resize(1280, 800);

    buildCentral();
    buildMenus();
    buildToolbar();
    buildStatus();

    // 全局撤销/重做（G-12）：防抖定时器把拖拽/连点的连续参数变更合并为一条历史。
    docHistoryTimer_ = new QTimer(this);
    docHistoryTimer_->setSingleShot(true);
    docHistoryTimer_->setInterval(500); // 500ms 静默即视为一次离散操作结束（拖拽释放/滑块停手）。
    connect(docHistoryTimer_, &QTimer::timeout, this, &MainWindow::onDocHistoryTimeout);

    connectAll();

    syncPanels();
    refreshPreview();
    resetDocHistory();          // 以初始参数态为撤销基线（G-12）。
    updateUndoRedoEnabled();
    notify(QStringLiteral("请打开一张图像开始（Ctrl+O）。"), false);
    showFirstRunGuide();
}

// 装配中央区：左面板 | 画布 | 右侧参数/导出选项卡（§4.1）。
void MainWindow::buildCentral() {
    scene_ = new CanvasScene(this);
    view_ = new CanvasView(scene_, this);

    left_ = new LeftPanel(this);
    param_ = new ParamPanel(this);
    exportPanel_ = new ExportPanel(this);
    imagePanel_ = new ImagePanel(this);
    toolPanel_ = new ToolPanel(this);
    layerPanel_ = new LayerPanel(this);
    annoPropPanel_ = new AnnotationPropPanel(this);
    left_->setDocument(&doc_);
    param_->setDocument(&doc_);
    exportPanel_->setDocument(&doc_);
    imagePanel_->setDocument(&doc_);
    toolPanel_->setModel(&annoBridge_);
    layerPanel_->setModel(&annoBridge_);
    annoPropPanel_->setModel(&annoBridge_);

    // 右侧用 QTabWidget 分组，避免面板过长（§4.1 布局约束）。
    rightTabs_ = new QTabWidget(this);
    rightTabs_->addTab(param_, QStringLiteral("参数"));
    rightTabs_->addTab(exportPanel_, QStringLiteral("导出"));
    rightTabs_->addTab(imagePanel_, QStringLiteral("图像"));
    rightTabs_->addTab(annoPropPanel_, QStringLiteral("标注"));
    rightTabs_->setMinimumWidth(240); // 可拖拽调宽；设下限避免控件被挤到不可用。

    // 左侧容器：模式/极性(LeftPanel) + 标注工具(ToolPanel) + 图层(LayerPanel) 竖排，可滚动避免拥挤。
    auto* leftContainer = new QWidget(this);
    auto* leftLayout = new QVBoxLayout(leftContainer);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(6);
    leftLayout->addWidget(left_);
    leftLayout->addWidget(toolPanel_);
    leftLayout->addWidget(layerPanel_);
    leftLayout->addStretch(1);
    auto* leftScroll = new QScrollArea(this);
    leftScroll->setWidgetResizable(true);
    leftScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    leftScroll->setWidget(leftContainer);
    // 左面板最小宽：标注工具为 3 列网格（含「等腰直角三角形」等宽标签），需足够宽才不拥挤/截断。
    leftScroll->setMinimumWidth(340);

    auto* split = new QSplitter(Qt::Horizontal, this);
    split->addWidget(leftScroll);
    split->addWidget(view_);
    split->addWidget(rightTabs_);
    split->setStretchFactor(0, 0);   // 左面板：窗口整体缩放时不抢空间
    split->setStretchFactor(1, 1);   // 画布：占据剩余空间
    split->setStretchFactor(2, 0);   // 右面板：窗口整体缩放时不抢空间
    split->setChildrenCollapsible(false); // 禁止把面板拖到 0 而完全折叠消失
    split->setHandleWidth(6);             // 稍宽的分隔条，拖拽手感更明显
    split->setSizes({340, 860, 300});     // 初始宽度（左面板预留足够容纳标注工具网格；此后可自由拖拽）
    setCentralWidget(split);
}

// 装配菜单栏（§4.1）。后续阶段的功能先以禁用项占位，明确交付边界。
void MainWindow::buildMenus() {
    // ---- 文件 ----
    QMenu* mFile = menuBar()->addMenu(QStringLiteral("文件(&F)"));
    QAction* aOpen = mFile->addAction(QStringLiteral("打开图像(&O)"));
    aOpen->setShortcut(QKeySequence::Open);
    connect(aOpen, &QAction::triggered, this, &MainWindow::onOpen);
    QAction* aExport = mFile->addAction(QStringLiteral("导出(&S)"));
    aExport->setShortcut(QKeySequence::Save);
    connect(aExport, &QAction::triggered, this, &MainWindow::onExport);
    mFile->addSeparator();
    // 配置文件加载/保存（G-13 / FR-L3.8）：复用 Core loadEngineConfig/saveEngineConfig（§9 schema）。
    QAction* aLoadCfg = mFile->addAction(QStringLiteral("加载配置(&L)…"));
    aLoadCfg->setToolTip(QStringLiteral("从 JSON 配置文件还原切割/排序/导出参数（§9 schema）；可撤销。"));
    connect(aLoadCfg, &QAction::triggered, this, &MainWindow::onLoadConfig);
    QAction* aSaveCfg = mFile->addAction(QStringLiteral("保存配置(&C)…"));
    aSaveCfg->setToolTip(QStringLiteral("把当前切割/排序/导出参数存为 JSON 配置文件，可复用于同尺寸图像。"));
    connect(aSaveCfg, &QAction::triggered, this, &MainWindow::onSaveConfig);
    mFile->addSeparator();
    const QAction* aExit = mFile->addAction(QStringLiteral("退出(&X)"));
    connect(aExit, &QAction::triggered, this, &QWidget::close);

    // ---- 编辑（全局撤销/重做 G-12：上下文路由，复用 Core HistoryManager）----
    QMenu* mEdit = menuBar()->addMenu(QStringLiteral("编辑(&E)"));
    aUndo_ = mEdit->addAction(QStringLiteral("撤销(&U)"));
    aUndo_->setShortcut(QKeySequence::Undo);
    aUndo_->setToolTip(QStringLiteral("撤销（Ctrl+Z）：标注上下文（绘制工具激活/选中标注）撤销标注，否则撤销文档参数变更。"));
    connect(aUndo_, &QAction::triggered, this, &MainWindow::onUndo);
    aRedo_ = mEdit->addAction(QStringLiteral("重做(&R)"));
    aRedo_->setShortcut(QKeySequence::Redo);
    aRedo_->setToolTip(QStringLiteral("重做（Ctrl+Y）：与撤销对称。"));
    connect(aRedo_, &QAction::triggered, this, &MainWindow::onRedo);
    mEdit->addSeparator();
    QAction* aClear = mEdit->addAction(QStringLiteral("清除选区(&C)"));
    aClear->setShortcut(QKeySequence(Qt::Key_Escape));
    connect(aClear, &QAction::triggered, this, &MainWindow::onClearCut);

    // ---- 图像（预处理 FR-1 / G-3：旋转/翻转/黑白/反色/重置；完整控件见右侧「图像」页）----
    QMenu* mImage = menuBar()->addMenu(QStringLiteral("图像(&I)"));
    const QAction* aRotL = mImage->addAction(QStringLiteral("左转 90°"));
    connect(aRotL, &QAction::triggered, this, [this] { onRotate(-90); });
    const QAction* aRotR = mImage->addAction(QStringLiteral("右转 90°"));
    connect(aRotR, &QAction::triggered, this, [this] { onRotate(90); });
    const QAction* aRot180 = mImage->addAction(QStringLiteral("旋转 180°"));
    connect(aRot180, &QAction::triggered, this, [this] { onRotate(180); });
    mImage->addSeparator();
    const QAction* aFlipH = mImage->addAction(QStringLiteral("水平翻转"));
    connect(aFlipH, &QAction::triggered, this, [this] { onFlip(true); });
    const QAction* aFlipV = mImage->addAction(QStringLiteral("垂直翻转"));
    connect(aFlipV, &QAction::triggered, this, [this] { onFlip(false); });
    mImage->addSeparator();
    const QAction* aGray = mImage->addAction(QStringLiteral("黑白"));
    connect(aGray, &QAction::triggered, this, &MainWindow::onGray);
    const QAction* aInvert = mImage->addAction(QStringLiteral("反色（全通道）"));
    connect(aInvert, &QAction::triggered, this, [this] { onInvert(true, true, true); });
    mImage->addSeparator();
    const QAction* aResetPre = mImage->addAction(QStringLiteral("重置预处理"));
    connect(aResetPre, &QAction::triggered, this, &MainWindow::onResetPreprocess);
    const QAction* aImagePanel = mImage->addAction(QStringLiteral("图像处理面板（缩放/尺寸/色道）…"));
    connect(aImagePanel, &QAction::triggered, this, [this] {
        if (rightTabs_ && imagePanel_) rightTabs_->setCurrentWidget(imagePanel_);
    });

    // ---- 标注（第四阶段 G-4/G-5）：撤销/重做/删除选中/清除全部/属性定位 ----
    // 标注撤销/重做复用 Core AnnotationLayer 内建分层快照；快捷键 Ctrl+Z/Y 已统一交给编辑菜单
    // （上下文路由：标注上下文时自动转发到此处），故本菜单项不再绑定快捷键，避免冲突。
    QMenu* mAnno = menuBar()->addMenu(QStringLiteral("标注(&A)"));
    const QAction* aAnnoUndo = mAnno->addAction(QStringLiteral("撤销标注(&U)"));
    connect(aAnnoUndo, &QAction::triggered, this, &MainWindow::onAnnoUndo);
    const QAction* aAnnoRedo = mAnno->addAction(QStringLiteral("重做标注(&R)"));
    connect(aAnnoRedo, &QAction::triggered, this, &MainWindow::onAnnoRedo);
    mAnno->addSeparator();
    QAction* aAnnoDel = mAnno->addAction(QStringLiteral("删除选中标注(&D)"));
    aAnnoDel->setToolTip(QStringLiteral("删除当前选中的标注（或按 Delete 键）。"));
    connect(aAnnoDel, &QAction::triggered, this, &MainWindow::onAnnoDeleteSelected);
    const QAction* aAnnoClear = mAnno->addAction(QStringLiteral("清除全部标注(&C)"));
    connect(aAnnoClear, &QAction::triggered, this, &MainWindow::onAnnoClearAll);
    mAnno->addSeparator();
    const QAction* aAnnoProp = mAnno->addAction(QStringLiteral("标注属性…"));
    connect(aAnnoProp, &QAction::triggered, this, [this] {
        if (rightTabs_ && annoPropPanel_) rightTabs_->setCurrentWidget(annoPropPanel_);
    });

    // ---- 视图 ----
    QMenu* mView = menuBar()->addMenu(QStringLiteral("视图(&V)"));
    QAction* aZoomIn = mView->addAction(QStringLiteral("放大"));
    aZoomIn->setShortcut(QKeySequence::ZoomIn);
    connect(aZoomIn, &QAction::triggered, this, [this] { view_->zoomIn(); });
    QAction* aZoomOut = mView->addAction(QStringLiteral("缩小"));
    aZoomOut->setShortcut(QKeySequence::ZoomOut);
    connect(aZoomOut, &QAction::triggered, this, [this] { view_->zoomOut(); });
    QAction* aFit = mView->addAction(QStringLiteral("适应窗口"));
    aFit->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    connect(aFit, &QAction::triggered, this, [this] { view_->fitToWindow(); });
    const QAction* aReset = mView->addAction(QStringLiteral("重置视图 (1:1)"));
    connect(aReset, &QAction::triggered, this, [this] { view_->resetZoom(); });
    mView->addSeparator();
    const QAction* aMasks = mView->addAction(QStringLiteral("切换预览遮罩"));
    connect(aMasks, &QAction::triggered, this, &MainWindow::onToggleMasks);

    // ---- 帮助 ----
    QMenu* mHelp = menuBar()->addMenu(QStringLiteral("帮助(&H)"));
    const QAction* aUsage = mHelp->addAction(QStringLiteral("使用说明"));
    connect(aUsage, &QAction::triggered, this, [this] {
        QMessageBox::information(this, QStringLiteral("使用说明"),
            QStringLiteral("1) 文件→打开图像。\n"
                           "2) 左侧选择模式（L1 标准提取 / L2 反向剔除 / L3 网格分割）与极性（保留/删除框内）。\n"
                           "3) L1/L2：在画布上拖拽出选区；橙色切割线贯穿全图（与选区同色），绿色为保留、红色为删除。\n"
                           "4) 可拖动选区/四角手柄调整，或直接拖动橙色切割线（＝选区边）移动对应边；方向键微调（Shift 大步）。\n"
                           "5) L3：在右侧「参数」页设定基准点、单元尺寸与余量策略，网格线自动铺满全图；用全选/反选/清空与排序策略控制输出。\n"
                           "6) 右侧「导出」页选择输出模式与路径，点击导出（Ctrl+S）。\n\n"
                           "完整操作手册见程序目录下的 USAGE.md（设计说明见 DESIGN.md）。"));
    });
    const QAction* aAbout = mHelp->addAction(QStringLiteral("关于"));
    connect(aAbout, &QAction::triggered, this, [this] {
        QMessageBox::about(this, QStringLiteral("关于"),
            QStringLiteral("ImageDiscropper GUI %1\n基于统一 Grid-Selection-Emit 引擎（Core %2）。\n"
                           "L1/L2/L3 共用同一代码路径，模式仅为参数预设。")
                .arg(QStringLiteral(IDC_GUI_VERSION), QStringLiteral(IDC_GUI_VERSION)));
    });
}

// 装配工具栏（§4.1/§4.3）：打开/导出、缩放、模式切换（遮罩切换已迁入左侧图层面板）。
void MainWindow::buildToolbar() {
    QToolBar* tb = addToolBar(QStringLiteral("主工具栏"));
    tb->setMovable(false);

    const QAction* aOpen = tb->addAction(QStringLiteral("打开"));
    connect(aOpen, &QAction::triggered, this, &MainWindow::onOpen);
    const QAction* aExport = tb->addAction(QStringLiteral("导出"));
    connect(aExport, &QAction::triggered, this, &MainWindow::onExport);
    tb->addSeparator();
    // 撤销/重做（G-12 / §4.7 工具栏按钮）：复用编辑菜单同一 QAction，共享 Ctrl+Z/Y 快捷键与动态启用态。
    if (aUndo_) tb->addAction(aUndo_);
    if (aRedo_) tb->addAction(aRedo_);
    tb->addSeparator();

    const QAction* aZoomIn = tb->addAction(QStringLiteral("放大"));
    connect(aZoomIn, &QAction::triggered, this, [this] { view_->zoomIn(); });
    const QAction* aZoomOut = tb->addAction(QStringLiteral("缩小"));
    connect(aZoomOut, &QAction::triggered, this, [this] { view_->zoomOut(); });
    const QAction* aFit = tb->addAction(QStringLiteral("适应"));
    connect(aFit, &QAction::triggered, this, [this] { view_->fitToWindow(); });
    tb->addSeparator();

    // 模式切换（§4.3）：三个可选动作（L1 标准提取 / L2 反向剔除 / L3 网格分割）。
    auto* group = new QActionGroup(this);
    group->setExclusive(true);
    modeActionL1_ = group->addAction(QStringLiteral("标准提取 (L1)"));
    modeActionL2_ = group->addAction(QStringLiteral("反向剔除 (L2)"));
    modeActionL3_ = group->addAction(QStringLiteral("网格分割 (L3)"));
    for (QAction* a : {modeActionL1_, modeActionL2_, modeActionL3_}) {
        a->setCheckable(true);
        tb->addAction(a);
    }
    modeActionL1_->setChecked(true);
    modeActionL3_->setToolTip(QStringLiteral("网格分割：铺满全图的网格 + 单元选择 + 排序。"));
    connect(modeActionL1_, &QAction::triggered, this, [this] { onModeAction(1); });
    connect(modeActionL2_, &QAction::triggered, this, [this] { onModeAction(2); });
    connect(modeActionL3_, &QAction::triggered, this, [this] { onModeAction(3); });
    // 遮罩切换已迁入左侧「图层」面板（成为正式图层项）；视图菜单与画布右键仍保留快捷切换。
}

// 装配状态栏（§4.9）：光标坐标 / 像素颜色 / 当前模式 / 选中块数 / 缩放倍数 / 提示信息。
void MainWindow::buildStatus() {
    stCoord_ = new QLabel(QStringLiteral("坐标: (-, -)"), this);
    stColor_ = new QLabel(QStringLiteral("RGB(-,-,-)"), this);
    stMode_ = new QLabel(modeName(doc_.mode()), this);
    stCount_ = new QLabel(QStringLiteral("保留块: 0"), this);
    stZoom_ = new QLabel(QStringLiteral("缩放: 100%"), this);
    stHint_ = new QLabel(QStringLiteral("就绪"), this);
    statusBar()->addWidget(stCoord_);
    statusBar()->addWidget(stColor_);
    statusBar()->addWidget(stMode_);
    statusBar()->addWidget(stCount_);
    statusBar()->addWidget(stZoom_);
    statusBar()->addWidget(stHint_, 1); // 提示信息占据剩余空间。
}

// 串联全部信号槽，形成「交互 → Document → Core → 画布」数据流。
void MainWindow::connectAll() {
    connect(&doc_, &Document::imageChanged, this, &MainWindow::onImageChanged);
    connect(&doc_, &Document::changed, this, &MainWindow::onDocChanged);
    // 全局撤销/重做（G-12）：参数变更同时驱动防抖历史采集（与 onDocChanged 刷新并行，互不干扰）。
    connect(&doc_, &Document::changed, this, &MainWindow::scheduleHistoryCapture);

    connect(view_, &CanvasView::rubberSelect, this, &MainWindow::onRubberSelect);
    connect(view_, &CanvasView::nudgeSelection, this, &MainWindow::onNudge);
    connect(view_, &CanvasView::cursorScenePos, this, &MainWindow::onCursor);
    connect(view_, &CanvasView::zoomChanged, this, &MainWindow::onZoomChanged);
    connect(view_, &CanvasView::clearCutRequested, this, &MainWindow::onClearCut);
    connect(view_, &CanvasView::toggleMasksRequested, this, &MainWindow::onToggleMasks);
    connect(view_, &CanvasView::resetViewRequested, view_, &CanvasView::fitToWindow);
    connect(scene_, &CanvasScene::selectionEdited, this, &MainWindow::onSelectionEdited);
    connect(scene_, &CanvasScene::multiRectEdited, this, &MainWindow::onMultiRectEdited);
    connect(scene_, &CanvasScene::cellToggled, this, &MainWindow::onCellToggled);
    connect(scene_, &CanvasScene::cellsMarqueeSelected, this, &MainWindow::onCellsMarquee);
    connect(scene_, &CanvasScene::cellReordered, this, &MainWindow::onCellReordered);

    connect(exportPanel_, &ExportPanel::exportRequested, this, &MainWindow::onExport);
    connect(param_, &ParamPanel::rectSelected, this, &MainWindow::onRectSelected);

    // 图像处理面板（预处理）：各意图信号→对应槽（经 EngineBridge 调 Core pixel_ops）。
    connect(imagePanel_, &ImagePanel::rotateRequested, this, &MainWindow::onRotate);
    connect(imagePanel_, &ImagePanel::flipRequested, this, &MainWindow::onFlip);
    connect(imagePanel_, &ImagePanel::scaleRequested, this, &MainWindow::onScale);
    connect(imagePanel_, &ImagePanel::resizeRequested, this, &MainWindow::onResize);
    connect(imagePanel_, &ImagePanel::grayRequested, this, &MainWindow::onGray);
    connect(imagePanel_, &ImagePanel::invertRequested, this, &MainWindow::onInvert);
    connect(imagePanel_, &ImagePanel::splitRequested, this, &MainWindow::onSplit);
    connect(imagePanel_, &ImagePanel::resetRequested, this, &MainWindow::onResetPreprocess);

    // ---- 标注（G-4/G-5）：模型/画布/面板 → MainWindow 编排 ----
    connect(&annoBridge_, &AnnotationBridge::changed, this, &MainWindow::onAnnoBridgeChanged);
    // 绘制拖拽预览（橡皮筋）：begin/updateShape 仅发 pendingChanged，必须连到实时刷新预览图元，
    // 否则拖拽过程中形状不显示、只有松手提交（changed）后才可见。
    connect(&annoBridge_, &AnnotationBridge::pendingChanged, this, &MainWindow::onAnnoPendingChanged);
    connect(&annoBridge_, &AnnotationBridge::selectionChanged, this, &MainWindow::onAnnoSelectionChanged);
    connect(&annoBridge_, &AnnotationBridge::toolChanged, this, &MainWindow::onAnnoToolChanged);

    connect(view_, &CanvasView::annoDragStart, this, &MainWindow::onAnnoDragStart);
    connect(view_, &CanvasView::annoDragMove, this, &MainWindow::onAnnoDragMove);
    connect(view_, &CanvasView::annoDragEnd, this, &MainWindow::onAnnoDragEnd);
    connect(view_, &CanvasView::annoHover, this, &MainWindow::onAnnoHover);
    connect(view_, &CanvasView::annoFinish, this, &MainWindow::onAnnoFinish);
    connect(view_, &CanvasView::annoEscape, this, &MainWindow::onAnnoEscape);

    connect(scene_, &CanvasScene::annotationSelectRequested, this, &MainWindow::onAnnotationSelect);
    connect(scene_, &CanvasScene::annotationMoved, this, &MainWindow::onAnnotationMoved);
    connect(scene_, &CanvasScene::annotationTransformed, this, &MainWindow::onAnnotationTransformed);
    connect(scene_, &CanvasScene::annotationTransformPreview, this, &MainWindow::onAnnotationTransformPreview);

    connect(toolPanel_, &ToolPanel::toolSelected, this, &MainWindow::onToolSelected);
    connect(layerPanel_, &LayerPanel::baseVisibilityChanged, this, &MainWindow::onBaseVisibilityChanged);
    connect(layerPanel_, &LayerPanel::maskVisibilityChanged, this, &MainWindow::onMaskVisibilityChanged);
    connect(layerPanel_, &LayerPanel::gridVisibilityChanged, this, &MainWindow::onGridVisibilityChanged);
    connect(layerPanel_, &LayerPanel::cutLineVisibilityChanged, this, &MainWindow::onCutLineVisibilityChanged);
    connect(layerPanel_, &LayerPanel::selectionVisibilityChanged, this, &MainWindow::onSelectionVisibilityChanged);
    connect(layerPanel_, &LayerPanel::annotationVisibilityChanged, this, &MainWindow::onAnnotationVisibilityChanged);
    // 「导出时烧录标注」开关已迁至导出面板（原属图层面板）。
    connect(exportPanel_, &ExportPanel::burnInChanged, this, &MainWindow::onBurnInChanged);
    exportPanel_->setBurnInChecked(annoBridge_.burnInEnabled()); // 初始同步一次（两侧默认 false，对齐意图）。
    // 切到「导出」页时补渲染输出预览（A：不可见时不渲染，切回时若已脏则重算）。
    connect(rightTabs_, &QTabWidget::currentChanged, this, &MainWindow::onRightTabChanged);
    connect(annoPropPanel_, &AnnotationPropPanel::colorPicked, this, &MainWindow::onAnnoColorPicked);
    connect(annoPropPanel_, &AnnotationPropPanel::strokeChanged, this, &MainWindow::onAnnoStrokeChanged);
    connect(annoPropPanel_, &AnnotationPropPanel::fillChanged, this, &MainWindow::onAnnoFillChanged);
    connect(annoPropPanel_, &AnnotationPropPanel::textChanged, this, &MainWindow::onAnnoTextChanged);
    connect(annoPropPanel_, &AnnotationPropPanel::fontSizeChanged, this, &MainWindow::onAnnoFontSizeChanged);
    connect(annoPropPanel_, &AnnotationPropPanel::transformApplyRequested, this, &MainWindow::onAnnoTransformApply);
}

// 依工作图重建降采样预览底图（NFR-3）。
void MainWindow::rebuildPreviewPixmap() {
    if (!doc_.hasImage()) {
        scene_->clearAll();
        exportSrcPixmap_ = QPixmap();   // 无图像：清空输出预览小源图。
        return;
    }
    preview_ = makePreview(doc_.working(), previewMaxDim_);
    const QPixmap pm = toPixmap(preview_.image);
    // 场景坐标 = 原图像素坐标；底图用放大系数把预览 pixmap 铺到原图尺寸。
    scene_->setBaseImage(pm, preview_.scaleX, preview_.scaleY, doc_.width(), doc_.height());
    // 输出预览（G-11）专用小源图：把底图再降到最长边 ≤ kExportSrcMaxDim，使缩略图只在小图上 blit。
    const int longest = std::max(pm.width(), pm.height());
    exportSrcPixmap_ = longest > kExportSrcMaxDim
        ? pm.scaled(kExportSrcMaxDim, kExportSrcMaxDim, Qt::KeepAspectRatio, Qt::SmoothTransformation)
        : pm;
    // working（原图）→ 小源图 的放大系数（供 util::composeOutputThumbnail 把 Composition 源区域映射到小图坐标）。
    exportSrcScaleX_ = exportSrcPixmap_.width()  > 0 ? static_cast<double>(doc_.width())  / exportSrcPixmap_.width()  : 1.0;
    exportSrcScaleY_ = exportSrcPixmap_.height() > 0 ? static_cast<double>(doc_.height()) / exportSrcPixmap_.height() : 1.0;
    view_->fitToWindow();
}

// 跑 Core 预览并刷新画布与状态（A-0.8 实时预览）。
void MainWindow::refreshPreview() {
    stMode_->setText(modeName(doc_.mode()));
    // 重排为 L3 专属：先把重排上下文清零（非 L3 保持 0），L3 分支再回灌真实保留块数/格尺寸。
    exportPanel_->setRearrangeContext(0, 0, 0);

    // 无图像：清空叠加层。
    if (!doc_.hasImage()) {
        scene_->clearCutLines();
        scene_->clearGrid();
        scene_->clearMultiRects();
        scene_->updateMasks(engine::EngineResult{});
        updateExportPreview(engine::EngineResult{});
        stCount_->setText(QStringLiteral("保留块: 0"));
        stHint_->setText(QStringLiteral("请打开图像"));
        exportPanel_->setPreviewInfo(QStringLiteral("尚未加载图像。"));
        return;
    }

    // L3 网格分割：网格由「基准点 + 单元尺寸 + 余量策略」定义，不依赖选区矩形，
    // 故在选区判定之前单独处理。网格线交给 GridLayer 画灰色虚线；橙色选区框在 L3 隐藏
    // （单元点选交互由后续增量接入）。遮罩仍复用 runEngine 的 kept 结果（红底 + 绿块）。
    if (doc_.mode() == engine::Tier::L3) {
        const engine::EngineConfig cfg = doc_.buildEngineConfig();
        // 依 Core 网格产出刷新网格线，并回灌派生行列数（供参数面板只读显示）。
        const engine::Grid grid = EngineBridge::buildGrid(cfg.cut, cfg.source);
        doc_.setDerivedGridSize(grid.rowCount(), grid.colCount());
        scene_->updateGrid(grid, true);
        scene_->clearCutLines();                   // L3 不画 L1/L2 的橙色贯穿切割线。
        scene_->clearMultiRects();                 // L3 不显示 L2 多矩形轮廓。
        scene_->syncSelection(doc_.rect(), false); // 隐藏橙色选区框。
        // 把当前选择集与排序策略下发给点选图元（高亮已选单元）。
        scene_->updateCellSelection(doc_.selectedCells(), doc_.order().strategy);

        const engine::EngineResult res = EngineBridge::runPreview(doc_.working(), cfg);
        scene_->updateMasks(res);
        updateExportPreview(res);

        const int kept = res.ok ? static_cast<int>(res.kept.size()) : 0;
        stCount_->setText(QStringLiteral("保留块: %1").arg(kept));
        stHint_->setText(res.ok ? QStringLiteral("就绪") : QString::fromStdString(res.error));
        exportPanel_->setCollapsible(res.collapsible);
        // 回灌重排上下文：保留块数 + 网格单元尺寸（供自动 cols/rows 与画布/单元尺寸警告）。
        exportPanel_->setRearrangeContext(kept, doc_.gridParams().cellWidth, doc_.gridParams().cellHeight);
        if (res.ok) {
            exportPanel_->setPreviewInfo(QStringLiteral("网格: %1×%2 · 输出画布 %3×%4 · 保留 %5 块")
                .arg(grid.rowCount()).arg(grid.colCount())
                .arg(res.composition.canvasWidth).arg(res.composition.canvasHeight).arg(kept));
        } else {
            exportPanel_->setPreviewInfo(QStringLiteral("无有效结果：%1").arg(QString::fromStdString(res.error)));
        }
        return;
    }

    // 非 L3：清空网格线（若从 L3 切回）。
    scene_->clearGrid();

    // L2 多矩形并集剔除：选区来自 rects_（非单 rect_），单独处理。
    // 用 Core 诱导网格画灰色网格线（各矩形十字带并集诱导），但不启动单元点选图元——
    // picker 会 grab 鼠标、阻断画布框选（多矩形靠框选逐个追加）。橙色轮廓标出各矩形，
    // 单选区框隐藏。遮罩复用 runPreview 的 kept 结果（A-0.1：GUI 不算并集）。
    if (doc_.mode() == engine::Tier::L2 && doc_.l2Sub() == L2Sub::MULTI_RECT) {
        scene_->updateMultiRects(doc_.rects());     // 增量刷新可拖拽选区框（拖拽中不回设正在拖者）。
        scene_->clearCutLines();
        scene_->syncSelection(doc_.rect(), false);  // 隐藏单选区框（改用多矩形轮廓）。
        if (doc_.rects().empty()) {
            // 尚无矩形：不跑引擎（Core 对空 rects 的 MULTI_RECT 会报错），提示框选追加。
            scene_->updateMultiRectCutLines(engine::Grid{}, false);
            scene_->updateMasks(engine::EngineResult{});
            updateExportPreview(engine::EngineResult{});
            stCount_->setText(QStringLiteral("保留块: 0"));
            stHint_->setText(QStringLiteral("在画布上拖拽以追加矩形"));
            exportPanel_->setPreviewInfo(QStringLiteral("等待矩形…"));
            return;
        }
        const engine::EngineConfig cfg = doc_.buildEngineConfig();
        const engine::Grid grid = EngineBridge::buildGrid(cfg.cut, cfg.source);
        scene_->updateMultiRectCutLines(grid, true);
        const engine::EngineResult res = EngineBridge::runPreview(doc_.working(), cfg);
        scene_->updateMasks(res);
        updateExportPreview(res);

        const int kept = res.ok ? static_cast<int>(res.kept.size()) : 0;
        stCount_->setText(QStringLiteral("保留块: %1").arg(kept));
        stHint_->setText(res.ok ? QStringLiteral("就绪") : QString::fromStdString(res.error));
        param_->setCollapseHint(res.collapsible, res.ok ? QString() : QString::fromStdString(res.error));
        exportPanel_->setCollapsible(res.collapsible);
        if (res.ok) {
            exportPanel_->setPreviewInfo(QStringLiteral("多矩形: %1 个 · 输出画布 %2×%3 · 保留 %4 块")
                .arg(static_cast<int>(doc_.rects().size()))
                .arg(res.composition.canvasWidth).arg(res.composition.canvasHeight).arg(kept));
        } else {
            exportPanel_->setPreviewInfo(QStringLiteral("无有效结果：%1").arg(QString::fromStdString(res.error)));
        }
        return;
    }

    // 非多矩形：清空多矩形轮廓（从 MULTI_RECT 切回其他子功能/模式时）。
    scene_->clearMultiRects();

    // 有图但无选区：提示用户拖拽创建选区（不跑引擎，避免 E-1 噪声）。
    if (!doc_.hasRect()) {
        scene_->clearCutLines();
        scene_->updateMasks(engine::EngineResult{});
        updateExportPreview(engine::EngineResult{});
        scene_->syncSelection(doc_.rect(), false);
        stCount_->setText(QStringLiteral("保留块: 0"));
        stHint_->setText(QStringLiteral("在画布上拖拽以创建选区"));
        exportPanel_->setPreviewInfo(QStringLiteral("等待选区…"));
        return;
    }

    // 组装配置 → 调 Core 预览（仅区域数学，实时）。
    const engine::EngineConfig cfg = doc_.buildEngineConfig();
    const engine::EngineResult res = EngineBridge::runPreview(doc_.working(), cfg);

    // 切割线来自 Core generateCutLines（GUI 不自算几何）；场景据此判定选区哪几条边有贯穿切割线，
    // 下发给选区框绘制为橙色贯穿线——切割线即选区边的延伸，拖动选区边＝移动切割线（同一图元）。
    scene_->updateCutLines(EngineBridge::cutLines(cfg.cut, cfg.source), doc_.rect());
    scene_->updateMasks(res);
    updateExportPreview(res);
    // 拖拽进行中不用 Document 的取整矩形回设选区框：它已被手势精确定位（qreal 亚像素），
    // 逐帧回设会带来整数量化抖动 + 覆盖全图 boundingRect 的 prepareGeometryChange/update 卡顿；
    // 释放时（已退出拖拽态）会照常回设，保证最终与 Document 一致。
    if (!scene_->isDraggingSelection()) scene_->syncSelection(doc_.rect(), true);

    // 状态栏与面板提示。
    const int kept = res.ok ? static_cast<int>(res.kept.size()) : 0;
    stCount_->setText(QStringLiteral("保留块: %1").arg(kept));
    stHint_->setText(res.ok ? QStringLiteral("就绪") : QString::fromStdString(res.error));

    param_->setCollapseHint(res.collapsible, res.ok ? QString() : QString::fromStdString(res.error));
    exportPanel_->setCollapsible(res.collapsible);
    if (res.ok) {
        exportPanel_->setPreviewInfo(QStringLiteral("输出画布: %1×%2 · 保留 %3 块 · %4")
            .arg(res.composition.canvasWidth)
            .arg(res.composition.canvasHeight)
            .arg(kept)
            .arg(res.collapsible ? QStringLiteral("可坍缩") : QStringLiteral("需重排")));
    } else {
        exportPanel_->setPreviewInfo(QStringLiteral("无有效结果：%1").arg(QString::fromStdString(res.error)));
    }
}

// 依引擎结果刷新导出面板的输出预览缩略图（G-11 / §4.6）：先缓存 res.ok/composition，再转 renderExportPreviewFromCache。
// 缓存让后续「不影响切割几何」的事件（烧录开关/标注变更）只需重渲染缩略图、无需重跑 Core。
void MainWindow::updateExportPreview(const engine::EngineResult& res) {
    lastExportResOk_ = res.ok;
    lastComposition_ = res.composition;   // 拷贝 placements（相对 Core 区域计算是小头），供轻量重渲染复用。
    renderExportPreviewFromCache();
}

// 用缓存的 lastComposition_/lastExportResOk_ 重渲染输出预览（不重跑 Core）。
// A：导出选项卡不可见时只置脏标志，切回该页由 onRightTabChanged 调本函数补渲染，省去后台无谓开销。
void MainWindow::renderExportPreviewFromCache() {
    if (rightTabs_ && rightTabs_->currentWidget() != exportPanel_) { exportPreviewDirty_ = true; return; }
    exportPreviewDirty_ = false;
    if (!lastExportResOk_) { exportPanel_->setPreviewPixmap(QPixmap()); return; }
    exportPanel_->setPreviewPixmap(composeOutputThumbnail(
        lastComposition_, exportPreviewSource(), 1.0 / exportSrcScaleX_, 1.0 / exportSrcScaleY_,
        kExportPreviewMaxDim));
}

// 右侧选项卡切换：切到「导出」页且预览已脏时用缓存补渲染一次（不重跑 Core）。
void MainWindow::onRightTabChanged() {
    if (rightTabs_ && rightTabs_->currentWidget() == exportPanel_ && exportPreviewDirty_) renderExportPreviewFromCache();
}

// 输出预览的源图（B）：默认用未烧录的小源图；若「导出时烧录标注」开启且有标注，则委托 util::bakeAnnotationsInto
// 把各标注按世界路径矢量烘焙到小源图副本上（working→小图变换），使预览与最终「烧录后随像素被切割落位」一致。
// 渲染逻辑已下沉到 util（无状态），本方法只做「是否烧录 + 取哪张源图」的编排（app 层只编排，A-0.1）。
QPixmap MainWindow::exportPreviewSource() const {
    if (exportSrcPixmap_.isNull()) return exportSrcPixmap_;
    if (!annoBridge_.burnInEnabled() || annoBridge_.count() == 0) return exportSrcPixmap_;
    return bakeAnnotationsInto(exportSrcPixmap_, 1.0 / exportSrcScaleX_, 1.0 / exportSrcScaleY_,
                               annoBridge_.annotations());
}

// 同步三个面板 + 工具栏模式动作到 Document。
void MainWindow::syncPanels() const {
    left_->syncFromDocument();
    param_->syncFromDocument();
    exportPanel_->syncFromDocument();
    imagePanel_->syncFromDocument();
    if (modeActionL1_) modeActionL1_->setChecked(doc_.mode() == engine::Tier::L1);
    if (modeActionL2_) modeActionL2_->setChecked(doc_.mode() == engine::Tier::L2);
    if (modeActionL3_) modeActionL3_->setChecked(doc_.mode() == engine::Tier::L3);
}

// 非模态提示：写状态栏提示标签 + 限时消息（错误不打断用户，§5.2）。
void MainWindow::notify(const QString& msg, const bool isError) const {
    stHint_->setText(msg);
    statusBar()->showMessage(msg, isError ? 8000 : 4000);
}

// 首次进入引导（§5.2）：一次性说明三种模式的区别。
void MainWindow::showFirstRunGuide() {
    QMessageBox::information(this, QStringLiteral("欢迎使用 ImageDiscropper"),
        QStringLiteral("本工具沿「贯穿全图的切割线」切开图像，再决定保留哪些块、如何重新拼合。\n\n"
                       "• 标准提取 (L1)：保留选区内的区域，最易上手（默认）。\n"
                       "• 反向剔除 (L2)：删除选区诱导的十字带，保留其余——核心特色。\n"
                       "• 网格分割 (L3)：铺满全图的网格 + 单元选择 + 排序。\n\n"
                       "L1/L2/L3 共用同一引擎，模式只是参数预设。"));
}

// 判断焦点是否在数值/文本输入控件上（用于放行单键快捷键）。
bool MainWindow::focusInTextInput() {
    QWidget* fw = QApplication::focusWidget();
    return qobject_cast<QLineEdit*>(fw) ||
           qobject_cast<QAbstractSpinBox*>(fw) ||
           qobject_cast<QComboBox*>(fw);
}

// 单键快捷键（§4.10）：1/2/3 切模式、K/R 切极性。
// 用 keyPressEvent 而非 QShortcut：焦点在坐标输入框时，数字/字母键被输入框消费、不冒泡到主窗口，
// 故「数值输入时天然不触发」——修复了 QShortcut 抢先拦截数字键 1/2/3 导致坐标无法直接键入、
// 只能点增减按钮的问题（NFR-5）。
void MainWindow::keyPressEvent(QKeyEvent* event) {
    if (!focusInTextInput()) {
        switch (event->key()) {
            case Qt::Key_1: onModeAction(1); event->accept(); return;
            case Qt::Key_2: onModeAction(2); event->accept(); return;
            case Qt::Key_3: onModeAction(3); event->accept(); return;
            case Qt::Key_K: onPolarityShortcut(false); event->accept(); return;
            case Qt::Key_R: onPolarityShortcut(true);  event->accept(); return;
            // Delete 与 Backspace 均可删除选中标注（两者等价，符合常见图形编辑器习惯）。
            case Qt::Key_Delete:
            case Qt::Key_Backspace: onAnnoDeleteSelected(); event->accept(); return;
            default: break;
        }
    }
    QMainWindow::keyPressEvent(event);
}

// 模式中文名。
QString MainWindow::modeName(const engine::Tier tier) {
    switch (tier) {
        case engine::Tier::L1: return QStringLiteral("模式: L1 标准提取");
        case engine::Tier::L2: return QStringLiteral("模式: L2 反向剔除");
        case engine::Tier::L3: return QStringLiteral("模式: L3 网格分割");
    }
    return QStringLiteral("模式: -");
}

// 打开图像：弹出文件对话框选取路径，再交由 openImageFromPath 载入。
void MainWindow::onOpen() {
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("打开图像"), QString(),
        QStringLiteral("图像 (*.png *.jpg *.jpeg *.bmp *.webp *.gif *.tga);;所有文件 (*)"));
    if (path.isEmpty()) return;
    openImageFromPath(path);
}

// 从给定路径载入图像（委托 EngineBridge → Core readImageFile），成功后建预览并刷新。
void MainWindow::openImageFromPath(const QString& path) {
    if (path.isEmpty()) return;

    core::Image img;
    if (QString err; !EngineBridge::loadImage(path, img, err)) {
        notify(QStringLiteral("打开失败：%1").arg(err), true); // 非模态。
        return;
    }
    doc_.setImage(std::move(img), path); // 触发 imageChanged。
    annoBridge_.setBaseImage(doc_.working()); // 新图＝新标注会话（Core setImage 清空旧标注）。
    resetDocHistory();          // 换图开新撤销会话：清空跨图历史（快照不含图像，跨图撤销无意义）。
    updateUndoRedoEnabled();
    notify(QStringLiteral("已打开：%1（%2×%3）").arg(path).arg(doc_.width()).arg(doc_.height()), false);
}

// 导出（委托 EngineBridge → Core runEngine + exportImage）。
void MainWindow::onExport() {
    if (!doc_.hasImage()) { notify(QStringLiteral("无图像可导出"), true); return; }
    if (!doc_.hasRect()) { notify(QStringLiteral("请先在画布创建选区"), true); return; }

    const engine::EngineConfig cfg = doc_.buildEngineConfig();

    // 合并重排参数警告（cols*rows < 保留块数，或 cw/ch < 网格单元尺寸）：导出前弹窗二次确认。
    // 内联红字已在导出面板实时显示，此处按需求再加一道模态确认。
    if (const QString rearrangeWarn = exportPanel_->rearrangeWarning(); !rearrangeWarn.isEmpty()) {
        const QMessageBox::StandardButton ret = QMessageBox::warning(
            this, QStringLiteral("重排参数警告"),
            rearrangeWarn + QStringLiteral("\n\n仍要继续导出吗？"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (ret != QMessageBox::Yes) return;
    }

    QString path;
    if (cfg.emitParams.mode == engine::EmitMode::SEPARATE) {
        path = doc_.outputDir();
        if (path.isEmpty()) {
            path = QFileDialog::getExistingDirectory(this, QStringLiteral("选择输出目录"));
            if (path.isEmpty()) return;
            doc_.setOutputDir(path);
            exportPanel_->syncFromDocument();
        }
    } else {
        path = doc_.outputFile();
        if (path.isEmpty()) {
            path = QFileDialog::getSaveFileName(this, QStringLiteral("导出为"), QString(),
                                                QStringLiteral("图像 (*.png *.jpg *.jpeg *.bmp *.webp)"));
            if (path.isEmpty()) return;
            doc_.setOutputFile(path);
            exportPanel_->syncFromDocument();
        }
    }

    // 导出烧录（G-4）：开关开且有标注时，以当前工作图为底逐个调 Core rasterize 合成标注，
    // 再送引擎切割（标注随像素被切开，A-0.15/A-0.16）；否则直接送工作图。
    core::Image exportSrc = doc_.working();
    if (annoBridge_.burnInEnabled() && annoBridge_.count() > 0) {
        exportSrc = annoBridge_.burnIn(doc_.working());
    }

    if (QString err; EngineBridge::exportResult(exportSrc, cfg, path, err)) {
        notify(QStringLiteral("导出成功：%1").arg(path), false);
    } else {
        notify(QStringLiteral("导出失败：%1").arg(err), true);
    }
}

// 换图：重建预览底图、刷新与同步面板。
void MainWindow::onImageChanged() {
    rebuildPreviewPixmap();
    syncPanels();
    refreshPreview();
    // 同维度预处理不清标注（矢量叠加 + 导出注入当前 base），故重绘标注层以对齐最新底图。
    scene_->updateAnnotations(annoBridge_);
}

// 参数变更：刷新预览并同步面板。
void MainWindow::onDocChanged() {
    refreshPreview();
    // 拖拽选区（单矩形或多矩形）期间跳过面板/工具栏全量回同步：模式/极性/格式/质量/命名等均与 rect 无关，
    // 逐帧 syncPanels 是纯浪费且加剧拖拽卡顿；释放时会照常同步一次。
    if (!scene_->isDraggingSelection() && !scene_->isDraggingMultiRect()) syncPanels();
}

// 框选新建选区：把场景矩形钳制到图像内并写回 Document。
void MainWindow::onRubberSelect(const QRectF& sceneRect) {
    if (!doc_.hasImage()) return;
    // L3 网格分割不用矩形选区：框选（含从图像外起拖、未被 CellPickerItem grab 而落到视图橡皮筋的情形）
    // → 把命中的单元并入选择集（与 picker 自身框选发出的 cellsMarqueeSelected 走同一 addCells 路径）。
    if (doc_.mode() == engine::Tier::L3) {
        if (const std::vector<int> idx = scene_->cellsIntersecting(sceneRect); !idx.empty()) doc_.addCells(idx);
        return;
    }
    const qreal W = doc_.width(), H = doc_.height();
    const qreal l = std::clamp(sceneRect.left(), 0.0, W);
    const qreal t = std::clamp(sceneRect.top(), 0.0, H);
    const qreal r = std::clamp(sceneRect.right(), 0.0, W);
    const qreal b = std::clamp(sceneRect.bottom(), 0.0, H);
    const engine::RectRegion rr(qRound(l), qRound(t), qRound(r), qRound(b));
    if (rr.width() <= 0 || rr.height() <= 0) return; // 忽略退化选区。
    // L2 多矩形并集：框选逐个追加矩形（而非替换单选区）。
    if (doc_.mode() == engine::Tier::L2 && doc_.l2Sub() == L2Sub::MULTI_RECT) {
        doc_.addRect(rr);
        return;
    }
    doc_.setRect(rr);
}

// 拖动/缩放既有选区：同框选处理（选区框已做钳制与吸附）。
void MainWindow::onSelectionEdited(const QRectF& sceneRect) {
    onRubberSelect(sceneRect);
}

// L2 多矩形：拖动/缩放第 index 个选区框 → 写回 Document 对应矩形（选区框已钳制到图像内）。
void MainWindow::onMultiRectEdited(const int index, const QRectF& sceneRect) {
    if (index < 0 || !doc_.hasImage()) return;
    if (doc_.mode() != engine::Tier::L2 || doc_.l2Sub() != L2Sub::MULTI_RECT) return;
    const engine::RectRegion rr(qRound(sceneRect.left()), qRound(sceneRect.top()),
                                     qRound(sceneRect.right()), qRound(sceneRect.bottom()));
    doc_.updateRect(static_cast<std::size_t>(index), rr);
    // 拖拽期间 syncPanels 被跳过（避免逐帧重建），故在此定向刷新：
    // 让列表跟随选中被拖矩形（并高亮），并实时回显其新尺寸。
    param_->selectRectRow(index);
    param_->updateRectListItem(index);
}

// L2 多矩形：面板列表选中行变化 → 高亮画布上对应选区框（-1 清除高亮）。
void MainWindow::onRectSelected(const int index) const {
    if (scene_) scene_->setActiveMultiRect(index);
}

// L3 单击切换某单元：写回 Document（触发刷新与点选图元高亮更新）。
void MainWindow::onCellToggled(const int index) {
    if (!doc_.hasImage() || doc_.mode() != engine::Tier::L3) return;
    doc_.toggleCell(index);
}

// L3 拖拽框选：把命中的单元并入选择集（去重）。
void MainWindow::onCellsMarquee(const std::vector<int>& indices) {
    if (!doc_.hasImage() || doc_.mode() != engine::Tier::L3) return;
    doc_.addCells(indices);
}

// L3 CUSTOM 拖拽调序：把 from 单元移到 to 单元原序位（重排选择集顺序即自定义输出序）。
void MainWindow::onCellReordered(const int fromIndex, const int toIndex) {
    if (!doc_.hasImage() || doc_.mode() != engine::Tier::L3) return;
    doc_.moveCellOrder(fromIndex, toIndex);
}

// 方向键微调选区（保持尺寸，钳制到图像内）。
void MainWindow::onNudge(const int dx, const int dy) {
    if (!doc_.hasImage() || !doc_.hasRect()) return;
    const engine::RectRegion r = doc_.rect();
    const int w = r.width(), h = r.height();
    const int W = doc_.width(), H = doc_.height();
    if (w >= W || h >= H) return; // 选区已达整幅，无需微调。
    const int nl = std::clamp(r.left + dx, 0, W - w);
    const int nt = std::clamp(r.top + dy, 0, H - h);
    doc_.setRect(engine::RectRegion(nl, nt, nl + w, nt + h));
}

// 光标移动：更新状态栏坐标与像素颜色。
void MainWindow::onCursor(const QPointF& scenePos) const {
    const int x = static_cast<int>(scenePos.x());
    const int y = static_cast<int>(scenePos.y());
    stCoord_->setText(QStringLiteral("坐标: (%1, %2)").arg(x).arg(y));
    if (doc_.hasImage() && doc_.working().inBounds(x, y)) {
        const core::Color c = doc_.working().getPixel(x, y);
        stColor_->setText(QStringLiteral("RGB(%1,%2,%3)").arg(c.r).arg(c.g).arg(c.b));
    } else {
        stColor_->setText(QStringLiteral("RGB(-,-,-)"));
    }
}

// 视图缩放倍数变化：更新状态栏放大倍数显示（100% = 1:1）。
void MainWindow::onZoomChanged(const qreal factor) const {
    if (!stZoom_) return;
    stZoom_->setText(QStringLiteral("缩放: %1%").arg(qRound(factor * 100.0)));
}

// 清除切割线（清除选区）。
void MainWindow::onClearCut() {
    if (!doc_.hasRect()) return;
    doc_.clearRect(); // 触发 changed → refreshPreview 清空叠加层。
    notify(QStringLiteral("已清除选区与切割线"), false);
}

// 切换预览遮罩显隐（视图菜单 / 画布右键）；同步图层面板的遮罩复选框。
void MainWindow::onToggleMasks() {
    const bool v = !scene_->masksVisible();
    scene_->setMasksVisible(v);
    layerPanel_->setMaskVisible(v);   // 反向同步面板（blockSignals 防回环）。
    refreshPreview();
    notify(v ? QStringLiteral("已显示预览遮罩") : QStringLiteral("已隐藏预览遮罩"), false);
}

// 工具栏/快捷键切换模式（L1/L2/L3）。
void MainWindow::onModeAction(const int tierInt) {
    switch (tierInt) {
        case 1: doc_.setMode(engine::Tier::L1); break;
        case 2: doc_.setMode(engine::Tier::L2); break;
        case 3: doc_.setMode(engine::Tier::L3); break;
        default: break;
    }
}

// K/R 快捷键切换极性。
void MainWindow::onPolarityShortcut(const bool remove) {
    doc_.setPolarity(remove ? engine::Polarity::REMOVE : engine::Polarity::KEEP);
}

// ---------------------------------------------------------------------------
// 预处理（FR-1 / G-3）：各操作经 EngineBridge 调 Core pixel_ops 变换工作图。
// GUI 不自实现像素运算（A-0.1）；所有变换走公共收尾 applyWorkingImage。
// ---------------------------------------------------------------------------

// 预处理公共收尾：维度变化（旋转 90/270、缩放）会使既有选区坐标越界/失配，
// 故先清除失效选区（避免 Core 切割报错），再写回工作图（触发 imageChanged→重建预览底图+刷新）。
void MainWindow::applyWorkingImage(core::Image next, const QString& okMsg) {
    if (!doc_.hasImage()) return;
    if (next.empty()) { notify(QStringLiteral("预处理失败：结果为空图"), true); return; }
    if (next.width() != doc_.width() || next.height() != doc_.height()) {
        doc_.clearRect();    // 单选区坐标基于旧尺寸，已失效。
        doc_.clearRects();   // L2 多矩形同理。
        doc_.clearCells();   // L3 选择集序号对应旧网格，一并清空。
        annoBridge_.clearAll(); // 维度变化使标注坐标失配，一并清空（G-4）。
    }
    doc_.setWorkingImage(std::move(next));
    notify(okMsg, false);
}

// 在模态忙碌对话框内同步执行 op：缩放/尺寸重采样在大图上可能耗时，为避免用户误以为卡死
// 而在处理期间再次点击，弹出不可取消、应用级模态的进度对话框（不确定进度条）阻断其余输入。
// 同步执行下忙碌条不会动画，但 processEvents 先保证对话框绘制出来；执行完立即关闭。
void MainWindow::runWithBusyDialog(const QString& text, const std::function<void()>& op) {
    QProgressDialog dlg(text, QString(), 0, 0, this);
    dlg.setWindowTitle(QStringLiteral("正在处理图像"));
    dlg.setWindowModality(Qt::ApplicationModal); // 模态阻断全部其他窗口的输入。
    dlg.setCancelButton(nullptr);                // 不可取消（重采样中途无法安全回退）。
    dlg.setMinimumDuration(0);                   // 立即显示，不等阈值。
    dlg.setRange(0, 0);                          // 不确定进度（忙碌滚动样式）。
    dlg.show();
    QApplication::processEvents();               // 先让对话框绘制出来，再进入耗时处理。
    op();
    dlg.close();
}

// 旋转（90 的整数倍；-90=左转、90=右转、180）。
void MainWindow::onRotate(const int angleDeg) {
    if (!doc_.hasImage()) return;
    applyWorkingImage(EngineBridge::rotateImage(doc_.working(), angleDeg),
                      QStringLiteral("已旋转 %1°").arg(angleDeg));
}

// 翻转（水平/垂直）。
void MainWindow::onFlip(const bool horizontal) {
    if (!doc_.hasImage()) return;
    applyWorkingImage(EngineBridge::flipImage(doc_.working(), horizontal),
                      horizontal ? QStringLiteral("已水平翻转") : QStringLiteral("已垂直翻转"));
}

// 按比例缩放（factor 为倍数）。可能耗时，包在模态忙碌对话框内并加重入守卫。
void MainWindow::onScale(const double factor) {
    if (!doc_.hasImage() || busyResample_) return;
    busyResample_ = true;
    runWithBusyDialog(QStringLiteral("正在按比例缩放图像，请稍候…"), [this, factor] {
        applyWorkingImage(EngineBridge::scaleImage(doc_.working(), factor),
                          QStringLiteral("已缩放至 %1%").arg(qRound(factor * 100.0)));
    });
    busyResample_ = false;
}

// 目标尺寸缩放。可能耗时，包在模态忙碌对话框内并加重入守卫。
void MainWindow::onResize(const int newWidth, const int newHeight) {
    if (!doc_.hasImage() || busyResample_) return;
    busyResample_ = true;
    runWithBusyDialog(QStringLiteral("正在缩放到目标尺寸，请稍候…"), [this, newWidth, newHeight] {
        applyWorkingImage(EngineBridge::resizeImage(doc_.working(), newWidth, newHeight),
                          QStringLiteral("已缩放到 %1×%2").arg(newWidth).arg(newHeight));
    });
    busyResample_ = false;
}

// 黑白（灰度）。
void MainWindow::onGray() {
    if (!doc_.hasImage()) return;
    applyWorkingImage(EngineBridge::toGrayImage(doc_.working()), QStringLiteral("已转为黑白"));
}

// 色道反色：依勾选的 R/G/B 拼出长度 3 的反相掩码（'1' 反相、'0' 保持）；灰度图 Core 忽略掩码、整体反相。
void MainWindow::onInvert(const bool invR, const bool invG, const bool invB) {
    if (!doc_.hasImage()) return;
    const std::string mask = std::string(invR ? "1" : "0") + (invG ? "1" : "0") + (invB ? "1" : "0");
    applyWorkingImage(EngineBridge::invertImage(doc_.working(), mask), QStringLiteral("已按通道反色"));
}

// 色道分离：依勾选的 R/G/B 拼出长度 3 的保留掩码（'1' 保留、'0' 置零）。
void MainWindow::onSplit(const bool keepR, const bool keepG, const bool keepB) {
    if (!doc_.hasImage()) return;
    const std::string mask = std::string(keepR ? "1" : "0") + (keepG ? "1" : "0") + (keepB ? "1" : "0");
    applyWorkingImage(EngineBridge::splitImage(doc_.working(), mask), QStringLiteral("已按通道分离"));
}

// 重置预处理：工作图恢复为原图。若原图与当前工作图尺寸不同（曾旋转/缩放），先清失效选区。
void MainWindow::onResetPreprocess() {
    if (!doc_.hasImage() || !doc_.hasPreprocess()) return;
    if (doc_.original().width() != doc_.width() || doc_.original().height() != doc_.height()) {
        doc_.clearRect();
        doc_.clearRects();
        doc_.clearCells();
    }
    doc_.resetPreprocess();
    notify(QStringLiteral("已重置预处理，恢复原图"), false);
}

// ===========================================================================
// 标注（第四阶段 G-4/G-5）：MainWindow 只做编排——把面板/画布意图转交 AnnotationBridge
// （其内部调 Core annotation/geometry），再把模型变化下发画布与属性面板。不含几何/光栅化（A-0.1）。
// ===========================================================================

// 工具面板选择：先收笔未完成的折线/画笔路径，再切换工具（setTool 内部会放弃两点预览）。
void MainWindow::onToolSelected(const AnnoTool tool) {
    if (annoBridge_.hasPathDraft()) annoBridge_.commitPath();
    annoBridge_.setTool(tool);
}

// 标注列表/预览变化：增量重绘画布标注层，并同步属性面板回显。
void MainWindow::onAnnoBridgeChanged() {
    scene_->updateAnnotations(annoBridge_);
    annoPropPanel_->syncFromModel();
    updateUndoRedoEnabled(); // 标注增删改影响标注上下文与可撤销性，刷新编辑菜单启用态。
    // B：若「导出时烧录标注」开启，标注变化只需反映到输出预览——用缓存重渲染、不重跑 Core（切割几何不受标注影响）。
    if (annoBridge_.burnInEnabled()) renderExportPreviewFromCache();
}

// 绘制拖拽预览（橡皮筋）变化：仅刷新预览图元（不重建已提交标注，避免逐帧开销），实现“绘制即实时成形”。
void MainWindow::onAnnoPendingChanged() const {
    scene_->updatePendingAnnotation(annoBridge_);
}

// 选中项变化：重绘高亮（updateAnnotations 内含选中态）并同步属性面板。
void MainWindow::onAnnoSelectionChanged() const {
    scene_->updateAnnotations(annoBridge_);
    annoPropPanel_->syncFromModel();
    updateUndoRedoEnabled(); // 选中标注会使 Ctrl+Z/Y 路由到标注撤销，刷新启用态。
}

// 工具变化：同步工具面板按钮组，并按「是否 SELECT」切换画布绘制态门控。
void MainWindow::onAnnoToolChanged() const {
    toolPanel_->syncFromModel();
    view_->setAnnotationDrawActive(annoBridge_.currentTool() != AnnoTool::SELECT);
    annoPropPanel_->syncFromModel();
    updateUndoRedoEnabled(); // 工具激活态决定 Ctrl+Z/Y 是否路由到标注撤销，刷新启用态。
}

// 绘制手势起点：依当前工具分派——文字落点取文本；折线/画笔起笔；其余两点形状记起点。
void MainWindow::onAnnoDragStart(const QPointF& scenePos) {
    const core::Point2D p(scenePos.x(), scenePos.y());
    switch (annoBridge_.currentTool()) {
        case AnnoTool::TEXT: {
            bool ok = false;
            const QString text = QInputDialog::getText(this, QStringLiteral("文字标注"),
                QStringLiteral("请输入标注文字："), QLineEdit::Normal,
                QString::fromStdString(annoBridge_.currentText()), &ok);
            if (ok && !text.isEmpty()) annoBridge_.addText(p, text.toStdString());
            break;
        }
        case AnnoTool::POLYLINE:
            if (annoBridge_.hasPathDraft()) annoBridge_.appendPathPoint(p);
            else annoBridge_.beginPath(p);
            break;
        case AnnoTool::BRUSH:
            annoBridge_.beginPath(p);
            break;
        default:
            annoBridge_.beginShape(p);
            break;
    }
}

// 绘制手势拖拽（按住左键移动）：两点形状实时更新预览；画笔追加顶点；折线仅橡皮筋预览。
// 折线为点击式：顶点已在 onAnnoDragStart（按下）落定，故拖拽中只预览、不再追加正式顶点。
void MainWindow::onAnnoDragMove(const QPointF& scenePos) {
    const core::Point2D p(scenePos.x(), scenePos.y());
    switch (annoBridge_.currentTool()) {
        case AnnoTool::BRUSH:
            annoBridge_.appendPathPoint(p);
            break;
        case AnnoTool::POLYLINE:
            annoBridge_.previewPolyline(p);   // 按住拖动时也走橡皮筋预览（与悬停一致）。
            break;
        case AnnoTool::TEXT:
        case AnnoTool::SELECT:
            break;
        default:
            annoBridge_.updateShape(p);
            break;
    }
}

// 绘制手势释放：两点形状提交；画笔收笔；折线保持草稿（待 Esc/切换工具收笔）。
void MainWindow::onAnnoDragEnd(const QPointF& scenePos) {
    Q_UNUSED(scenePos);
    switch (annoBridge_.currentTool()) {
        case AnnoTool::BRUSH:
            annoBridge_.commitPath();
            break;
        case AnnoTool::POLYLINE:
        case AnnoTool::TEXT:
        case AnnoTool::SELECT:
            break;
        default:
            annoBridge_.commitShape();
            break;
    }
}

// 绘制态 Esc：有折线/画笔草稿则收笔提交，否则取消当前两点预览。
void MainWindow::onAnnoEscape() {
    if (annoBridge_.hasPathDraft()) annoBridge_.commitPath();
    else annoBridge_.cancelPending();
}

// 绘制态悬停（未按键移动）：折线实时预览「已落顶点 + 到光标连线」橡皮筋（不落顶点）。
// 其余工具无悬停语义（两点形状靠拖拽预览、画笔靠按住追点），故忽略。
void MainWindow::onAnnoHover(const QPointF& scenePos) {
    if (annoBridge_.currentTool() != AnnoTool::POLYLINE) return;
    annoBridge_.previewPolyline(core::Point2D(scenePos.x(), scenePos.y()));
}

// 绘制态右键：退出当前绘制手势——有折线/画笔草稿则收笔提交（折线在此结束），
// 否则取消当前预览（与 Esc 同义，满足「右键退出多线段」的交互约定）。
void MainWindow::onAnnoFinish() {
    if (annoBridge_.hasPathDraft()) annoBridge_.commitPath();
    else annoBridge_.cancelPending();
}

// SELECT 工具下点中标注图元：委托 Core hitTest 选中（几何命中在 Core，A-0.1）。
void MainWindow::onAnnotationSelect(const QPointF& scenePos) {
    annoBridge_.selectAt(core::Point2D(scenePos.x(), scenePos.y()));
}

// 拖动选中标注：委托 Core 平移其几何（下标须与当前选中项一致，防御误触）。
void MainWindow::onAnnotationMoved(const int index, const double dx, const double dy) {
    if (const std::optional<std::size_t> sel = annoBridge_.selectedIndex(); !sel || static_cast<int>(*sel) != index) return;
    annoBridge_.moveSelectedBy(dx, dy);
}

// 拖定向包围盒手柄（释放提交）：委托 Core 对选中标注施加缩放/旋转（与属性面板变换同一入口，下标防御误触）。
void MainWindow::onAnnotationTransformed(const int index, const double sx, const double sy,
                                         const double rotateDeg) {
    if (const std::optional<std::size_t> sel = annoBridge_.selectedIndex(); !sel || static_cast<int>(*sel) != index) return;
    annoBridge_.transformSelected(sx, sy, rotateDeg);
    // 提交后模型发 changed → onAnnoBridgeChanged → syncFromModel 依最新累积值回显面板（忠实反映底层，不回弹）。
}

// 手柄拖拽**进行中**：把逐帧**绝对**预览值实时回显到属性面板变换区（仅回显，不写模型；下标防御误触）。
void MainWindow::onAnnotationTransformPreview(const int index, const double sx, const double sy,
                                              const double rotateDeg) const {
    if (const std::optional<std::size_t> sel = annoBridge_.selectedIndex(); !sel || static_cast<int>(*sel) != index) return;
    if (annoPropPanel_) annoPropPanel_->setTransformPreview(sx, sy, rotateDeg);
}

// 属性面板 → 模型（EDIT 作用选中项，DRAW 改当前默认；均触发 changed→重绘）。
void MainWindow::onAnnoColorPicked(const QColor& c) { annoBridge_.setColor(toCoreColor(c)); }
void MainWindow::onAnnoStrokeChanged(const int width) { annoBridge_.setStrokeWidth(width); }
void MainWindow::onAnnoFillChanged(const bool fill) { annoBridge_.setFill(fill); }
void MainWindow::onAnnoTextChanged(const QString& text) { annoBridge_.setText(text.toStdString()); }
void MainWindow::onAnnoFontSizeChanged(const double size) { annoBridge_.setFontSize(size); }
// 属性面板「变换」：将缩放/旋转写回模型（委托 Core Shape 的非破坏性矩阵，保留类型与字形；无选中时模型内部忽略）。
void MainWindow::onAnnoTransformApply(const double sx, const double sy, const double rotateDeg) {
    annoBridge_.transformSelected(sx, sy, rotateDeg);
}

// 图层面板 → 画布/模型（底图仅切画布可见；标注同时同步模型标志与画布图元）。
void MainWindow::onBaseVisibilityChanged(const bool visible) const { scene_->setBaseVisible(visible); }
void MainWindow::onMaskVisibilityChanged(const bool visible) const { scene_->setMasksVisible(visible); }
void MainWindow::onGridVisibilityChanged(const bool visible) {
    scene_->setGridVisible(visible);
    refreshPreview();   // 网格线依 gridVisible_ 门控重建（L3 / L2 多矩形）。
}
void MainWindow::onCutLineVisibilityChanged(const bool visible) {
    scene_->setCutLinesVisible(visible);
    refreshPreview();   // L2 多矩形诱导线依 cutLinesVisible_ 门控重建（橙色切割线）。
}
void MainWindow::onSelectionVisibilityChanged(const bool visible) {
    scene_->setSelectionVisible(visible);
    refreshPreview();   // L3 单元选择高亮（CellPickerItem）依 selectionVisible_ 门控重建。
}
void MainWindow::onAnnotationVisibilityChanged(const bool visible) {
    annoBridge_.setLayerVisible(visible);
    scene_->setAnnotationsVisible(visible);
}
void MainWindow::onBurnInChanged(const bool on) {
    annoBridge_.setBurnIn(on);
    renderExportPreviewFromCache();   // B：烧录开关只影响输出预览（叠加/去除标注），用缓存重渲染、不重跑 Core。
}

// 标注菜单动作：撤销/重做/删除选中/清除全部（均复用 Core AnnotationLayer）。
void MainWindow::onAnnoUndo() { annoBridge_.undo(); }
void MainWindow::onAnnoRedo() { annoBridge_.redo(); }
void MainWindow::onAnnoDeleteSelected() {
    if (!annoBridge_.selectedIndex()) return;
    annoBridge_.removeSelected();
}
void MainWindow::onAnnoClearAll() {
    if (annoBridge_.count() == 0) return;
    const QMessageBox::StandardButton ret = QMessageBox::question(
        this, QStringLiteral("清除全部标注"),
        QStringLiteral("确定要清除全部 %1 个标注吗？此操作可用「撤销标注」回退。")
            .arg(annoBridge_.count()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret != QMessageBox::Yes) return;
    annoBridge_.clearAll();
}

// ===========================================================================
// 全局撤销/重做（G-12）与配置文件（G-13）：MainWindow 只做编排——
// 文档参数态历史复用 Core HistoryManager<EngineConfig>（capture=buildEngineConfig、
// restore=applyEngineConfig）；标注历史沿用 Core AnnotationLayer 内建快照，由上下文路由分发。
// 配置存取经 EngineBridge 委托 Core save/loadEngineConfig（A-0.1：Core 调用集中在桥）。
// ===========================================================================

// 保存配置（G-13）：把当前作业配置写为 §9 schema 的 JSON 文件（委托 EngineBridge → Core）。
void MainWindow::onSaveConfig() {
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("保存配置"), QString(),
        QStringLiteral("ImageDiscropper 配置 (*.json);;所有文件 (*)"));
    if (path.isEmpty()) return;
    if (QString err; EngineBridge::saveConfig(path, doc_.buildEngineConfig(), err))
        notify(QStringLiteral("配置已保存：%1").arg(path), false);
    else
        notify(QStringLiteral("保存配置失败：%1").arg(err), true);
}

// 加载配置（G-13）：读 JSON → Document::applyEngineConfig 反向映射；载入本身可撤销（入同一历史栈）。
void MainWindow::onLoadConfig() {
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("加载配置"), QString(),
        QStringLiteral("ImageDiscropper 配置 (*.json);;所有文件 (*)"));
    if (path.isEmpty()) return;
    engine::EngineConfig cfg;
    if (QString err; !EngineBridge::loadConfig(path, cfg, err)) { notify(QStringLiteral("加载配置失败：%1").arg(err), true); return; }
    // 先冲刷未落定的防抖变更，保证「栈顶＝载入前当前态」，使加载配置可一步撤销回去。
    if (docHistoryTimer_->isActive()) { docHistoryTimer_->stop(); onDocHistoryTimeout(); }
    suppressHistory_ = true;
    doc_.applyEngineConfig(cfg);   // 触发 changed → refreshPreview + syncPanels（还原期间不采集历史）。
    suppressHistory_ = false;
    docHistory_.push(doc_.buildEngineConfig()); // 新态压栈（其下即载入前态），维持「栈顶＝当前态」不变式。
    updateUndoRedoEnabled();
    notify(QStringLiteral("配置已加载：%1").arg(path), false);
}

// 编辑菜单撤销（G-12）：上下文路由——标注上下文转发到标注撤销，否则走文档参数撤销。
void MainWindow::onUndo() {
    if (annotationContextActive()) onAnnoUndo();
    else undoDocument();
}

// 编辑菜单重做（G-12）：与撤销对称的上下文路由。
void MainWindow::onRedo() {
    if (annotationContextActive()) onAnnoRedo();
    else redoDocument();
}

// 标注上下文判定：绘制工具激活（非 SELECT）或存在选中标注时，Ctrl+Z/Y 路由到标注撤销/重做。
bool MainWindow::annotationContextActive() const {
    return annoBridge_.currentTool() != AnnoTool::SELECT || annoBridge_.selectedIndex().has_value();
}

// 参数变更→重启防抖定时器（还原期间被 suppressHistory_ 抑制，避免 undo/redo 自身再入栈）。
void MainWindow::scheduleHistoryCapture() const {
    if (suppressHistory_) return;
    docHistoryTimer_->start(); // 连续变更（拖拽/连点）只在静默 500ms 后合并为一条历史。
}

// 防抖到点：把当前参数态快照压入历史（栈顶＝当前态），并刷新撤销/重做启用态。
void MainWindow::onDocHistoryTimeout() {
    if (suppressHistory_) return;
    docHistory_.push(doc_.buildEngineConfig());
    updateUndoRedoEnabled();
}

// 清空历史并以当前参数态为唯一基线（构造/换图后调用；栈顶＝当前态，undoSize==1 时不可撤销）。
void MainWindow::resetDocHistory() {
    if (docHistoryTimer_) docHistoryTimer_->stop();
    docHistory_.clearAll();
    docHistory_.push(doc_.buildEngineConfig());
}

// 文档参数撤销：栈顶恒为当前态，需至少两态（基线 + 一次变更）才可撤销；
// 弹出当前态到重做栈后，还原新栈顶（上一态）。还原经 applyEngineConfig 触发 changed 刷新画布/面板。
void MainWindow::undoDocument() {
    if (docHistory_.undoSize() <= 1) { notify(QStringLiteral("没有可撤销的参数变更"), false); return; }
    docHistory_.popToRedo();
    const engine::EngineConfig* prev = docHistory_.top();
    if (!prev) return;
    suppressHistory_ = true;
    doc_.applyEngineConfig(*prev);
    suppressHistory_ = false;
    updateUndoRedoEnabled();
    notify(QStringLiteral("已撤销"), false);
}

// 文档参数重做：从重做栈弹回最近撤销的态并还原（与 undoDocument 对称）。
void MainWindow::redoDocument() {
    if (!docHistory_.canRedo()) { notify(QStringLiteral("没有可重做的参数变更"), false); return; }
    const auto st = docHistory_.popFromRedo();
    if (!st) return;
    suppressHistory_ = true;
    doc_.applyEngineConfig(*st);
    suppressHistory_ = false;
    updateUndoRedoEnabled();
    notify(QStringLiteral("已重做"), false);
}

// 依文档历史深度与标注上下文刷新编辑菜单撤销/重做启用态。
// 文档侧：栈顶为当前态，需 >1 态才可撤销、重做栈非空才可重做；标注上下文激活时按上下文放行
// （Core 标注历史无法在不触碰 Core 的前提下精确查询可用性，故按工具/选中态放行，空历史时撤销为无害空操作）。
void MainWindow::updateUndoRedoEnabled() const {
    if (!aUndo_ || !aRedo_) return;
    const bool docCanUndo = docHistory_.undoSize() > 1;
    const bool docCanRedo = docHistory_.canRedo();
    const bool annoCtx = annotationContextActive();
    aUndo_->setEnabled(docCanUndo || annoCtx);
    aRedo_->setEnabled(docCanRedo || annoCtx);
}

} // namespace idc::gui
