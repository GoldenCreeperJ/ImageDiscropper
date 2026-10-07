// ============================================================================
// 文件：canvas/canvas_scene.cpp
// 作用：实现 CanvasScene 的图层装配与刷新（见同名头文件说明）。
// 分块依据：底图/遮罩/切割线/选区四类图元各由专门方法管理；场景不含切割或几何逻辑，
//           仅按 Core 结果与 Document 状态渲染（CONTRIBUTING.md「分层纪律」）。
// ============================================================================
#include "canvas/canvas_scene.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

#include <QGraphicsSceneMouseEvent>
#include <QTransform>

#include "canvas/z_order.h"
#include "model/annotation_bridge.h"
#include "geometry/shapes.h"

namespace idc::gui {
namespace {

// 在候选坐标集 vals 中取最接近 target 者；vals 为空返回 -1。
// 用于把 Core 的切割线（xs/ys 含 0/W 边界）关联到最近的选区边。
int nearest(const std::vector<int>& vals, const int target) {
    int best = -1;
    int bestD = -1;
    for (const int v : vals) {
        if (const int d = std::abs(v - target); bestD < 0 || d < bestD) { bestD = d; best = v; }
    }
    return best;
}

} // namespace

// 构造：空场景。底图与选区框在 setBaseImage 时惰性创建。
// 【越界行为】不做越界裁剪：图层落位框与标注矢量均可绘制到画布（sceneRect）之外，视口内自由可见
//   （Qt6 已移除 QGraphicsView::setItemClippingEnabled，且产品决定不保留“两者都裁”）。故本构造无需额外开关。
CanvasScene::CanvasScene(QObject* parent) : QGraphicsScene(parent) {}

// 设置底图并同步场景矩形与选区边界。
void CanvasScene::setBaseImage(const QPixmap& preview, const double scaleX, const double scaleY,
                               const int fullW, const int fullH) {
    imgW_ = fullW;
    imgH_ = fullH;
    setSceneRect(0.0, 0.0, fullW, fullH);

    // 底图：预览 pixmap 经放大变换铺满原图尺寸（快速变换，预览足够）。
    if (!baseItem_) {
        baseItem_ = addPixmap(QPixmap());
        baseItem_->setZValue(zorder::kBase);
        baseItem_->setAcceptedMouseButtons(Qt::NoButton);
    }
    baseItem_->setTransformationMode(Qt::FastTransformation);
    baseItem_->setTransform(QTransform::fromScale(scaleX, scaleY));
    baseItem_->setPixmap(preview);

    // 选区框：惰性创建，设定图像边界（供钳制与吸附）。
    if (!selItem_) {
        selItem_ = new SelectionRectItem();
        addItem(selItem_);
        selItem_->setZValue(zorder::kSelection);
        selItem_->setVisible(false);
        // 选区框的编辑事件转发为场景信号，供 MainWindow 写回 Document（视图/场景不认识 Document）。
        connect(selItem_, &SelectionRectItem::rectChanged, this, &CanvasScene::selectionEdited);
        // 拖动选区边框开始：转发为场景交互信号，供上层清除标注选中（标注 OBB 手柄随选区拖动消失）。
        connect(selItem_, &SelectionRectItem::dragStarted, this, &CanvasScene::selectionInteractionStarted);
        // 同步图层面板的切割线 / 选取边框显隐开关（新建图元需继承当前标志）。
        selItem_->setCutLinesVisible(cutLinesVisible_);
        selItem_->setBorderVisible(selectionVisible_);
    }
    selItem_->setImageBounds(fullW, fullH);

    // 单元点选图元：惰性创建（L3 交互）；仅 L3 显示（updateGrid 时切换）。
    // 其点选/框选事件同样转发为场景信号，供 MainWindow 写回 Document。
    if (!pickerItem_) {
        pickerItem_ = new CellPickerItem();
        addItem(pickerItem_);
        pickerItem_->setVisible(false);
        connect(pickerItem_, &CellPickerItem::cellToggled, this, &CanvasScene::cellToggled);
        connect(pickerItem_, &CellPickerItem::cellsMarqueeSelected,
                this, &CanvasScene::cellsMarqueeSelected);
        connect(pickerItem_, &CellPickerItem::cellReordered, this, &CanvasScene::cellReordered);
    }
}

// 清空画布全部内容。
void CanvasScene::clearAll() {
    maskLayer_.clear();
    gridLayer_.clear();
    clearCutLines();
    clearMultiRects();
    clearAnnotations();
    clearImageLayers();
    if (baseItem_) { removeItem(baseItem_); delete baseItem_; baseItem_ = nullptr; }
    if (selItem_) { removeItem(selItem_); delete selItem_; selItem_ = nullptr; }
    if (pickerItem_) { removeItem(pickerItem_); delete pickerItem_; pickerItem_ = nullptr; }
    imgW_ = 0;
    imgH_ = 0;
}

// 刷新保留/删除遮罩（委托 MaskLayer）。
void CanvasScene::updateMasks(const engine::EngineResult& result) {
    maskLayer_.rebuild(*this, result, imgW_, imgH_, masksVisible_);
}

// 遮罩显隐切换。
void CanvasScene::setMasksVisible(const bool visible) {
    masksVisible_ = visible;
    maskLayer_.setVisible(visible);
}

// 依 Core 切割线集合，判定选区四条边各自是否有「内部贯穿切割线」，把结果下发给选区框：
// 选区框据此把这些边延伸绘制为橙色贯穿线，并允许直接抓取拖动（拖动该边＝移动切割线）。
// 单矩形/十字(RECT)四边皆有；横带(HLINE)仅上/下；竖带(VLINE)仅左/右；落在图像边界(0/W/H)
// 的线不算「内部切割线」（此时选区边即图像边，内部无切割可言），对应边不延伸。
void CanvasScene::updateCutLines(const engine::CutLineSet& lines, const engine::RectRegion& rect) const {
    if (!selItem_ || imgW_ <= 0 || imgH_ <= 0) { clearCutLines(); return; }

    // 每条边取 Core 线集中最接近该边者，落在图像内部才视为有效贯穿切割线。
    const int lx = nearest(lines.xs, rect.left);
    const int rx = nearest(lines.xs, rect.right);
    const int ty = nearest(lines.ys, rect.top);
    const int by = nearest(lines.ys, rect.bottom);
    const bool left   = lx > 0 && lx < imgW_;
    const bool right  = rx > 0 && rx < imgW_;
    const bool top    = ty > 0 && ty < imgH_;
    const bool bottom = by > 0 && by < imgH_;
    selItem_->setCutEdges(left, right, top, bottom);
}

// 清空切割线（换图/关闭图像/无选区时）：取消选区框的边延伸标记。
void CanvasScene::clearCutLines() const {
    if (selItem_) selItem_->setCutEdges(false, false, false, false);
}

// 依 Core 诱导网格刷新 L3 网格线（委托 GridLayer，灰色虚线）；show=false 时清空。
// 同时把网格下发给单元点选图元并按 show 切换其显隐。
// 单元选择高亮属「选取边框」图层范畴：受 selectionVisible_ 门控（隐藏后不可点选，与选区框一致）。
void CanvasScene::updateGrid(const engine::Grid& grid, const bool show) {
    gridLayer_.rebuild(*this, grid, imgW_, imgH_, show && gridVisible_);
    if (pickerItem_) {
        pickerItem_->setGrid(grid, imgW_, imgH_);
        pickerItem_->setVisible(show && selectionVisible_);
    }
}

// 清空网格线（非 L3 模式 / 换图时），并隐藏、清空单元点选图元。
void CanvasScene::clearGrid() {
    gridLayer_.clear();
    if (pickerItem_) {
        pickerItem_->setVisible(false);
        pickerItem_->setGrid(engine::Grid{}, imgW_, imgH_);
    }
}

// 单元编号角标图层显隐（图层面板「单元编号」开关）：角标是单元点选图元的子图层，
// 随点选图元一起显隐；本开关只控制角标子图层自身。
void CanvasScene::setCellNumberVisible(const bool on) const {
    if (pickerItem_) pickerItem_->setBadgesVisible(on);
}

// 仅刷新 L2 多矩形的诱导切割线（委托 GridLayer 以橙色切割线样式重画），不触碰单元点选图元——
// 多矩形靠画布框选追加，若显示 picker 会 grab 鼠标、阻断框选，故此处不启动 picker。
// 这些诱导线本质是各矩形十字带并集的切割线，故画为橙色（asCutLines）并受 cutLinesVisible_ 门控。
void CanvasScene::updateMultiRectCutLines(const engine::Grid& grid, const bool show) {
    gridLayer_.rebuild(*this, grid, imgW_, imgH_, show && cutLinesVisible_, /*asCutLines=*/true);
}

// 依 Document 多矩形列表刷新可交互的橙色选区框（L2 MULTI_RECT）。每个矩形一个 SelectionRectItem，
// 可整体移动 / 四角缩放 / 拖边微调，与单矩形选区一致。坐标即原图像素（场景坐标），无需换算。
// 【拖拽安全】增量维护：按数量增删末位图元、逐个刷新几何；绝不 clear+重建（否则会在拖拽中
// 删除正在处理鼠标事件的图元→崩溃）；且跳过对正在拖拽图元的回设（避免逐帧量化抖动）。
// 多矩形不画贯穿切割线（各矩形十字带的并集由遮罩呈现），故 cutEdges 全 false，boundingRect
// 仅覆盖各自矩形，不阻断在空白处框选追加新矩形。
void CanvasScene::updateMultiRects(const std::vector<engine::RectRegion>& rects) {
    const int n = static_cast<int>(rects.size());
    // 收缩：删除多余末位图元（拖拽期间不会发生删减，防御性跳过正在拖拽者）。
    while (multiRectItems_.size() > n) {
        SelectionRectItem* it = multiRectItems_.last();
        if (it->isDragging()) break;   // 防御：绝不删除正在拖拽的图元。
        multiRectItems_.removeLast();
        removeItem(it);
        delete it;
    }
    // 扩张：为新增矩形创建图元并连接编辑信号（发射时按指针查当前下标，避免下标漂移）。
    while (multiRectItems_.size() < n) {
        auto* it = new SelectionRectItem();
        it->setZValue(zorder::kSelection);
        it->setImageBounds(imgW_, imgH_);
        it->setHandleSize(multiHandleSize_);
        it->setCutLinesVisible(cutLinesVisible_);   // 继承图层面板切割线显隐开关。
        it->setBorderVisible(selectionVisible_);    // 继承图层面板选取边框显隐开关。
        addItem(it);
        connect(it, &SelectionRectItem::rectChanged, this, [this, it](const QRectF& r) {
            if (const int idx = multiRectItems_.indexOf(it); idx >= 0) emit multiRectEdited(idx, r);
        });
        // 多矩形拖框开始同样转发交互信号（任一选区框被拖动都清除标注选中）。
        connect(it, &SelectionRectItem::dragStarted, this, &CanvasScene::selectionInteractionStarted);
        multiRectItems_.append(it);
    }
    // 刷新几何：跳过正在拖拽者（其位置/边界由手势维护，释放时才回设，避免逐帧 prepareGeometryChange 抖动）。
    // 非拖拽者也只在边界变化/矩形变化时才重推，避免拖拽期间对其余矩形的无谓重绘。
    const bool boundsChanged = multiBoundsW_ != imgW_ || multiBoundsH_ != imgH_;
    for (int i = 0; i < multiRectItems_.size(); ++i) {
        SelectionRectItem* it = multiRectItems_[i];
        it->setVisible(true);
        it->setHighlighted(i == activeMultiRect_);   // 高亮当前选中项（仅变化时重绘）。
        if (i < n && !it->isDragging()) {
            if (boundsChanged) it->setImageBounds(imgW_, imgH_);
            const engine::RectRegion& r = rects[static_cast<std::size_t>(i)];
            const QRectF nr(r.left, r.top,
                            (r.width()), (r.height()));
            if (it->rect() != nr) it->setRect(nr);
        }
    }
    multiBoundsW_ = imgW_;
    multiBoundsH_ = imgH_;
}

// 清空多矩形选区框（非 MULTI_RECT / 换图时）。
void CanvasScene::clearMultiRects() {
    for (auto* item : multiRectItems_) { removeItem(item); delete item; }
    multiRectItems_.clear();
    multiBoundsW_ = -1;   // 重置边界缓存，下次重建时重推图像边界。
    multiBoundsH_ = -1;
    activeMultiRect_ = -1; // 无矩形时无高亮。
}

// 设置多矩形选区框的手柄尺寸（同时缓存供后续新建图元使用）。
void CanvasScene::setMultiRectHandleSize(const qreal sceneUnits) {
    multiHandleSize_ = sceneUnits;
    for (SelectionRectItem* it : multiRectItems_) it->setHandleSize(sceneUnits);
}

// 是否有任一多矩形选区框正被拖拽。
bool CanvasScene::isDraggingMultiRect() const {
    for (const SelectionRectItem* it : multiRectItems_)
        if (it->isDragging()) return true;
    return false;
}

// 设置当前高亮（选中）的多矩形下标：对应选区框颜色略微加强，其余恢复普通。
void CanvasScene::setActiveMultiRect(const int index) {
    activeMultiRect_ = index;
    for (int i = 0; i < multiRectItems_.size(); ++i)
        multiRectItems_[i]->setHighlighted(i == index);
}

// 依 Document 的选择集与排序策略刷新单元点选图元的高亮。
void CanvasScene::updateCellSelection(const std::vector<int>& selected,
                                      const engine::SortStrategy strategy) const {
    if (pickerItem_) pickerItem_->setSelection(selected, strategy);
}

// L3 框选命中查询：委托单元点选图元。无图元（尚无图像 / 非 L3）时返回空。
std::vector<int> CanvasScene::cellsIntersecting(const QRectF& sceneRect) const {
    if (!pickerItem_) return {};
    return pickerItem_->cellsIntersecting(sceneRect);
}

// 依 Document 的选区同步选区框显示。
void CanvasScene::syncSelection(const engine::RectRegion& rect, const bool hasRect) const {
    if (!selItem_) return; // 尚无图像。
    if (!hasRect) {
        selItem_->setVisible(false);
        return;
    }
    selItem_->setRect(QRectF(rect.left, rect.top,
                             rect.width(), rect.height()));
    selItem_->setVisible(true);
}

// ===========================================================================
// 标注图层
// ===========================================================================

// 依 AnnotationBridge 增量维护标注矢量图元与拖拽预览图元。
// 【拖拽安全】按数量增删末位图元、逐个刷新几何；绝不 clear+重建（否则会在拖拽中删除
// 正在处理鼠标事件的图元→崩溃）；且跳过对正在拖拽图元的几何回设（其位置由手势维护）。
void CanvasScene::updateAnnotations(const AnnotationBridge& model) {
    const std::vector<annotation::Annotation>& anns = model.annotations();
    const int n = static_cast<int>(anns.size());

    // 收缩：删除多余末位图元（防御：绝不删除正在拖拽者）。
    while (annoItems_.size() > n) {
        AnnotationItem* it = annoItems_.last();
        if (it->isDragging()) break;
        annoItems_.removeLast();
        removeItem(it);
        delete it;
    }
    // 扩张：为新增标注创建图元并连接信号（发射时按指针查当前下标，避免下标漂移）。
    while (annoItems_.size() < n) {
        auto* it = new AnnotationItem();
        it->setHandleSize(annoHandleSize_);
        // 依当前标注选取模式决定图元是否吃左键（默认框选态＝不吃，手势落到视图框选）。
        it->setAcceptedMouseButtons(annotationSelectMode_ ? Qt::LeftButton : Qt::NoButton);
        addItem(it);
        connect(it, &AnnotationItem::pressed, this, &CanvasScene::annotationSelectRequested);
        connect(it, &AnnotationItem::moveRequested, this, [this, it](const double dx, const double dy) {
            if (const int idx = annoItems_.indexOf(it); idx >= 0) emit annotationMoved(idx, dx, dy);
        });
        connect(it, &AnnotationItem::transformRequested,
                this, [this, it](const double sx, const double sy, const double deg) {
                    if (const int idx = annoItems_.indexOf(it); idx >= 0) emit annotationTransformed(idx, sx, sy, deg);
        });
        connect(it, &AnnotationItem::transformPreview,
                this, [this, it](const double sx, const double sy, const double deg) {
                    if (const int idx = annoItems_.indexOf(it); idx >= 0) emit annotationTransformPreview(idx, sx, sy, deg);
        });
        annoItems_.append(it);
    }
    // 刷新几何与选中态：跳过正在拖拽者（其位置/几何由手势维护，释放时才回灌）。
    const std::optional<std::size_t> sel = model.selectedIndex();
    for (int i = 0; i < annoItems_.size(); ++i) {
        AnnotationItem* it = annoItems_[i];
        it->setVisible(annotationsVisible_);
        if (i < n && !it->isDragging()) it->setAnnotation(anns[static_cast<std::size_t>(i)]);
        it->setSelectedState(sel.has_value() && *sel == static_cast<std::size_t>(i));
    }

    // 拖拽预览（橡皮筋矢量）与已提交图元解耦，走同一刷新入口。
    updatePendingAnnotation(model);
}

// 仅刷新拖拽预览图元：以当前属性临时封装一个 Annotation 渲染，不进 Core 历史。
// 绘制手势逐帧只调本方法，避免逐帧重建/克隆已提交标注（性能）。
void CanvasScene::updatePendingAnnotation(const AnnotationBridge& model) {
    if (const geometry::Shape* ps = model.pendingShape(); ps && annotationsVisible_) {
        if (!pendingAnnoItem_) {
            pendingAnnoItem_ = new AnnotationItem();
            pendingAnnoItem_->setHandleSize(annoHandleSize_);
            pendingAnnoItem_->setAcceptedMouseButtons(Qt::NoButton);  // 预览不可交互
            // 橡皮筋预览抬到已提交标注之上（构造默认落在 kAnnotation，与后提交的标注同 z 会被盖住）。
            pendingAnnoItem_->setZValue(zorder::kAnnotationPreview);
            addItem(pendingAnnoItem_);
        }
        annotation::Annotation tmp;
        tmp.shape = ps->clone();
        tmp.style.color = model.currentColor();
        tmp.style.fillColor = model.currentFillColor();
        tmp.style.strokeWidth = model.currentStrokeWidth();
        tmp.style.fill = model.currentFill();
        pendingAnnoItem_->setAnnotation(tmp);
        pendingAnnoItem_->setVisible(true);
    } else if (pendingAnnoItem_) {
        pendingAnnoItem_->setVisible(false);
    }
}

// 标注图层显隐：只切图元可见性，不删除（保留 Core 标注数据）。
void CanvasScene::setAnnotationsVisible(const bool on) {
    annotationsVisible_ = on;
    for (AnnotationItem* it : annoItems_) it->setVisible(on);
    if (pendingAnnoItem_ && !on) pendingAnnoItem_->setVisible(false);
}

// 清空全部标注图元（含预览）。QGraphicsItem 析构会自动从场景移除。
void CanvasScene::clearAnnotations() {
    for (AnnotationItem* it : annoItems_) { removeItem(it); delete it; }
    annoItems_.clear();
    if (pendingAnnoItem_) { removeItem(pendingAnnoItem_); delete pendingAnnoItem_; pendingAnnoItem_ = nullptr; }
}

// 设置标注控制点手柄尺寸（同时缓存供后续新建图元使用）。
void CanvasScene::setAnnotationHandleSize(const qreal sceneUnits) {
    annoHandleSize_ = sceneUnits;
    for (AnnotationItem* it : annoItems_) it->setHandleSize(sceneUnits);
    if (pendingAnnoItem_) pendingAnnoItem_->setHandleSize(sceneUnits);
}

// 底图图层显隐：仅切换 base 图元可见性（不影响遮罩 / 标注 / 导出）。
void CanvasScene::setBaseVisible(const bool on) const {
    if (baseItem_) baseItem_->setVisible(on);
}

// 网格线图层显隐：仅置位门控标志（不直接切 gridLayer_ 可见性）。
// 说明：GridLayer 被 L3 灰色网格与 L2 橙色切割线共用，直接 setVisible 会与 cutLinesVisible_ 相互覆盖；
// 故只置标志，由 MainWindow 切换后调 refreshPreview 重建落地（updateGrid/updateMultiRectCutLines 依各自门控）。
void CanvasScene::setGridVisible(const bool on) {
    gridVisible_ = on;
}

// 切割线图层显隐：下发到单选区框与全部多矩形框（仅影响贯穿延伸段绘制，不影响拖边交互）。
void CanvasScene::setCutLinesVisible(const bool on) {
    cutLinesVisible_ = on;
    if (selItem_) selItem_->setCutLinesVisible(on);
    for (SelectionRectItem* it : multiRectItems_) it->setCutLinesVisible(on);
}

// 选取边框图层显隐：下发到单选区框与全部多矩形框（隐藏边框同时禁用其拖拽交互，遵循「隐藏图层=不可交互」）。
// L3 单元选择高亮（CellPickerItem）由上层 refreshPreview 依 selectionVisible_ 重建显隐。
void CanvasScene::setSelectionVisible(const bool on) {
    selectionVisible_ = on;
    if (selItem_) selItem_->setBorderVisible(on);
    for (SelectionRectItem* it : multiRectItems_) it->setBorderVisible(on);
}

// ===========================================================================
// 图片图层落位框（多层画布：底图之上、标注之下的可拾取框）
// ===========================================================================

// 依 Document 图层栈增量维护每个图片图层的落位框图元。
// 【拖拽安全】仿 updateMultiRects：按 layerId 对齐增删，绝不 clear+重建（否则会在拖拽中删除
// 正在处理鼠标事件的图元→崩溃）；且跳过对正在拖拽者的几何回设（其位置由手势维护，释放才回灌）。
// 落位矩形来自 Core canvas::layerPlacement（GUI 不自算锚点/枢轴/缩放，CONTRIBUTING.md「分层纪律」）。
// 无可呈现位图（layerPlacement 返回 nullopt）的层：已有图元仅隐藏（不删，规避拖拽期删除风险），否则跳过。
void CanvasScene::updateImageLayers(const std::vector<canvas::ImageLayer>& layers,
                                    const int canvasW, const int canvasH,
                                    const std::optional<int> selectedId) {
    // 收缩：删除已不在 layers 中的图元（按 id 匹配；防御：绝不删除正在拖拽者）。
    for (int i = imageLayerItems_.size() - 1; i >= 0; --i) {
        ImageLayerItem* it = imageLayerItems_[i];
        const bool stillPresent = std::any_of(layers.begin(), layers.end(),
                                              [it](const canvas::ImageLayer& l) { return l.id == it->layerId(); });
        if (stillPresent) continue;
        if (it->isDragging()) continue;
        imageLayerItems_.remove(i);
        removeItem(it);
        delete it;
    }
    // 扩张 + 刷新：逐层确保有对应图元，再按 Core 落位刷新几何与选中态。
    for (const canvas::ImageLayer& l : layers) {
        ImageLayerItem* it = nullptr;
        for (ImageLayerItem* cand : imageLayerItems_) {
            if (cand->layerId() == l.id) { it = cand; break; }
        }
        const auto p = canvas::layerPlacement(l, canvasW, canvasH);
        if (!p) {
            if (it) it->setVisible(false);   // 空位图：隐藏既有图元，不删除。
            continue;
        }
        if (!it) {
            it = new ImageLayerItem();
            it->setLayerId(l.id);
            // 拾取按下 / 拖拽平移转发为场景信号，交 MainWindow 写回 Document（场景不认识 Document）。
            connect(it, &ImageLayerItem::pressed, this, &CanvasScene::imageLayerPressed);
            connect(it, &ImageLayerItem::moved, this, &CanvasScene::imageLayerMoved);
            // 新图元交互态：拾取模式开即为循环选层候选；z 恒用常规落位层序（拾取不靠抬层）。
            it->setInteractive(imagePickMode_);
            it->setZValue(zorder::kImageLayer);
            addItem(it);
            imageLayerItems_.append(it);
        }
        it->setVisible(true);
        if (!it->isDragging()) it->setPlacement(QRectF(p->x, p->y, p->width, p->height));
        it->setSelectedFrame(selectedId.has_value() && selectedId == l.id);
    }
}

// 清空全部图片图层落位框图元。QGraphicsItem 析构会自动从场景移除。
void CanvasScene::clearImageLayers() {
    for (ImageLayerItem* it : imageLayerItems_) { removeItem(it); delete it; }
    imageLayerItems_.clear();
}

// 仅切换已有落位框的选中高亮（不重算 layerPlacement）：选中层变化时廉价刷新（跳过正在拖拽者）。
void CanvasScene::setImageLayerSelection(const std::optional<int> selectedId) {
    for (ImageLayerItem* it : imageLayerItems_) {
        if (it->isDragging()) continue;
        it->setSelectedFrame(selectedId.has_value() && selectedId == it->layerId());
    }
}

// 图层拾取模式：切换全部落位框的可拾取标志（不再动 z 序——拾取纯由四态模式 + 场景级循环选层，与叠放先后无关）。
// 开→落位框登记为拾取候选（点选由本场景 mousePressEvent 循环路由，非 z 序自动抓取）；
// 关→全部退出候选（仅示意），并放弃进行中的场景级拖拽、复位循环态（重入从最顶层重新开始）。
void CanvasScene::setImagePickMode(const bool on) {
    imagePickMode_ = on;
    if (!on) { pickDragItem_ = nullptr; lastPickStack_.clear(); lastPickIndex_ = 0; }
    for (ImageLayerItem* it : imageLayerItems_) it->setInteractive(on);
}

// 收集 scenePos 处命中的图片图层落位框 id，按叠放序「顶在前」返回。去 z 序自动抓取依赖：本函数
//   自行遍历候选图元做几何命中，不依赖 Qt 把按下事件递给最顶图元；仅拾取模式下可见且登记的候选入栈。
std::vector<int> CanvasScene::layerStackAt(const QPointF& scenePos) const {
    struct Hit { int id; double z; int order; };
    std::vector<Hit> hits;
    for (int i = 0; i < imageLayerItems_.size(); ++i) {
        const ImageLayerItem* it = imageLayerItems_[i];
        if (!it->isVisible() || !it->interactive()) continue;
        if (!it->placement().contains(scenePos)) continue;
        hits.push_back({it->layerId(), it->zValue(), i});
    }
    // 顶在前：zValue 降序；同 z 则后加入者（绘制在上）在前，保证叠放序确定。
    std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
        if (a.z != b.z) return a.z > b.z;
        return a.order > b.order;
    });
    std::vector<int> stack;
    stack.reserve(hits.size());
    for (const Hit& h : hits) stack.push_back(h.id);
    return stack;
}

// 依循环态决定本次应选中的图层 id：同点（容差内）续击且候选栈不变 → 沿栈向下推进一位（到底回顶，仿 Figma
//   连击选层）；换点或栈变化 → 重置为最顶（下标 0）。记录本次命中点/栈/下标供下次比较。
int CanvasScene::advancePickCycle(const QPointF& scenePos, const std::vector<int>& stack) {
    if (stack.empty()) return -1;
    constexpr double kSamePointTol = 3.0;   // 场景单位：近似同点判定（连击微抖容忍）。
    const bool sameSpot = !lastPickStack_.empty()
                          && std::hypot(scenePos.x() - lastPickPos_.x(), scenePos.y() - lastPickPos_.y()) <= kSamePointTol
                          && lastPickStack_ == stack;
    std::size_t idx = 0;
    if (sameSpot) idx = (lastPickIndex_ + 1) % stack.size();
    lastPickPos_ = scenePos;
    lastPickStack_ = stack;
    lastPickIndex_ = idx;
    return stack[idx];
}

// 按 id 取回对应的落位框图元（无则 nullptr）。
ImageLayerItem* CanvasScene::imageLayerItemById(const int id) const {
    for (ImageLayerItem* it : imageLayerItems_) if (it->layerId() == id) return it;
    return nullptr;
}

// 场景级按下：拾取模式下重叠落位框靠循环选层（layerStackAt + advancePickCycle），而非 z 序自动抓取。
//   命中候选 → 推进循环选中对应层并起拖（beginDrag 内发 pressed → imageLayerPressed）；
//   未命中任何层 → 按落点路由：画布内（底图铺满）发 basePickRequested 选中底图、画布外发 pickDeselectRequested 取消全部；
//   图片拾取态恒 accept、不落基类（杜绝残余框选/默认抓取，四态互斥）；非拾取模式 / 非左键 → 交基类。
void CanvasScene::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (imagePickMode_ && event->button() == Qt::LeftButton) {
        if (const std::vector<int> stack = layerStackAt(event->scenePos()); !stack.empty()) {
            if (ImageLayerItem* it = imageLayerItemById(advancePickCycle(event->scenePos(), stack))) {
                pickDragItem_ = it;
                it->beginDrag(event->scenePos());
                event->accept();
                return;
            }
        }
        // 未命中可拖图层：复位循环态，再按落点分派底图选中 / 取消全部选中（坐实四态互斥）。
        lastPickStack_.clear();
        lastPickIndex_ = 0;
        if (const QPointF p = event->scenePos(); p.x() >= 0 && p.y() >= 0 && p.x() < imgW_ && p.y() < imgH_)
            emit basePickRequested();
        else
            emit pickDeselectRequested();
        event->accept();
        return;
    }
    QGraphicsScene::mousePressEvent(event);
}

// 场景级移动：拾取拖拽期驱动当前被拖落位框（dragMove 仅视觉位移），否则交基类。
void CanvasScene::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    if (pickDragItem_) {
        pickDragItem_->dragMove(event->scenePos());
        event->accept();
        return;
    }
    QGraphicsScene::mouseMoveEvent(event);
}

// 场景级释放：结束拾取拖拽（dragEnd 内一次性发 moved → imageLayerMoved），清空拖拽句柄，否则交基类。
void CanvasScene::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    if (pickDragItem_) {
        pickDragItem_->dragEnd(event->scenePos());
        pickDragItem_ = nullptr;
        event->accept();
        return;
    }
    QGraphicsScene::mouseReleaseEvent(event);
}

// 标注选取模式：切换全部已提交标注图元的左键可交互性（预览图元恒不可交互，不受影响）。
// 开→标注可点选 / 拖拽 / 手柄；关→标注不吃鼠标，框选 / 拾取手势不被抢走。
void CanvasScene::setAnnotationSelectMode(const bool on) {
    annotationSelectMode_ = on;
    const auto annoButtons = on ? Qt::LeftButton : Qt::NoButton;
    for (AnnotationItem* it : annoItems_) it->setAcceptedMouseButtons(annoButtons);
    // 标注选取态：z 位于标注之上（选区 60 / 单元 / 多矩形）的覆盖图元改吃无鼠标，使被这些框盖住的标注
    //   也能点中（否则顶图元抢占按下事件、下方标注收不到）；退态恢复其可交互（仅左键，与既有语义一致）。
    const auto overlayButtons = on ? Qt::NoButton : Qt::LeftButton;
    if (selItem_) selItem_->setAcceptedMouseButtons(overlayButtons);
    for (SelectionRectItem* it : multiRectItems_) it->setAcceptedMouseButtons(overlayButtons);
    if (pickerItem_) pickerItem_->setAcceptedMouseButtons(overlayButtons);
}

} // namespace idc::gui
