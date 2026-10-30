// ============================================================================
// 文件：canvas/canvas_touch_input_adapter.cpp
// 作用：TouchInputAdapter 实现——触控设备语义 → CanvasView 业务手势的翻译。
//       设计取舍与不变式见同名头文件；本文件是「几根手指」知识唯一的存放处之一
//       （另一处是桌面适配器的按键路由，两者产出等价业务手势，SPEC §8.3 行为同构）。
// ============================================================================
#include "canvas/canvas_touch_input_adapter.h"

#include <QContextMenuEvent>
#include <QGesture>
#include <QGestureEvent>
#include <QMouseEvent>
#include <QPinchGesture>
#include <QTapAndHoldGesture>
#include <QWheelEvent>

#include "canvas/canvas_view.h"
#include "util/gui_nums.h"

namespace idc::gui {

qreal TouchInputAdapter::screenOverlayPx() const { return nums::canvas::kTouchOverlayScreenPx; }

// 触控装配：①注册捏合手势——双指缩放/平移由此进入 onGestureEvent（单指仍走 QPA 合成鼠标序列）；
// ②注册长按手势（Qt::TapAndHoldGesture）——长按与捏合共用同一套 QGestureEvent 识别/投递机制；
//   Widgets 框架不会据长按识别自动产出 QContextMenuEvent，故长按菜单由本类 onGestureEvent
//   自行处理（GestureFinished 时弹 CanvasView::showCanvasContextMenu）。
// ③防误触：关闭 hover 鼠标跟踪——触控无持续悬停，若不关则拖拽途中的
//   合成 move 会逐帧触发悬停拾取/光标切换，坐标回显抖动且易误变可交互态；按下拖拽序列不受影响。
//   画布手势与抽屉面板滚动的分区由 widget 边界天然保证：触控事件不跨 viewport/抽屉传播，
//   捏合/长按也仅 view 单点 grabGesture，面板内滑动只驱动其 QScrollArea 滚动。
void TouchInputAdapter::attach(CanvasView& view) {
    view.grabGesture(Qt::PinchGesture);
    view.grabGesture(Qt::TapAndHoldGesture);
    view.setMouseTracking(false);
}

// 按下：先做双击过滤（第二击 = 适应窗口并吞掉本序列），否则记轻点候选并委托桌面路由（后者的
//   扣单击延迟判定同样只对非绘制态的空白落点生效）。
void TouchInputAdapter::onPress(CanvasView& view, QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        const QPoint vp = event->pos();
        // 双击第二击：距上一有效轻点足够近且足够快且落在**非交互图元**处且处于**非绘制态** → 适应窗口；
        // 吞掉本序列后续事件，不进入框选/图元路由。绘制态（AnnoCreate）的按下是落顶点/起笔的动作手势，
        // 扣住会推迟成形、快速连点还会被误判成双击而丢点，故照常即时路由——双击＝适应窗口覆盖
        // 框选/拾取/标注选取三态的空白（与桌面路由同一门控口径）。交互图元（选区框/落位框/图层/标注
        // 等接收左键者）上的双击略过适应窗口——按下照常路由（桌面路由内交互图元即时下发）。
        if (tapClock_.isValid() && tapClock_.elapsed() <= nums::canvas::kDoubleTapGapMs
            && (vp - lastTapPos_).manhattanLength() <= nums::canvas::kDoubleTapPosScreenPx
            && view.canvasMode() != CanvasMode::AnnoCreate
            && !view.interactiveItemAt(event->position())) {
            swallowing_ = true;
            tapClock_.invalidate();          // 已消费，防止三连击连续触发
            router_.cancelPendingClick();    // 双击成立：等待中的单击不落地
            view.fitToWindow();
            event->accept();
            return;
        }
        pressPos_ = vp;
        tapCandidate_ = true;
        gestureSeen_ = false;
        pressClock_.restart();
    }
    router_.onPress(view, event);
}

// 双击正主：QPA 把间隔小于系统双击阈值的第二击合成为 MouseButtonDblClick（不是第二次 press），
//   挂在 press 上的自检路径等不到它——此处先按落点消歧：交互图元（图层/标注/选区框）上**略过双击**，
//   按普通点击补发（连击循环照常推进）；底图/空白上（非绘制态）：窗口内首击未落地 → 双击成立，适应窗口并作废
//   单击；首击已落地（间隔 ≥ kDoubleTapGapMs 的中速连击，或绘制态根本不扣单击）→ 同样补发普通点击。
//   吞掉本击的合成 release（press 未入桌面路由，不成对）。
void TouchInputAdapter::onDoubleClick(CanvasView& view, QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) { event->ignore(); return; }
    if (view.interactiveItemAt(event->position())) {
        // 交互图元上的双击略过适应窗口：普通点击补发（连击循环照常推进）。
        tapClock_.restart();             // 本击按普通轻点落地：重启轻点时钟（可与后续轻点再组双击）
        swallowing_ = true;              // 吞掉本击的合成 release（点击已由合成对补发，不成对）
        router_.onDoubleClick(view, event);
    } else if (router_.pendingClickActive()) {
        tapClock_.invalidate();
        swallowing_ = true;
        router_.cancelPendingClick();    // 双击成立：等待中的单击不落地（单击/双击不再同时生效）
        view.fitToWindow();
    } else {
        tapClock_.restart();
        swallowing_ = true;
        router_.onDoubleClick(view, event);
    }
    event->accept();
}

// 取消等待中的单击（长按/捏合/双击与视图取消路径共用）：扣在桌面路由里的单击不得再补发。
void TouchInputAdapter::cancelPendingClick() {
    router_.cancelPendingClick();
}

// 移动：吞序列或直接吞；否则更新轻点候选（大幅移动即作废）并委托桌面路由。
void TouchInputAdapter::onMove(CanvasView& view, QMouseEvent* event) {
    if (swallowing_) { event->accept(); return; }
    if (tapCandidate_
        && (event->pos() - pressPos_).manhattanLength() > nums::canvas::kTapMoveScreenPx)
        tapCandidate_ = false;
    router_.onMove(view, event);
}

// 释放：吞序列收尾；有效轻点则记录时刻/落点供下一次按下做双击判定；否则委托桌面路由。
void TouchInputAdapter::onRelease(CanvasView& view, QMouseEvent* event) {
    if (swallowing_) {
        swallowing_ = false;                 // 序列结束，恢复正常路由
        router_.clearSwallowNextRelease();   // 本层吞掉的释放不得让桌面路由的吞释放标志残留到下一击
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton) {
        const bool isTap = tapCandidate_ && !gestureSeen_
            && pressClock_.isValid() && pressClock_.elapsed() <= nums::canvas::kTapMaxMs;
        if (isTap) {
            lastTapPos_ = event->pos();
            tapClock_.restart();
        } else {
            tapClock_.invalidate();          // 拖拽/长按收尾后重新开始双击计数
        }
        tapCandidate_ = false;
    }
    router_.onRelease(view, event);
}

// 触控无滚轮：不消费（若外接鼠标滚轮事件到达，ignore 交回默认处理，行为不受扰）。
void TouchInputAdapter::onWheel(CanvasView& /*view*/, QWheelEvent* event) {
    event->ignore();
}

// 长按菜单：本适配器已走 TapAndHold 手势自行弹菜单（见 onGestureEvent），正常不产生 QContextMenuEvent；
// 仅当外部真鼠标右键（或未来 QPA 确实合成本事件）时才进此处 → 委托桌面路由，与长按共用同一份菜单。
void TouchInputAdapter::onContextMenu(CanvasView& view, QContextMenuEvent* event) {
    router_.onContextMenu(view, event);
}

// 双指捏合：scaleFactor 为相对上次事件的增量 → gestureStepZoom（钳制复用桌面链路）；
// 中心点位移差 → gesturePanDelta（SPEC §8.3 双指平移）；出现手势即作废本序列 tap 判定。
bool TouchInputAdapter::onGestureEvent(CanvasView& view, QGestureEvent* event) {
    // 长按识别（TapAndHold）：与捏合同一管线，由本类自行合成菜单——Widgets 不自动把长按识别
    // 转为 QContextMenuEvent。gestureSeen_ 置位使本序列不再被当作轻点/双击（同捏合，防长按误触发适应窗口）；
    // **Cancelled 除外**——抬指未达长按时限时识别器恒发 Cancelled，若一并置位，每次普通轻点都会被
    // 污染判定、双击永远不成立（真机双击失效的根因之一）。
    if (const auto* hold = dynamic_cast<QTapAndHoldGesture*>(event->gesture(Qt::TapAndHoldGesture))) {
        if (hold->state() != Qt::GestureCanceled) {
            gestureSeen_ = true;
            router_.cancelPendingClick();   // 长按不是单击：等待中的单击不落地（也免去按住期间定时器补发）
        }
        if (hold->state() == Qt::GestureFinished) {
            view.gestureCancelCurrent();   // 清长按前落下的合成左键 press 副作用（框选/反选/图元抓取）。
            if (view.annotationDrawActive())
                view.gestureStrokeFinish();   // 绘制态：长按＝收笔（等价桌面右键）。
            else
                view.showCanvasContextMenu(view.mapToGlobal(hold->position().toPoint()));
        }
        event->accept();
        return true;
    }
    const auto* pinch = dynamic_cast<QPinchGesture*>(event->gesture(Qt::PinchGesture));
    if (!pinch) return false;
    if (!gestureSeen_ && pinch->state() != Qt::GestureCanceled) {
        gestureSeen_ = true;
        router_.cancelPendingClick();   // 捏合不是单击：等待中的单击不落地
        // 捏合开始：取消第一指可能已启动的框选/平移（QPA 在识别手势前已把第一指
        // 合成鼠标事件进入桌面路由——不取消则缩放时橡皮筋/画布跟着动）。
        view.gestureCancelCurrent();
    }
    if (pinch->changeFlags() & QPinchGesture::ScaleFactorChanged)
        view.gestureStepZoom(pinch->scaleFactor());
    if (pinch->changeFlags() & QPinchGesture::CenterPointChanged) {
        if (const QPointF delta = pinch->centerPoint() - pinch->lastCenterPoint(); !delta.isNull())
            view.gesturePanDelta(QPoint(qRound(delta.x()), qRound(delta.y())));
    }
    event->accept();
    return true;
}

} // namespace idc::gui
