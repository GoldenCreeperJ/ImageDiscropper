// ============================================================================
// 文件：app/main_window_ui.cpp
// 作用：实现 MainWindowUi 的四个静态建造方法（见同名头文件说明）。
// 分块依据：
//   - buildCentral：部件创建 + 注入 + 布局（左滚动容器 | 画布 | 右选项卡）；
//   - buildMenus/buildToolbar：动作创建 + 连接（全部动作触发连接到 w 的槽/控制器槽）；
//   - buildStatus：StatusBar 创建 + 初始模式名。
// 说明：所有 connect 的接收者/上下文均为 w（friend 权限连接私有槽）；lambda 捕获 w 而非 this。
// ============================================================================
#include "app/main_window_ui.h"

#include <QAction>
#include <QActionGroup>
#include <QKeySequence>
#include <QMenuBar>
#include <QMessageBox>
#include <QScrollArea>
#include <QSplitter>
#include <QTabWidget>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

#include "app/annotation_coordinator.h"
#include "app/main_window.h"
#include "app/preprocess_controller.h"
#include "app/status_bar.h"
#include "canvas/canvas_scene.h"
#include "canvas/canvas_view.h"
#include "panels/annotation_prop_panel.h"
#include "panels/export_panel.h"
#include "panels/image_panel.h"
#include "panels/layer_panel.h"
#include "panels/left_panel.h"
#include "panels/param_panel.h"
#include "panels/tool_panel.h"

// 版本号经 CMake 编译期宏注入；独立配置本目录时回退占位版本（与构建说明一致）。
#ifndef IDC_GUI_VERSION
#define IDC_GUI_VERSION "dev"
#endif
#ifndef IDC_CORE_VERSION
#define IDC_CORE_VERSION "dev"
#endif

namespace idc::gui {

// 装配中央区：左面板 | 画布 | 右侧选项卡（参数 / 导出 / 图像 / 标注）。
void MainWindowUi::buildCentral(MainWindow* w) {
    w->scene_ = new CanvasScene(w);
    w->view_ = new CanvasView(w->scene_, w);

    w->left_ = new LeftPanel(w);
    w->param_ = new ParamPanel(w);
    w->exportPanel_ = new ExportPanel(w);
    w->imagePanel_ = new ImagePanel(w);
    w->toolPanel_ = new ToolPanel(w);
    w->layerPanel_ = new LayerPanel(w);
    w->annoPropPanel_ = new AnnotationPropPanel(w);
    w->left_->setDocument(&w->doc_);
    w->param_->setDocument(&w->doc_);
    w->exportPanel_->setDocument(&w->doc_);
    w->imagePanel_->setDocument(&w->doc_);
    w->toolPanel_->setModel(&w->annoBridge_);
    w->layerPanel_->setModel(&w->annoBridge_);
    w->annoPropPanel_->setModel(&w->annoBridge_);

    // 右侧用 QTabWidget 分组，避免面板过长（布局约束见本目录 README「布局结构与响应式规则」）。
    w->rightTabs_ = new QTabWidget(w);
    w->rightTabs_->addTab(w->param_, QStringLiteral("参数"));
    w->rightTabs_->addTab(w->exportPanel_, QStringLiteral("导出"));
    w->rightTabs_->addTab(w->imagePanel_, QStringLiteral("图像"));
    w->rightTabs_->addTab(w->annoPropPanel_, QStringLiteral("标注"));
    w->rightTabs_->setMinimumWidth(240); // 可拖拽调宽；设下限避免控件被挤到不可用。

    // 左侧容器：模式/极性(LeftPanel) + 标注工具(ToolPanel) + 图层(LayerPanel) 竖排，可滚动避免拥挤。
    auto* leftContainer = new QWidget(w);
    auto* leftLayout = new QVBoxLayout(leftContainer);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(6);
    leftLayout->addWidget(w->left_);
    leftLayout->addWidget(w->toolPanel_);
    leftLayout->addWidget(w->layerPanel_);
    leftLayout->addStretch(1);
    auto* leftScroll = new QScrollArea(w);
    leftScroll->setWidgetResizable(true);
    leftScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    leftScroll->setWidget(leftContainer);
    // 左面板最小宽：标注工具为 3 列网格（含「等腰直角三角形」等宽标签），需足够宽才不拥挤/截断。
    leftScroll->setMinimumWidth(340);

    auto* split = new QSplitter(Qt::Horizontal, w);
    split->addWidget(leftScroll);
    split->addWidget(w->view_);
    split->addWidget(w->rightTabs_);
    split->setStretchFactor(0, 0);   // 左面板：窗口整体缩放时不抢空间
    split->setStretchFactor(1, 1);   // 画布：占据剩余空间
    split->setStretchFactor(2, 0);   // 右面板：窗口整体缩放时不抢空间
    split->setChildrenCollapsible(false); // 禁止把面板拖到 0 而完全折叠消失
    split->setHandleWidth(6);             // 稍宽的分隔条，拖拽手感更明显
    split->setSizes({340, 860, 300});     // 初始宽度（左面板预留足够容纳标注工具网格；此后可自由拖拽）
    w->setCentralWidget(split);
}

// 装配菜单栏：文件 / 编辑 / 图像 / 标注 / 视图 / 帮助。
void MainWindowUi::buildMenus(MainWindow* w) {
    // ---- 文件 ----
    QMenu* mFile = w->menuBar()->addMenu(QStringLiteral("文件(&F)"));
    QAction* aOpen = mFile->addAction(QStringLiteral("打开图像(&O)"));
    aOpen->setShortcut(QKeySequence::Open);
    QObject::connect(aOpen, &QAction::triggered, w, &MainWindow::onOpen);
    QAction* aExport = mFile->addAction(QStringLiteral("导出(&S)"));
    aExport->setShortcut(QKeySequence::Save);
    QObject::connect(aExport, &QAction::triggered, w, &MainWindow::onExport);
    mFile->addSeparator();
    // 配置文件加载/保存（FR-L3.8）：复用 Core loadEngineConfig/saveEngineConfig（SPEC §7 schema）。
    QAction* aLoadCfg = mFile->addAction(QStringLiteral("加载配置(&L)…"));
    aLoadCfg->setToolTip(QStringLiteral("从 JSON 配置文件还原切割/排序/导出参数；可撤销。"));
    QObject::connect(aLoadCfg, &QAction::triggered, w, &MainWindow::onLoadConfig);
    QAction* aSaveCfg = mFile->addAction(QStringLiteral("保存配置(&C)…"));
    aSaveCfg->setToolTip(QStringLiteral("把当前切割/排序/导出参数存为 JSON 配置文件，可复用于同尺寸图像。"));
    QObject::connect(aSaveCfg, &QAction::triggered, w, &MainWindow::onSaveConfig);
    mFile->addSeparator();
    const QAction* aExit = mFile->addAction(QStringLiteral("退出(&X)"));
    QObject::connect(aExit, &QAction::triggered, w, &QWidget::close);

    // ---- 编辑（全局撤销/重做：上下文路由，复用 Core HistoryManager）----
    QMenu* mEdit = w->menuBar()->addMenu(QStringLiteral("编辑(&E)"));
    w->aUndo_ = mEdit->addAction(QStringLiteral("撤销(&U)"));
    w->aUndo_->setShortcut(QKeySequence::Undo);
    w->aUndo_->setToolTip(QStringLiteral("撤销（Ctrl+Z）：正在画标注时撤销标注操作，否则撤销选区/排序/导出等参数修改。"));
    QObject::connect(w->aUndo_, &QAction::triggered, w, &MainWindow::onUndo);
    w->aRedo_ = mEdit->addAction(QStringLiteral("重做(&R)"));
    w->aRedo_->setShortcut(QKeySequence::Redo);
    w->aRedo_->setToolTip(QStringLiteral("重做（Ctrl+Y）：恢复上一步被撤销的操作。"));
    QObject::connect(w->aRedo_, &QAction::triggered, w, &MainWindow::onRedo);
    mEdit->addSeparator();
    QAction* aClear = mEdit->addAction(QStringLiteral("清除选区(&C)"));
    aClear->setShortcut(QKeySequence(Qt::Key_Escape));
    QObject::connect(aClear, &QAction::triggered, w, &MainWindow::onClearCut);

    // ---- 图像（预处理 FR-1：旋转/翻转/黑白/反色/重置；完整控件见右侧「图像」页）----
    QMenu* mImage = w->menuBar()->addMenu(QStringLiteral("图像(&I)"));
    const QAction* aRotL = mImage->addAction(QStringLiteral("左转 90°"));
    QObject::connect(aRotL, &QAction::triggered, w, [w] { w->preprocess_->onRotate(-90); });
    const QAction* aRotR = mImage->addAction(QStringLiteral("右转 90°"));
    QObject::connect(aRotR, &QAction::triggered, w, [w] { w->preprocess_->onRotate(90); });
    const QAction* aRot180 = mImage->addAction(QStringLiteral("旋转 180°"));
    QObject::connect(aRot180, &QAction::triggered, w, [w] { w->preprocess_->onRotate(180); });
    mImage->addSeparator();
    const QAction* aFlipH = mImage->addAction(QStringLiteral("水平翻转"));
    QObject::connect(aFlipH, &QAction::triggered, w, [w] { w->preprocess_->onFlip(true); });
    const QAction* aFlipV = mImage->addAction(QStringLiteral("垂直翻转"));
    QObject::connect(aFlipV, &QAction::triggered, w, [w] { w->preprocess_->onFlip(false); });
    mImage->addSeparator();
    const QAction* aGray = mImage->addAction(QStringLiteral("黑白"));
    QObject::connect(aGray, &QAction::triggered, w, [w] { w->preprocess_->onGray(); });
    const QAction* aInvert = mImage->addAction(QStringLiteral("反色（全通道）"));
    QObject::connect(aInvert, &QAction::triggered, w, [w] { w->preprocess_->onInvert(true, true, true); });
    mImage->addSeparator();
    const QAction* aResetPre = mImage->addAction(QStringLiteral("重置预处理"));
    QObject::connect(aResetPre, &QAction::triggered, w, [w] { w->preprocess_->onResetPreprocess(); });
    const QAction* aImagePanel = mImage->addAction(QStringLiteral("图像处理面板（缩放/尺寸/色道）…"));
    QObject::connect(aImagePanel, &QAction::triggered, w, [w] {
        if (w->rightTabs_ && w->imagePanel_) w->rightTabs_->setCurrentWidget(w->imagePanel_);
    });

    // ---- 标注（第四阶段 G-4/G-5）：撤销/重做/删除选中/清除全部/属性定位 ----
    // 标注撤销/重做复用 Core AnnotationLayer 内建分层快照；快捷键 Ctrl+Z/Y 已统一交给编辑菜单
    // （上下文路由：标注上下文时自动转发到此处），故本菜单项不再绑定快捷键，避免冲突。
    QMenu* mAnno = w->menuBar()->addMenu(QStringLiteral("标注(&A)"));
    const QAction* aAnnoUndo = mAnno->addAction(QStringLiteral("撤销标注(&U)"));
    QObject::connect(aAnnoUndo, &QAction::triggered, w, [w] { w->annotation_->onAnnoUndo(); });
    const QAction* aAnnoRedo = mAnno->addAction(QStringLiteral("重做标注(&R)"));
    QObject::connect(aAnnoRedo, &QAction::triggered, w, [w] { w->annotation_->onAnnoRedo(); });
    mAnno->addSeparator();
    QAction* aAnnoDel = mAnno->addAction(QStringLiteral("删除选中标注(&D)"));
    aAnnoDel->setToolTip(QStringLiteral("删除当前选中的标注（或按 Delete 键）。"));
    QObject::connect(aAnnoDel, &QAction::triggered, w, [w] { w->annotation_->onAnnoDeleteSelected(); });
    const QAction* aAnnoClear = mAnno->addAction(QStringLiteral("清除全部标注(&C)"));
    QObject::connect(aAnnoClear, &QAction::triggered, w, [w] { w->annotation_->onAnnoClearAll(); });
    mAnno->addSeparator();
    const QAction* aAnnoProp = mAnno->addAction(QStringLiteral("标注属性…"));
    QObject::connect(aAnnoProp, &QAction::triggered, w, [w] {
        if (w->rightTabs_ && w->annoPropPanel_) w->rightTabs_->setCurrentWidget(w->annoPropPanel_);
    });

    // ---- 视图 ----
    QMenu* mView = w->menuBar()->addMenu(QStringLiteral("视图(&V)"));
    QAction* aZoomIn = mView->addAction(QStringLiteral("放大"));
    aZoomIn->setShortcut(QKeySequence::ZoomIn);
    QObject::connect(aZoomIn, &QAction::triggered, w, [w] { w->view_->zoomIn(); });
    QAction* aZoomOut = mView->addAction(QStringLiteral("缩小"));
    aZoomOut->setShortcut(QKeySequence::ZoomOut);
    QObject::connect(aZoomOut, &QAction::triggered, w, [w] { w->view_->zoomOut(); });
    QAction* aFit = mView->addAction(QStringLiteral("适应窗口"));
    aFit->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    QObject::connect(aFit, &QAction::triggered, w, [w] { w->view_->fitToWindow(); });
    const QAction* aReset = mView->addAction(QStringLiteral("重置视图 (1:1)"));
    QObject::connect(aReset, &QAction::triggered, w, [w] { w->view_->resetZoom(); });
    mView->addSeparator();
    const QAction* aMasks = mView->addAction(QStringLiteral("切换预览遮罩"));
    QObject::connect(aMasks, &QAction::triggered, w, &MainWindow::onToggleMasks);

    // ---- 帮助 ----
    QMenu* mHelp = w->menuBar()->addMenu(QStringLiteral("帮助(&H)"));
    const QAction* aUsage = mHelp->addAction(QStringLiteral("使用说明"));
    QObject::connect(aUsage, &QAction::triggered, w, [w] {
        QMessageBox::information(w, QStringLiteral("使用说明"),
            QStringLiteral("1) 文件→打开图像。\n"
                           "2) 左侧选择模式（L1 标准提取 / L2 反向剔除 / L3 网格分割），并决定保留还是删除框内区域。\n"
                           "3) L1/L2：在画布上拖拽出选区；橙色切割线贯穿全图（与选区同色），绿色为保留、红色为删除。\n"
                           "4) 可拖动选区/四角手柄调整，或直接拖动橙色切割线（＝选区边）移动对应边；方向键微调（Shift 大步）。\n"
                           "5) L3：在右侧「参数」页设定基准点、单元尺寸与余量策略，网格线自动铺满全图；用全选/反选/清空与排序策略控制输出。\n"
                           "6) 右侧「导出」页选择输出模式与路径，点击导出（Ctrl+S）。\n\n"
                           "完整操作手册见程序目录下的 README.md。"));
    });
    const QAction* aAbout = mHelp->addAction(QStringLiteral("关于"));
    QObject::connect(aAbout, &QAction::triggered, w, [w] {
        QMessageBox::about(w, QStringLiteral("关于"),
            QStringLiteral("ImageDiscropper GUI %1\n基于统一 Grid-Selection-Emit 引擎（Core %2）。\n"
                           "L1/L2/L3 共用同一代码路径，模式仅为参数预设。")
                .arg(QStringLiteral(IDC_GUI_VERSION), QStringLiteral(IDC_CORE_VERSION)));
    });
}

// 装配工具栏：打开/导出、缩放、模式切换（遮罩切换已迁入左侧图层面板）。
void MainWindowUi::buildToolbar(MainWindow* w) {
    QToolBar* tb = w->addToolBar(QStringLiteral("主工具栏"));
    tb->setMovable(false);

    const QAction* aOpen = tb->addAction(QStringLiteral("打开"));
    QObject::connect(aOpen, &QAction::triggered, w, &MainWindow::onOpen);
    const QAction* aExport = tb->addAction(QStringLiteral("导出"));
    QObject::connect(aExport, &QAction::triggered, w, &MainWindow::onExport);
    tb->addSeparator();
    // 撤销/重做（工具栏按钮）：复用编辑菜单同一 QAction，共享 Ctrl+Z/Y 快捷键与动态启用态。
    if (w->aUndo_) tb->addAction(w->aUndo_);
    if (w->aRedo_) tb->addAction(w->aRedo_);
    tb->addSeparator();

    const QAction* aZoomIn = tb->addAction(QStringLiteral("放大"));
    QObject::connect(aZoomIn, &QAction::triggered, w, [w] { w->view_->zoomIn(); });
    const QAction* aZoomOut = tb->addAction(QStringLiteral("缩小"));
    QObject::connect(aZoomOut, &QAction::triggered, w, [w] { w->view_->zoomOut(); });
    const QAction* aFit = tb->addAction(QStringLiteral("适应"));
    QObject::connect(aFit, &QAction::triggered, w, [w] { w->view_->fitToWindow(); });
    tb->addSeparator();

    // 模式切换：三个可选动作（L1 标准提取 / L2 反向剔除 / L3 网格分割）。
    auto* group = new QActionGroup(w);
    group->setExclusive(true);
    w->modeActionL1_ = group->addAction(QStringLiteral("标准提取 (L1)"));
    w->modeActionL2_ = group->addAction(QStringLiteral("反向剔除 (L2)"));
    w->modeActionL3_ = group->addAction(QStringLiteral("网格分割 (L3)"));
    for (QAction* a : {w->modeActionL1_, w->modeActionL2_, w->modeActionL3_}) {
        a->setCheckable(true);
        tb->addAction(a);
    }
    w->modeActionL1_->setChecked(true);
    w->modeActionL3_->setToolTip(QStringLiteral("网格分割：铺满全图的网格 + 单元选择 + 排序。"));
    QObject::connect(w->modeActionL1_, &QAction::triggered, w, [w] { w->onModeAction(1); });
    QObject::connect(w->modeActionL2_, &QAction::triggered, w, [w] { w->onModeAction(2); });
    QObject::connect(w->modeActionL3_, &QAction::triggered, w, [w] { w->onModeAction(3); });
    // 遮罩切换已迁入左侧「图层」面板（成为正式图层项）；视图菜单与画布右键仍保留快捷切换。
}

// 装配状态栏：StatusBar 组件（光标坐标 / 像素颜色 / 当前模式 / 保留块数 / 缩放倍数 / 提示信息）。
void MainWindowUi::buildStatus(MainWindow* w) {
    w->status_ = new StatusBar(w);
    w->status_->setModeTier(w->doc_.mode()); // 初始模式名（原 buildStatus 用 modeName(doc_.mode()) 初始化标签）。
    w->setStatusBar(w->status_);
}

} // namespace idc::gui
