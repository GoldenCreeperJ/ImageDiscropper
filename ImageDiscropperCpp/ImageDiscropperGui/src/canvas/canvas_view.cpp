// ============================================================================
// 文件：canvas/canvas_view.cpp
// 作用：实现画布交互（缩放/平移/框选/微调/右键菜单/光标回报），见同名头文件说明。
// 分块依据：
//   - 平移用手写滚动条位移（中键 / 空格+左键），避免与选区拖拽争用 DragMode；
//   - 框选仅在「无图元接管鼠标」时启动（借 mouseGrabberItem() 判定），从而与选区框的
//     移动/缩放手柄互不冲突；
//   - 所有手势都翻译成信号，视图不改任何业务状态（A-0.1）。
// ============================================================================
#include "canvas/canvas_view.h"

#include <algorithm>

#include <QColor>
#include <QKeyEvent>
#include <QMenu>
#include <QPainter>
#include <QRubberBand>
#include <QScrollBar>

#include "canvas/canvas_scene.h"
#include "canvas/selection_rect_item.h"

namespace idc::gui {
namespace {
// 缩放倍率允许区间（宽松）：最小 5%、最大 4000%，防止无限缩小/放大导致
// 图像消失、手柄尺寸或变换数值失控（预留足够余量适配各种尺寸图像）。
constexpr qreal kMinZoom = 0.05;
constexpr qreal kMaxZoom = 40.0;
} // namespace

// 构造：抗锯齿、锚点、深灰背景、开启鼠标追踪（回报光标坐标）。
CanvasView::CanvasView(CanvasScene* scene, QWidget* parent)
    : QGraphicsView(scene, parent), scene_(scene) {
    setRenderHint(QPainter::Antialiasing, true);
    setTransformationAnchor(AnchorUnderMouse); // 缩放锚定光标下
    setResizeAnchor(AnchorViewCenter);
    setDragMode(NoDrag);                     // 拖拽逻辑自管
    setMouseTracking(true);                                 // 无按键也回报光标位置
    setBackgroundBrush(QColor(45, 45, 45));                 // 画布深灰（§5.1）
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

// 依当前缩放换算选区手柄的场景尺寸（约 8 屏幕px），保证手柄屏幕观感恒定。
void CanvasView::updateHandleSize() {
    const qreal m11 = transform().m11();
    emit zoomChanged(m11);       // 回报当前缩放倍数（所有缩放入口都经过此函数）。
    if (!scene_ || m11 <= 0.0) return;
    const qreal hs = 8.0 / m11;
    if (scene_->selectionItem()) scene_->selectionItem()->setHandleSize(hs); // 手柄与抓边条带同步随缩放换算。
    scene_->setMultiRectHandleSize(hs); // L2 多矩形选区框手柄同步随缩放换算。
    scene_->setAnnotationHandleSize(hs); // 标注控制点手柄同步随缩放换算。
    if (scene_->cellPickerItem()) scene_->cellPickerItem()->setOverlayScale(hs); // L3 点选阈值/边框同步随缩放换算。
}

// 标注绘制态门控：开启时左键手势路由到标注绘制（橡皮筋选区让位）；关闭时复位绘制手势态。
void CanvasView::setAnnotationDrawActive(const bool on) {
    annotationDrawActive_ = on;
    if (!on) annoDrawing_ = false;
    viewport()->setCursor(on ? Qt::CrossCursor : Qt::ArrowCursor);
}

// 开始平移。
void CanvasView::beginPan(const QPoint& viewportPos) {
    panning_ = true;
    lastPanPos_ = viewportPos;
    viewport()->setCursor(Qt::ClosedHandCursor);
}

// 滚轮缩放（锚定光标下，倍率钳制在允许区间内）。
void CanvasView::wheelEvent(QWheelEvent* event) {
    constexpr double factor = 1.15;
    zoomBy(event->angleDelta().y() > 0 ? factor : 1.0 / factor);
    event->accept();
}

// 鼠标按下：中键/空格+左键平移；左键先交图元，未接管则框选。
void CanvasView::mousePressEvent(QMouseEvent* event) {
    const QPoint vp = event->position().toPoint();

    if (event->button() == Qt::MiddleButton ||
        (event->button() == Qt::LeftButton && spacePan_)) {
        beginPan(vp);
        event->accept();
        return;
    }

    // 标注绘制态：左键按下即路由到标注绘制（不交付图元 / 不启动橡皮筋选区）。
    if (event->button() == Qt::LeftButton && annotationDrawActive_) {
        annoDrawing_ = true;
        emit annoDragStart(mapToScene(vp));
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton) {
        QGraphicsView::mousePressEvent(event); // 交付场景：选区框移动/缩放手柄优先。
        if (scene_ && scene_->mouseGrabberItem() == nullptr) {
            // 无图元接管 → 启动框选新选区。
            rubber_ = true;
            rubberStart_ = vp;
            if (!rubberBand_) rubberBand_ = new QRubberBand(QRubberBand::Rectangle, viewport());
            rubberBand_->setGeometry(QRect(rubberStart_, QSize()));
            rubberBand_->show();
        }
        event->accept();
        return;
    }

    QGraphicsView::mousePressEvent(event);
}

// 鼠标移动：平移 / 交付抓取图元 / 更新框选带 / 回报光标坐标。
void CanvasView::mouseMoveEvent(QMouseEvent* event) {
    const QPoint vp = event->position().toPoint();

    if (panning_) {
        const int dx = vp.x() - lastPanPos_.x();
        const int dy = vp.y() - lastPanPos_.y();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - dx);
        verticalScrollBar()->setValue(verticalScrollBar()->value() - dy);
        lastPanPos_ = vp;
        event->accept();
        return;
    }

    // 标注绘制拖拽：实时上报当前场景坐标（两点形状橡皮筋预览）。
    if (annoDrawing_) {
        const QPointF sp = mapToScene(vp);
        emit annoDragMove(sp);
        emit cursorScenePos(sp);
        event->accept();
        return;
    }

    // 标注绘制态但未按键（悬停）：上报悬停点，供折线实时预览「落点 + 到光标的连线」橡皮筋。
    // 折线为点击式（左键落顶点），顶点之间靠悬停预览连线，故须在未拖拽时也持续上报。
    if (annotationDrawActive_) {
        const QPointF sp = mapToScene(vp);
        emit annoHover(sp);
        emit cursorScenePos(sp);
        event->accept();
        return;
    }

    QGraphicsView::mouseMoveEvent(event); // 交付抓取图元（选区拖拽实时刷新遮罩）。

    if (rubber_ && rubberBand_) {
        rubberBand_->setGeometry(QRect(rubberStart_, vp).normalized());
    }
    emit cursorScenePos(mapToScene(vp)); // 状态栏坐标。
}

// 鼠标释放：结束平移 / 完成框选 / 交付图元。
void CanvasView::mouseReleaseEvent(QMouseEvent* event) {
    const QPoint vp = event->position().toPoint();

    if (panning_ && (event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton)) {
        panning_ = false;
        viewport()->unsetCursor();
        event->accept();
        return;
    }

    // 标注绘制释放：结束拖拽手势并上报落点（两点形状提交 / 文字取文本）。
    if (annoDrawing_ && event->button() == Qt::LeftButton) {
        annoDrawing_ = false;
        emit annoDragEnd(mapToScene(vp));
        event->accept();
        return;
    }

    if (rubber_ && event->button() == Qt::LeftButton) {
        rubber_ = false;
        const QRect vr = QRect(rubberStart_, vp).normalized();
        if (rubberBand_) rubberBand_->hide();
        // 过滤误触（过小框选）；把视口矩形映射回场景/原图坐标后上报。
        if (vr.width() > 3 && vr.height() > 3) {
            const QPointF tl = mapToScene(vr.topLeft());
            const QPointF br = mapToScene(vr.bottomRight());
            emit rubberSelect(QRectF(tl, br).normalized());
        }
        event->accept();
        return;
    }

    QGraphicsView::mouseReleaseEvent(event);
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

// 右键菜单：清除切割线 / 重置视图 / 切换预览遮罩。
// 标注绘制态下右键＝收笔 / 退出当前绘制手势（折线在此结束并提交），不弹视图菜单。
void CanvasView::contextMenuEvent(QContextMenuEvent* event) {
    if (annotationDrawActive_) {
        emit annoFinish();
        event->accept();
        return;
    }
    QMenu menu(viewport());
    const QAction* aClear = menu.addAction(QStringLiteral("清除切割线"));
    const QAction* aReset = menu.addAction(QStringLiteral("重置视图"));
    const bool masksOn = scene_ && scene_->masksVisible();
    const QAction* aToggle = menu.addAction(masksOn ? QStringLiteral("隐藏预览遮罩")
                                              : QStringLiteral("显示预览遮罩"));
    const QAction* chosen = menu.exec(event->globalPos());
    if (chosen == aClear) emit clearCutRequested();
    else if (chosen == aReset) emit resetViewRequested();
    else if (chosen == aToggle) emit toggleMasksRequested();
}

} // namespace idc::gui
