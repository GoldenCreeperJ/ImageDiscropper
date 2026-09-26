// ============================================================================
// 文件：canvas/canvas_view.cpp
// 作用：实现画布交互（缩放/平移/框选/微调/右键菜单/光标回报），见同名头文件说明。
// 分块依据：
//   - 事件入口一行转发给输入适配器（GuideLine 阶段 2）：按键角色、滚轮刻度、右键菜单 UI
//     等设备语义全部在 DesktopInputAdapter（canvas_desktop_input_adapter.cpp）；
//   - 本文件的手势执行面（gesture* ）只含与输入设备无关的状态机与意图信号：
//     平移用手写滚动条位移，避免与选区拖拽争用 DragMode；框选仅在「无图元接管鼠标」
//     时启动（借 mouseGrabberItem() 判定），从而与选区框的手柄互不冲突；
//   - 键盘（Esc / 空格 / 方向键）是桌面专属通道，保留在本视图；移动端由补偿 UI 调同一 gesture*。
//   - 阶段 3：构造时按平台换装适配器（Android → TouchInputAdapter）；手势事件（pinch）
//     在 event() 入口交适配器消费；抓取带屏幕基准（updateHandleSize）改由适配器提供。
// 说明：所有手势都翻译成信号，视图不改任何业务状态（CONTRIBUTING.md「分层纪律」）。
// ============================================================================
#include "canvas/canvas_view.h"

#include <algorithm>
#include <memory>

#include <QColor>
#include <QGestureEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QRubberBand>
#include <QScrollBar>

#include "canvas/canvas_desktop_input_adapter.h"
#include "canvas/canvas_scene.h"
#include "canvas/canvas_touch_input_adapter.h"
#include "canvas/selection_rect_item.h"

namespace idc::gui {
namespace {
// 缩放倍率允许区间（宽松）：最小 5%、最大 4000%，防止无限缩小/放大导致
// 图像消失、手柄尺寸或变换数值失控（预留足够余量适配各种尺寸图像）。
constexpr qreal kMinZoom = 0.05;
constexpr qreal kMaxZoom = 40.0;
} // namespace

// 构造：抗锯齿、锚点、深灰背景、开启鼠标追踪（回报光标坐标）；按平台注入输入适配器。
CanvasView::CanvasView(CanvasScene* scene, QWidget* parent)
    : QGraphicsView(scene, parent), scene_(scene) {
    setRenderHint(QPainter::Antialiasing, true);
    setTransformationAnchor(AnchorUnderMouse); // 缩放锚定光标下
    setResizeAnchor(AnchorViewCenter);
    setDragMode(NoDrag);                     // 拖拽逻辑自管
    setMouseTracking(true);                                 // 无按键也回报光标位置
    setBackgroundBrush(QColor(45, 45, 45));                 // 画布深灰（本目录 README「画布视觉规范」）
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    input_ = std::make_unique<TouchInputAdapter>();         // 触控：合成鼠标路由复用桌面 + pinch/双击（SPEC §8.3）
#else
    input_ = std::make_unique<DesktopInputAdapter>();
#endif
    input_->attach(*this);                                  // 适配器自注册（如 grabGesture）
}

// 放大。
void CanvasView::zoomIn() { zoomBy(1.25); }
// 缩小。
void CanvasView::zoomOut() { zoomBy(0.8); }
// 复位到 1:1。
void CanvasView::resetZoom() { resetTransform(); updateHandleSize(); }
// 适应窗口。
void CanvasView::fitToWindow() {
    if (scene_ && !scene_->sceneRect().isEmpty()) {
        fitInView(scene_->sceneRect(), Qt::KeepAspectRatio);
        clampZoom(); // fit 结果可能超出倍率区间，钳制回边界。
    }
    updateHandleSize();
}

// 按倍数缩放并钳制到 [kMinZoom, kMaxZoom]：以绝对目标倍率换算增量，避免累积越界。
void CanvasView::zoomBy(const qreal factor) {
    const qreal cur = transform().m11();
    if (cur <= 0.0) return;
    if (const qreal next = std::clamp(cur * factor, kMinZoom, kMaxZoom); !qFuzzyCompare(next, cur)) scale(next / cur, next / cur); // 已达边界则不再缩放。
    updateHandleSize();
}

// 把当前变换的缩放钳制回允许区间（供 fitToWindow 等绝对变换后兜底）。
void CanvasView::clampZoom() {
    const qreal cur = transform().m11();
    if (cur <= 0.0) return;
    if (const qreal c = std::clamp(cur, kMinZoom, kMaxZoom); !qFuzzyCompare(c, cur)) scale(c / cur, c / cur);
}

// 依当前缩放换算选区手柄的场景尺寸（屏幕基准由适配器提供，桌面同既有的 8 屏幕px），
// 保证手柄屏幕观感恒定；触控下基准 ≥12pt 自动扩宽抓取带（SPEC §8.3，图元无需知晓）。
void CanvasView::updateHandleSize() {
    const qreal m11 = transform().m11();
    emit zoomChanged(m11);       // 回报当前缩放倍数（所有缩放入口都经过此函数）。
    if (!scene_ || m11 <= 0.0) return;
    const qreal hs = (input_ ? input_->screenOverlayPx() : 8.0) / m11;
    if (scene_->selectionItem()) scene_->selectionItem()->setHandleSize(hs); // 手柄与抓边条带同步随缩放换算。
    scene_->setMultiRectHandleSize(hs); // L2 多矩形选区框手柄同步随缩放换算。
    scene_->setAnnotationHandleSize(hs); // 标注控制点手柄同步随缩放换算。
    if (scene_->cellPickerItem()) scene_->cellPickerItem()->setOverlayScale(hs); // L3 点选阈值/重绘余量同步随缩放换算。
}

// 标注绘制态门控：开启时左键手势路由到标注绘制（橡皮筋选区让位）；关闭时复位绘制手势态。
void CanvasView::setAnnotationDrawActive(const bool on) {
    annotationDrawActive_ = on;
    if (!on) annoDrawing_ = false;
    viewport()->setCursor(on ? Qt::CrossCursor : Qt::ArrowCursor);
}

// 开始平移（手势执行面：置态 + 抓手光标）。
void CanvasView::gesturePanBegin(const QPoint& viewportPos) {
    panning_ = true;
    lastPanPos_ = viewportPos;
    viewport()->setCursor(Qt::ClosedHandCursor);
}

// ===========================================================================
// 事件入口：一行转发给输入适配器（设备语义只在适配器内存在）
// ===========================================================================

// 滚轮缩放（锚定光标下，倍率钳制在允许区间内）。
void CanvasView::wheelEvent(QWheelEvent* event) { input_->onWheel(*this, event); }

// 鼠标按下：交给适配器路由（平移 / 绘制 / 图元优先 / 框选）。
void CanvasView::mousePressEvent(QMouseEvent* event) { input_->onPress(*this, event); }

// 鼠标移动：交给适配器路由（平移 / 绘制 / 图元拖拽 / 框选更新 / 光标回报）。
void CanvasView::mouseMoveEvent(QMouseEvent* event) { input_->onMove(*this, event); }

// 鼠标释放：交给适配器路由（收平移 / 收绘制 / 收框选 / 交图元）。
void CanvasView::mouseReleaseEvent(QMouseEvent* event) { input_->onRelease(*this, event); }

// 右键：绘制态收笔 / 普通弹菜单，翻译逻辑在适配器；触控长按由 QPA 合成本事件（等价右键）。
void CanvasView::contextMenuEvent(QContextMenuEvent* event) { input_->onContextMenu(*this, event); }

// 手势事件（pinch 等）：交适配器消费；未消费（桌面适配器）照常下发基类。
bool CanvasView::event(QEvent* event) {
    if (event->type() == QEvent::Gesture && input_
        && input_->onGestureEvent(*this, dynamic_cast<QGestureEvent*>(event)))
        return true;
    return QGraphicsView::event(event);
}

// 键盘：空格切换平移预备态；方向键微调选区（Shift 大步）。
void CanvasView::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape && annotationDrawActive_) {
        emit annoEscape();   // 绘制态 Esc：交上层收笔（折线/画笔）或取消预览
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        spacePan_ = true;
        viewport()->setCursor(Qt::OpenHandCursor);
        event->accept();
        return;
    }
    const int step = event->modifiers() & Qt::ShiftModifier ? 10 : 1;
    switch (event->key()) {
        case Qt::Key_Left:  emit nudgeSelection(-step, 0); event->accept(); return;
        case Qt::Key_Right: emit nudgeSelection(step, 0);  event->accept(); return;
        case Qt::Key_Up:    emit nudgeSelection(0, -step); event->accept(); return;
        case Qt::Key_Down:  emit nudgeSelection(0, step);  event->accept(); return;
        default: break;
    }
    QGraphicsView::keyPressEvent(event);
}

// 键盘释放：空格抬起结束平移预备态。
void CanvasView::keyReleaseEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        spacePan_ = false;
        if (!panning_) viewport()->unsetCursor();
        event->accept();
        return;
    }
    QGraphicsView::keyReleaseEvent(event);
}

// ===========================================================================
// 业务手势执行面：与输入设备无关的状态机与意图信号（由输入适配器调用）
// ===========================================================================

// 平移更新：手写滚动条位移（不与图元 DragMode 争用），并记录上一视口坐标。
void CanvasView::gesturePanUpdate(const QPoint& viewportPos) {
    const int dx = viewportPos.x() - lastPanPos_.x();
    const int dy = viewportPos.y() - lastPanPos_.y();
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() - dx);
    verticalScrollBar()->setValue(verticalScrollBar()->value() - dy);
    lastPanPos_ = viewportPos;
}

// 平移结束：复位态并恢复光标。
void CanvasView::gesturePanEnd() {
    panning_ = false;
    viewport()->unsetCursor();
}

// 双指平移（触控）：按视口位移量直接滚动，与 gesturePanUpdate 同一执行链路，
// 不进入 panning_ 态（不依赖上一坐标，也不改光标）。
void CanvasView::gesturePanDelta(const QPoint& deltaViewport) const {
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() - deltaViewport.x());
    verticalScrollBar()->setValue(verticalScrollBar()->value() - deltaViewport.y());
}

// 标注起笔：进入绘制拖拽手势态并上报场景坐标（上层依工具分派）。
void CanvasView::gestureStrokeBegin(const QPoint& viewportPos) {
    annoDrawing_ = true;
    emit annoDragStart(mapToScene(viewportPos));
}

// 绘制拖拽：实时上报当前场景坐标（两点形状橡皮筋预览）。
void CanvasView::gestureStrokeUpdate(const QPoint& viewportPos) {
    const QPointF sp = mapToScene(viewportPos);
    emit annoDragMove(sp);
    emit cursorScenePos(sp);
}

// 绘制态悬停（未按键）：上报悬停点，供折线实时预览「落点 + 到光标的连线」橡皮筋。
// 折线为点击式（落点即顶点），顶点之间靠悬停预览连线，故未拖拽时也须持续上报。
void CanvasView::gestureStrokeHover(const QPoint& viewportPos) {
    const QPointF sp = mapToScene(viewportPos);
    emit annoHover(sp);
    emit cursorScenePos(sp);
}

// 绘制释放：结束拖拽手势并上报落点（两点形状提交 / 文字取文本）。
void CanvasView::gestureStrokeEnd(const QPoint& viewportPos) {
    annoDrawing_ = false;
    emit annoDragEnd(mapToScene(viewportPos));
}

// 收笔：绘制态右键（或等效手势）→ 交上层结束并提交折线 / 取消当前预览。
void CanvasView::gestureStrokeFinish() { emit annoFinish(); }

// 框选起带：记录起点并显示橡皮筋。
void CanvasView::gestureMarqueeBegin(const QPoint& viewportPos) {
    rubber_ = true;
    rubberStart_ = viewportPos;
    if (!rubberBand_) rubberBand_ = new QRubberBand(QRubberBand::Rectangle, viewport());
    rubberBand_->setGeometry(QRect(rubberStart_, QSize()));
    rubberBand_->show();
}

// 框选更新：以起点为一角伸缩橡皮筋。
void CanvasView::gestureMarqueeUpdate(const QPoint& viewportPos) const {
    if (rubberBand_) rubberBand_->setGeometry(QRect(rubberStart_, viewportPos).normalized());
}

// 框选收尾：隐藏橡皮筋；过滤误触（过小框选）；把视口矩形映射回场景/原图坐标后上报。
void CanvasView::gestureMarqueeEnd(const QPoint& viewportPos) {
    rubber_ = false;
    const QRect vr = QRect(rubberStart_, viewportPos).normalized();
    if (rubberBand_) rubberBand_->hide();
    if (vr.width() > 3 && vr.height() > 3) {
        const QPointF tl = mapToScene(vr.topLeft());
        const QPointF br = mapToScene(vr.bottomRight());
        emit rubberSelect(QRectF(tl, br).normalized());
    }
}

// 非绘制态悬停移动：回报光标场景坐标（状态栏）；图元事件交付由适配器 dispatchSceneMove 完成。
void CanvasView::gestureCursorMoved(const QPoint& viewportPos) {
    emit cursorScenePos(mapToScene(viewportPos));
}

// 步进缩放：适配器只给倍率；钳制与手柄重同步归 zoomBy。
void CanvasView::gestureStepZoom(const qreal factor) { zoomBy(factor); }

// 右键菜单项意图代理：菜单 UI 在桌面适配器，此处仅补发既有意图信号（MainWindow 接线不变）。
void CanvasView::requestClearCut() { emit clearCutRequested(); }
void CanvasView::requestResetView() { emit resetViewRequested(); }
void CanvasView::requestToggleMasks() { emit toggleMasksRequested(); }

// 无键盘补偿：步进盘/取消按钮与键盘同一信号链路（方向键微调 → nudgeSelection；
// 绘制态 Esc → annoEscape），移动端不新增任何行为分支（SPEC §8.3）。
void CanvasView::requestNudge(const int dx, const int dy) { emit nudgeSelection(dx, dy); }
void CanvasView::requestAnnoEscape() { emit annoEscape(); }

} // namespace idc::gui
