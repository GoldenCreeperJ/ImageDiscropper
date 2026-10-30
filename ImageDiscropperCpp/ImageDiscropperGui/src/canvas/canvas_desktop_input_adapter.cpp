// ============================================================================
// 文件：canvas/canvas_desktop_input_adapter.cpp
// 作用：DesktopInputAdapter 实现——桌面鼠标 / 滚轮 / 右键事件到 CanvasView 业务
//       手势的翻译（映射表见同名头文件）。
// 分块依据：本文件集中承载桌面设备语义（鼠标按键角色、滚轮刻度、右键菜单触发时机）→ 业务手势的翻译；
//           一切状态与信号发射都委托 view 的 gesture*（菜单 UI 本身住在 CanvasView::showCanvasContextMenu，
//           本类只判「绘制态收笔 vs 普通弹菜单」）。触控适配器仅在双击过滤与
//           吞序列判定时额外触及 Qt::LeftButton，其余业务路由全部经 router_ 委托本类。
// 说明：平移中 / 绘制中 / 框选中的互斥路由靠 view 的状态查询（panning() 等）完成，
//       顺序：平移 → 绘制拖拽 → 绘制悬停 → 交付场景图元 + 框选更新。
// ============================================================================
#include "canvas/canvas_desktop_input_adapter.h"

#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QObject>   // connect（QTimer 仅前向可达，PMF 重载需完整声明）
#include <QWheelEvent>

#include "canvas/canvas_scene.h"
#include "canvas/canvas_view.h"
#include "util/gui_nums.h"

namespace idc::gui {

DesktopInputAdapter::DesktopInputAdapter() {
    QObject::connect(&clickTimer_, &QTimer::timeout, &clickTimer_, [this] { flushPendingClick(); });
}

// 单击延迟判定（双击成立时单击不落地，二者不再同时生效）——对**非绘制态（框选/拾取/标注选取）的底图/空白**生效：
//   绘制态（AnnoCreate）的左键按下是落顶点/起笔的动作手势，扣住会把成形推迟到窗口到期（且迟到的补发
//   落点已不是用户看清的当前位置），且快速连点落两个顶点会被误判成双击而丢点，故本态不扣、即时路由。
//   其余三态的空白落点都无“连点即丢动作”的风险，双击＝适应窗口因此三态均生效。
//   再按落点细分：交互图元（选区框/落位框/图层/标注等接收左键者）上的按下即时路由——对应位置的双击会
//   略过适应窗口、按两次普通点击处理（见 onDoubleClick），单击双击无同发冲突，无需延迟判定
//   （同点连击循环即时推进）。底图/空白上的干净左键按下先扣住不路由；窗口（kDoubleTapGapMs——本应用
//   双击界限）内若来同点第二击（onDoubleClick / 离散第二击按下）→ 单击作废、仅落适应窗口；窗口到期 →
//   补发原位置的 press+release 对（合成事件，语义与即时路由一致）。
//   拖拽不受影响：移动越过轻点容差即补发按下、拖拽从原点起算。平移起手（中键/空格+左键）与其余键
//   无双击语义，直接路由（其他输入同时取消等待中的单击，见 onWheel/onContextMenu）。
void DesktopInputAdapter::onPress(CanvasView& view, QMouseEvent* event) {
    swallowNextRelease_ = false;   // 新按下到达：上一记吞释放标记作废（触控层自吞后不得残留）
    if (event->button() == Qt::LeftButton && !view.panArmed()
        && view.canvasMode() != CanvasMode::AnnoCreate) {
        if (view.interactiveItemAt(event->position())) {
            cancelPendingClick();   // 换点：作废上一记空画布单击（不得再补发，避免误反选/误拾取）
            routePress(view, event);
            return;
        }
        if (clickHeld_) {   // 窗口内离散第二击（无 dblclick 合成的平台兜底）
            if ((event->position() - heldLocalPos_).manhattanLength()
                <= nums::canvas::kDoubleTapPosScreenPx) {   // 同点 → 双击成立
                cancelPendingClick();
                swallowNextRelease_ = true;   // 本击 press 已消费，其真实 release 吞掉（不成对）
                view.fitToWindow();
                event->accept();
                return;
            }
            flushPendingClick();   // 换点连击：上一记单击按原位置落地（两击都不废），本击重新起扣
        }
        clickHeld_ = true;
        heldLocalPos_ = event->position();
        heldGlobalPos_ = event->globalPosition();
        heldMods_ = event->modifiers();
        heldView_ = &view;
        event->accept();
        return;
    }
    if (event->button() != Qt::LeftButton) cancelPendingClick();   // 右键/中键等：等待中的单击不再落地
    routePress(view, event);
}

// 纯路由（无延迟判定）：原 onPress 语义。
void DesktopInputAdapter::routePress(CanvasView& view, QMouseEvent* event) {
    const QPoint vp = event->position().toPoint();

    if (event->button() == Qt::MiddleButton ||
        (event->button() == Qt::LeftButton && view.panArmed())) {
        view.gesturePanBegin(vp);
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton) {
        switch (view.canvasMode()) {
            case CanvasMode::AnnoCreate:
                view.gestureStrokeBegin(vp);   // 绘制态：左键起笔（不交付图元 / 不启动框选）。
                break;
            case CanvasMode::ImagePick:
                view.dispatchScenePress(event); // 场景级循环拾取；空白无候选则不做任何选择。
                break;
            case CanvasMode::AnnoSelect:
                view.dispatchScenePress(event); // 仅标注 / 选区框交互；禁框选。
                // 空白反选：图元命中时会在 press 内 accept 并成为抓取者（AnnotationItem /
                // SelectionRectItem 手柄）；无抓取者即落在空白 → 请求取消标注选中
                //（框选态由 marqueeStarted 承担此职责，本态禁框选故需专用通道）。
                if (view.scene() && view.scene()->mouseGrabberItem() == nullptr) {
                    view.gestureAnnoBlankPress();
                }
                break;
            case CanvasMode::Marquee:
            default:
                view.dispatchScenePress(event); // 交付场景：选区框移动/缩放手柄优先。
                if (view.scene() && view.scene()->mouseGrabberItem() == nullptr) {
                    view.gestureMarqueeBegin(vp);   // 无图元接管 → 启动框选新选区。
                }
                break;
        }
        event->accept();
        return;
    }

    view.dispatchScenePress(event);
}

// 移动：平移 → 绘制拖拽 → 绘制悬停（未按键）→ 交付抓取图元 + 框选带更新 + 光标回报。
void DesktopInputAdapter::onMove(CanvasView& view, QMouseEvent* event) {
    if (clickHeld_ && event->buttons() & Qt::LeftButton) {
        // 按键仍按住且越过轻点容差 → 判为拖拽：补发被扣按下（原位置），随后本 move 正常路由。
        // 已释放（等待判定中）后的无按键移动**不吞**——悬停/坐标回报（折线橡皮筋、状态栏）照常，
        // 否则每次空白单击后 ≤300ms 内预览与回报全被冻结。
        if ((event->position() - heldLocalPos_).manhattanLength() > nums::canvas::kTapMoveScreenPx) {
            clickTimer_.stop();
            clickHeld_ = false;
            QMouseEvent press(QEvent::MouseButtonPress, heldLocalPos_, heldGlobalPos_,
                              Qt::LeftButton, Qt::LeftButton, heldMods_);
            routePress(*heldView_, &press);
        } else {
            event->accept();   // 扣住期内移动不出路由（无按下即无 move 语义）
            return;
        }
    }
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

// 释放：干净左键单击不立即落地（起延迟窗口，期间第二击成立则单击作废）；其余原样路由。
// 窗口＝kDoubleTapGapMs（本应用双击界限）：系统合成窗口内越界的中速连击由 onDoubleClick
// 按「首击已落地」补发普通点击，窗口无需盖过系统双击间隔。
void DesktopInputAdapter::onRelease(CanvasView& view, QMouseEvent* event) {
    if (swallowNextRelease_ && event->button() == Qt::LeftButton) {
        swallowNextRelease_ = false;   // 双击/兜底第二击的 press 已被消费（合成对/适应窗口），其真实释放不成对
        event->accept();
        return;
    }
    if (clickHeld_ && event->button() == Qt::LeftButton) {
        clickTimer_.start(nums::canvas::kDoubleTapGapMs);
        event->accept();
        return;
    }
    routeRelease(view, event);
}

// 纯路由（无延迟判定）：原 onRelease 语义。
void DesktopInputAdapter::routeRelease(CanvasView& view, QMouseEvent* event) {
    const QPoint vp = event->position().toPoint();

    if (view.panning() && (event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton)) {
        view.gesturePanEnd();
        event->accept();
        return;
    }

    // 标注绘制释放：结束拖拽手势并上报落点（两点形状提交 / 画笔收笔 / 点击式工具在此定落点：折线落顶点、文字取文本）。
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

// 双击事件（QPA 合成，间隔小于系统双击阈值的第二击）：先按落点消歧——
//   交互图元（选区框/落位框等接收左键者）上：**略过双击**，按普通点击补发（press+release 对，
//   原位置原修饰键）——同点连击循环照常推进，双击界限不吞图元点击；
//   底图/空白上：窗口内首击未落地（clickHeld_）→ 双击成立：作废等待中的单击、适应窗口（非绘制态
//   均扣单击，见 onPress）；首击已落地（间隔 ≥ kDoubleTapGapMs 的中速连击或绘制态）→ 同样补发普通点击。
//   任何分支都消费了本击的 press，其随后的真实 release 由 swallowNextRelease_ 吞掉（不成对）。
void DesktopInputAdapter::onDoubleClick(CanvasView& view, QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) { event->ignore(); return; }
    swallowNextRelease_ = true;
    if (view.interactiveItemAt(event->position())) {
        routeClickPair(view, event);
        event->accept();
        return;
    }
    if (clickHeld_) {
        cancelPendingClick();
        view.fitToWindow();
    } else {
        routeClickPair(view, event);
    }
    event->accept();
}

// 把双击事件当普通点击补发：合成 press+release 对（原位置原修饰键）走正常路由。
// 用于两类越过双击界限的情形：交互图元上的双击（略过适应窗口）与空画布上的中速连击（首击已落地）。
void DesktopInputAdapter::routeClickPair(CanvasView& view, const QMouseEvent* event) {
    QMouseEvent press(QEvent::MouseButtonPress, event->position(), event->globalPosition(),
                      Qt::LeftButton, Qt::LeftButton, event->modifiers());
    QMouseEvent release(QEvent::MouseButtonRelease, event->position(), event->globalPosition(),
                        Qt::LeftButton, Qt::NoButton, event->modifiers());
    routePress(view, &press);
    routeRelease(view, &release);
}

// 取消等待中的单击落地（双击/长按/捏合路径调用）。
void DesktopInputAdapter::cancelPendingClick() {
    clickTimer_.stop();
    clickHeld_ = false;
}

// 延迟窗口到期：补发被扣住的单击（合成 press+release 对，原位置原修饰键，语义与即时路由一致）。
// 定时器必须同步停止——换点连击分支在上一记的窗口未走完时调用本函数，随后本击重新起扣：不停则
// 剩余窗口一到就提前补发**本击**（按住思考/微动时）→ 提前的 marqueeStarted 抹掉选中、真正释放时
// clickHeld_ 已清 → 未配对释放落场景，且按住期间拖不出框选。
void DesktopInputAdapter::flushPendingClick() {
    if (!clickHeld_ || !heldView_) return;
    clickTimer_.stop();
    clickHeld_ = false;
    QMouseEvent press(QEvent::MouseButtonPress, heldLocalPos_, heldGlobalPos_,
                      Qt::LeftButton, Qt::LeftButton, heldMods_);
    QMouseEvent release(QEvent::MouseButtonRelease, heldLocalPos_, heldGlobalPos_,
                        Qt::LeftButton, Qt::NoButton, heldMods_);
    routePress(*heldView_, &press);
    routeRelease(*heldView_, &release);
}

// 滚轮：一格 ×1.15 / ÷1.15 的步进缩放（以滚轮事件处的光标为原点，显式锚定、不依赖视图内部鼠标位）。
// 缩放即切换视图目标：等待中的单击不再落地（避免缩放后补出一记迟到点击）。
// 缩放前先作窄口径取消：进行中的绘制拖拽 / 图元拖拽若继续，后续落点会按新缩放换算、与手势起点的
// 旧缩放混算（形状尺寸被乘进倍率）；触控板双指捏合上的就是本通道。平移 / 框选不动——
//「空格按住 + 滚轮缩放」是常用组合，一并取消平移会迫使松键重来（见 CanvasView::gestureCancelForZoom）。
void DesktopInputAdapter::onWheel(CanvasView& view, QWheelEvent* event) {
    cancelPendingClick();
    view.gestureCancelForZoom();
    const qreal factor = event->angleDelta().y() > 0
        ? nums::canvas::kWheelZoomFactor : 1.0 / nums::canvas::kWheelZoomFactor;
    view.gestureStepZoomAt(event->position().toPoint(), factor);
    event->accept();
}

// 右键：绘制态＝收笔（不弹菜单）；普通＝弹上下文菜单（清除切割线 / 重置视图 / 切换遮罩）。
// 菜单 UI 与意图补发统一收敛到 CanvasView::showCanvasContextMenu（桌面右键与触控长按共用一份）。
// 桌面右键不产生合成左键 press，无框选/平移副作用需清理；菜单以 popup() 非阻塞弹出，本函数
// 内不起嵌套事件循环，故无需旧 exec 前后的状态复位。右键即切换交互目标：等待中的单击不再落地
// （避免菜单动作后补出一记迟到点击，如清选区后再被迟到的 marqueeStarted 抹掉标注选中）。
void DesktopInputAdapter::onContextMenu(CanvasView& view, QContextMenuEvent* event) {
    cancelPendingClick();
    if (view.annotationDrawActive()) {
        view.gestureStrokeFinish();
        event->accept();
        return;
    }
    view.showCanvasContextMenu(event->globalPos());
    event->accept();
}

} // namespace idc::gui
