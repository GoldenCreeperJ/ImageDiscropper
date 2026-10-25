// ============================================================================
// 文件：canvas/items/draggable_canvas_item.h
// 作用：可平移拖拽图元的共享基类——把「场景驱动的平移手势」整段循环（moved 判定 / 位移累积 /
//       setPos 视觉反馈 / 起收复位）一次性收进基类；子类只留送达翻译（onDragFinished 钩子）。
// 分块依据：拖拽安全模式（拖动期仅 setPos 反馈、释放先复位 pos 再送达，CONTRIBUTING.md「拖拽安全」）
//       是各可拖图元共同的手势骨架；手势数学收在零 Qt 的 DragGesture 值类型（可被 CTest 直编回归）。
//       送达形式（信号 vs 返回值）不统一（标注→clicked/moveRequested 信号，图层→moved 信号 +
//       场景消费 wasClick 返回值），故不设统一送达契约：基类只把纯数据 (wasClick, scenePos, dx, dy)
//       交给子类钩子，各子类自己翻译成自身输出。
// 说明：本类继承 QGraphicsObject（含 Q_OBJECT）；AnnotationItem / ImageLayerItem 改由此继承。
// ============================================================================
#pragma once

#include <QGraphicsObject>
#include <QPointF>

#include "canvas/drag_gesture.h"

namespace idc::gui {

// ---------------------------------------------------------------------------
// DraggableCanvasItem：可平移拖拽图元基类（手势循环 + 视觉反馈）。
// ---------------------------------------------------------------------------
class DraggableCanvasItem : public QGraphicsObject {
    Q_OBJECT
public:
    explicit DraggableCanvasItem(QGraphicsItem* parent = nullptr);

    // ---- 场景驱动的平移拖拽三段（按下归属由场景决定，本类只执行手势）----
    // 起手势：复位遗留视觉位移，从本场景坐标起算。
    void beginTranslationDrag(const QPointF& scenePos);
    // 逐帧：累加位移并 setPos 视觉反馈（不回写模型）；收到过非零位移即置 moved（点击/拖动判定）。
    void dragMoveTo(const QPointF& scenePos);
    // 收手势：复位视觉位移（先复位再送达），把 {wasClick, scenePos, dx, dy} 交子类送达钩子，
    //   返回本次是否为原地点击（场景据此决定是否推进同点连击循环）。
    bool endTranslationDrag(const QPointF& scenePos);

    // 是否正处于平移拖拽手势中（供上层在拖拽期跳过对本图元的几何回设）。
    virtual bool isDragging() const { return gesture_.active(); }

protected:
    // 送达钩子：手势收尾的纯数据（含释放点场景坐标），由子类翻译成自己的输出——
    //   标注：wasClick→clicked(scenePos)，否则→moveRequested(dx,dy)；
    //   图层：非点击→moved(id, dxPx, dyPx)；返回值 wasClick 已由 endTranslationDrag 给出。
    virtual void onDragFinished(bool wasClick, const QPointF& scenePos, double dx, double dy);

    DragGesture gesture_;   // 手势数学（零 Qt）：moved 判定与位移累积（见 drag_gesture.h）。

private:
    // 手势中把累计位移搬上 item 位置（纯视觉，见 dragMoveTo / endTranslationDrag 实现）。
    void applyVisualOffset();
};

} // namespace idc::gui
