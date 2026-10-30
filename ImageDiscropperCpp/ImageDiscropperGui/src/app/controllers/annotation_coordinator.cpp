// ============================================================================
// 文件：app/controllers/annotation_coordinator.cpp
// 作用：实现 AnnotationCoordinator（见同名头文件说明）。
// 分块依据：
//   - connectSignals：标注域全部信号连接（模型/画布/工具面板/图层面板/导出面板/属性面板）+ 烧录初始同步；
//   - 手势分派状态机：onAnnoDragStart/Move/End（依工具分派两点/折线/画笔/文字；折线与文字为点击式，落点在释放定）；
//     onAnnoDragCancel 为缩放打断专用（与释放的提交语义互斥，同样按工具分类作废）；
//   - 属性/图层/菜单槽：薄编排，直接转交 AnnotationBridge 与画布/面板。
// ============================================================================
#include "app/controllers/annotation_coordinator.h"

#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>

#include "app/controllers/preview_controller.h"
#include "canvas/canvas_scene.h"
#include "canvas/canvas_view.h"
#include "core/point.h"
#include "panels/annotation_prop_panel.h"
#include "panels/export_panel.h"
#include "panels/layer_panel.h"
#include "panels/mode_tool_panel.h"
#include "i18n/gui_texts.h"
#include "i18n/gui_i18n.h"
#include "util/path_qt_adapter.h"

namespace idc::gui {

AnnotationCoordinator::AnnotationCoordinator(QObject* parent) : QObject(parent) {}

void AnnotationCoordinator::setAnnotationBridge(AnnotationBridge* anno) { anno_ = anno; }
void AnnotationCoordinator::setScene(CanvasScene* scene) { scene_ = scene; }
void AnnotationCoordinator::setView(CanvasView* view) { view_ = view; }
void AnnotationCoordinator::setToolPanel(ModeToolPanel* panel) { toolPanel_ = panel; }
void AnnotationCoordinator::setLayerPanel(LayerPanel* panel) { layerPanel_ = panel; }
void AnnotationCoordinator::setAnnoPropPanel(AnnotationPropPanel* panel) { annoPropPanel_ = panel; }
void AnnotationCoordinator::setExportPanel(ExportPanel* panel) { exportPanel_ = panel; }
void AnnotationCoordinator::setPreviewController(PreviewController* preview) { preview_ = preview; }
void AnnotationCoordinator::setDialogParent(QWidget* parent) { dialogParent_ = parent; }

// ---- 标注（SPEC §8.2 标注绘制与编辑）：模型/画布/面板 → 本控制器编排 ----
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
    connect(view_, &CanvasView::annoDragCancel, this, &AnnotationCoordinator::onAnnoDragCancel);
    connect(view_, &CanvasView::annoHover, this, &AnnotationCoordinator::onAnnoHover);
    connect(view_, &CanvasView::annoFinish, this, &AnnotationCoordinator::onAnnoFinish);
    connect(view_, &CanvasView::annoEscape, this, &AnnotationCoordinator::onAnnoEscape);
    // 框选起带（空白处按下）：若当前有选中标注则清除选中，使其 OBB 交互手柄随框选即时消失
    //（点选/拖动标注由图元接管、不走 marquee，故只在空白起框时清，免无谓 selectionChanged）。
    connect(view_, &CanvasView::marqueeStarted, this, [this] {
        if (anno_ && anno_->selectedIndex()) anno_->clearSelection();
    });
    // 标注选取态空白按下：本态禁框选、无 marqueeStarted 通道 → 专用反选，点空白即取消选中
    //（有选中才清，免空操作触发 selectionChanged 全量重绘）。
    connect(view_, &CanvasView::annoBlankPressed, this, [this] {
        if (anno_ && anno_->selectedIndex()) anno_->clearSelection();
    });
    // 拖动选区边框（移动 / 缩放 / 拖边，含多矩形）：同样清除标注选中，使 OBB 手柄随选区交互即时消失。
    connect(scene_, &CanvasScene::selectionInteractionStarted, this, [this] {
        if (anno_ && anno_->selectedIndex()) anno_->clearSelection();
    });

    connect(scene_, &CanvasScene::annotationSelectRequested, this, &AnnotationCoordinator::onAnnotationSelect);
    connect(scene_, &CanvasScene::annotationClicked, this, &AnnotationCoordinator::onAnnotationClicked);
    connect(scene_, &CanvasScene::annotationMoved, this, &AnnotationCoordinator::onAnnotationMoved);
    connect(scene_, &CanvasScene::annotationTransformed, this, &AnnotationCoordinator::onAnnotationTransformed);
    connect(scene_, &CanvasScene::annotationTransformPreview, this, &AnnotationCoordinator::onAnnotationTransformPreview);

    connect(toolPanel_, &ModeToolPanel::toolSelected, this, &AnnotationCoordinator::onToolSelected);
    connect(layerPanel_, &LayerPanel::baseVisibilityChanged, this, &AnnotationCoordinator::onBaseVisibilityChanged);
    connect(layerPanel_, &LayerPanel::maskVisibilityChanged, this, &AnnotationCoordinator::onMaskVisibilityChanged);
    connect(layerPanel_, &LayerPanel::gridVisibilityChanged, this, &AnnotationCoordinator::onGridVisibilityChanged);
    connect(layerPanel_, &LayerPanel::selectionVisibilityChanged, this, &AnnotationCoordinator::onSelectionVisibilityChanged);
    connect(layerPanel_, &LayerPanel::annotationVisibilityChanged, this, &AnnotationCoordinator::onAnnotationVisibilityChanged);
    connect(layerPanel_, &LayerPanel::numberVisibilityChanged, this,
            [this](const bool v) { if (scene_) scene_->setCellNumberVisible(v); });
    // 「导出时烧录标注」开关位于导出面板（控制导出时是否把标注烧录进像素）。
    connect(exportPanel_, &ExportPanel::burnInChanged, this, &AnnotationCoordinator::onBurnInChanged);
    exportPanel_->setBurnInChecked(anno_->burnInEnabled()); // 初始同步一次（两侧默认 false，对齐意图）。
    connect(annoPropPanel_, &AnnotationPropPanel::colorPicked, this, &AnnotationCoordinator::onAnnoColorPicked);
    connect(annoPropPanel_, &AnnotationPropPanel::fillColorPicked, this, &AnnotationCoordinator::onAnnoFillColorPicked);
    connect(annoPropPanel_, &AnnotationPropPanel::strokeChanged, this, &AnnotationCoordinator::onAnnoStrokeChanged);
    connect(annoPropPanel_, &AnnotationPropPanel::fillChanged, this, &AnnotationCoordinator::onAnnoFillChanged);
    connect(annoPropPanel_, &AnnotationPropPanel::textChanged, this, &AnnotationCoordinator::onAnnoTextChanged);
    connect(annoPropPanel_, &AnnotationPropPanel::fontSizeChanged, this, &AnnotationCoordinator::onAnnoFontSizeChanged);
    connect(annoPropPanel_, &AnnotationPropPanel::cornerRadiusChanged, this, &AnnotationCoordinator::onAnnoCornerRadiusChanged);
    connect(annoPropPanel_, &AnnotationPropPanel::transformApplyRequested, this, &AnnotationCoordinator::onAnnoTransformApply);
    // 叠放次序（位于属性面板）：4 意图信号 → 层序四槽（作于选中项，经 Bridge 调 Core moveAnnotation）。
    connect(annoPropPanel_, &AnnotationPropPanel::stackRaiseRequested, this, &AnnotationCoordinator::onAnnoRaise);
    connect(annoPropPanel_, &AnnotationPropPanel::stackLowerRequested, this, &AnnotationCoordinator::onAnnoLower);
    connect(annoPropPanel_, &AnnotationPropPanel::stackToFrontRequested, this, &AnnotationCoordinator::onAnnoBringToFront);
    connect(annoPropPanel_, &AnnotationPropPanel::stackBackRequested, this, &AnnotationCoordinator::onAnnoSendToBack);
    // 一键清除全部标注（属性面板按钮→与标注菜单同一入口 onAnnoClearAll，弹确认后清空）。
    connect(annoPropPanel_, &AnnotationPropPanel::clearAllRequested, this, &AnnotationCoordinator::onAnnoClearAll);
    // 删除选中标注（属性面板按钮→与标注菜单/Delete 键同一入口 onAnnoDeleteSelected；移动端唯一入口）。
    connect(annoPropPanel_, &AnnotationPropPanel::deleteSelectedRequested, this, &AnnotationCoordinator::onAnnoDeleteSelected);
    // 复制选中标注（属性面板按钮→与标注菜单同一入口 onAnnoCopySelected）。
    connect(annoPropPanel_, &AnnotationPropPanel::copySelectedRequested, this, &AnnotationCoordinator::onAnnoCopySelected);
}

// 工具面板选择：先收笔未完成的折线/画笔路径，再切换工具（setTool 内部会放弃两点预览）。
void AnnotationCoordinator::onToolSelected(const AnnoTool tool) const {
    if (anno_->hasPathDraft()) anno_->commitPath();
    anno_->setTool(tool);
}

// 标注列表/预览变化：增量重绘画布标注层，并同步属性面板回显。
void AnnotationCoordinator::onAnnoBridgeChanged() const {
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

// 工具变化：同步工具面板形状子行 + 属性面板。绘制态门控（是否进入标注绘制）由画布四态
// 模式机承担（CanvasView::canvasMode()==AnnoCreate），本处不据工具切换绘制态。
void AnnotationCoordinator::onAnnoToolChanged() const {
    toolPanel_->syncFromModel();
    annoPropPanel_->syncFromModel();
}

// 绘制手势起点：只有「按住即成形」的工具在按下起笔——画笔起路径草稿（随后逐帧追点）、两点形状记锚点。
// 折线与文字是**点击式**（一次点击＝按下＋释放），落点恒在**释放**才定（见 onAnnoDragEnd）：按下即落会在
// 「按住拖到别处再松手」时于按下点多留一个顶点——拖拽不是点击。
void AnnotationCoordinator::onAnnoDragStart(const QPointF& scenePos) const {
    const core::Point2D p(scenePos.x(), scenePos.y());
    switch (anno_->currentTool()) {
        case AnnoTool::Polyline:
        case AnnoTool::Text:
        case AnnoTool::Select:
            break;   // 点击式：本处不做任何落点副作用
        case AnnoTool::Brush:
            anno_->beginPath(p);
            break;
        default:
            anno_->beginShape(p);
            break;
    }
}

// 绘制手势拖拽（按住左键移动）：两点形状实时更新预览；画笔连续追点；折线仅橡皮筋预览。
// 折线的顶点在释放才落定，故拖拽中与悬停同义：只把「已落顶点 + 到光标连线」画成橡皮筋，不落正式顶点。
void AnnotationCoordinator::onAnnoDragMove(const QPointF& scenePos) const {
    const core::Point2D p(scenePos.x(), scenePos.y());
    switch (anno_->currentTool()) {
        case AnnoTool::Brush:
            anno_->appendPathPoint(p);
            break;
        case AnnoTool::Polyline:
            anno_->previewPolyline(p);   // 按住拖动时也走橡皮筋预览（与悬停一致）。
            break;
        case AnnoTool::Text:
        case AnnoTool::Select:
            break;
        default:
            anno_->updateShape(p);
            break;
    }
}

// 绘制手势释放：点击式工具在此定落点——折线落顶点、文字取文本；两点形状提交；画笔收笔。
// 折线保持草稿（待 Esc / 右键 / 切工具收笔），每次释放只追加一个顶点。
void AnnotationCoordinator::onAnnoDragEnd(const QPointF& scenePos) const {
    const core::Point2D p(scenePos.x(), scenePos.y());
    switch (anno_->currentTool()) {
        case AnnoTool::Text: {
            bool ok = false;
            const QString text = QInputDialog::getText(dialogParent_, IDC_GTR(label, kDlgTitleAnnoText),
                IDC_GTR(label, kDlgLabelAnnoText), QLineEdit::Normal,
                QString::fromStdString(anno_->currentText()), &ok);
            if (ok && !text.isEmpty()) anno_->addText(p, text.toStdString());
            break;
        }
        case AnnoTool::Polyline:
            if (anno_->hasPathDraft()) anno_->appendPathPoint(p);
            else anno_->beginPath(p);
            break;
        case AnnoTool::Brush:
            anno_->commitPath();
            break;
        case AnnoTool::Select:
            break;
        default:
            anno_->commitShape();
            break;
    }
}

// 缩放（捏合/滚轮）打断进行中的这一笔：分类作废，绝不当提交处理。
//   两点形状：半截预览的尺寸已被新缩放乘过、不可信 → 弃掉（此时 pathDraft_ 恒空，cancelPending 不误伤）。
//   画笔：追到的点迹不可复现 → 收笔提交（与 Esc/右键「有草稿即收笔」同取向）。
//   折线：顶点在释放才落定，按住期间被打断时本笔尚无落点副作用 → 什么都不做即自然作废；此前的
//         已落顶点保留可继续加。橡皮筋临时段在桌面由下一次悬停重建，触控无悬停、下一次落顶点自然
//         覆盖（不会留下错形）。
//   文字/选取：无按下副作用（对话框在释放才弹）。
void AnnotationCoordinator::onAnnoDragCancel() const {
    switch (anno_->currentTool()) {
        case AnnoTool::Brush:
            if (anno_->hasPathDraft()) anno_->commitPath();
            else anno_->cancelPending();
            break;
        case AnnoTool::Polyline:
        case AnnoTool::Text:
        case AnnoTool::Select:
            break;
        default:
            anno_->cancelPending();
            break;
    }
}

// 绘制态 Esc：有折线/画笔草稿则收笔提交，否则取消当前两点预览。
void AnnotationCoordinator::onAnnoEscape() const {
    if (anno_->hasPathDraft()) anno_->commitPath();
    else anno_->cancelPending();
}

// 绘制态悬停（未按键移动）：折线实时预览「已落顶点 + 到光标连线」橡皮筋（不落顶点）。
// 其余工具无悬停语义（两点形状靠拖拽预览、画笔靠按住追点），故忽略。
void AnnotationCoordinator::onAnnoHover(const QPointF& scenePos) const {
    if (anno_->currentTool() != AnnoTool::Polyline) return;
    anno_->previewPolyline(core::Point2D(scenePos.x(), scenePos.y()));
}

// 绘制态右键：退出当前绘制手势——有折线/画笔草稿则收笔提交（折线在此结束），
// 否则取消当前预览（与 Esc 同义，满足「右键退出多线段」的交互约定）。
void AnnotationCoordinator::onAnnoFinish() const {
    if (anno_->hasPathDraft()) anno_->commitPath();
    else anno_->cancelPending();
}

// Select 工具下点中标注图元：委托 Core hitTest 选中（几何命中在 Core，CONTRIBUTING.md「分层纪律」）。
// 选中后随即由场景对**选中项**发起平移拖拽（重叠时被按项是最顶图元，拖动须跟随选中项）。
void AnnotationCoordinator::onAnnotationSelect(const QPointF& scenePos) const {
    anno_->selectAt(core::Point2D(scenePos.x(), scenePos.y()));
    if (const std::optional<std::size_t> sel = anno_->selectedIndex())
        scene_->beginAnnotationDrag(static_cast<int>(*sel), scenePos);
}

// Select 工具下原地点击某标注图元：推进同点连击循环（重叠标注轮流选中）。
void AnnotationCoordinator::onAnnotationClicked(const QPointF& scenePos) const {
    anno_->cycleAt(core::Point2D(scenePos.x(), scenePos.y()));
}

// 拖动选中标注：委托 Core 平移其几何（下标须与当前选中项一致，防御误触）。
void AnnotationCoordinator::onAnnotationMoved(const int index, const double dx, const double dy) const {
    if (const std::optional<std::size_t> sel = anno_->selectedIndex(); !sel || static_cast<int>(*sel) != index) return;
    anno_->moveSelectedBy(dx, dy);
}

// 拖定向包围盒手柄（释放提交）：委托 Core 对选中标注施加缩放/旋转（与属性面板变换同一入口，下标防御误触）。
void AnnotationCoordinator::onAnnotationTransformed(const int index, const double sx, const double sy,
                                                    const double rotateDeg) const {
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

// 属性面板 → 模型（Edit 作用选中项，Draw 改当前默认；均触发 changed→重绘）。
void AnnotationCoordinator::onAnnoColorPicked(const QColor& c) const { anno_->setColor(toCoreColor(c)); }
void AnnotationCoordinator::onAnnoFillColorPicked(const QColor& c) const { anno_->setFillColor(toCoreColor(c)); }
void AnnotationCoordinator::onAnnoStrokeChanged(const int width) const { anno_->setStrokeWidth(width); }
void AnnotationCoordinator::onAnnoFillChanged(const bool fill) const { anno_->setFill(fill); }
void AnnotationCoordinator::onAnnoTextChanged(const QString& text) const { anno_->setText(text.toStdString()); }
void AnnotationCoordinator::onAnnoFontSizeChanged(const double size) const { anno_->setFontSize(size); }
void AnnotationCoordinator::onAnnoCornerRadiusChanged(const double radius) const { anno_->setCornerRadius(radius); }
// 属性面板「变换」：将缩放/旋转写回模型（委托 Core Shape 的非破坏性矩阵，保留类型与字形；无选中时模型内部忽略）。
void AnnotationCoordinator::onAnnoTransformApply(const double sx, const double sy, const double rotateDeg) const {
    anno_->transformSelected(sx, sy, rotateDeg);
}

// 图层面板 → 画布/模型（底图仅切画布可见；标注同时同步模型标志与画布图元）。
void AnnotationCoordinator::onBaseVisibilityChanged(const bool visible) const { scene_->setBaseVisible(visible); }
void AnnotationCoordinator::onMaskVisibilityChanged(const bool visible) const { scene_->setMasksVisible(visible); }
// 网格线/切割线共用同一开关：L3 为灰色网格线，L1/L2 为贯穿切割线（延伸段 + 多矩形诱导线）。
void AnnotationCoordinator::onGridVisibilityChanged(const bool visible) const {
    scene_->setGridLinesVisible(visible);
    preview_->refreshPreview();   // L3 灰色网格 / L2 多矩形诱导线依门控重建。
}
void AnnotationCoordinator::onSelectionVisibilityChanged(const bool visible) const {
    scene_->setSelectionVisible(visible);
    preview_->refreshPreview();   // L3 单元选择高亮（CellPickerItem）依 selectionVisible_ 门控重建。
}
void AnnotationCoordinator::onAnnotationVisibilityChanged(const bool visible) const {
    anno_->setLayerVisible(visible);
    scene_->setAnnotationsVisible(visible);
}
void AnnotationCoordinator::onBurnInChanged(const bool on) const {
    anno_->setBurnIn(on);
    preview_->refreshExportPreviewFromCache();   // B：烧录开关只影响输出预览（叠加/去除标注），用缓存重渲染、不重跑 Core。
}

// 标注菜单动作：撤销/重做/删除选中/清除全部（均复用 AnnotationEditor 内建会话态）。
void AnnotationCoordinator::onAnnoUndo() const { anno_->undo(); }
void AnnotationCoordinator::onAnnoRedo() const { anno_->redo(); }
void AnnotationCoordinator::onAnnoDeleteSelected() const {
    if (!anno_->selectedIndex()) return;
    anno_->removeSelected();
}
void AnnotationCoordinator::onAnnoCopySelected() const {
    if (!anno_->selectedIndex()) return;
    anno_->copySelected();
}
void AnnotationCoordinator::onAnnoClearAll() const {
    if (anno_->count() == 0) return;
    const QMessageBox::StandardButton ret = QMessageBox::question(
        dialogParent_, IDC_GTR(msg, kClearAnnosTitle),
        IDC_GTR(msg, kClearAnnosAsk)
            .arg(anno_->count()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret != QMessageBox::Yes) return;
    anno_->clearAll();
}

// 标注层序（叠放）：均作用于选中项，委托 AnnotationBridge（内部调 AnnotationEditor::moveAnnotation）。
// 无选中时桥内自行忽略；变化经 changed()/selectionChanged() → 重绘标注层 + 必要时刷新输出预览。
void AnnotationCoordinator::onAnnoRaise() const { anno_->raiseSelected(); }
void AnnotationCoordinator::onAnnoLower() const { anno_->lowerSelected(); }
void AnnotationCoordinator::onAnnoBringToFront() const { anno_->bringSelectedToFront(); }
void AnnotationCoordinator::onAnnoSendToBack() const { anno_->sendSelectedToBack(); }

} // namespace idc::gui
