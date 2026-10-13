// ============================================================================
// 文件：canvas/grid_layer.cpp
// 作用：实现 L3 网格线层的重建、显隐与清理（见同名头文件说明）。
// 分块依据：线样式常量集中定义；rebuild 只做「按 Core 单元边界收集去重坐标 → 画贯穿线」，
//           无任何网格几何推导（那在 Core Grid::build；CONTRIBUTING.md「分层纪律」）。
// ============================================================================
#include "canvas/items/grid_layer.h"

#include <set>

#include <QColor>
#include <QGraphicsScene>
#include <QPen>

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

// 依 Core 网格重建线：收集单元边界的唯一 x/y（裁剪到图像内、忽略越界的残缺单元边），
// 在每个坐标画一条贯穿全图的竖线 / 横线。asCutLines=true 时用橙色切割线样式，否则灰色虚线网格。
void GridLayer::rebuild(QGraphicsScene& scene, const engine::Grid& grid,
                        const int width, const int height, const bool visible, const bool asCutLines) {
    clear();
    if (!visible || width <= 0 || height <= 0 || grid.cellCount() == 0) return;

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
        lineItems_.push_back(it);
    }
    for (const int y : ys) {
        const qreal fy = y;
        QGraphicsLineItem* it = scene.addLine(0.0, fy, W, fy, pen);
        it->setZValue(zorder::kGrid);
        it->setAcceptedMouseButtons(Qt::NoButton);
        lineItems_.push_back(it);
    }
}

// 显隐切换（不重建）。
void GridLayer::setVisible(const bool visible) const {
    for (QGraphicsLineItem* it : lineItems_) {
        if (it) it->setVisible(visible);
    }
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
void GridLayer::clear() {
    for (const QGraphicsLineItem* it : lineItems_) delete it;
    lineItems_.clear();
}

} // namespace idc::gui
