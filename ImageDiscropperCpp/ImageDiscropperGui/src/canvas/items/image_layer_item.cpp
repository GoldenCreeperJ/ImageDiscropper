// ============================================================================
// 文件：canvas/items/image_layer_item.cpp
// 作用：实现 ImageLayerItem（见同名头文件说明）——落位框绘制 + 拾取拖拽手势。
// 分块依据：渲染（boundingRect/paint）与送达翻译（onDragFinished）各成一块；无任何落位几何计算
//          （矩形来自 Core layerPlacement，经场景下发），手势循环在基类 DraggableCanvasItem，
//          符合「图元只渲染并拖动它」的分层纪律。
// ============================================================================
#include "canvas/items/image_layer_item.h"

#include <cmath>

#include <QPainter>
#include <QPen>

namespace idc::gui {

ImageLayerItem::ImageLayerItem(QGraphicsItem* parent) : DraggableCanvasItem(parent) {
    setAcceptedMouseButtons(Qt::NoButton);   // 默认不可交互：不抢既有框选 / 选区 / 标注交互。
    setFlag(ItemIsSelectable, false);
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
    // 拾取/拖拽仍由 CanvasScene 统一路由（场景按下分支在基类分发前处理，不依赖图元按键受理）；
    // 但图元必须吃左键：输入适配器以「是否接受左键」（interactiveItemAt）判定交互图元并即时路由——
    // 不吃左键则图层每次按下都被单击延迟判定扣住（双击窗口 kDoubleTapGapMs），拾取手感明显迟滞。
    setAcceptedMouseButtons(on ? Qt::LeftButton : Qt::NoButton);
    update();
}

QRectF ImageLayerItem::boundingRect() const {
    // 外扩 2px 容纳描边。
    return rect_.adjusted(-2.0, -2.0, 2.0, 2.0);
}

void ImageLayerItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    if (!rect_.isValid()) return;
    const QColor base = selected_ ? QColor(37, 99, 235)      // 选中：实线蓝
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

// 平移手势送达（基类钩子）：非原地点击→一次性发 moved(id, dxPx, dyPx) 交上层累加进 transform
//（先复位视觉位移再送达，由基类 endTranslationDrag 保证）；原地点击仅推进循环选层、不发 moved。
void ImageLayerItem::onDragFinished(const bool wasClick, const QPointF&, const double dx, const double dy) {
    if (!wasClick && (dx != 0.0 || dy != 0.0)) {
        emit moved(id_, static_cast<int>(std::lround(dx)), static_cast<int>(std::lround(dy)));
    }
}

} // namespace idc::gui
