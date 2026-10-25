// ============================================================================
// 文件：canvas/canvas_desktop_input_adapter.cpp
// 作用：DesktopInputAdapter 实现——桌面鼠标 / 滚轮 / 右键事件到 CanvasView 业务
//       手势的翻译（映射表见同名头文件）。
// 分块依据：本文件集中承载桌面设备语义（鼠标按键角色、滚轮刻度、QMenu 等处方的）→ 业务手势的翻译；
//           一切状态与信号发射都委托 view 的 gesture*。触控适配器仅在双击过滤与
//           吞序列判定时额外触及 Qt::LeftButton，其余业务路由全部经 router_ 委托本类。
// 说明：平移中 / 绘制中 / 框选中的互斥路由靠 view 的状态查询（panning() 等）完成，
//       顺序：平移 → 绘制拖拽 → 绘制悬停 → 交付场景图元 + 框选更新。
// ============================================================================
#include "canvas/canvas_desktop_input_adapter.h"

#include <QContextMenuEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QWheelEvent>

#include "canvas/canvas_scene.h"
#include "canvas/canvas_view.h"
#include "util/gui_nums.h"
#include "i18n/gui_texts.h"
#include "i18n/gui_i18n.h"

namespace idc::gui {

// 按下：中键 / 空格+左键 → 平移；其余左键按当前画布四态模式显式路由（互斥分离）：
//   · 标注创建 → 起笔绘制；
//   · 图片拾取 → 仅交付场景（重叠落位框由场景循环选层，非 z 序优先抓取），不起框选、不交标注；
//   · 标注选取 → 仅交付场景（标注图元 / 选区框手柄抓取），不起框选；空白按下（无图元接管）→ 反选清标注选中；
//   · 框选（默认）→ 先交付场景（选区框手柄优先；此态标注不可交互），无抓取者才起框选带。
// 其余键原样交付场景基类。
void DesktopInputAdapter::onPress(CanvasView& view, QMouseEvent* event) {
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

// 滚轮：一格 ×1.15 / ÷1.15 的步进缩放（以滚轮事件处的光标为原点，显式锚定、不依赖视图内部鼠标位）。
void DesktopInputAdapter::onWheel(CanvasView& view, QWheelEvent* event) {
    const qreal factor = event->angleDelta().y() > 0
        ? nums::canvas::kWheelZoomFactor : 1.0 / nums::canvas::kWheelZoomFactor;
    view.gestureStepZoomAt(event->position().toPoint(), factor);
    event->accept();
}

// 右键：绘制态＝收笔（不弹菜单）；普通＝弹上下文菜单（清除切割线 / 重置视图 / 切换遮罩），
// 菜单项命中后转发为视图的既有意图信号。
// 移动端长按＝先落一段合成左键 press（Marquee 态已起框选 / AnnoSelect 空白已起反选 / 图元已抓取），
//   紧随才到本 context 事件；QMenu::exec() 是嵌套阻塞循环，可能吃掉随后的合成 Release，令 rubber_ 等
//   状态永久悬置。故 exec 前显式 gestureCancelCurrent() 收掉 press 起的副作用，exec 返回后再清一次兜底
//   （Release 若已被循环吞掉，这里补复位；正常桌面右键时二者近似 no-op，不改桌面行为）。
void DesktopInputAdapter::onContextMenu(CanvasView& view, QContextMenuEvent* event) {
    if (view.annotationDrawActive()) {
        view.gestureStrokeFinish();
        event->accept();
        return;
    }
    view.gestureCancelCurrent();   // exec 前：收拾长按合成左键 press 已起的框选/反选/图元抓取。
    QMenu menu(view.viewport());
    const QAction* aClear = menu.addAction(IDC_GTR(menu, kCtxClearCutLine));
    const QAction* aReset = menu.addAction(IDC_GTR(menu, kCtxResetView));
    const auto* scene = dynamic_cast<CanvasScene*>(view.scene());
    const bool masksOn = scene && scene->masksVisible();
    const QAction* aToggle = menu.addAction(masksOn ? IDC_GTR(menu, kCtxHideMask)
                                              : IDC_GTR(menu, kCtxShowMask));
    if (const QAction* chosen = menu.exec(event->globalPos()); chosen == aClear) view.requestClearCut();
    else if (chosen == aReset) view.requestResetView();
    else if (chosen == aToggle) view.requestToggleMasks();
    view.gestureCancelCurrent();   // exec 后：菜单阻塞循环吞掉 Release 时补复位框选/平移态，防橡皮筋卡死。
}

} // namespace idc::gui
