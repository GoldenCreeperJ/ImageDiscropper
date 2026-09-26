// ============================================================================
// 文件：app/main_window.cpp
// 作用：实现主窗口装配壳的装配、数据流编排转发与状态栏呈现（见同名头文件说明）。
// 分块依据：
//   - 构造：MainWindowUi 装配 → 实例化并注入四个控制器 → connectAll 串联 → 初始同步；
//   - 文件/编辑槽：打开/导出/配置存取/上下文路由转发；
//   - 画布交互槽：把用户意图落到 Document（预览刷新由 PreviewController 承担）；
//   - 装配细节在 main_window_ui.cpp，预览/预处理/标注/历史编排在四个控制器
//     （详见 src/app/README.md「编排」），本文件只做「装配 + 编排 + 转发」，避免上帝文件。
// ============================================================================
#include "app/main_window.h"

#include <algorithm>
#include <utility>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMessageBox>
#include <QStandardPaths>

#include "app/annotation_coordinator.h"
#include "app/doc_history.h"
#include "app/main_window_ui.h"
#include "app/preprocess_controller.h"
#include "app/preview_controller.h"
#include "app/status_bar.h"
#include "canvas/canvas_scene.h"
#include "canvas/canvas_view.h"
#include "model/engine_bridge.h"
#include "panels/export_panel.h"
#include "panels/image_panel.h"
#include "panels/layer_panel.h"
#include "panels/left_panel.h"
#include "panels/param_panel.h"

// 版本号经 CMake 编译期宏注入；独立配置本目录时回退占位版本（与构建说明一致）。
#ifndef IDC_GUI_VERSION
#define IDC_GUI_VERSION "dev"
#endif
#ifndef IDC_CORE_VERSION
#define IDC_CORE_VERSION "dev"
#endif

namespace idc::gui {

// 构造（桌面）：装配全部部件 + 接线（顺序承重，见 src/app/README.md「编排」——connectAll 必须先于 history_->reset()，
// 否则首次 availabilityChanged 丢失、撤销/重做动作不会在启动时置灰）。
MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("ImageDiscropper GUI %1").arg(QStringLiteral(IDC_GUI_VERSION)));
    resize(1280, 800);
    buildDesktopUi();
    initCore();
}

// 移动端骨架构造（GuideLine 阶段 3）：只做 QObject 挂接；标题/尺寸与部件装配、initCore
// 均由子类 MobileShell 按触控形态自行完成（接线序列与桌面逐行共用，SPEC §8.3 行为同构）。
MainWindow::MainWindow(MobileShellTag, QWidget* parent) : QMainWindow(parent) {}

// 桌面骨架装配：三栏中央区 + 菜单 + 顶部工具栏 + 状态栏（逐行原迁自原构造）。
void MainWindow::buildDesktopUi() {
    MainWindowUi::buildCentral(this);   // scene_/view_/7 面板/rightTabs_
    MainWindowUi::buildMenus(this);     // aUndo_/aRedo_ + 菜单动作连接
    MainWindowUi::buildToolbar(this);   // modeActionL1_/L2_/L3_
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
    preview_->connectSignals();

    preprocess_ = new PreprocessController(this);
    preprocess_->setDocument(&doc_);
    preprocess_->setAnnotationBridge(&annoBridge_);
    preprocess_->setStatusBar(status_);
    preprocess_->setDialogParent(this);
    preprocess_->setImagePanel(imagePanel_);
    preprocess_->connectSignals();

    annotation_ = new AnnotationCoordinator(this);
    annotation_->setAnnotationBridge(&annoBridge_);
    annotation_->setScene(scene_);
    annotation_->setView(view_);
    annotation_->setToolPanel(toolPanel_);
    annotation_->setLayerPanel(layerPanel_);
    annotation_->setAnnoPropPanel(annoPropPanel_);
    annotation_->setExportPanel(exportPanel_);
    annotation_->setPreviewController(preview_);
    annotation_->setDialogParent(this);
    annotation_->connectSignals();   // 含 exportPanel_->setBurnInChecked 初始同步。

    history_ = new DocHistory(this);
    history_->setDocument(&doc_);
    history_->setAnnotationBridge(&annoBridge_);
    history_->setStatusBar(status_);
    history_->start();               // Document::changed → scheduleCapture（防抖采集）。

    connectAll();                    // 剩余跨件串联（须在 history_->reset() 之前）。

    syncPanels();
    preview_->refreshPreview();
    history_->reset();               // 以初始参数态为撤销基线（内部发 availabilityChanged → 动作置灰）。
    notify(QStringLiteral("请打开一张图像开始（Ctrl+O）。"), false);
}

// 串联剩余跨件信号槽：Document 信号、画布交互意图、历史启用态回灌与标注上下文触发
// （其余连接住在属主旁：面板意图在 PreprocessController/AnnotationCoordinator，选项卡切换在
// PreviewController，菜单/工具栏动作在 MainWindowUi，防抖采集在 DocHistory）。
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

    connect(history_, &DocHistory::availabilityChanged, this, &MainWindow::onHistoryAvailabilityChanged);
    // 标注上下文变化（模型 changed/selectionChanged/toolChanged）→ 重算撤销/重做启用态：
    // 与原标注槽内三处 updateUndoRedoEnabled() 调用同源触发，改在 MainWindow 侧统一连接。
    connect(&annoBridge_, &AnnotationBridge::changed, history_, &DocHistory::updateEnabled);
    connect(&annoBridge_, &AnnotationBridge::selectionChanged, history_, &DocHistory::updateEnabled);
    connect(&annoBridge_, &AnnotationBridge::toolChanged, history_, &DocHistory::updateEnabled);
}

// 同步左侧面板与右侧「参数/导出/图像」页 + 工具栏模式动作到 Document。
void MainWindow::syncPanels() const {
    left_->syncFromDocument();
    param_->syncFromDocument();
    exportPanel_->syncFromDocument();
    imagePanel_->syncFromDocument();
    if (modeActionL1_) modeActionL1_->setChecked(doc_.mode() == engine::Tier::L1);
    if (modeActionL2_) modeActionL2_->setChecked(doc_.mode() == engine::Tier::L2);
    if (modeActionL3_) modeActionL3_->setChecked(doc_.mode() == engine::Tier::L3);
}

// 非模态提示：写状态栏提示标签 + 限时消息（错误不打断用户）。
void MainWindow::notify(const QString& msg, const bool isError) const {
    status_->notify(msg, isError);
}

// 判断焦点是否在数值/文本输入控件上（用于放行单键快捷键）。
bool MainWindow::focusInTextInput() {
    QWidget* fw = QApplication::focusWidget();
    return qobject_cast<QLineEdit*>(fw) ||
           qobject_cast<QAbstractSpinBox*>(fw) ||
           qobject_cast<QComboBox*>(fw);
}

// 单键快捷键（见本目录 README「快捷键列表」）：1/2/3 切模式、K/R 切极性、Delete/Backspace 删除选中标注。
// 用 keyPressEvent 而非 QShortcut，数值输入时天然不触发（理由见头文件说明，NFR-5）。
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
            case Qt::Key_Backspace: annotation_->onAnnoDeleteSelected(); event->accept(); return;
            default: break;
        }
    }
    QMainWindow::keyPressEvent(event);
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

    // Android SAF：文件选择器返回 content:// URI（内容提供者地址，非文件路径），
    // 而 Core 的 stb 按 C 接口读文件路径——先把内容复制到缓存目录转为真实路径
    //（stb 按内容嗅探格式，临时文件名无需扩展名）。桌面平台文件对话框返回本地
    // 路径，此分支不触发；doc_ 与提示仍保留用户选择的原始路径。
    QString resolvedPath = path;
    if (path.startsWith(QStringLiteral("content://"))) {
        if (QFile src(path); src.open(QIODevice::ReadOnly)) {
            const QByteArray data = src.readAll();
            src.close();
            const QString tmp = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                                + QStringLiteral("/opened-image-%1")
                                      .arg(QDateTime::currentMSecsSinceEpoch());
            if (QFile out(tmp); out.open(QIODevice::WriteOnly)) {
                out.write(data);
                out.close();
                resolvedPath = tmp;
            }
        }
        if (resolvedPath == path) {
            notify(QStringLiteral("打开失败：无法读取该内容提供者地址"), true);
            return;
        }
    }

    core::Image img;
    if (QString err; !EngineBridge::loadImage(resolvedPath, img, err)) {
        notify(QStringLiteral("打开失败：%1").arg(err), true); // 非模态。
        return;
    }
    doc_.setImage(std::move(img), path); // 触发 imageChanged。
    annoBridge_.setBaseImage(doc_.working()); // 新图＝新标注会话（Core setImage 清空旧标注）。
    history_->reset();          // 换图开新撤销会话：清空跨图历史（快照不含图像，跨图撤销无意义）。
    notify(QStringLiteral("已打开：%1（%2×%3）").arg(path).arg(doc_.width()).arg(doc_.height()), false);
}

// 导出（委托 EngineBridge → Core runEngine + exportImage）。
void MainWindow::onExport() {
    if (!doc_.hasImage()) { notify(QStringLiteral("无图像可导出，请先打开一张图像（Ctrl+O）。"), true); return; }
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
#ifdef Q_OS_ANDROID
            // Android SAF 目录选择器返回 content:// 树 URI，Core 按路径写多文件会失败
            // （SPEC §8.3 形态差异）：改为应用文档目录下按时间戳建子目录——可写、
            // 路径可经状态栏通知用户。
            path = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                   + QStringLiteral("/ImageDiscropper/")
                   + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss"));
            QDir().mkpath(path);
#else
            path = QFileDialog::getExistingDirectory(this, QStringLiteral("选择输出目录"));
            if (path.isEmpty()) return;
#endif
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

    // Android 单文件：SAF 保存对话框返回 content:// URI，Core 按路径写文件会失败——
    // 先写应用缓存再经 QFile 复制进 content URI（Qt 在 Android 支持 content:// 写入）。
    QString writePath = path;
#ifdef Q_OS_ANDROID
    if (path.startsWith(QStringLiteral("content://"))) {
        writePath = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                    + QStringLiteral("/export-") + QString::number(QDateTime::currentMSecsSinceEpoch());
    }
#endif

    // 导出烧录（G-4）：开关开且有标注时，以当前工作图为底逐个调 Core rasterize 合成标注，
    // 再送引擎切割（标注随像素被切开，CONTRIBUTING.md「分层纪律」）；否则直接送工作图。
    core::Image exportSrc = doc_.working();
    if (annoBridge_.burnInEnabled() && annoBridge_.count() > 0) {
        exportSrc = annoBridge_.burnIn(doc_.working());
    }

    if (QString err; EngineBridge::exportResult(exportSrc, cfg, writePath, err)) {
#ifdef Q_OS_ANDROID
        if (writePath != path && !QFile::copy(writePath, path)) {
            notify(QStringLiteral("导出成功但写入所选位置失败，文件在：%1").arg(writePath), true);
            return;
        }
#endif
        notify(QStringLiteral("导出成功：%1").arg(path), false);
    } else {
        notify(QStringLiteral("导出失败：%1").arg(err), true);
    }
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
    status_->setCoord(x, y);
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

// 清除切割线（清除选区）。
void MainWindow::onClearCut() {
    if (!doc_.hasRect()) return;
    doc_.clearRect(); // 触发 changed → refreshPreview 清空叠加层。
    notify(QStringLiteral("已清除选区与切割线"), false);
}

// 切换预览遮罩显隐（视图菜单 / 画布右键）；同步图层面板的遮罩复选框。
void MainWindow::onToggleMasks() const {
    const bool v = !scene_->masksVisible();
    scene_->setMasksVisible(v);
    layerPanel_->setMaskVisible(v);   // 反向同步面板（blockSignals 防回环）。
    preview_->refreshPreview();
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

// 保存配置：把当前作业配置写为 SPEC §7 schema 的 JSON 文件（委托 EngineBridge → Core）。
void MainWindow::onSaveConfig() {
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("保存配置"), QString(),
        QStringLiteral("ImageDiscropper 配置 (*.json);;所有文件 (*)"));
    if (path.isEmpty()) return;
    if (QString err; EngineBridge::saveConfig(path, doc_.buildEngineConfig(), err))
        notify(QStringLiteral("配置已保存：%1").arg(path), false);
    else
        notify(QStringLiteral("保存配置失败：%1").arg(err), true);
}

// 加载配置：读 JSON → DocHistory::recordConfigLoad（抑制采集地应用 + 压栈，载入本身可一步撤销）。
void MainWindow::onLoadConfig() {
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("加载配置"), QString(),
        QStringLiteral("ImageDiscropper 配置 (*.json);;所有文件 (*)"));
    if (path.isEmpty()) return;
    engine::EngineConfig cfg;
    if (QString err; !EngineBridge::loadConfig(path, cfg, err)) { notify(QStringLiteral("加载配置失败：%1").arg(err), true); return; }
    history_->recordConfigLoad(cfg);
    notify(QStringLiteral("配置已加载：%1").arg(path), false);
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
