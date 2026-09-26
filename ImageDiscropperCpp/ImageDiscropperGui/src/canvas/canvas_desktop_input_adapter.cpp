// ============================================================================
// 文件：canvas/canvas_desktop_input_adapter.cpp
// 作用：DesktopInputAdapter 实现——桌面鼠标 / 滚轮 / 右键事件到 CanvasView 业务
//       手势的翻译。逻辑自 canvas_view.cpp 原事件处理器逐条迁移（GuideLine 阶段 2
//       验收基线：行为零变化），映射表见同名头文件。
// 分块依据：本文件是画布交互链路中**唯一**允许出现 Qt::LeftButton / MiddleButton、
//           滚轮刻度与 QMenu 等处方的地方；一切状态与信号发射都委托 view 的 gesture*。
// 说明：平移中 / 绘制中 / 框选中的互斥路由靠 view 的状态查询（panning() 等）完成，
//       顺序与原实现一致：平移 → 绘制拖拽 → 绘制悬停 → 交付场景图元 + 框选更新。
// ============================================================================
#include "canvas/canvas_desktop_input_adapter.h"

#include <QContextMenuEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QWheelEvent>

#include "canvas/canvas_scene.h"
#include "canvas/canvas_view.h"

namespace idc::gui {
namespace {

// 滚轮缩放步长（桌面专属设备语义）：一格 = ×1.15 / 反向 ÷1.15（原 wheelEvent 常量）。
constexpr double kWheelZoomFactor = 1.15;

} // namespace

// 按下：中键 / 空格+左键 → 平移；左键（绘制态）→ 标注起笔；左键（普通）→ 先交场景，
// 无图元接管则起框选带；其余键原样交付场景基类。
void DesktopInputAdapter::onPress(CanvasView& view, QMouseEvent* event) {
    const QPoint vp = event->position().toPoint();

    if (event->button() == Qt::MiddleButton ||
        (event->button() == Qt::LeftButton && view.panArmed())) {
        view.gesturePanBegin(vp);
        event->accept();
        return;
    }

    // 标注绘制态：左键按下即路由到标注绘制（不交付图元 / 不启动橡皮筋选区）。
    if (event->button() == Qt::LeftButton) {
        if (view.annotationDrawActive()) {
            view.gestureStrokeBegin(vp);
        } else {
            view.dispatchScenePress(event); // 交付场景：选区框移动/缩放手柄优先。
            if (view.scene() && view.scene()->mouseGrabberItem() == nullptr) {
                view.gestureMarqueeBegin(vp);   // 无图元接管 → 启动框选新选区。
            }
        }
        event->accept();
        return;
    }

    view.dispatchScenePress(event);
}

// 移动：平移 → 绘制拖拽 → 绘制悬停（未按键）→ 交付抓取图元 + 框选带更新 + 光标回报。
void DesktopInputAdapter::onMove(CanvasView& view, QMouseEvent* event) {
    const QPoint vp = event->position().toPoint();

    if (view.panning()) {
        view.gesturePanUpdate(vp);
        event->accept();
        return;
    }

    // 标注绘制拖拽：实时上报当前场景坐标（两点形状橡皮筋预览）。
    if (view.strokeActive()) {
        view.gestureStrokeUpdate(vp);
        event->accept();
        return;
    }

    // 标注绘制态但未按键（悬停）：上报悬停点，供折线实时预览「落点 + 到光标的连线」橡皮筋。
    if (view.annotationDrawActive()) {
        view.gestureStrokeHover(vp);
        event->accept();
        return;
    }

    view.dispatchSceneMove(event); // 交付抓取图元（选区拖拽实时刷新遮罩）。
    if (view.marqueeActive()) {
        view.gestureMarqueeUpdate(vp);
    }
    view.gestureCursorMoved(vp); // 状态栏坐标回报。
}

// 释放：结束平移 / 收笔绘制 / 完成框选（均限对应键），否则交付场景图元。
void DesktopInputAdapter::onRelease(CanvasView& view, QMouseEvent* event) {
    const QPoint vp = event->position().toPoint();

    if (view.panning() && (event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton)) {
        view.gesturePanEnd();
        event->accept();
        return;
    }

    // 标注绘制释放：结束拖拽手势并上报落点（两点形状提交 / 文字取文本）。
    if (view.strokeActive() && event->button() == Qt::LeftButton) {
        view.gestureStrokeEnd(vp);
        event->accept();
        return;
    }

    if (view.marqueeActive() && event->button() == Qt::LeftButton) {
        view.gestureMarqueeEnd(vp);
        event->accept();
        return;
    }

    view.dispatchSceneRelease(event);
}

// 滚轮：一格 ×1.15 / ÷1.15 的步进缩放（锚定光标下由视图变换锚点保证）。
void DesktopInputAdapter::onWheel(CanvasView& view, QWheelEvent* event) {
    view.gestureStepZoom(event->angleDelta().y() > 0 ? kWheelZoomFactor : 1.0 / kWheelZoomFactor);
    event->accept();
}

// 右键：绘制态＝收笔（不弹菜单）；普通＝弹上下文菜单（清除切割线 / 重置视图 / 切换遮罩），
// 菜单项命中后转发为视图的既有意图信号。
void DesktopInputAdapter::onContextMenu(CanvasView& view, QContextMenuEvent* event) {
    if (view.annotationDrawActive()) {
        view.gestureStrokeFinish();
        event->accept();
        return;
    }
    QMenu menu(view.viewport());
    const QAction* aClear = menu.addAction(QStringLiteral("清除切割线"));
    const QAction* aReset = menu.addAction(QStringLiteral("重置视图"));
    const auto* scene = dynamic_cast<CanvasScene*>(view.scene());
    const bool masksOn = scene && scene->masksVisible();
    const QAction* aToggle = menu.addAction(masksOn ? QStringLiteral("隐藏预览遮罩")
                                              : QStringLiteral("显示预览遮罩"));
    if (const QAction* chosen = menu.exec(event->globalPos()); chosen == aClear) view.requestClearCut();
    else if (chosen == aReset) view.requestResetView();
    else if (chosen == aToggle) view.requestToggleMasks();
}

} // namespace idc::gui
