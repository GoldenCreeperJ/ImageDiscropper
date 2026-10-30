
// ============================================================================
// 文件：canvas/items/draggable_canvas_item.cpp
// 作用：实现 DraggableCanvasItem（见同名头文件说明）——平移手势循环与 setPos 视觉反馈；
//       送达翻译由子类 onDragFinished 完成（本文件不触及任何子类输出形式）。
// ============================================================================
#include "canvas/items/draggable_canvas_item.h"

namespace idc::gui {

DraggableCanvasItem::DraggableCanvasItem(QGraphicsItem* parent) : QGraphicsObject(parent) {}

void DraggableCanvasItem::beginTranslationDrag(const QPointF& scenePos) {
    gesture_.start(scenePos.x(), scenePos.y());
    setPos(QPointF(0, 0));   // 复位任何遗留视觉位移：本次手势从零起算。
}

void DraggableCanvasItem::dragMoveTo(const QPointF& scenePos) {
    if (!gesture_.active()) return;
    gesture_.moveTo(scenePos.x(), scenePos.y());
    applyVisualOffset();     // 仅视觉位移，不回写模型（释放时一次性提交）。
}

bool DraggableCanvasItem::endTranslationDrag(const QPointF& scenePos) {
    if (!gesture_.active()) return false;
    const auto [wasClick, dx, dy] = gesture_.end();
    setPos(QPointF(0, 0));   // 复位视觉位移：真正位置由上层改模型后重组刷新（拖拽安全：先复位再送达）。
    onDragFinished(wasClick, scenePos, dx, dy);
    return wasClick;
}

// 外部打断收尾：与 endTranslationDrag 同一复位路径，但不取结果也不送达——位移只挂在 item 位置上
//（从未回写模型），归零即彻底撤销本次平移，上层无需知道发生过拖动。
void DraggableCanvasItem::cancelTranslationDrag() {
    if (!gesture_.active()) return;
    gesture_.cancel();
    setPos(QPointF(0, 0));
}

void DraggableCanvasItem::onDragFinished(bool, const QPointF&, double, double) {}

void DraggableCanvasItem::applyVisualOffset() {
    setPos(QPointF(gesture_.accX(), gesture_.accY()));
}

} // namespace idc::gui
