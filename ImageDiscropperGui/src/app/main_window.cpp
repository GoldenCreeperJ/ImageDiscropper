// ============================================================================
// 文件：app/main_window.cpp
// 作用：实现主窗口的装配、数据流编排与状态栏呈现（见同名头文件说明）。
// 分块依据：
//   - buildCentral/buildMenus/buildToolbar/buildStatus/buildShortcuts/connectAll 各管一块装配；
//   - on* 槽把用户意图落到 Document，再经 refreshPreview 调 EngineBridge（Core）刷新画布；
//   - MainWindow 不含任何切割/几何/导出实现（A-0.1），只做编排与呈现。
// ============================================================================
#include "app/main_window.h"

#include <algorithm>
#include <functional>
#include <string>
#include <utility>

#include <QAbstractSpinBox>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressDialog>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>

#include "canvas/canvas_scene.h"
#include "canvas/canvas_view.h"
#include "panels/export_panel.h"
#include "panels/image_panel.h"
#include "panels/left_panel.h"
#include "panels/param_panel.h"
#include "util/image_qt_adapter.h"

#ifndef IDC_GUI_VERSION
#define IDC_GUI_VERSION "dev"
#endif

namespace idc::gui {

// 构造：装配全部部件并显示首次引导。
MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("ImageDiscropper GUI %1").arg(QStringLiteral(IDC_GUI_VERSION)));
    resize(1280, 800);

    buildCentral();
    buildMenus();
    buildToolbar();
    buildStatus();
    connectAll();

    syncPanels();
    refreshPreview();
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
    left_->setDocument(&doc_);
    param_->setDocument(&doc_);
    exportPanel_->setDocument(&doc_);
    imagePanel_->setDocument(&doc_);

    // 右侧用 QTabWidget 分组，避免面板过长（§4.1 布局约束）。
    rightTabs_ = new QTabWidget(this);
    rightTabs_->addTab(param_, QStringLiteral("参数"));
    rightTabs_->addTab(exportPanel_, QStringLiteral("导出"));
    rightTabs_->addTab(imagePanel_, QStringLiteral("图像"));
    rightTabs_->setMinimumWidth(240); // 可拖拽调宽；设下限避免控件被挤到不可用。

    auto* split = new QSplitter(Qt::Horizontal, this);
    split->addWidget(left_);
    split->addWidget(view_);
    split->addWidget(rightTabs_);
    split->setStretchFactor(0, 0);   // 左面板：窗口整体缩放时不抢空间
    split->setStretchFactor(1, 1);   // 画布：占据剩余空间
    split->setStretchFactor(2, 0);   // 右面板：窗口整体缩放时不抢空间
    split->setChildrenCollapsible(false); // 禁止把面板拖到 0 而完全折叠消失
    split->setHandleWidth(6);             // 稍宽的分隔条，拖拽手感更明显
    split->setSizes({220, 900, 300});     // 初始宽度（此后左右面板均可自由拖拽调整）
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
    QAction* aExit = mFile->addAction(QStringLiteral("退出(&X)"));
    connect(aExit, &QAction::triggered, this, &QWidget::close);

    // ---- 编辑（撤销/重做第三阶段接入）----
    QMenu* mEdit = menuBar()->addMenu(QStringLiteral("编辑(&E)"));
    QAction* aUndo = mEdit->addAction(QStringLiteral("撤销(&U)"));
    aUndo->setShortcut(QKeySequence::Undo);
    aUndo->setEnabled(false);
    aUndo->setToolTip(QStringLiteral("撤销/重做将在第三阶段接入（复用 Core HistoryManager）。"));
    QAction* aRedo = mEdit->addAction(QStringLiteral("重做(&R)"));
    aRedo->setShortcut(QKeySequence::Redo);
    aRedo->setEnabled(false);
    mEdit->addSeparator();
    QAction* aClear = mEdit->addAction(QStringLiteral("清除选区(&C)"));
    aClear->setShortcut(QKeySequence(Qt::Key_Escape));
    connect(aClear, &QAction::triggered, this, &MainWindow::onClearCut);

    // ---- 图像（预处理 FR-1 / G-3：旋转/翻转/黑白/反色/重置；完整控件见右侧「图像」页）----
    QMenu* mImage = menuBar()->addMenu(QStringLiteral("图像(&I)"));
    QAction* aRotL = mImage->addAction(QStringLiteral("左转 90°"));
    connect(aRotL, &QAction::triggered, this, [this] { onRotate(-90); });
    QAction* aRotR = mImage->addAction(QStringLiteral("右转 90°"));
    connect(aRotR, &QAction::triggered, this, [this] { onRotate(90); });
    QAction* aRot180 = mImage->addAction(QStringLiteral("旋转 180°"));
    connect(aRot180, &QAction::triggered, this, [this] { onRotate(180); });
    mImage->addSeparator();
    QAction* aFlipH = mImage->addAction(QStringLiteral("水平翻转"));
    connect(aFlipH, &QAction::triggered, this, [this] { onFlip(true); });
    QAction* aFlipV = mImage->addAction(QStringLiteral("垂直翻转"));
    connect(aFlipV, &QAction::triggered, this, [this] { onFlip(false); });
    mImage->addSeparator();
    QAction* aGray = mImage->addAction(QStringLiteral("黑白"));
    connect(aGray, &QAction::triggered, this, &MainWindow::onGray);
    QAction* aInvert = mImage->addAction(QStringLiteral("反色（全通道）"));
    connect(aInvert, &QAction::triggered, this, [this] { onInvert(true, true, true); });
    mImage->addSeparator();
    QAction* aResetPre = mImage->addAction(QStringLiteral("重置预处理"));
    connect(aResetPre, &QAction::triggered, this, &MainWindow::onResetPreprocess);
    QAction* aImagePanel = mImage->addAction(QStringLiteral("图像处理面板（缩放/尺寸/色道）…"));
    connect(aImagePanel, &QAction::triggered, this, [this] {
        if (rightTabs_ && imagePanel_) rightTabs_->setCurrentWidget(imagePanel_);
    });

    // ---- 标注（第四阶段接入）----
    QMenu* mAnno = menuBar()->addMenu(QStringLiteral("标注(&A)"));
    QAction* aAnno = mAnno->addAction(QStringLiteral("标注工具…"));
    aAnno->setEnabled(false);
    aAnno->setToolTip(QStringLiteral("标注图层将在第四阶段接入（调用 Core annotation/geometry）。"));

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
    QAction* aReset = mView->addAction(QStringLiteral("重置视图 (1:1)"));
    connect(aReset, &QAction::triggered, this, [this] { view_->resetZoom(); });
    mView->addSeparator();
    QAction* aMasks = mView->addAction(QStringLiteral("切换预览遮罩"));
    connect(aMasks, &QAction::triggered, this, &MainWindow::onToggleMasks);

    // ---- 帮助 ----
    QMenu* mHelp = menuBar()->addMenu(QStringLiteral("帮助(&H)"));
    QAction* aUsage = mHelp->addAction(QStringLiteral("使用说明"));
    connect(aUsage, &QAction::triggered, this, [this] {
        QMessageBox::information(this, QStringLiteral("使用说明"),
            QStringLiteral("1) 文件→打开图像。\n"
                           "2) 左侧选择模式（L1 标准提取 / L2 反向剔除 / L3 网格分割）与极性（保留/删除框内）。\n"
                           "3) L1/L2：在画布上拖拽出选区；橙色切割线贯穿全图（与选区同色），绿色为保留、红色为删除。\n"
                           "4) 可拖动选区/四角手柄调整，或直接拖动橙色切割线（＝选区边）移动对应边；方向键微调（Shift 大步）。\n"
                           "5) L3：在右侧「参数」页设定基准点、单元尺寸与余量策略，网格线自动铺满全图；用全选/反选/清空与排序策略控制输出。\n"
                           "6) 右侧「导出」页选择输出模式与路径，点击导出（Ctrl+S）。"));
    });
    QAction* aAbout = mHelp->addAction(QStringLiteral("关于"));
    connect(aAbout, &QAction::triggered, this, [this] {
        QMessageBox::about(this, QStringLiteral("关于"),
            QStringLiteral("ImageDiscropper GUI %1\n基于统一 Grid-Selection-Emit 引擎（Core %2）。\n"
                           "L1/L2/L3 共用同一代码路径，模式仅为参数预设。")
                .arg(QStringLiteral(IDC_GUI_VERSION), QStringLiteral(IDC_GUI_VERSION)));
    });
}

// 装配工具栏（§4.1/§4.3）：打开/导出、缩放、模式切换、遮罩切换。
void MainWindow::buildToolbar() {
    QToolBar* tb = addToolBar(QStringLiteral("主工具栏"));
    tb->setMovable(false);

    QAction* aOpen = tb->addAction(QStringLiteral("打开"));
    connect(aOpen, &QAction::triggered, this, &MainWindow::onOpen);
    QAction* aExport = tb->addAction(QStringLiteral("导出"));
    connect(aExport, &QAction::triggered, this, &MainWindow::onExport);
    tb->addSeparator();

    QAction* aZoomIn = tb->addAction(QStringLiteral("放大"));
    connect(aZoomIn, &QAction::triggered, this, [this] { view_->zoomIn(); });
    QAction* aZoomOut = tb->addAction(QStringLiteral("缩小"));
    connect(aZoomOut, &QAction::triggered, this, [this] { view_->zoomOut(); });
    QAction* aFit = tb->addAction(QStringLiteral("适应"));
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
    tb->addSeparator();

    QAction* aMasks = tb->addAction(QStringLiteral("遮罩"));
    aMasks->setToolTip(QStringLiteral("切换保留/删除预览遮罩显隐。"));
    connect(aMasks, &QAction::triggered, this, &MainWindow::onToggleMasks);
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

    // 图像处理面板（预处理）：各意图信号→对应槽（经 EngineBridge 调 Core processing）。
    connect(imagePanel_, &ImagePanel::rotateRequested, this, &MainWindow::onRotate);
    connect(imagePanel_, &ImagePanel::flipRequested, this, &MainWindow::onFlip);
    connect(imagePanel_, &ImagePanel::scaleRequested, this, &MainWindow::onScale);
    connect(imagePanel_, &ImagePanel::resizeRequested, this, &MainWindow::onResize);
    connect(imagePanel_, &ImagePanel::grayRequested, this, &MainWindow::onGray);
    connect(imagePanel_, &ImagePanel::invertRequested, this, &MainWindow::onInvert);
    connect(imagePanel_, &ImagePanel::splitRequested, this, &MainWindow::onSplit);
    connect(imagePanel_, &ImagePanel::resetRequested, this, &MainWindow::onResetPreprocess);
}

// 依工作图重建降采样预览底图（NFR-3）。
void MainWindow::rebuildPreviewPixmap() {
    if (!doc_.hasImage()) {
        scene_->clearAll();
        return;
    }
    preview_ = makePreview(doc_.working(), previewMaxDim_);
    const QPixmap pm = toPixmap(preview_.image);
    // 场景坐标 = 原图像素坐标；底图用放大系数把预览 pixmap 铺到原图尺寸。
    scene_->setBaseImage(pm, preview_.scaleX, preview_.scaleY, doc_.width(), doc_.height());
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
        scene_->updateMasks(idc::engine::EngineResult{});
        stCount_->setText(QStringLiteral("保留块: 0"));
        stHint_->setText(QStringLiteral("请打开图像"));
        exportPanel_->setPreviewInfo(QStringLiteral("尚未加载图像。"));
        return;
    }

    // L3 网格分割：网格由「基准点 + 单元尺寸 + 余量策略」定义，不依赖选区矩形，
    // 故在选区判定之前单独处理。网格线交给 GridLayer 画灰色虚线；橙色选区框在 L3 隐藏
    // （单元点选交互由后续增量接入）。遮罩仍复用 runEngine 的 kept 结果（红底 + 绿块）。
    if (doc_.mode() == idc::engine::Tier::L3) {
        const idc::engine::EngineConfig cfg = doc_.buildEngineConfig();
        // 依 Core 网格产出刷新网格线，并回灌派生行列数（供参数面板只读显示）。
        const idc::engine::Grid grid = bridge_.buildGrid(cfg.cut, cfg.source);
        doc_.setDerivedGridSize(grid.rowCount(), grid.colCount());
        scene_->updateGrid(grid, true);
        scene_->clearCutLines();                   // L3 不画 L1/L2 的橙色贯穿切割线。
        scene_->clearMultiRects();                 // L3 不显示 L2 多矩形轮廓。
        scene_->syncSelection(doc_.rect(), false); // 隐藏橙色选区框。
        // 把当前选择集与排序策略下发给点选图元（高亮已选单元）。
        scene_->updateCellSelection(doc_.selectedCells(), doc_.order().strategy);

        const idc::engine::EngineResult res = bridge_.runPreview(doc_.working(), cfg);
        scene_->updateMasks(res);

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
    if (doc_.mode() == idc::engine::Tier::L2 && doc_.l2Sub() == L2Sub::MULTI_RECT) {
        scene_->updateMultiRects(doc_.rects());     // 增量刷新可拖拽选区框（拖拽中不回设正在拖者）。
        scene_->clearCutLines();
        scene_->syncSelection(doc_.rect(), false);  // 隐藏单选区框（改用多矩形轮廓）。
        if (doc_.rects().empty()) {
            // 尚无矩形：不跑引擎（Core 对空 rects 的 MULTI_RECT 会报错），提示框选追加。
            scene_->updateGridLines(idc::engine::Grid{}, false);
            scene_->updateMasks(idc::engine::EngineResult{});
            stCount_->setText(QStringLiteral("保留块: 0"));
            stHint_->setText(QStringLiteral("在画布上拖拽以追加矩形"));
            exportPanel_->setPreviewInfo(QStringLiteral("等待矩形…"));
            return;
        }
        const idc::engine::EngineConfig cfg = doc_.buildEngineConfig();
        const idc::engine::Grid grid = bridge_.buildGrid(cfg.cut, cfg.source);
        scene_->updateGridLines(grid, true);
        const idc::engine::EngineResult res = bridge_.runPreview(doc_.working(), cfg);
        scene_->updateMasks(res);

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
        scene_->updateMasks(idc::engine::EngineResult{});
        scene_->syncSelection(doc_.rect(), false);
        stCount_->setText(QStringLiteral("保留块: 0"));
        stHint_->setText(QStringLiteral("在画布上拖拽以创建选区"));
        exportPanel_->setPreviewInfo(QStringLiteral("等待选区…"));
        return;
    }

    // 组装配置 → 调 Core 预览（仅区域数学，实时）。
    const idc::engine::EngineConfig cfg = doc_.buildEngineConfig();
    const idc::engine::EngineResult res = bridge_.runPreview(doc_.working(), cfg);

    // 切割线来自 Core generateCutLines（GUI 不自算几何）；场景据此判定选区哪几条边有贯穿切割线，
    // 下发给选区框绘制为橙色贯穿线——切割线即选区边的延伸，拖动选区边＝移动切割线（同一图元）。
    scene_->updateCutLines(bridge_.cutLines(cfg.cut, cfg.source), doc_.rect());
    scene_->updateMasks(res);
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

// 同步三个面板 + 工具栏模式动作到 Document。
void MainWindow::syncPanels() {
    left_->syncFromDocument();
    param_->syncFromDocument();
    exportPanel_->syncFromDocument();
    imagePanel_->syncFromDocument();
    if (modeActionL1_) modeActionL1_->setChecked(doc_.mode() == idc::engine::Tier::L1);
    if (modeActionL2_) modeActionL2_->setChecked(doc_.mode() == idc::engine::Tier::L2);
    if (modeActionL3_) modeActionL3_->setChecked(doc_.mode() == idc::engine::Tier::L3);
}

// 非模态提示：写状态栏提示标签 + 限时消息（错误不打断用户，§5.2）。
void MainWindow::notify(const QString& msg, const bool isError) {
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
bool MainWindow::focusInTextInput() const {
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
            default: break;
        }
    }
    QMainWindow::keyPressEvent(event);
}

// 模式中文名。
QString MainWindow::modeName(const idc::engine::Tier tier) {
    switch (tier) {
        case idc::engine::Tier::L1: return QStringLiteral("模式: L1 标准提取");
        case idc::engine::Tier::L2: return QStringLiteral("模式: L2 反向剔除");
        case idc::engine::Tier::L3: return QStringLiteral("模式: L3 网格分割");
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

    idc::core::Image img;
    QString err;
    if (!bridge_.loadImage(path, img, err)) {
        notify(QStringLiteral("打开失败：%1").arg(err), true); // 非模态。
        return;
    }
    doc_.setImage(std::move(img), path); // 触发 imageChanged。
    notify(QStringLiteral("已打开：%1（%2×%3）").arg(path).arg(doc_.width()).arg(doc_.height()), false);
}

// 导出（委托 EngineBridge → Core runEngine + exportImage）。
void MainWindow::onExport() {
    if (!doc_.hasImage()) { notify(QStringLiteral("无图像可导出"), true); return; }
    if (!doc_.hasRect()) { notify(QStringLiteral("请先在画布创建选区"), true); return; }

    const idc::engine::EngineConfig cfg = doc_.buildEngineConfig();

    // 合并重排参数警告（cols*rows < 保留块数，或 cw/ch < 网格单元尺寸）：导出前弹窗二次确认。
    // 内联红字已在导出面板实时显示，此处按需求再加一道模态确认。
    const QString rearrangeWarn = exportPanel_->rearrangeWarning();
    if (!rearrangeWarn.isEmpty()) {
        const QMessageBox::StandardButton ret = QMessageBox::warning(
            this, QStringLiteral("重排参数警告"),
            rearrangeWarn + QStringLiteral("\n\n仍要继续导出吗？"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (ret != QMessageBox::Yes) return;
    }

    QString path;
    if (cfg.emitParams.mode == idc::engine::EmitMode::SEPARATE) {
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

    QString err;
    if (bridge_.exportResult(doc_.working(), cfg, path, err)) {
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
    if (doc_.mode() == idc::engine::Tier::L3) {
        const std::vector<int> idx = scene_->cellsIntersecting(sceneRect);
        if (!idx.empty()) doc_.addCells(idx);
        return;
    }
    const qreal W = doc_.width(), H = doc_.height();
    const qreal l = std::clamp(sceneRect.left(), 0.0, W);
    const qreal t = std::clamp(sceneRect.top(), 0.0, H);
    const qreal r = std::clamp(sceneRect.right(), 0.0, W);
    const qreal b = std::clamp(sceneRect.bottom(), 0.0, H);
    idc::engine::RectRegion rr(qRound(l), qRound(t), qRound(r), qRound(b));
    if (rr.width() <= 0 || rr.height() <= 0) return; // 忽略退化选区。
    // L2 多矩形并集：框选逐个追加矩形（而非替换单选区）。
    if (doc_.mode() == idc::engine::Tier::L2 && doc_.l2Sub() == L2Sub::MULTI_RECT) {
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
    if (doc_.mode() != idc::engine::Tier::L2 || doc_.l2Sub() != L2Sub::MULTI_RECT) return;
    const idc::engine::RectRegion rr(qRound(sceneRect.left()), qRound(sceneRect.top()),
                                     qRound(sceneRect.right()), qRound(sceneRect.bottom()));
    doc_.updateRect(static_cast<std::size_t>(index), rr);
    // 拖拽期间 syncPanels 被跳过（避免逐帧重建），故在此定向刷新：
    // 让列表跟随选中被拖矩形（并高亮），并实时回显其新尺寸。
    param_->selectRectRow(index);
    param_->updateRectListItem(index);
}

// L2 多矩形：面板列表选中行变化 → 高亮画布上对应选区框（-1 清除高亮）。
void MainWindow::onRectSelected(const int index) {
    if (scene_) scene_->setActiveMultiRect(index);
}

// L3 单击切换某单元：写回 Document（触发刷新与点选图元高亮更新）。
void MainWindow::onCellToggled(const int index) {
    if (!doc_.hasImage() || doc_.mode() != idc::engine::Tier::L3) return;
    doc_.toggleCell(index);
}

// L3 拖拽框选：把命中的单元并入选择集（去重）。
void MainWindow::onCellsMarquee(const std::vector<int>& indices) {
    if (!doc_.hasImage() || doc_.mode() != idc::engine::Tier::L3) return;
    doc_.addCells(indices);
}

// L3 CUSTOM 拖拽调序：把 from 单元移到 to 单元原序位（重排选择集顺序即自定义输出序）。
void MainWindow::onCellReordered(const int from, const int to) {
    if (!doc_.hasImage() || doc_.mode() != idc::engine::Tier::L3) return;
    doc_.moveCellOrder(from, to);
}

// 方向键微调选区（保持尺寸，钳制到图像内）。
void MainWindow::onNudge(const int dx, const int dy) {
    if (!doc_.hasImage() || !doc_.hasRect()) return;
    const idc::engine::RectRegion r = doc_.rect();
    const int w = r.width(), h = r.height();
    const int W = doc_.width(), H = doc_.height();
    if (w >= W || h >= H) return; // 选区已达整幅，无需微调。
    const int nl = std::clamp(r.left + dx, 0, W - w);
    const int nt = std::clamp(r.top + dy, 0, H - h);
    doc_.setRect(idc::engine::RectRegion(nl, nt, nl + w, nt + h));
}

// 光标移动：更新状态栏坐标与像素颜色。
void MainWindow::onCursor(const QPointF& scenePos) {
    const int x = static_cast<int>(scenePos.x());
    const int y = static_cast<int>(scenePos.y());
    stCoord_->setText(QStringLiteral("坐标: (%1, %2)").arg(x).arg(y));
    if (doc_.hasImage() && doc_.working().inBounds(x, y)) {
        const idc::core::Color c = doc_.working().getPixel(x, y);
        stColor_->setText(QStringLiteral("RGB(%1,%2,%3)").arg(c.r).arg(c.g).arg(c.b));
    } else {
        stColor_->setText(QStringLiteral("RGB(-,-,-)"));
    }
}

// 视图缩放倍数变化：更新状态栏放大倍数显示（100% = 1:1）。
void MainWindow::onZoomChanged(const qreal factor) {
    if (!stZoom_) return;
    stZoom_->setText(QStringLiteral("缩放: %1%").arg(qRound(factor * 100.0)));
}

// 清除切割线（清除选区）。
void MainWindow::onClearCut() {
    if (!doc_.hasRect()) return;
    doc_.clearRect(); // 触发 changed → refreshPreview 清空叠加层。
    notify(QStringLiteral("已清除选区与切割线"), false);
}

// 切换预览遮罩显隐。
void MainWindow::onToggleMasks() {
    const bool v = !scene_->masksVisible();
    scene_->setMasksVisible(v);
    refreshPreview();
    notify(v ? QStringLiteral("已显示预览遮罩") : QStringLiteral("已隐藏预览遮罩"), false);
}

// 工具栏/快捷键切换模式（L1/L2/L3）。
void MainWindow::onModeAction(const int tierInt) {
    switch (tierInt) {
        case 1: doc_.setMode(idc::engine::Tier::L1); break;
        case 2: doc_.setMode(idc::engine::Tier::L2); break;
        case 3: doc_.setMode(idc::engine::Tier::L3); break;
        default: break;
    }
}

// K/R 快捷键切换极性。
void MainWindow::onPolarityShortcut(const bool remove) {
    doc_.setPolarity(remove ? idc::engine::Polarity::REMOVE : idc::engine::Polarity::KEEP);
}

// ---------------------------------------------------------------------------
// 预处理（FR-1 / G-3）：各操作经 EngineBridge 调 Core processing 变换工作图。
// GUI 不自实现像素运算（A-0.1）；所有变换走公共收尾 applyWorkingImage。
// ---------------------------------------------------------------------------

// 预处理公共收尾：维度变化（旋转 90/270、缩放）会使既有选区坐标越界/失配，
// 故先清除失效选区（避免 Core 切割报错），再写回工作图（触发 imageChanged→重建预览底图+刷新）。
void MainWindow::applyWorkingImage(idc::core::Image next, const QString& okMsg) {
    if (!doc_.hasImage()) return;
    if (next.empty()) { notify(QStringLiteral("预处理失败：结果为空图"), true); return; }
    if (next.width() != doc_.width() || next.height() != doc_.height()) {
        doc_.clearRect();    // 单选区坐标基于旧尺寸，已失效。
        doc_.clearRects();   // L2 多矩形同理。
        doc_.clearCells();   // L3 选择集序号对应旧网格，一并清空。
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
    applyWorkingImage(bridge_.rotateImage(doc_.working(), angleDeg),
                      QStringLiteral("已旋转 %1°").arg(angleDeg));
}

// 翻转（水平/垂直）。
void MainWindow::onFlip(const bool horizontal) {
    if (!doc_.hasImage()) return;
    applyWorkingImage(bridge_.flipImage(doc_.working(), horizontal),
                      horizontal ? QStringLiteral("已水平翻转") : QStringLiteral("已垂直翻转"));
}

// 按比例缩放（factor 为倍数）。可能耗时，包在模态忙碌对话框内并加重入守卫。
void MainWindow::onScale(const double factor) {
    if (!doc_.hasImage() || busyResample_) return;
    busyResample_ = true;
    runWithBusyDialog(QStringLiteral("正在按比例缩放图像，请稍候…"), [this, factor] {
        applyWorkingImage(bridge_.scaleImage(doc_.working(), factor),
                          QStringLiteral("已缩放至 %1%").arg(qRound(factor * 100.0)));
    });
    busyResample_ = false;
}

// 目标尺寸缩放。可能耗时，包在模态忙碌对话框内并加重入守卫。
void MainWindow::onResize(const int newWidth, const int newHeight) {
    if (!doc_.hasImage() || busyResample_) return;
    busyResample_ = true;
    runWithBusyDialog(QStringLiteral("正在缩放到目标尺寸，请稍候…"), [this, newWidth, newHeight] {
        applyWorkingImage(bridge_.resizeImage(doc_.working(), newWidth, newHeight),
                          QStringLiteral("已缩放到 %1×%2").arg(newWidth).arg(newHeight));
    });
    busyResample_ = false;
}

// 黑白（灰度）。
void MainWindow::onGray() {
    if (!doc_.hasImage()) return;
    applyWorkingImage(bridge_.toGrayImage(doc_.working()), QStringLiteral("已转为黑白"));
}

// 色道反色：依勾选的 R/G/B 拼出长度 3 的反相掩码（'1' 反相、'0' 保持）；灰度图 Core 忽略掩码、整体反相。
void MainWindow::onInvert(const bool invR, const bool invG, const bool invB) {
    if (!doc_.hasImage()) return;
    const std::string mask = std::string(invR ? "1" : "0") + (invG ? "1" : "0") + (invB ? "1" : "0");
    applyWorkingImage(bridge_.invertImage(doc_.working(), mask), QStringLiteral("已按通道反色"));
}

// 色道分离：依勾选的 R/G/B 拼出长度 3 的保留掩码（'1' 保留、'0' 置零）。
void MainWindow::onSplit(const bool keepR, const bool keepG, const bool keepB) {
    if (!doc_.hasImage()) return;
    const std::string mask = std::string(keepR ? "1" : "0") + (keepG ? "1" : "0") + (keepB ? "1" : "0");
    applyWorkingImage(bridge_.splitImage(doc_.working(), mask), QStringLiteral("已按通道分离"));
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

} // namespace idc::gui
