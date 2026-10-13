

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

#include "canvas/items/z_order.h"
#include "model/annotation_bridge.h"
#include "geometry/shapes.h"
#include "util/gui_nums.h"

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

// 单元区域 → 场景矩形（原图像素坐标；RectRegion 左闭右开，width/height 即跨度）。
QRectF cellSceneRect(const engine::Cell& c) {
    return {static_cast<qreal>(c.area.left), static_cast<qreal>(c.area.top),
            static_cast<qreal>(c.area.width()), static_cast<qreal>(c.area.height())};
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
    applySceneExtent();   // 余量与内容边界沿用当前值（网格内容由随后的 updateGrid/clearGrid 回灌）。

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

// 内容边界 = 原图 ∪ 全部网格单元区域（无网格时 gridBounds_ 为空矩形，united 直接返回原图）。
QRectF CanvasScene::contentBounds() const {
    return QRectF(0.0, 0.0, imgW_, imgH_).united(gridBounds_);
}

// 场景矩形 = 内容边界 + 外圈余量（余量由视图按「视口各边百分比 / 当前缩放」折算为场景单位注入，视口口径、与图幅
//   无关，见 CanvasView::updateScrollMargins）：横向按视口宽、纵向按视口高，故左右与上下可不等量。内容边界由
//   updateGrid 按 Core 单元并入：补白(PAD) 的残缺单元伸出原图时必须**完整**进滚动范围（夹住它 → 放大后可滚进来的
//   部分越来越小、边界块永远看不全），余量再包在这层内容外面。
// 无图（尺寸 0）时不改场景矩形：空矩形会让 QGraphicsScene 退回 itemsBoundingRect 自动口径。
void CanvasScene::applySceneExtent() {
    if (imgW_ <= 0 || imgH_ <= 0) return;
    setSceneRect(contentBounds().adjusted(-viewMarginXScene_, -viewMarginYScene_,
                                         viewMarginXScene_, viewMarginYScene_));
}

// 视图滚动余量（场景单位，由 CanvasView 按「视口各边百分比 / 当前缩放」折算）：任何缩放档位下都能把内容往外推
// 同一视口比例幅度（横＝视口宽×百分比、纵＝视口高×百分比）。本方法是场景矩形唯一的「外部余量」入口，与
// updateGrid 记下的内容边界互不干涉。
void CanvasScene::setScrollMargins(const qreal marginXScene, const qreal marginYScene) {
    const qreal mx = std::max(0.0, marginXScene);
    const qreal my = std::max(0.0, marginYScene);
    if (qFuzzyCompare(mx, viewMarginXScene_) && qFuzzyCompare(my, viewMarginYScene_)) return;
    viewMarginXScene_ = mx;
    viewMarginYScene_ = my;
    applySceneExtent();
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

    // 内容边界 = 各单元区域的并集（场景坐标）。补白(PAD) 策略下 Core 会保留越界单元值（`layoutAxis` 不夹取
    //   partial 单元，虚拟起点还可落在负坐标），那截越界必须**完整**进滚动范围，否则放大后可滚进来的部分
    //   越来越小、边界块永远看不全。余量策略不传进场景（三种策略共用同一套相位 `virtualStart`）：DISCARD
    //   （跳过残缺单元）/ KEEP_PARTIAL（已夹回图内）的单元恒在原图内 → 并集就是原图，与其余模式同口径。
    QRectF bounds;
    if (show) {
        for (const engine::Cell& c : grid.cells())
            bounds = bounds.united(cellSceneRect(c));
    }
    gridBounds_ = bounds;   // show=false 时为空矩形 → 内容边界退回原图。
    applySceneExtent();
}

// 清空网格线（非 L3 模式 / 换图时），并隐藏、清空单元点选图元。
void CanvasScene::clearGrid() {
    gridLayer_.clear();
    if (pickerItem_) {
        pickerItem_->setVisible(false);
        pickerItem_->setGrid(engine::Grid{}, imgW_, imgH_);
    }
    // 撤掉网格带来的内容外扩：非 L3（或换图后）内容边界退回原图，只剩外圈余量。
    gridBounds_ = QRectF();
    applySceneExtent();
}

// Palette 改色后刷新网格/诱导切割线 pen（rebuild 期烘焙、不随 update() 变色）；其余 paint 期取色图元由调用方 update() 重绘。
void CanvasScene::refreshPaletteColors() const {
    gridLayer_.recolor();
}

// 吸附运行时开关：回设单矩形选区与全部 L2 多矩形选区图元的 snapEnabled（无需重建）。
void CanvasScene::applySnapEnabled(const bool on) const {
    if (selItem_) selItem_->setSnapEnabled(on);
    for (SelectionRectItem* it : multiRectItems_) if (it) it->setSnapEnabled(on);
}

// 单元编号角标图层显隐（图层面板「单元编号」开关）：角标是单元点选图元的子图层，
// 随点选图元一起显隐；本开关只控制角标子图层自身。
void CanvasScene::setCellNumberVisible(const bool on) const {
    if (pickerItem_) pickerItem_->setBadgesVisible(on);
}

// 仅刷新 L2 多矩形的诱导切割线（委托 GridLayer 以橙色切割线样式重画），不触碰单元点选图元——
// 多矩形靠画布框选追加，若显示 picker 会 grab 鼠标、阻断框选，故此处不启动 picker。
// 这些诱导线本质是各矩形十字带并集的切割线，故画为橙色（asCutLines）并受 cutLinesVisible_ 门控。
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
// 底图是经 canvasGeom（画布级有效管线）作用后的工作图，场景坐标即工作图坐标系；layerPlacement 产出的
//   是画布坐标系矩形，需经 mapRectGeometry 正向映射后再落位（与逐像素几何同口径；空管线/仅色彩步→恒等），
//   否则画布级 rotate/flip/resize 后选框大小方向与原样不跟随、拖动命中错位。
// 无可呈现位图（layerPlacement 返回 nullopt）的层：已有图元仅隐藏（不删，规避拖拽期删除风险），否则跳过。
void CanvasScene::updateImageLayers(const std::vector<canvas::ImageLayer>& layers,
                                    const int canvasW, const int canvasH,
                                    const preprocess::PreprocessPipeline& canvasGeom,
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
        // 画布坐标 → 工作图坐标：随画布级几何步同步变换落位框（坐标空间与底图对齐）。
        const auto [x, y, width, height] = canvasGeom.mapRectGeometry(p->x, p->y, p->width, p->height, canvasW, canvasH);
        if (!it->isDragging()) it->setPlacement(QRectF(x, y, width, height));
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
    const bool sameSpot = !lastPickStack_.empty()
                          && std::hypot(scenePos.x() - lastPickPos_.x(), scenePos.y() - lastPickPos_.y())
                             <= nums::canvas::kPickSamePointTolScenePx
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
