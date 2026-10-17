// ============================================================================
// 文件：app/main_window.cpp
// 作用：实现主窗口装配壳的装配、控制器注入接线与转发（见同名头文件说明）。
// 分块依据：
//   - 构造：MainWindowUi 装配 → 实例化并注入八个控制器 → connectAll 串联 → 初始同步；
//   - Document 信号响应与状态栏回报、模式/极性动作、撤销/重做路由保留在壳内；
//   - 各域编排在八个控制器：画布交互 CanvasCoordinator、会话 SessionController、导出
//     ExportController、配置 ConfigController（详见 src/app/README.md「编排」），本文件
//     只做「装配 + 编排 + 转发」，避免上帝文件。
// ============================================================================
#include "app/main_window.h"

#include <algorithm>

#include <QAction>
#include <QApplication> // applyPanelMinWidths 冲刷延迟布局失效（sendPostedEvents）。
#include <QDockWidget>  // applyPanelMinWidths 调 drawer_->width() + resizeDocks 需完整类型。
#include <QEvent>       // applyPanelMinWidths 冲刷 QEvent::LayoutRequest。
#include <QLayout>      // applyPanelMinWidths 遍历子面板 itemAt()/QLayoutItem。
#include <QKeyEvent>
#include <QSplitter>   // applyPanelMinWidths 需完整类型调 split_->sizes()/setSizes()。

#include "app/app_prefs.h"
#include "app/controllers/annotation_coordinator.h"
#include "app/controllers/canvas_coordinator.h"
#include "app/controllers/config_controller.h"
#include "app/controllers/doc_history.h"
#include "app/controllers/export_controller.h"
#include "app/main_window_ui.h"
#include "app/controllers/preprocess_controller.h"
#include "app/controllers/preview_controller.h"
#include "app/controllers/session_controller.h"
#include "app/status_bar.h"
#include "canvas/canvas_scene.h"
#include "canvas/canvas_view.h"
#include "panels/accordion_panel.h"   // applyPanelMinWidths 需完整类型取 accordion_->sizeHint()。
#include "panels/annotation_prop_panel.h"   // retranslateUi 调 annoPropPanel_->retranslate()。
#include "panels/export_panel.h"
#include "panels/preprocess_panel.h"       // retranslateUi 调 preprocessPanel_->retranslate()。
#include "panels/layer_edit_panel.h"  // retranslateUi 调 layerEditPanel_->retranslate()。
#include "panels/layer_panel.h"
#include "panels/left_panel.h"        // retranslateUi 调 left_->retranslate()。
#include "panels/param_panel.h"       // retranslateUi 调 param_->retranslate()。
#include "panels/mode_tool_panel.h"
#include "util/gui_nums.h"
#include "i18n/gui_texts.h"
#include "i18n/gui_i18n.h"

// 版本号经 CMake 编译期宏注入；独立配置本目录时回退占位版本（与构建说明一致）。
#ifndef IDC_GUI_VERSION
#define IDC_GUI_VERSION "dev"
#endif
#ifndef IDC_CORE_VERSION
#define IDC_CORE_VERSION "dev"
#endif

namespace idc::gui {

// 构造（桌面）：装配全部部件 + 接线（顺序承重，见 src/app/README.md「编排」——
// connectAll 必须先于历史 reset，否则首次 availabilityChanged 丢失、撤销/重做动作不会置灰）。
// 语言热切换走原地重放（retranslateUi），窗口不销毁，故 doc_/annoBridge_ 按值随窗持有即可。
MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    // 窗口标题带版本号：设当前值 + 登记重放闭包（切语言后原地改文，见 retranslateUi）。
    setWindowTitle(i18n::qs(IDC_GT(label, kWindowTitleFmt)).arg(QStringLiteral(IDC_GUI_VERSION)));
    retranslator_.add([this] {
        setWindowTitle(i18n::qs(IDC_GT(label, kWindowTitleFmt)).arg(QStringLiteral(IDC_GUI_VERSION)));
    });
    resize(nums::app::kWindowWidthPx, nums::app::kWindowHeightPx);
    buildDesktopUi();
    initCore();
}

// 移动端骨架构造：仅做 QObject 挂接；标题/尺寸与部件装配、initCore
// 均由子类 MobileShell 按触控形态自行完成（接线序列与桌面逐行共用，SPEC §8.3 行为同构）。
MainWindow::MainWindow(MobileShellTag, QWidget* parent)
    : QMainWindow(parent) {}

// 桌面骨架装配：三栏中央区 + 菜单 + 顶部工具栏 + 状态栏（逐行原迁自原构造）。
void MainWindow::buildDesktopUi() {
    MainWindowUi::buildCentral(this);   // scene_/view_/7 面板/rightTabs_
    MainWindowUi::buildMenus(this);     // aUndo_/aRedo_ + 菜单动作连接
    MainWindowUi::buildToolbar(this);   // 文件/配置/撤销重做/缩放/模式/重置全部
    MainWindowUi::buildStatus(this);    // status_
}

// 控制器接线与初始同步（桌面/移动共用的唯一接线序列）。
void MainWindow::initCore() {
    preview_ = new PreviewController(this);
    preview_->setDocument(&doc_);
    preview_->setScene(scene_);
    preview_->setView(view_);
    preview_->setExportPanel(exportPanel_);
    preview_->setParamPanel(param_);
    preview_->setStatusBar(status_);
    preview_->setAnnotationBridge(&annoBridge_);
    preview_->setMultiPageContainer(rightTabs_);
    preview_->setLayerPanel(layerPanel_);
    preview_->connectSignals();   // 含画布右键遮罩切换（onToggleMasks）。

    preprocess_ = new PreprocessController(this);
    preprocess_->setDocument(&doc_);
    preprocess_->setStatusBar(status_);
    preprocess_->setDialogParent(this);
    preprocess_->setPreprocessPanel(preprocessPanel_);
    preprocess_->connectSignals();

    annotation_ = new AnnotationCoordinator(this);
    annotation_->setAnnotationBridge(&annoBridge_);
    annotation_->setScene(scene_);
    annotation_->setView(view_);
    annotation_->setToolPanel(toolPanel_);
    annotation_->setLayerPanel(layerPanel_);
    layerPanel_->setDocument(&doc_);   // 「图层」面板图片图层单开关数据源（整体显隐读写）。
    annotation_->setAnnoPropPanel(annoPropPanel_);
    annotation_->setExportPanel(exportPanel_);
    annotation_->setPreviewController(preview_);
    annotation_->setDialogParent(this);
    annotation_->connectSignals();   // 含 exportPanel_->setBurnInChecked 初始同步。

    // 历史控制器：本窗新建并持有（QObject 子对象，随窗口析构）；接文档/标注桥与状态栏，
    // start() 连 Document::changed/layersChanged→scheduleCapture（防抖采集）。
    history_ = new DocHistory(this);
    history_->setDocument(&doc_);
    history_->setAnnotationBridge(&annoBridge_);
    history_->setStatusBar(status_);
    history_->start();

    canvas_ = new CanvasCoordinator(this);
    canvas_->setDocument(&doc_);
    canvas_->setAnnotationBridge(&annoBridge_);
    canvas_->setScene(scene_);
    canvas_->setView(view_);
    canvas_->setParamPanel(param_);
    canvas_->setStatusBar(status_);
    canvas_->setToolPanel(toolPanel_);
    canvas_->connectSignals();

    session_ = new SessionController(this);
    session_->setDocument(&doc_);
    session_->setAnnotationBridge(&annoBridge_);
    session_->setPreprocessController(preprocess_);
    session_->setHistory(history_);
    session_->setStatusBar(status_);
    session_->setToolPanel(toolPanel_);
    session_->setLayerEditPanel(layerEditPanel_);
    session_->setLayerPanel(layerPanel_);
    session_->setDialogParent(this);
    session_->connectSignals();

    export_ = new ExportController(this);
    export_->setDocument(&doc_);
    export_->setAnnotationBridge(&annoBridge_);
    export_->setPreprocessController(preprocess_);
    export_->setExportPanel(exportPanel_);
    export_->setStatusBar(status_);
    export_->setDialogParent(this);
    export_->connectSignals();

    config_ = new ConfigController(this);
    config_->setDocument(&doc_);
    config_->setAnnotationBridge(&annoBridge_);
    config_->setPreprocessController(preprocess_);
    config_->setHistory(history_);
    config_->setStatusBar(status_);
    config_->setDialogParent(this);

    connectAll();                    // 剩余跨件串联（须在历史启用态刷新之前）。

    syncPanels();
    preview_->refreshPreview();
    history_->reset();               // 以当前（空）态为唯一基线，回灌撤销/重做初始置灰态。
    canvas_->updateInteractionHint();   // 初始态提示：默认「框选」模式 + 预处理目标（启动时四态面板已默认勾选框选）。
}

// 串联剩余跨件信号槽：Document 信号、状态栏回报、历史启用态回灌与菜单勾选态回同步
// （其余连接住在属主旁：画布交互在 CanvasCoordinator、图层编辑在 SessionController、
// 导出在 ExportController、选项卡切换与遮罩右键在 PreviewController、防抖采集在 DocHistory）。
void MainWindow::connectAll() {
    connect(&doc_, &Document::imageChanged, this, &MainWindow::onImageChanged);
    connect(&doc_, &Document::changed, this, &MainWindow::onDocChanged);
    // 多层画布：图层栈/画布尺寸声明变 → 控制器 composeCanvas 重组展平底图（再叠画布预处理→working_）。
    connect(&doc_, &Document::layersChanged, preprocess_, &PreprocessController::recomposeLayerCanvas);

    connect(view_, &CanvasView::cursorScenePos, this, &MainWindow::onCursor);
    connect(view_, &CanvasView::zoomChanged, this, &MainWindow::onZoomChanged);
    connect(view_, &CanvasView::resetViewRequested, view_, &CanvasView::fitToWindow);

    // 「导出」面板烧录开关 → 菜单「导出设置」勾选态回同步（落地连接在 ExportController/AnnotationCoordinator）；
    // 初始同步一次（默认开）。
    connect(exportPanel_, &ExportPanel::layersBurnInChanged, this,
            [this](const bool on) {
                doc_.setLayersBurnIn(on);
                if (aBurnLayers_) aBurnLayers_->setChecked(on);
            });
    exportPanel_->setLayersBurnInChecked(doc_.layersBurnIn());
    connect(exportPanel_, &ExportPanel::burnInChanged, this,
            [this](const bool on) { if (aBurnAnno_) aBurnAnno_->setChecked(on); });
    // 导出设置「坍缩」置灰随 Core 坍缩可行性回灌（与导出面板组合框同源）。
    connect(exportPanel_, &ExportPanel::collapsibleChanged, this,
            [this](const bool ok) { if (aEmitCol_) aEmitCol_->setEnabled(ok); });

    connect(history_, &DocHistory::availabilityChanged, this, &MainWindow::onHistoryAvailabilityChanged);

    // 画布四态菜单勾选态：CanvasCoordinator 的模式落地连接先于本条（initCore 连接序），
    // 模式变更后先 applyCanvasMode 落地、再读视图态回同步菜单勾选。
    // 附带重测左栏最小宽：形状子行（3 列网格，仅「标注绘制」态可见）在 modeChanged 发射前刚完成显隐，
    // 隐藏态不参与 sizeHint——不随显隐重测，英文绘制态的长工具名（Right Triangle 等）会被截断。
    connect(toolPanel_, &ModeToolPanel::modeChanged, this, [this] {
        MainWindowUi::syncCanvasModeActions(*this);
        applyPanelMinWidths();
    });

    // 用户切模式(L1/L2/L3)或极性(K/R)后，若不在「框选」态则回到框选态（统一收口）：
    // 菜单动作 / LeftPanel 按钮 / 快捷键均经 doc_.setMode / setPolarity 发此信号，一处覆盖全部入口；
    // ModeToolPanel::setMode 对已选中按钮为无操作（不重复发 modeChanged），故无需额外判当前态。
    connect(&doc_, &Document::modeOrPolarityChanged, this,
            [this] { toolPanel_->setMode(CanvasMode::Marquee); });

    // 标注上下文变化 → 重算撤销/重做启用态（annoBridge_→history_）：两者均随窗口同生命周期
    //（doc_/annoBridge_ 按值、history_ 是本窗子对象），无跨窗常驻问题，连接落在本窗。
    connect(&annoBridge_, &AnnotationBridge::changed, history_, &DocHistory::updateEnabled);
    connect(&annoBridge_, &AnnotationBridge::selectionChanged, history_, &DocHistory::updateEnabled);
    connect(&annoBridge_, &AnnotationBridge::toolChanged, history_, &DocHistory::updateEnabled);
    // 菜单/工具栏勾选态回同步（接收者 this）。
    connect(&annoBridge_, &AnnotationBridge::toolChanged, this, [this] { MainWindowUi::syncToolActions(*this); });
    connect(&annoBridge_, &AnnotationBridge::selectionChanged, this, [this] { MainWindowUi::syncAnnoSelActions(*this); });
    connect(&annoBridge_, &AnnotationBridge::changed, this, [this] { MainWindowUi::syncAnnoSelActions(*this); });
}

// 同步左侧面板与右侧「图片/预处理/参数/导出」各节 + 菜单/工具栏动作到 Document（实现在 MainWindowUi，纯 UI 状态）。
void MainWindow::syncPanels() const {
    MainWindowUi::syncUi(*this);
}

// 单键快捷键事件入口（1/2/3 切模式、K/R 切极性、Esc 清选区、Delete/Backspace 删除选中标注）：
// 分派逻辑在 KeyRouter（见 app/controllers/key_router.h），此处只转发（仍须为 QMainWindow 虚函数）。
void MainWindow::keyPressEvent(QKeyEvent* event) {
    if (KeyRouter::handleKeyPress(this, event)) return;
    QMainWindow::keyPressEvent(event);
}

// 语言热切换重放（原地只改文本、不重建窗口）：splitter 尺寸/展开节/滚动/焦点/选区/图层/标注/撤销栈
// 全部原样保留。顺序承重：先按新偏好设全局语言（读在重放前），再本窗登记表 + 各子件依序重放，
// 然后重测左右栏最小宽（文本长度随语言变），末尾重派生动态文本。
void MainWindow::retranslateUi() {
    i18n::setupFromPreference(UiPrefs::instance().language());   // 读在重放前：否则闭包读到旧语言。

    retranslator_.run();                     // 本窗骨架：窗口标题（菜单/工具栏/状态栏初值经各自 retranslate）。
    if (status_) status_->retranslate();
    if (left_) left_->retranslate();
    if (toolPanel_) toolPanel_->retranslate();
    if (layerPanel_) layerPanel_->retranslate();
    if (param_) param_->retranslate();
    if (exportPanel_) exportPanel_->retranslate();
    if (preprocessPanel_) preprocessPanel_->retranslate();
    if (layerEditPanel_) layerEditPanel_->retranslate();
    if (annoPropPanel_) annoPropPanel_->retranslate();
    // 菜单/工具栏/折叠节标题均已在装配期登记入 retranslator_（见 main_window_ui.cpp / main_window_menus.cpp），
    // 上面 retranslator_.run() 已重放。

    applyPanelMinWidths();                   // 重测栏宽：英文长标签抬升下限、免截断。

    // 重派生动态文本（网格信息/选择计数/矩形列表行/坠缩可行性提示/模式目标提示/状态栏）。
    syncPanels();
    preview_->refreshPreview();
    canvas_->updateInteractionHint();
}

// 按当前语言/显隐态实测左右栏内容自然宽并抬高最小宽：与 main_window_ui.cpp 构造期同一口径，
// 抽为成员函数供构造、语言热切换与进出「标注绘制」态（shapeArea_ 显隐）共用。
// 栏宽随内容变：低于新下限一律抬升防截断；仅当栏当前宽仍等于上次实际自动值（用户没手动拖宽）
// 时才跟随内容回收（退出绘制态可缩回）。
void MainWindow::applyPanelMinWidths() {
    // 承重：shapeArea_（形状子行，仅「标注绘制」态可见）显隐的几何失效沿祖先链**逐层** post
    // LayoutRequest（shapeArea_→box→ModeToolPanel→leftContainer_→viewport），每层处理时又向父层再 post
    // 新事件——单轮事件循环只推进一层，同步读到的 sizeHint（容器与子面板皆有）会停在“进态撑大”
    // 的旧值，退绘制态便测不回窄宽。故先多轮冲刷待处理布局事件至排空（链深有界），再实测。
    for (int pass = 0; pass < 4; ++pass)
        QApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    // 取各**直接子面板**当前 sizeHint 宽的最大值作内容自然宽：绕开容器缓存 sizeHint（widgetResizable
    // 把容器撑到视口宽后其 sizeHint 会被拉伸污染），子面板随显隐即时刷新更可靠。
    const auto contentWidth = [](const QWidget* host, const int fallback) {
        int w = fallback;
        if (const auto* lay = host->layout()) {
            for (int i = 0; i < lay->count(); ++i) {
                if (const QLayoutItem* it = lay->itemAt(i); it && it->widget())
                    w = std::max(w, it->widget()->sizeHint().width());
            }
        }
        return w;
    };

    // 移动端抽屉：与桌面**同一落点**——把 max(设计下限, 页内容自然宽) 抬到每页 QScrollArea 的最小宽上
    //（桌面也是抬在 leftScroll_/rightScroll_ 而非内容上），使抽屉页区实际加宽、进标注绘制态时形状子行（3 列
    // 网格）不被横向 AlwaysOff 裁掉；随语言重放与进出绘制态重测，防英文长标签截断。桌面形态下本列表恒空，跳过。
    int maxPageMinW = 0;
    for (QScrollArea* pageScroll : drawerPageScrolls_)
        if (pageScroll && pageScroll->widget()) {
            const int minW = contentWidth(pageScroll->widget(), nums::panels::kLeftPanelMinWidthPx);
            pageScroll->setMinimumWidth(minW);
            maxPageMinW = std::max(maxPageMinW, minW);
        }
    // 直接在 dock 本体上设最小宽——等价桌面把 min 设在 splitter 的直接子项 leftScroll_ 上。
    // 深层传播（sa→stacked→drawerContent→dock）不可靠，直接约束 dock 保证窗口缩小时内容不被裁。
    if (drawer_ && maxPageMinW > 0)
        drawer_->setMinimumWidth(maxPageMinW);

    // 以下桌面三栏；移动形态无 leftScroll_/split_ 等成员，直接返回。
    if (!leftScroll_ || !rightScroll_ || !split_ || !leftContainer_ || !accordion_) return;
    const int rightMinW = contentWidth(accordion_, nums::app::kRightPanelWidthPx);
    const int leftMinW = contentWidth(leftContainer_, nums::app::kLeftPanelWidthPx);
    rightScroll_->setMinimumWidth(rightMinW);
    leftScroll_->setMinimumWidth(leftMinW);
    // 分隔器取整容差：当前宽与上次实际自动值相差在此内视为“仍处自动宽”（用户没手动拖宽）。
    constexpr int kTolPx = 2;
    const auto nearAuto = [](const int cur, const int lastAuto) {
        return lastAuto < 0 || (cur > lastAuto ? cur - lastAuto : lastAuto - cur) <= kTolPx;
    };
    if (QList<int> sizes = split_->sizes(); sizes.size() == 3) {
        // 不足下限→必抬升（防截断）；或仍在自动宽→跟随新下限（可升可回）。
        if (sizes[0] < leftMinW || nearAuto(sizes[0], lastLeftAutoMinW_))
            sizes[0] = leftMinW;
        if (sizes[2] < rightMinW || nearAuto(sizes[2], lastRightAutoMinW_))
            sizes[2] = rightMinW;
        split_->setSizes(sizes);
        // 记录「实际生效」栏宽（setSizes 后读回）而非请求值：窗口小于三栏请求总宽时 QSplitter 会
        // 等比压缩各列，实际宽 < 请求宽；若记请求值，下次读到的实际值与之错位超容差 → nearAuto 误判
        // 为「用户拖宽」而拒绝回收。比对双方统一取实际值才稳定。
        const QList<int> applied = split_->sizes();
        lastLeftAutoMinW_  = applied.size() == 3 ? applied[0] : sizes[0];
        lastRightAutoMinW_ = applied.size() == 3 ? applied[2] : sizes[2];
    }
}

// 从给定路径打开图像（公共面转发：main.cpp 命令行首参自动打开）。
void MainWindow::openImageFromPath(const QString& path) const {
    session_->openImageFromPath(path);
}

// 换图：重建预览底图、刷新与同步面板。
void MainWindow::onImageChanged() const {
    preview_->rebuildPreviewPixmap();
    syncPanels();
    preview_->refreshPreview();
    // 同维度预处理不清标注（矢量叠加 + 导出注入当前 base），故重绘标注层以对齐最新底图。
    scene_->updateAnnotations(annoBridge_);
}

// 参数变更：刷新预览并同步面板。
void MainWindow::onDocChanged() const {
    preview_->refreshPreview();
    // 选中层变化时刷新画布落位框高亮（不重算几何）；非图层画布时无落位框，为空操作。
    scene_->setImageLayerSelection(doc_.hasLayers() ? doc_.selectedLayerId() : std::optional<int>{});
    // 拖拽选区（单矩形或多矩形）期间跳过面板/工具栏全量回同步：模式/极性/格式/质量/命名等均与 rect 无关，
    // 逐帧 syncPanels 是纯浪费且加剧拖拽卡顿；释放时会照常同步一次。
    if (!scene_->isDraggingSelection() && !scene_->isDraggingMultiRect()) syncPanels();
}

// 光标移动：更新状态栏坐标与像素颜色。
void MainWindow::onCursor(const QPointF& scenePos) const {
    const int x = static_cast<int>(scenePos.x());
    const int y = static_cast<int>(scenePos.y());
    status_->setCoord(x, y);
    // 标注为矢量叠加、不烘焙进 working()：先问画布此处有无标注命中，命中则显其自身颜色（否则下面只会读到标注底下的图像像素）。
    if (const auto anno = scene_->annotationColorAt(scenePos)) {
        status_->setColor(anno->r, anno->g, anno->b);
        return;
    }
    if (doc_.hasImage() && doc_.working().inBounds(x, y)) {
        const core::Color c = doc_.working().getPixel(x, y);
        status_->setColor(c.r, c.g, c.b);
    } else {
        status_->clearColor();
    }
}

// 视图缩放倍数变化：更新状态栏放大倍数显示（100% = 1:1）。
void MainWindow::onZoomChanged(const qreal factor) const {
    if (!status_) return;
    status_->setZoomPercent(qRound(factor * 100.0));
}

// 工具栏/快捷键切换模式（L1/L2/L3）。
void MainWindow::onModeAction(const int tierInt) {
    switch (tierInt) {
        case 1: doc_.setMode(engine::Tier::Standard); break;
        case 2: doc_.setMode(engine::Tier::Erase); break;
        case 3: doc_.setMode(engine::Tier::Grid); break;
        default: break;
    }
}

// K/R 快捷键切换极性。
void MainWindow::onPolarityShortcut(const bool remove) {
    doc_.setPolarity(remove ? engine::Polarity::Remove : engine::Polarity::Keep);
}

// 编辑菜单撤销：上下文路由——标注上下文转发到标注撤销，否则走文档参数撤销。
void MainWindow::onUndo() const {
    if (history_->annotationContextActive()) annotation_->onAnnoUndo();
    else history_->undo();
}

// 编辑菜单重做：与撤销对称的上下文路由。
void MainWindow::onRedo() const {
    if (history_->annotationContextActive()) annotation_->onAnnoRedo();
    else history_->redo();
}

// 撤销/重做可用性回灌：刷新编辑菜单/工具栏动作启用态。
void MainWindow::onHistoryAvailabilityChanged(const bool canUndo, const bool canRedo) const {
    if (aUndo_) aUndo_->setEnabled(canUndo);
    if (aRedo_) aRedo_->setEnabled(canRedo);
}

} // namespace idc::gui
