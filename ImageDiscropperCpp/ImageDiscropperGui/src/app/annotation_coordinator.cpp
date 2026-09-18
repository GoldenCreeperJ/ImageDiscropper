// ============================================================================
// 文件：app/annotation_coordinator.cpp
// 作用：实现 AnnotationCoordinator（见同名头文件说明）。
// 分块依据：
//   - connectSignals：标注域全部信号连接（模型/画布/工具面板/图层面板/导出面板/属性面板）+ 烧录初始同步；
//   - 手势分派状态机：onAnnoDragStart/Move/End（依工具分派两点/折线/画笔/文字）；
//   - 属性/图层/菜单槽：薄编排，直接转交 AnnotationBridge 与画布/面板。
// ============================================================================
#include "app/annotation_coordinator.h"

#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>

#include "app/preview_controller.h"
#include "canvas/canvas_scene.h"
#include "canvas/canvas_view.h"
#include "core/point.h"
#include "panels/annotation_prop_panel.h"
#include "panels/export_panel.h"
#include "panels/layer_panel.h"
#include "panels/tool_panel.h"
#include "util/path_qt_adapter.h"

namespace idc::gui {

AnnotationCoordinator::AnnotationCoordinator(QObject* parent) : QObject(parent) {}

void AnnotationCoordinator::setAnnotationBridge(AnnotationBridge* anno) { anno_ = anno; }
void AnnotationCoordinator::setScene(CanvasScene* scene) { scene_ = scene; }
void AnnotationCoordinator::setView(CanvasView* view) { view_ = view; }
void AnnotationCoordinator::setToolPanel(ToolPanel* panel) { toolPanel_ = panel; }
void AnnotationCoordinator::setLayerPanel(LayerPanel* panel) { layerPanel_ = panel; }
void AnnotationCoordinator::setAnnoPropPanel(AnnotationPropPanel* panel) { annoPropPanel_ = panel; }
void AnnotationCoordinator::setExportPanel(ExportPanel* panel) { exportPanel_ = panel; }
void AnnotationCoordinator::setPreviewController(PreviewController* preview) { preview_ = preview; }
void AnnotationCoordinator::setDialogParent(QWidget* parent) { dialogParent_ = parent; }

// ---- 标注（G-4/G-5）：模型/画布/面板 → 本控制器编排 ----
void AnnotationCoordinator::connectSignals() {
    connect(anno_, &AnnotationBridge::changed, this, &AnnotationCoordinator::onAnnoBridgeChanged);
    // 绘制拖拽预览（橡皮筋）：begin/updateShape 仅发 pendingChanged，必须连到实时刷新预览图元，
    // 否则拖拽过程中形状不显示、只有松手提交（changed）后才可见。
    connect(anno_, &AnnotationBridge::pendingChanged, this, &AnnotationCoordinator::onAnnoPendingChanged);
    connect(anno_, &AnnotationBridge::selectionChanged, this, &AnnotationCoordinator::onAnnoSelectionChanged);
    connect(anno_, &AnnotationBridge::toolChanged, this, &AnnotationCoordinator::onAnnoToolChanged);

    connect(view_, &CanvasView::annoDragStart, this, &AnnotationCoordinator::onAnnoDragStart);
    connect(view_, &CanvasView::annoDragMove, this, &AnnotationCoordinator::onAnnoDragMove);
    connect(view_, &CanvasView::annoDragEnd, this, &AnnotationCoordinator::onAnnoDragEnd);
    connect(view_, &CanvasView::annoHover, this, &AnnotationCoordinator::onAnnoHover);
    connect(view_, &CanvasView::annoFinish, this, &AnnotationCoordinator::onAnnoFinish);
    connect(view_, &CanvasView::annoEscape, this, &AnnotationCoordinator::onAnnoEscape);

    connect(scene_, &CanvasScene::annotationSelectRequested, this, &AnnotationCoordinator::onAnnotationSelect);
    connect(scene_, &CanvasScene::annotationMoved, this, &AnnotationCoordinator::onAnnotationMoved);
    connect(scene_, &CanvasScene::annotationTransformed, this, &AnnotationCoordinator::onAnnotationTransformed);
    connect(scene_, &CanvasScene::annotationTransformPreview, this, &AnnotationCoordinator::onAnnotationTransformPreview);

    connect(toolPanel_, &ToolPanel::toolSelected, this, &AnnotationCoordinator::onToolSelected);
    connect(layerPanel_, &LayerPanel::baseVisibilityChanged, this, &AnnotationCoordinator::onBaseVisibilityChanged);
    connect(layerPanel_, &LayerPanel::maskVisibilityChanged, this, &AnnotationCoordinator::onMaskVisibilityChanged);
    connect(layerPanel_, &LayerPanel::gridVisibilityChanged, this, &AnnotationCoordinator::onGridVisibilityChanged);
    connect(layerPanel_, &LayerPanel::cutLineVisibilityChanged, this, &AnnotationCoordinator::onCutLineVisibilityChanged);
    connect(layerPanel_, &LayerPanel::selectionVisibilityChanged, this, &AnnotationCoordinator::onSelectionVisibilityChanged);
    connect(layerPanel_, &LayerPanel::annotationVisibilityChanged, this, &AnnotationCoordinator::onAnnotationVisibilityChanged);
    connect(layerPanel_, &LayerPanel::numberVisibilityChanged, this,
            [this](const bool v) { if (scene_) scene_->setCellNumberVisible(v); });
    // 「导出时烧录标注」开关位于导出面板（控制导出时是否把标注烧录进像素）。
    connect(exportPanel_, &ExportPanel::burnInChanged, this, &AnnotationCoordinator::onBurnInChanged);
    exportPanel_->setBurnInChecked(anno_->burnInEnabled()); // 初始同步一次（两侧默认 false，对齐意图）。
    connect(annoPropPanel_, &AnnotationPropPanel::colorPicked, this, &AnnotationCoordinator::onAnnoColorPicked);
    connect(annoPropPanel_, &AnnotationPropPanel::strokeChanged, this, &AnnotationCoordinator::onAnnoStrokeChanged);
    connect(annoPropPanel_, &AnnotationPropPanel::fillChanged, this, &AnnotationCoordinator::onAnnoFillChanged);
    connect(annoPropPanel_, &AnnotationPropPanel::textChanged, this, &AnnotationCoordinator::onAnnoTextChanged);
    connect(annoPropPanel_, &AnnotationPropPanel::fontSizeChanged, this, &AnnotationCoordinator::onAnnoFontSizeChanged);
    connect(annoPropPanel_, &AnnotationPropPanel::transformApplyRequested, this, &AnnotationCoordinator::onAnnoTransformApply);
}

// 工具面板选择：先收笔未完成的折线/画笔路径，再切换工具（setTool 内部会放弃两点预览）。
void AnnotationCoordinator::onToolSelected(const AnnoTool tool) {
    if (anno_->hasPathDraft()) anno_->commitPath();
    anno_->setTool(tool);
}

// 标注列表/预览变化：增量重绘画布标注层，并同步属性面板回显。
void AnnotationCoordinator::onAnnoBridgeChanged() {
    scene_->updateAnnotations(*anno_);
    annoPropPanel_->syncFromModel();
    // B：若「导出时烧录标注」开启，标注变化只需反映到输出预览——用缓存重渲染、不重跑 Core（切割几何不受标注影响）。
    if (anno_->burnInEnabled()) preview_->refreshExportPreviewFromCache();
}

// 绘制拖拽预览（橡皮筋）变化：仅刷新预览图元（不重建已提交标注，避免逐帧开销），实现“绘制即实时成形”。
void AnnotationCoordinator::onAnnoPendingChanged() const {
    scene_->updatePendingAnnotation(*anno_);
}

// 选中项变化：重绘高亮（updateAnnotations 内含选中态）并同步属性面板。
void AnnotationCoordinator::onAnnoSelectionChanged() const {
    scene_->updateAnnotations(*anno_);
    annoPropPanel_->syncFromModel();
}

// 工具变化：同步工具面板按钮组，并按「是否 SELECT」切换画布绘制态门控。
void AnnotationCoordinator::onAnnoToolChanged() const {
    toolPanel_->syncFromModel();
    view_->setAnnotationDrawActive(anno_->currentTool() != AnnoTool::SELECT);
    annoPropPanel_->syncFromModel();
}

// 绘制手势起点：依当前工具分派——文字落点取文本；折线/画笔起笔；其余两点形状记起点。
void AnnotationCoordinator::onAnnoDragStart(const QPointF& scenePos) {
    const core::Point2D p(scenePos.x(), scenePos.y());
    switch (anno_->currentTool()) {
        case AnnoTool::TEXT: {
            bool ok = false;
            const QString text = QInputDialog::getText(dialogParent_, QStringLiteral("文字标注"),
                QStringLiteral("请输入标注文字："), QLineEdit::Normal,
                QString::fromStdString(anno_->currentText()), &ok);
            if (ok && !text.isEmpty()) anno_->addText(p, text.toStdString());
            break;
        }
        case AnnoTool::POLYLINE:
            if (anno_->hasPathDraft()) anno_->appendPathPoint(p);
            else anno_->beginPath(p);
            break;
        case AnnoTool::BRUSH:
            anno_->beginPath(p);
            break;
        default:
            anno_->beginShape(p);
            break;
    }
}

// 绘制手势拖拽（按住左键移动）：两点形状实时更新预览；画笔追加顶点；折线仅橡皮筋预览。
// 折线为点击式：顶点已在 onAnnoDragStart（按下）落定，故拖拽中只预览、不再追加正式顶点。
void AnnotationCoordinator::onAnnoDragMove(const QPointF& scenePos) {
    const core::Point2D p(scenePos.x(), scenePos.y());
    switch (anno_->currentTool()) {
        case AnnoTool::BRUSH:
            anno_->appendPathPoint(p);
            break;
        case AnnoTool::POLYLINE:
            anno_->previewPolyline(p);   // 按住拖动时也走橡皮筋预览（与悬停一致）。
            break;
        case AnnoTool::TEXT:
        case AnnoTool::SELECT:
            break;
        default:
            anno_->updateShape(p);
            break;
    }
}

// 绘制手势释放：两点形状提交；画笔收笔；折线保持草稿（待 Esc/切换工具收笔）。
void AnnotationCoordinator::onAnnoDragEnd(const QPointF& scenePos) {
    Q_UNUSED(scenePos);
    switch (anno_->currentTool()) {
        case AnnoTool::BRUSH:
            anno_->commitPath();
            break;
        case AnnoTool::POLYLINE:
        case AnnoTool::TEXT:
        case AnnoTool::SELECT:
            break;
        default:
            anno_->commitShape();
            break;
    }
}

// 绘制态 Esc：有折线/画笔草稿则收笔提交，否则取消当前两点预览。
void AnnotationCoordinator::onAnnoEscape() {
    if (anno_->hasPathDraft()) anno_->commitPath();
    else anno_->cancelPending();
}

// 绘制态悬停（未按键移动）：折线实时预览「已落顶点 + 到光标连线」橡皮筋（不落顶点）。
// 其余工具无悬停语义（两点形状靠拖拽预览、画笔靠按住追点），故忽略。
void AnnotationCoordinator::onAnnoHover(const QPointF& scenePos) {
    if (anno_->currentTool() != AnnoTool::POLYLINE) return;
    anno_->previewPolyline(core::Point2D(scenePos.x(), scenePos.y()));
}

// 绘制态右键：退出当前绘制手势——有折线/画笔草稿则收笔提交（折线在此结束），
// 否则取消当前预览（与 Esc 同义，满足「右键退出多线段」的交互约定）。
void AnnotationCoordinator::onAnnoFinish() {
    if (anno_->hasPathDraft()) anno_->commitPath();
    else anno_->cancelPending();
}

// SELECT 工具下点中标注图元：委托 Core hitTest 选中（几何命中在 Core，CONTRIBUTING.md「分层纪律」）。
void AnnotationCoordinator::onAnnotationSelect(const QPointF& scenePos) {
    anno_->selectAt(core::Point2D(scenePos.x(), scenePos.y()));
}

// 拖动选中标注：委托 Core 平移其几何（下标须与当前选中项一致，防御误触）。
void AnnotationCoordinator::onAnnotationMoved(const int index, const double dx, const double dy) {
    if (const std::optional<std::size_t> sel = anno_->selectedIndex(); !sel || static_cast<int>(*sel) != index) return;
    anno_->moveSelectedBy(dx, dy);
}

// 拖定向包围盒手柄（释放提交）：委托 Core 对选中标注施加缩放/旋转（与属性面板变换同一入口，下标防御误触）。
void AnnotationCoordinator::onAnnotationTransformed(const int index, const double sx, const double sy,
                                                    const double rotateDeg) {
    if (const std::optional<std::size_t> sel = anno_->selectedIndex(); !sel || static_cast<int>(*sel) != index) return;
    anno_->transformSelected(sx, sy, rotateDeg);
    // 提交后模型发 changed → onAnnoBridgeChanged → syncFromModel 依最新累积值回显面板（忠实反映底层，不回弹）。
}

// 手柄拖拽**进行中**：把逐帧**绝对**预览值实时回显到属性面板变换区（仅回显，不写模型；下标防御误触）。
void AnnotationCoordinator::onAnnotationTransformPreview(const int index, const double sx, const double sy,
                                                         const double rotateDeg) const {
    if (const std::optional<std::size_t> sel = anno_->selectedIndex(); !sel || static_cast<int>(*sel) != index) return;
    if (annoPropPanel_) annoPropPanel_->setTransformPreview(sx, sy, rotateDeg);
}

// 属性面板 → 模型（EDIT 作用选中项，DRAW 改当前默认；均触发 changed→重绘）。
void AnnotationCoordinator::onAnnoColorPicked(const QColor& c) { anno_->setColor(toCoreColor(c)); }
void AnnotationCoordinator::onAnnoStrokeChanged(const int width) { anno_->setStrokeWidth(width); }
void AnnotationCoordinator::onAnnoFillChanged(const bool fill) { anno_->setFill(fill); }
void AnnotationCoordinator::onAnnoTextChanged(const QString& text) { anno_->setText(text.toStdString()); }
void AnnotationCoordinator::onAnnoFontSizeChanged(const double size) { anno_->setFontSize(size); }
// 属性面板「变换」：将缩放/旋转写回模型（委托 Core Shape 的非破坏性矩阵，保留类型与字形；无选中时模型内部忽略）。
void AnnotationCoordinator::onAnnoTransformApply(const double sx, const double sy, const double rotateDeg) {
    anno_->transformSelected(sx, sy, rotateDeg);
}

// 图层面板 → 画布/模型（底图仅切画布可见；标注同时同步模型标志与画布图元）。
void AnnotationCoordinator::onBaseVisibilityChanged(const bool visible) const { scene_->setBaseVisible(visible); }
void AnnotationCoordinator::onMaskVisibilityChanged(const bool visible) const { scene_->setMasksVisible(visible); }
void AnnotationCoordinator::onGridVisibilityChanged(const bool visible) {
    scene_->setGridVisible(visible);
    preview_->refreshPreview();   // 网格线依 gridVisible_ 门控重建（L3 / L2 多矩形）。
}
void AnnotationCoordinator::onCutLineVisibilityChanged(const bool visible) {
    scene_->setCutLinesVisible(visible);
    preview_->refreshPreview();   // L2 多矩形诱导线依 cutLinesVisible_ 门控重建（橙色切割线）。
}
void AnnotationCoordinator::onSelectionVisibilityChanged(const bool visible) {
    scene_->setSelectionVisible(visible);
    preview_->refreshPreview();   // L3 单元选择高亮（CellPickerItem）依 selectionVisible_ 门控重建。
}
void AnnotationCoordinator::onAnnotationVisibilityChanged(const bool visible) {
    anno_->setLayerVisible(visible);
    scene_->setAnnotationsVisible(visible);
}
void AnnotationCoordinator::onBurnInChanged(const bool on) {
    anno_->setBurnIn(on);
    preview_->refreshExportPreviewFromCache();   // B：烧录开关只影响输出预览（叠加/去除标注），用缓存重渲染、不重跑 Core。
}

// 标注菜单动作：撤销/重做/删除选中/清除全部（均复用 Core AnnotationLayer）。
void AnnotationCoordinator::onAnnoUndo() { anno_->undo(); }
void AnnotationCoordinator::onAnnoRedo() { anno_->redo(); }
void AnnotationCoordinator::onAnnoDeleteSelected() {
    if (!anno_->selectedIndex()) return;
    anno_->removeSelected();
}
void AnnotationCoordinator::onAnnoClearAll() {
    if (anno_->count() == 0) return;
    const QMessageBox::StandardButton ret = QMessageBox::question(
        dialogParent_, QStringLiteral("清除全部标注"),
        QStringLiteral("确定要清除全部 %1 个标注吗？此操作可用「撤销标注」回退。")
            .arg(anno_->count()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret != QMessageBox::Yes) return;
    anno_->clearAll();
}

} // namespace idc::gui
