// ============================================================================
// 文件：mobile/touch_scroll.cpp
// 作用：实现移动端滚动接管（见同名头文件说明）。
// ============================================================================
#include "mobile/touch_scroll.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QScrollBar>
#include <QScroller>
#include <QTextEdit>
#include <QTouchEvent>
#include <QWheelEvent>

namespace idc::gui {


/// 底部工具条滚轮重定向：QScrollArea 默认滚轮处理驱动的是**垂直**滚动条——横向条带里
/// 会出现「上下滚动」的错乱。拦截视口链（含悬停在按钮上时）的 Wheel 事件，
/// 换算成水平位移；无溢出/非滚轮事件不消费（交回默认链）。
WheelToHScrollFilter::WheelToHScrollFilter(QScrollBar* target, QObject* parent)
    : QObject(parent), target_(target) {}

bool WheelToHScrollFilter::eventFilter(QObject* /*obj*/, QEvent* ev) {
    if (ev->type() != QEvent::Wheel) return false;
    const auto* we = dynamic_cast<QWheelEvent*>(ev);
    const int dx = we->angleDelta().x();
    const int dy = we->angleDelta().y();
    const int step = dx != 0 ? dx : dy;   // 横向滚轮（触控板）优先，否则竖轮横用。
    if (step == 0) return true;           // 纯高精度横抖：吞掉即可。
    // 无水平溢出（含横屏单列形态）：交回默认链——横屏单列时默认竖滚正是想要的。
    if (target_->minimum() == target_->maximum()) return false;
    target_->setValue(target_->value() - step);
    return true;
}


// 触控滚动接管（QScroller 替代）：QScroller 在 Android 上的属性通道不完整——
// 过冲属性经 QPA 不生效（回弹关不干净），且其 click-through 合成会在拖拽释放时
// 向条带按钮投递点击（「滑动时按下按钮」的元凶——MaxClickThroughVelocity 只挡
// 快速甩动，慢速收尾照样投递）。自定义过滤器替代：全程消费触摸事件（合成鼠标
// 无从干扰），累计位移超阈值（kTapThreshold）即按增量滚动——滚动条自身钳制，
// 无过冲无回弹；未超阈值（真轻点）在释放时人工合成一次 press-release 触发子部件点击。
namespace {

class TouchScrollFilter : public QObject {
public:
    explicit TouchScrollFilter(QScrollArea* area, QObject* parent = nullptr)
        : QObject(parent), area_(area) {
        area_->viewport()->installEventFilter(this);
        area_->viewport()->setAttribute(Qt::WA_AcceptTouchEvents);
    }

protected:
    bool eventFilter(QObject* /*watched*/, QEvent* event) override {
        switch (event->type()) {
            case QEvent::TouchBegin:
            case QEvent::TouchUpdate:
            case QEvent::TouchEnd: {
                const auto* te = dynamic_cast<QTouchEvent*>(event);
                if (event->type() == QEvent::TouchBegin && !te->points().isEmpty()) {
                    // 落在文本输入类控件（输入框/数字框/下拉框）上的触摸序列放行：
                    // 文字选择、拖动删除、光标定位需要 Qt 原生的合成鼠标事件，
                    // 被本过滤器消费则「删除都删不了」（真机反馈）。
                    passThrough_ = isTextEntryWidgetAt(te->points().first().position());
                    if (passThrough_) return false;
                }
                if (passThrough_) {
                    if (event->type() == QEvent::TouchEnd) passThrough_ = false;
                    return false; // 放行整条序列，交 Qt 合成鼠标处理。
                }
                handleTouch(te);
                return true; // 全程消费：点击只由本过滤器合成，绝不误投。
            }
            default:
                return false;
        }
    }

    // 命中点（及其祖先）是否文本输入类控件——这类控件保留原生触摸语义。
    bool isTextEntryWidgetAt(const QPointF& viewportPos) const {
        const QPoint gp = area_->viewport()->mapToGlobal(viewportPos.toPoint());
        QWidget* w = QApplication::widgetAt(gp);
        while (w) {
            if (qobject_cast<QLineEdit*>(w) || qobject_cast<QTextEdit*>(w)
                || qobject_cast<QPlainTextEdit*>(w) || qobject_cast<QAbstractSpinBox*>(w)
                || qobject_cast<QComboBox*>(w))
                return true;
            w = w->parentWidget();
        }
        return false;
    }

private:
    void handleTouch(const QTouchEvent* event) {
        if (event->points().isEmpty()) return;
        const QPointF pos = event->points().first().position();
        switch (event->type()) {
            case QEvent::TouchBegin:
                tracking_ = true;
                moved_ = false;
                lastPos_ = pos;
                total_ = QPointF();
                break;
            case QEvent::TouchUpdate: {
                if (!tracking_) break;
                const QPointF delta = pos - lastPos_;
                lastPos_ = pos;
                total_ += delta;
                if (!moved_ && total_.manhattanLength() < kTapThreshold) break; // 轻点抖动容忍区内。
                moved_ = true;
                // 内容跟手：手指位移反作用于滚动条值（上/左滑 → 值增大）。
                area_->horizontalScrollBar()->setValue(
                    area_->horizontalScrollBar()->value() + qRound(-delta.x()));
                area_->verticalScrollBar()->setValue(
                    area_->verticalScrollBar()->value() + qRound(-delta.y()));
                break;
            }
            case QEvent::TouchEnd:
                if (tracking_ && !moved_) synthesizeTap(pos);
                tracking_ = false;
                break;
            default:
                break;
        }
    }

    // 人工点击合成：widgetAt 深查命中部件（透过条带容器直达按钮），发一对鼠标
    // press-release；只处理本滚动区子树内的目标。
    void synthesizeTap(const QPointF& viewportPos) const {
        const QWidget* vp = area_->viewport();
        const auto global = QPointF(vp->mapToGlobal(viewportPos.toPoint()));
        QWidget* child = QApplication::widgetAt(global.toPoint());
        if (!child || !vp->isAncestorOf(child)) return;
        const auto local = QPointF(child->mapFromGlobal(global.toPoint()));
        QMouseEvent press(QEvent::MouseButtonPress, local, global,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, local, global,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(child, &press);
        QApplication::sendEvent(child, &release);
    }

    static constexpr qreal kTapThreshold = 6.0; // 轻点抖动容忍（px）；超过即判为滑动。
    QScrollArea* area_;
    bool passThrough_ = false; // 本序列落在文本输入控件上：放行交 Qt 原生处理。
    bool tracking_ = false;
    bool moved_ = false;
    QPointF lastPos_;
    QPointF total_;
};

} // namespace

// 移动端全部滚动区（抽屉页/分组/底部条带）统一换装上述过滤器；
// 桌面预览保持 QScroller 鼠标协调器模式（无触摸事件，不受影响）。
void installTouchScroll(QScrollArea* area) {
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    new TouchScrollFilter(area, area); // 父对象持有生命周期，随滚动区销毁。
#else
    QScroller::grabGesture(area->viewport(), QScroller::LeftMouseButtonGesture);
#endif
}

} // namespace idc::gui
