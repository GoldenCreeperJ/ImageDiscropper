// ============================================================================
// 文件：canvas/image_layer_item.cpp
// 作用：实现 ImageLayerItem（见同名头文件说明）——落位框绘制 + 拾取拖拽手势。
// 分块依据：渲染（boundingRect/paint）与交互（press/move/release）各成一块；无任何落位几何计算
//          （矩形来自 Core layerPlacement，经场景下发），符合「图元只渲染并拖动它」的分层纪律。
// ============================================================================
#include "canvas/image_layer_item.h"

#include <cmath>

#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QPen>

namespace idc::gui {

ImageLayerItem::ImageLayerItem(QGraphicsItem* parent) : QGraphicsObject(parent) {
    setAcceptedMouseButtons(Qt::NoButton);   // 默认不可交互：不抢既有框选 / 选区 / 标注交互。
    setFlag(QGraphicsItem::ItemIsSelectable, false);
}

void ImageLayerItem::setPlacement(const QRectF& rect) {
    if (rect_ == rect) return;
    prepareGeometryChange();
    rect_ = rect;
    update();
}

void ImageLayerItem::setSelectedFrame(const bool on) {
    if (selected_ == on) return;
    selected_ = on;
    update();
}

void ImageLayerItem::setInteractive(const bool on) {
    if (interactive_ == on) return;
    interactive_ = on;
    // 图元自身恒不吃鼠标（拾取/拖拽由 CanvasScene 统一路由）：interactive_ 仅作拾取候选门控标志。
    update();
}

QRectF ImageLayerItem::boundingRect() const {
    // 外扩 2px 容纳描边。
    return rect_.adjusted(-2.0, -2.0, 2.0, 2.0);
}

void ImageLayerItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    if (!rect_.isValid()) return;
    QColor base = selected_ ? QColor(37, 99, 235)      // 选中：实线蓝
                            : QColor(14, 165, 233);    // 未选：青色虚线
    QPen pen(base);
    pen.setWidthF(selected_ ? 2.0 : 1.0);
    if (!selected_) {
        QVector<qreal> dashes;   // 非等比缩放：虚线以场景单位恒定（缩放下观感一致）。
        dashes << 6 << 4;
        pen.setDashPattern(dashes);
        pen.setCosmetic(true);
    }
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(rect_);
}

void ImageLayerItem::beginDrag(const QPointF& scenePos) {
    dragging_ = true;
    lastScenePos_ = scenePos;
    accumDelta_ = QPointF(0, 0);
    setPos(QPointF(0, 0));   // 复位任何遗留位移，从零开始本次拖拽的视觉偏移。
    emit pressed(id_);       // 交上层选中该图层。
}

void ImageLayerItem::dragMove(const QPointF& scenePos) {
    if (!dragging_) return;
    accumDelta_ += scenePos - lastScenePos_;
    lastScenePos_ = scenePos;
    setPos(accumDelta_);     // 仅视觉位移，不逐帧回写模型（释放时一次性提交）。
}

void ImageLayerItem::dragEnd(const QPointF& /*scenePos*/) {
    if (!dragging_) return;
    dragging_ = false;
    setPos(QPointF(0, 0));   // 复位视觉位移：真正位置由上层改 transform 后重组刷新。
    const int dx = static_cast<int>(std::lround(accumDelta_.x()));
    const int dy = static_cast<int>(std::lround(accumDelta_.y()));
    if (dx != 0 || dy != 0) emit moved(id_, dx, dy);
    update();
}

} // namespace idc::gui
