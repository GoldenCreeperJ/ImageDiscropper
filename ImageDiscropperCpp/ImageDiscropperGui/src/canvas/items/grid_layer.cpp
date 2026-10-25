// ============================================================================
// 文件：canvas/items/grid_layer.cpp
// 作用：实现 L3 网格线层的重建、显隐与清理（见同名头文件说明）。
// 分块依据：线样式常量集中定义；rebuild 只做「按 Core 单元边界收集去重坐标 → 画贯穿线」，
//           无任何网格几何推导（那在 Core Grid::build；CONTRIBUTING.md「分层纪律」）。
// ============================================================================
#include "canvas/items/grid_layer.h"

#include <set>

#include <QColor>
#include <QGraphicsScene>
#include <QPen>

#include "canvas/items/grid_handle_item.h"
#include "canvas/items/z_order.h"
#include "app/palette.h"

namespace idc::gui {
namespace {

// 网格线样式（本目录 README「画布视觉规范」）：灰色虚线 1px；cosmetic 使线宽不随缩放变化。
// 灰色取自 Palette（可在「视图→外观/配色…」自定义）。
QPen gridPen() {
    QPen pen(Palette::instance().gridColor());
    pen.setWidth(1);
    pen.setStyle(Qt::DashLine);
    pen.setCosmetic(true);
    return pen;
}

// 切割线样式（L2 多矩形诱导线）：橙色实线 2px，与 SelectionRectItem 的橙色一致（cosmetic）。
QPen cutLinePen() {
    QPen pen(Palette::instance().selectionColor());
    pen.setWidth(2);
    pen.setCosmetic(true);
    return pen;
}

} // namespace

// 析构：手柄是本层持有的独立图元（不在 lineItems_ 内，不随 rebuild 重建），故在此释放；
// QGraphicsItem 析构会自动从所属场景移除。
GridLayer::~GridLayer() {
    delete originHandle_;
    delete cellHandle_;
}

// 依 Core 网格重建线：收集单元边界的唯一 x/y（裁剪到图像内、忽略越界的残缺单元边），
// 在每个坐标画一条贯穿全图的竖线 / 横线。asCutLines=true 时用橙色切割线样式，否则灰色虚线网格。
// 图元恒按最新网格构建，visible 只决定其可见性：显隐开关切换无需重建，也不会因隐藏期重建而丢失图元。
void GridLayer::rebuild(QGraphicsScene& scene, const engine::Grid& grid,
                        const int width, const int height, const bool visible, const bool asCutLines) {
    clearLines();   // 只清线：手柄不在其中（拖拽预览逐帧重建时不能把正在拖的手柄抽掉）。
    if (width <= 0 || height <= 0 || grid.cellCount() == 0) return;

    // 单元边界坐标去重升序；仅保留严格落在图像内部 (0,W)/(0,H) 的边（图像外轮廓不画）。
    std::set<int> xs;
    std::set<int> ys;
    for (const engine::Cell& c : grid.cells()) {
        const engine::RectRegion& a = c.area;
        if (a.left > 0 && a.left < width) xs.insert(a.left);
        if (a.right > 0 && a.right < width) xs.insert(a.right);
        if (a.top > 0 && a.top < height) ys.insert(a.top);
        if (a.bottom > 0 && a.bottom < height) ys.insert(a.bottom);
    }

    const QPen pen = asCutLines ? cutLinePen() : gridPen();
    lastAsCutLines_ = asCutLines; // 记忆本批样式，供 recolor() 重新取正确笔触。
    const qreal W = width;
    const qreal H = height;
    for (const int x : xs) {
        const qreal fx = x;
        QGraphicsLineItem* it = scene.addLine(fx, 0.0, fx, H, pen);
        it->setZValue(zorder::kGrid);
        it->setAcceptedMouseButtons(Qt::NoButton); // 纯显示，不遮挡单元点选/选区交互。
        it->setVisible(visible);
        lineItems_.push_back(it);
    }
    for (const int y : ys) {
        const qreal fy = y;
        QGraphicsLineItem* it = scene.addLine(0.0, fy, W, fy, pen);
        it->setZValue(zorder::kGrid);
        it->setAcceptedMouseButtons(Qt::NoButton);
        it->setVisible(visible);
        lineItems_.push_back(it);
    }
    // 切割线样式（L2 多矩形诱导）不承载基准手柄：基准只在 L3 有意义，故一并退场。
    if (asCutLines) setHandlesVisible(false);
}

// 显隐切换（不重建）：线图元与基准手柄同口径随开关走。
void GridLayer::setVisible(const bool visible) const {
    for (QGraphicsLineItem* it : lineItems_) {
        if (it) it->setVisible(visible);
    }
    setHandlesVisible(visible);
}

// 改色后重新取笔触并回设到全部线图元（pen 在 rebuild 期烘焙进 QGraphicsLineItem，不随 update() 变色）。
// 按本批样式（grid/cutLine）选取对应 Palette 色。
void GridLayer::recolor() const {
    const QPen pen = lastAsCutLines_ ? cutLinePen() : gridPen();
    for (QGraphicsLineItem* it : lineItems_) {
        if (it) it->setPen(pen);
    }
}

// 清空全部网格线图元。QGraphicsItem 析构会自动从所属场景移除。
// 基准手柄不删（保留连接，下次 L3 直接复用）但退场：非 L3 时不应看到/拖到基准手柄。
void GridLayer::clear() {
    clearLines();
    setHandlesVisible(false);
}

// 只清线图元（不动手柄）：rebuild 用它重建线，避免拖拽预览把正在拖的手柄一并隐藏。
void GridLayer::clearLines() {
    for (const QGraphicsLineItem* it : lineItems_) delete it;
    lineItems_.clear();
}

// ===========================================================================
// L3 网格基准手柄
// ===========================================================================

// 惰性创建两个手柄并置顶（z 高于单元点选图元，否则按下会被点选图元吃掉）。
// 信号连接不在本层做（本层不认识 CanvasScene），由场景在创建后自行 connect。
void GridLayer::createHandles(QGraphicsScene& scene) {
    if (handlesReady()) return;
    originHandle_ = new GridHandleItem(GridHandleItem::Role::Origin);
    cellHandle_ = new GridHandleItem(GridHandleItem::Role::Cell);
    for (GridHandleItem* it : {originHandle_, cellHandle_}) {
        it->setZValue(zorder::kGridHandle);
        scene.addItem(it);
    }
}

// 按给定参数落位两个手柄并切显隐：拖拽预览时传草稿参数，使两手柄随网格线一起跟手。
// 每轴是否周期先于落位下发：分割数/比例轴无基准点与单元尺寸可调，手柄在该轴不参与拖动
// （两轴皆非周期时两道手柄整体退场）。
void GridLayer::syncHandles(const engine::GridParams& params, const int width, const int height,
                            const bool visible) const {
    const bool pitchX = engine::effectiveAxis(params, true).mode == engine::GridAxisMode::Pitch;
    const bool pitchY = engine::effectiveAxis(params, false).mode == engine::GridAxisMode::Pitch;
    for (GridHandleItem* it : {originHandle_, cellHandle_}) {
        if (!it) continue;
        it->setImageBounds(width, height);
        it->setPitchAxes(pitchX, pitchY);
        it->setParams(params);
        it->setEditable(visible);
    }
}

// 仅切显隐（不动参数/可交互）：可交互另由 setHandlesInteractive 按四态模式收紧。
void GridLayer::setHandlesVisible(const bool visible) const {
    for (GridHandleItem* it : {originHandle_, cellHandle_}) {
        if (it) it->setEditable(visible);
    }
}

// 四态模式门控：图片拾取 / 标注选取态让位（手柄仍可见，只不吃鼠标），避免跟那两态的图元抢按下。
void GridLayer::setHandlesInteractive(const bool on) const {
    for (GridHandleItem* it : {originHandle_, cellHandle_}) {
        if (it) it->setInteractive(on);
    }
}

// 手柄边长随视图缩放下发（恒约 8 屏幕px）。
void GridLayer::setHandleSize(const qreal sceneUnits) const {
    for (GridHandleItem* it : {originHandle_, cellHandle_}) {
        if (it) it->setHandleSize(sceneUnits);
    }
}

} // namespace idc::gui
