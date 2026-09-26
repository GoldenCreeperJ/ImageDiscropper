// ============================================================================
// 文件：canvas/canvas_touch_input_adapter.cpp
// 作用：TouchInputAdapter 实现——触控设备语义 → CanvasView 业务手势的翻译（阶段 3）。
//       设计取舍与不变式见同名头文件；本文件是「几根手指」知识唯一的存放处之一
//       （另一处是桌面适配器的按键路由，两者产出等价业务手势，SPEC §8.3 行为同构）。
// ============================================================================
#include "canvas/canvas_touch_input_adapter.h"

#include <QContextMenuEvent>
#include <QGesture>
#include <QGestureEvent>
#include <QMouseEvent>
#include <QPinchGesture>
#include <QWheelEvent>

#include "canvas/canvas_view.h"

namespace idc::gui {
namespace {

// 抓取带屏幕基准：12 逻辑 px（Android 上逻辑 px≈dp≈pt），SPEC §8.3 触控精度下限。
constexpr qreal kTouchOverlayPx = 12.0;

// 双击（= 适应窗口）判定阈值：两击间隔、落点容差、单击最长时长与移动容差。
constexpr qint64 kDoubleTapGapMs = 300;   // 第二击按下距上一击释放的上限
constexpr int    kDoubleTapPosPx = 64;    // 两击落点曼哈顿距离容差（手指抖动余量）
constexpr qint64 kTapMaxMs       = 250;   // 轻点的最长按下时长
constexpr int    kTapMovePx      = 8;     // 轻点允许的移动（超出即视为拖拽，不作双击候选）

} // namespace

qreal TouchInputAdapter::screenOverlayPx() const { return kTouchOverlayPx; }

// 触控装配：①注册捏合手势——双指缩放/平移由此进入 onGestureEvent（单指仍走 QPA 合成鼠标序列）；
// ②防误触（GuideLine 阶段 4）：关闭 hover 鼠标跟踪——触控无持续悬停，若不关则拖拽途中的
//   合成 move 会逐帧触发悬停拾取/光标切换，坐标回显抖动且易误变可交互态；按下拖拽序列不受影响。
//   画布手势与抽屉面板滚动的分区由 widget 边界天然保证：触控事件不跨 viewport/抽屉传播，
//   捏合也仅 view 单点 grabGesture，面板内滑动只驱动其 QScrollArea 滚动。
void TouchInputAdapter::attach(CanvasView& view) {
    view.grabGesture(Qt::PinchGesture);
    view.setMouseTracking(false);
}

// 按下：先做双击过滤（第二击 = 适应窗口并吞掉本序列），否则记轻点候选并委托桌面路由。
void TouchInputAdapter::onPress(CanvasView& view, QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        const QPoint vp = event->pos();
        // 双击第二击：距上一有效轻点足够近且足够快 → 适应窗口；吞掉本序列后续事件，
        // 不再进入框选/图元路由（桌面双击无绑定语义，移动端 SPEC 映射为适应窗口）。
        if (tapClock_.isValid() && tapClock_.elapsed() <= kDoubleTapGapMs
            && (vp - lastTapPos_).manhattanLength() <= kDoubleTapPosPx) {
            swallowing_ = true;
            tapClock_.invalidate();          // 已消费，防止三连击连续触发
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

// 移动：吞序列或直接吞；否则更新轻点候选（大幅移动即作废）并委托桌面路由。
void TouchInputAdapter::onMove(CanvasView& view, QMouseEvent* event) {
    if (swallowing_) { event->accept(); return; }
    if (tapCandidate_
        && (event->pos() - pressPos_).manhattanLength() > kTapMovePx)
        tapCandidate_ = false;
    router_.onMove(view, event);
}

// 释放：吞序列收尾；有效轻点则记录时刻/落点供下一次按下做双击判定；否则委托桌面路由。
void TouchInputAdapter::onRelease(CanvasView& view, QMouseEvent* event) {
    if (swallowing_) {
        swallowing_ = false;                 // 序列结束，恢复正常路由
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton) {
        const bool isTap = tapCandidate_ && !gestureSeen_
            && pressClock_.isValid() && pressClock_.elapsed() <= kTapMaxMs;
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

// 长按菜单：Android QPA 长按自动合成 QContextMenuEvent，与桌面右键完全同义 → 原样委托。
void TouchInputAdapter::onContextMenu(CanvasView& view, QContextMenuEvent* event) {
    router_.onContextMenu(view, event);
}

// 双指捏合：scaleFactor 为相对上次事件的增量 → gestureStepZoom（钳制复用桌面链路）；
// 中心点位移差 → gesturePanDelta（SPEC §8.3 双指平移）；出现手势即作废本序列 tap 判定。
bool TouchInputAdapter::onGestureEvent(CanvasView& view, QGestureEvent* event) {
    const auto* pinch = dynamic_cast<QPinchGesture*>(event->gesture(Qt::PinchGesture));
    if (!pinch) return false;
    gestureSeen_ = true;
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
