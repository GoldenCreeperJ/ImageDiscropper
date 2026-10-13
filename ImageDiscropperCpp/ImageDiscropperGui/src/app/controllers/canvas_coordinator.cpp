// ============================================================================
// 文件：app/canvas_coordinator.cpp
// 作用：实现 CanvasCoordinator 的画布四态落地与交互写回编排（见同名头文件说明）。
//       全部槽/连接自 MainWindow 原封迁出：意图 → Document（或场景高亮），
//       预览刷新仍由 PreviewController 经 Document 信号承担。
// ============================================================================
#include "app/controllers/canvas_coordinator.h"

#include <algorithm>
#include <cmath>

#include "app/status_bar.h"
#include "canvas/canvas_scene.h"
#include "canvas/canvas_view.h"
#include "engine/engine.h"
#include "model/annotation_bridge.h"
#include "model/document.h"
#include "panels/param_panel.h"
#include "panels/tool_panel.h"
#include "i18n/gui_texts.h"
#include "i18n/gui_i18n.h"

namespace idc::gui {

CanvasCoordinator::CanvasCoordinator(QObject* parent) : QObject(parent) {}

void CanvasCoordinator::setDocument(Document* doc) { doc_ = doc; }
void CanvasCoordinator::setAnnotationBridge(AnnotationBridge* anno) { anno_ = anno; }
void CanvasCoordinator::setScene(CanvasScene* scene) { scene_ = scene; }
void CanvasCoordinator::setView(CanvasView* view) { view_ = view; }
void CanvasCoordinator::setParamPanel(ParamPanel* param) { param_ = param; }
void CanvasCoordinator::setStatusBar(StatusBar* status) { status_ = status; }
void CanvasCoordinator::setToolPanel(ToolPanel* toolPanel) { toolPanel_ = toolPanel; }

// 连接画布/场景/参数面板/工具面板意图信号（自 MainWindow::connectAll 迁出；其余连接住在属主旁）。
void CanvasCoordinator::connectSignals() {
    connect(view_, &CanvasView::rubberSelect, this, &CanvasCoordinator::onRubberSelect);
    connect(view_, &CanvasView::nudgeSelection, this, &CanvasCoordinator::onNudge);
    connect(view_, &CanvasView::clearCutRequested, this, &CanvasCoordinator::onClearCut);
    connect(scene_, &CanvasScene::selectionEdited, this, &CanvasCoordinator::onSelectionEdited);
    connect(scene_, &CanvasScene::multiRectEdited, this, &CanvasCoordinator::onMultiRectEdited);
    connect(scene_, &CanvasScene::cellToggled, this, &CanvasCoordinator::onCellToggled);
    connect(scene_, &CanvasScene::cellsMarqueeSelected, this, &CanvasCoordinator::onCellsMarquee);
    connect(scene_, &CanvasScene::cellReordered, this, &CanvasCoordinator::onCellReordered);
    connect(param_, &ParamPanel::rectSelected, this, &CanvasCoordinator::onRectSelected);
    // 画布四态互斥模式（「交互模式」面板顶部按钮）→ 落地：切视图模式 + 场景图元可交互性 + 取消前态选中。
    connect(toolPanel_, &ToolPanel::modeChanged, this, &CanvasCoordinator::applyCanvasMode);
    // 画布上拾取图片图层（转发自 ImageLayerItem）：按下选中该层（预处理目标切到该层）并刷新当前态提示。
    connect(scene_, &CanvasScene::imageLayerPressed, this, [this](const int id) {
        doc_->selectLayer(id);
        updateInteractionHint();
    });
    // 图片拾取态点画布内非图层处 → 选中底图（预处理目标切 Base）；点画布外 → 取消全部选中（回画布目标）。
    connect(scene_, &CanvasScene::basePickRequested, this, [this] {
        doc_->selectBase(true);   // 选底图与选层互斥（selectBase 内部已清 selectedLayerId_）
        updateInteractionHint();
    });
    connect(scene_, &CanvasScene::pickDeselectRequested, this, [this] {
        doc_->selectLayer(std::nullopt);
        doc_->selectBase(false);
        if (anno_->selectedIndex()) anno_->clearSelection();
        updateInteractionHint();
    });
    connect(scene_, &CanvasScene::imageLayerMoved, this, [this](const int id, const int dx, const int dy) {
        if (const canvas::ImageLayer* layer = doc_->layerById(id)) {
            canvas::ImageLayer updated = *layer;
            // 拖拽增量来自场景（工作图）坐标系，而 transform.x/y 是画布坐标系声明态：
            //   需经画布级有效管线的逆映射（mapDeltaToInput）换回画布增量再累加，
            //   否则画布级 rotate/flip/resize 后拖动方向错乱（与选框映射同口径，空管线→恒等）。
            const auto canvasGeom = PreprocessSession::makeEffective(doc_->resizeTarget(), doc_->steps());
            double cdx = 0.0;
            double cdy = 0.0;
            canvasGeom.mapDeltaToInput(dx, dy, doc_->canvasWidth(), doc_->canvasHeight(), cdx, cdy);
            updated.transform.x += static_cast<int>(std::llround(cdx));
            updated.transform.y += static_cast<int>(std::llround(cdy));
            doc_->updateLayer(id, updated);   // → layersChanged → recomposeLayerCanvas → working_ → 落位框回灌。
        }
    });
}

// 画布四态互斥模式落地（计划 Phase 4）：任一时刻只处一态——
//   · 切视图模式（CanvasView 据此路由左键手势）+ 场景图元可交互性（拾取框 / 标注图元）；
//   · 进入一态即取消前态选中：标注相关态清标注选中、非图片相关态清选中层（回到「画布」预处理目标）。
// 菜单勾选态回同步由 MainWindow::connectAll 的 modeChanged 第二条连接承担（MainWindowUi::syncCanvasModeActions）。
void CanvasCoordinator::applyCanvasMode(const CanvasMode mode) const {
    view_->setCanvasMode(mode);
    scene_->setImagePickMode(mode == CanvasMode::ImagePick);
    scene_->setAnnotationSelectMode(mode == CanvasMode::AnnoSelect);
    switch (mode) {
        case CanvasMode::Marquee:
            if (doc_->selectedLayerId()) doc_->selectLayer(std::nullopt);      // 图片→画布
            if (doc_->baseSelected()) doc_->selectBase(false);                 // 底图→画布
            if (anno_->selectedIndex()) anno_->clearSelection();               // 清标注选中
            break;
        case CanvasMode::ImagePick:
            if (anno_->selectedIndex()) anno_->clearSelection();               // 离开标注态清标注选中
            break;
        case CanvasMode::AnnoSelect:
            if (doc_->selectedLayerId()) doc_->selectLayer(std::nullopt);      // 关键：选中图片后可切回画布
            if (doc_->baseSelected()) doc_->selectBase(false);                 // 离开图片拾取态：底图→画布
            // 选取态必须把标注工具切到 SELECT（→ AnnotationEditor EDIT 模式）：属性写回 change* 仅在 EDIT
            // 作用选中项，否则从绘制态（DRAW）切回时，选中标注能显示属性却改不动（颜色/填充/粗细/圆角落回默认）。
            if (anno_->currentTool() != AnnoTool::SELECT) anno_->setTool(AnnoTool::SELECT);
            break;
        case CanvasMode::AnnoCreate:
            if (doc_->selectedLayerId()) doc_->selectLayer(std::nullopt);
            if (doc_->baseSelected()) doc_->selectBase(false);                 // 离开图片拾取态：底图→画布
            if (anno_->selectedIndex()) anno_->clearSelection();
            if (anno_->currentTool() == AnnoTool::SELECT) anno_->setTool(AnnoTool::RECT);  // 绘制态需一个形状
            break;
    }
    updateInteractionHint();
}

// 状态栏提示：显式回报当前交互模式与当前预处理目标（画布 / 某图片图层）。
void CanvasCoordinator::updateInteractionHint() const {
    if (!status_) return;
    QString modeName;
    switch (view_->canvasMode()) {
        case CanvasMode::Marquee:    modeName = IDC_GTR(menu, kCanvasMarquee); break;
        case CanvasMode::ImagePick:  modeName = IDC_GTR(menu, kCanvasImagePick); break;
        case CanvasMode::AnnoSelect: modeName = IDC_GTR(menu, kCanvasAnnoSelect); break;
        case CanvasMode::AnnoCreate: modeName = IDC_GTR(menu, kCanvasAnnoCreate); break;
    }
    QString target;
    switch (doc_->preprocessTarget()) {
        case PreprocessTarget::Layer:  target = IDC_GTR(label, kImageLayerName); break;
        case PreprocessTarget::Base:   target = IDC_GTR(menu, kShowBase); break;
        case PreprocessTarget::Canvas: target = IDC_GTR(label, kTargetCanvasName); break;
    }
    status_->setHint(IDC_GTR(status, kModeTargetHintFmt).arg(modeName, target));
}

// 框选新建选区：把场景矩形钳制到图像内并写回 Document。
void CanvasCoordinator::onRubberSelect(const QRectF& sceneRect) const {
    if (!doc_->hasImage()) return;
    // L3 网格分割不用矩形选区：框选（含从图像外起拖、未被 CellPickerItem grab 而落到视图橡皮筋的情形）
    // → 把命中的单元并入选择集（与 picker 自身框选发出的 cellsMarqueeSelected 走同一 addCells 路径）。
    if (doc_->mode() == engine::Tier::L3) {
        if (const std::vector<int> idx = scene_->cellsIntersecting(sceneRect); !idx.empty()) doc_->addCells(idx);
        return;
    }
    const qreal W = doc_->width(), H = doc_->height();
    const qreal l = std::clamp(sceneRect.left(), 0.0, W);
    const qreal t = std::clamp(sceneRect.top(), 0.0, H);
    const qreal r = std::clamp(sceneRect.right(), 0.0, W);
    const qreal b = std::clamp(sceneRect.bottom(), 0.0, H);
    const engine::RectRegion rr(qRound(l), qRound(t), qRound(r), qRound(b));
    if (rr.width() <= 0 || rr.height() <= 0) return; // 忽略退化选区。
    // L2 多矩形并集：框选逐个追加矩形（而非替换单选区）。
    if (doc_->mode() == engine::Tier::L2 && doc_->l2Sub() == L2Sub::MULTI_RECT) {
        doc_->addRect(rr);
        return;
    }
    doc_->setRect(rr);
}

// 拖动/缩放既有选区：同框选处理（选区框已做钳制与吸附）。
void CanvasCoordinator::onSelectionEdited(const QRectF& sceneRect) {
    onRubberSelect(sceneRect);
}

// L2 多矩形：拖动/缩放第 index 个选区框 → 写回 Document 对应矩形（选区框已钳制到图像内）。
void CanvasCoordinator::onMultiRectEdited(const int index, const QRectF& sceneRect) const {
    if (index < 0 || !doc_->hasImage()) return;
    if (doc_->mode() != engine::Tier::L2 || doc_->l2Sub() != L2Sub::MULTI_RECT) return;
    const engine::RectRegion rr(qRound(sceneRect.left()), qRound(sceneRect.top()),
                                     qRound(sceneRect.right()), qRound(sceneRect.bottom()));
    doc_->updateRect(static_cast<std::size_t>(index), rr);
    // 拖拽期间 syncPanels 被跳过（避免逐帧重建），故在此定向刷新：
    // 让列表跟随选中被拖矩形（并高亮），并实时回显其新尺寸。
    param_->selectRectRow(index);
    param_->updateRectListItem(index);
}

// L2 多矩形：面板列表选中行变化 → 高亮画布上对应选区框（-1 清除高亮）。
void CanvasCoordinator::onRectSelected(const int index) const {
    if (scene_) scene_->setActiveMultiRect(index);
}

// L3 单击切换某单元：写回 Document（触发刷新与点选图元高亮更新）。
void CanvasCoordinator::onCellToggled(const int index) const {
    if (!doc_->hasImage() || doc_->mode() != engine::Tier::L3) return;
    doc_->toggleCell(index);
}

// L3 拖拽框选：把命中的单元并入选择集（去重）。
void CanvasCoordinator::onCellsMarquee(const std::vector<int>& indices) const {
    if (!doc_->hasImage() || doc_->mode() != engine::Tier::L3) return;
    doc_->addCells(indices);
}

// L3 CUSTOM 拖拽调序：把 from 单元移到 to 单元原序位（重排选择集顺序即自定义输出序）。
void CanvasCoordinator::onCellReordered(const int fromIndex, const int toIndex) const {
    if (!doc_->hasImage() || doc_->mode() != engine::Tier::L3) return;
    doc_->moveCellOrder(fromIndex, toIndex);
}

// 方向键微调选区（保持尺寸，钳制到图像内）。
void CanvasCoordinator::onNudge(const int dx, const int dy) const {
    if (!doc_->hasImage() || !doc_->hasRect()) return;
    const engine::RectRegion r = doc_->rect();
    const int w = r.width(), h = r.height();
    const int W = doc_->width(), H = doc_->height();
    if (w >= W || h >= H) return; // 选区已达整幅，无需微调。
    const int nl = std::clamp(r.left + dx, 0, W - w);
    const int nt = std::clamp(r.top + dy, 0, H - h);
    doc_->setRect(engine::RectRegion(nl, nt, nl + w, nt + h));
}

// 清除切割线（清除选区；按模式路由——原实现只清矩形，L3 的单元选择集与 L2 多矩形列表不受影响）：
//   · L3：显式单元选择集即选区（rect 恒空），清 cells；
//   · L2 MULTI_RECT：选区＝多矩形列表，连同单矩形一并清；
//   · 其余（L1 / L2 单矩形）：清单矩形。
void CanvasCoordinator::onClearCut() const {
    if (!doc_->hasImage()) return;
    if (doc_->mode() == engine::Tier::L3) {
        if (doc_->selectedCells().empty()) return;
        doc_->clearCells(); // 触发 changed → refreshPreview 清空叠加层。
        status_->notify(IDC_GTR(status, kCellsCleared), false);
        return;
    }
    if (doc_->mode() == engine::Tier::L2 && doc_->l2Sub() == L2Sub::MULTI_RECT) {
        if (!doc_->hasRect() && doc_->rects().empty()) return;
        doc_->clearRect();
        doc_->clearRects();
        status_->notify(IDC_GTR(status, kSelectionCleared), false);
        return;
    }
    if (!doc_->hasRect()) return;
    doc_->clearRect(); // 触发 changed → refreshPreview 清空叠加层。
    status_->notify(IDC_GTR(status, kSelectionCleared), false);
}

// L3 全选单元（与参数面板「全选」按钮同一 Document 入口；面板按钮无提示，此处保持一致）。
void CanvasCoordinator::onCellsSelectAll() const {
    if (!doc_->hasImage() || doc_->mode() != engine::Tier::L3) return;
    doc_->selectAllCells();
}

// L3 反选单元。
void CanvasCoordinator::onCellsInvert() const {
    if (!doc_->hasImage() || doc_->mode() != engine::Tier::L3) return;
    doc_->invertCells();
}

// L3 清空单元选择集。
void CanvasCoordinator::onCellsClear() const {
    if (!doc_->hasImage() || doc_->mode() != engine::Tier::L3) return;
    doc_->clearCells();
}

} // namespace idc::gui
