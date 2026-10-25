// ============================================================================
// 文件：canvas/canvas_view.cpp
// 作用：实现画布交互（缩放/平移/框选/微调/右键菜单/光标回报），见同名头文件说明。
// 分块依据：
//   - 事件入口一行转发给输入适配器（输入适配器分层）：按键角色、滚轮刻度、右键菜单 UI
//     等设备语义全部在 DesktopInputAdapter（canvas_desktop_input_adapter.cpp）；
//   - 本文件的手势执行面（gesture* ）只含与输入设备无关的状态机与意图信号：
//     平移用手写滚动条位移，避免与选区拖拽争用 DragMode；框选仅在「无图元接管鼠标」
//     时启动（借 mouseGrabberItem() 判定），从而与选区框的手柄互不冲突；
//   - 键盘（Esc / 空格 / 方向键）是桌面专属通道，保留在本视图；移动端由补偿 UI 调同一 gesture*。
//   - 触控：构造时按平台换装适配器（Android → TouchInputAdapter）；手势事件（pinch）
//     在 event() 入口交适配器消费；抓取带屏幕基准（updateHandleSize）由适配器提供。
// 说明：所有手势都翻译成信号，视图不改任何业务状态（CONTRIBUTING.md「分层纪律」）。
// ============================================================================
#include "canvas/canvas_view.h"

#include <algorithm>
#include <memory>

#include <QColor>
#include <QGestureEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QRubberBand>
#include <QScrollBar>
#include <QTransform>

#include "canvas/canvas_desktop_input_adapter.h"
#include "canvas/canvas_scene.h"
#include "canvas/canvas_touch_input_adapter.h"
#include "canvas/items/selection_rect_item.h"
#include "app/palette.h"
#include "app/canvas_prefs.h"
#include "util/gui_nums.h"

namespace idc::gui {

// 构造：抗锯齿、锚点、深灰背景、开启鼠标追踪（回报光标坐标）；按平台注入输入适配器。
CanvasView::CanvasView(CanvasScene* scene, QWidget* parent)
    : QGraphicsView(scene, parent), scene_(scene) {
    setRenderHint(QPainter::Antialiasing, true);
    setTransformationAnchor(AnchorUnderMouse); // 缩放锚定光标下
    setResizeAnchor(AnchorViewCenter);
    setDragMode(NoDrag);                     // 拖拽逻辑自管
    setMouseTracking(true);                                 // 无按键也回报光标位置
    setBackgroundBrush(Palette::instance().background());   // 画布深灰（本目录 README「画布视觉规范」；可「视图→外观/配色…」自定义）
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    input_ = std::make_unique<TouchInputAdapter>();         // 触控：合成鼠标路由复用桌面 + pinch/双击（SPEC §8.3）
#else
    input_ = std::make_unique<DesktopInputAdapter>();
#endif
    input_->attach(*this);                                  // 适配器自注册（如 grabGesture）

    // Palette 改色联动：重设背景刷 + 刷新网格 pen + 全场景重绘（遮罩/选区/标注 paint 期取色随之变色）。
    connect(&Palette::instance(), &Palette::changed, this, [this] {
        setBackgroundBrush(Palette::instance().background());
        if (scene_) {
            scene_->refreshPaletteColors();
            scene_->update();
        }
    });

    // CanvasPrefs 交互偏好联动：重钳缩放区间→重算手柄屏幕基准与滚动余量→回设吸附开关。
    connect(&CanvasPrefs::instance(), &CanvasPrefs::changed, this, [this] {
        clampZoom();          // 当前倍率可能落在新的[min,max]外，先收敛（updateHandleSize 会重报倍率）。
        updateHandleSize();   // 手柄/抓边条带/点选阈值随新的屏幕基准、边缘滚动余量随新的视口百分比重算。
        if (scene_) scene_->applySnapEnabled(CanvasPrefs::instance().snapEnabled());
    });
}

// 析构在此定义：此翻译单元已含适配器实现头（完整类型），unique_ptr 释放安全。
CanvasView::~CanvasView() = default;

// 放大（每档倍率增量取 CanvasPrefs 缩放步进，默认 +25%）。
void CanvasView::zoomIn() { zoomBy(1.0 + CanvasPrefs::instance().zoomStepPercent() / 100.0); }
// 缩小（与放大互逆：÷(1+步进)）。
void CanvasView::zoomOut() { zoomBy(1.0 / (1.0 + CanvasPrefs::instance().zoomStepPercent() / 100.0)); }
// 复位到 1:1。
void CanvasView::resetZoom() { resetTransform(); updateHandleSize(); }
// 适应窗口：按「内容边界 contentBounds（原图 ∪ 全部网格单元）× 固定小边距」自算倍率，使内容恰基本铺满视口——
// 与「内容边界 + 外圈滚动余量」自算不同：滚动余量是**平移范围**偏好（视口各边百分比），不该拿来限制适配缩放；
// 否则用户把滚动余量调大时，适配会把内容挤到 (1－2×余量百分比) 乃至趋零、退化成“缩到最小尺寸”。
// 倍率 = 视口×固定适配比例 / 内容（横纵取小），令内容最长边铺到视口的 kFitToWindowKeepRatio；clampZoom 兜到允许区间。
void CanvasView::fitToWindow() {
    if (const QRectF content = scene_ ? scene_->contentBounds() : QRectF(); !content.isEmpty()) {
        const QSize vp = viewport()->size();
        constexpr qreal keep = nums::canvas::kFitToWindowKeepRatio;
        const qreal target = std::min(vp.width() * keep / content.width(),
                                      vp.height() * keep / content.height());
        if (target > 0.0) {
            setTransform(QTransform().scale(target, target));   // 绝对倍率（非累乘），与 fitInView 同口径。
            centerOn(content.center());                          // 内容不对称（补白只伸一侧）时居中的也是内容。
            clampZoom();   // 自算结果可能超出允许区间（极小图 + 极大视口），钳回边界。
        }
    }
    updateHandleSize();
}

// 按倍数缩放并钳制到 CanvasPrefs 允许区间：以绝对目标倍率换算增量，避免累积越界。
void CanvasView::zoomBy(const qreal factor) {
    const qreal cur = transform().m11();
    if (cur <= 0.0) return;
    const auto& prefs = CanvasPrefs::instance();
    if (const qreal next = std::clamp(cur * factor, prefs.zoomMinFactor(), prefs.zoomMaxFactor()); !qFuzzyCompare(next, cur)) scale(next / cur, next / cur); // 已达边界则不再缩放。
    updateHandleSize();
}

// 把当前变换的缩放钳制回 CanvasPrefs 允许区间（供 fitToWindow 等绝对变换后兜底）。
void CanvasView::clampZoom() {
    const qreal cur = transform().m11();
    if (cur <= 0.0) return;
    const auto& prefs = CanvasPrefs::instance();
    if (const qreal c = std::clamp(cur, prefs.zoomMinFactor(), prefs.zoomMaxFactor()); !qFuzzyCompare(c, cur)) scale(c / cur, c / cur);
}

// 依当前缩放换算选区手柄的场景尺寸（屏幕基准由适配器提供，桌面同既有的 8 屏幕px），
// 保证手柄屏幕观感恒定；触控下基准 ≥12pt 自动扩宽抓取带（SPEC §8.3，图元无需知晓）。
void CanvasView::updateHandleSize() {
    const qreal m11 = transform().m11();
    emit zoomChanged(m11);       // 回报当前缩放倍数（所有缩放入口都经过此函数）。
    if (!scene_ || m11 <= 0.0) return;
    // 滚动余量（视口口径百分比）同样随缩放折算为场景单位，与手柄一起在此重算（拆成独立方法供 resize 复用）。
    updateScrollMargins();
    const qreal platformBase = input_ ? input_->screenOverlayPx() : nums::canvas::kDesktopOverlayScreenPx;
    const qreal hs = CanvasPrefs::instance().handleScreenPxOr(platformBase) / m11;
    if (scene_->selectionItem()) scene_->selectionItem()->setHandleSize(hs); // 手柄与抓边条带同步随缩放换算。
    scene_->setMultiRectHandleSize(hs); // L2 多矩形选区框手柄同步随缩放换算。
    scene_->setAnnotationHandleSize(hs); // 标注控制点手柄同步随缩放换算。
    scene_->setGridHandleSize(hs); // L3 网格基准手柄（基准点 / 单元尺寸）同步随缩放换算。
    if (scene_->cellPickerItem()) scene_->cellPickerItem()->setOverlayScale(hs); // L3 点选阈值/重绘余量同步随缩放换算。
}

// 滚动余量注入：横 = 视口宽×百分比、纵 = 视口高×百分比，再除以 m11 折算为场景单位。内容（原图 ∪ 补白单元）
// 外面恒推同一**视口比例**幅度（不随图幅与缩放档变化，但随窗口大小同比）；只管「外面留多少」，内容边界由
// 场景自行包住。两个变化源共用本方法：缩放基准（updateHandleSize）与视口尺寸（resizeEvent）。
void CanvasView::updateScrollMargins() const {
    if (!scene_) return;
    const qreal m11 = transform().m11();
    const QSize vp = viewport()->size();
    if (m11 <= 0.0 || vp.isEmpty()) return;   // 未显示时视口尺寸无意义，交由首次 resizeEvent 补推。
    const qreal ratio = CanvasPrefs::instance().scrollMarginPercent() / 100.0;
    scene_->setScrollMargins(ratio * vp.width() / m11, ratio * vp.height() / m11);
}

// 视口尺寸变化：余量按视口各边百分比生效，故尺寸变了必须重推场景矩形（手柄只依赖缩放，无需重算）。
void CanvasView::resizeEvent(QResizeEvent* event) {
    QGraphicsView::resizeEvent(event);
    updateScrollMargins();
}

// 画布四态互斥模式：切换当前交互态；离开标注创建态时复位拖拽手势标志，并按态设光标。
void CanvasView::setCanvasMode(const CanvasMode mode) {
    mode_ = mode;
    const bool draw = mode == CanvasMode::AnnoCreate;
    if (!draw) annoDrawing_ = false;
    viewport()->setCursor(draw ? Qt::CrossCursor : Qt::ArrowCursor);
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
    if (event->key() == Qt::Key_Escape && annotationDrawActive()) {
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
    const int step = event->modifiers() & Qt::ShiftModifier
        ? nums::canvas::kNudgeStepShiftPx : nums::canvas::kNudgeStepPx;
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

// 取消进行中的框选/平移（见头文件声明）：捏合开始前由触控适配器调用，
// 避免第一指合成鼠标事件已启动的橡皮筋/平移与双指缩放并存。
void CanvasView::gestureCancelCurrent() {
    if (panning_) gesturePanEnd();
    if (rubber_) {
        rubber_ = false;
        if (rubberBand_) rubberBand_->hide();
    }
    // 图元拖拽：第一指按在选区框手柄/标注图元上时 QGraphicsView 已开始拖拽
    //（场景 mouse grabber 持有）——捏合期间合成 move 会继续拖动图元、图形随之偏移。
    // ungrabMouse 后图元不再接收移动事件，缩放不再带动图形。
    if (scene_ && scene_->mouseGrabberItem())
        scene_->mouseGrabberItem()->ungrabMouse();
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

// 框选起带：记录起点并显示橡皮筋（同时上报框选起始，供上层清除进行中的标注选中）。
// 「选取边框」图层隐藏时选区整体不可交互，三种模式均不起带（L1/L2 选区框、L3 单元选择）——
// 否则会拖出一条看不到结果、且误发 marqueeStarted 抹掉标注选中的橡皮筋；与图元级点选/拖拽同一口径。
void CanvasView::gestureMarqueeBegin(const QPoint& viewportPos) {
    if (scene_ && !scene_->selectionVisible()) return;
    rubber_ = true;
    rubberStart_ = viewportPos;
    emit marqueeStarted();
    if (!rubberBand_) rubberBand_ = new QRubberBand(QRubberBand::Rectangle, viewport());
    rubberBand_->setGeometry(QRect(rubberStart_, QSize()));
    rubberBand_->show();
}

// 框选更新：以起点为一角伸缩橡皮筋。橡皮筋按「吸附后的场景整数矩形」回映射到视口，
// 于是拖动中所见框边已落在像素网格上（释放时上报的也正是同一矩形，不会跳位）。
void CanvasView::gestureMarqueeUpdate(const QPoint& viewportPos) const {
    if (!rubberBand_) return;
    rubberBand_->setGeometry(mapFromScene(snappedMarqueeSceneRect(rubberStart_, viewportPos)).boundingRect());
}

// 框选收尾：隐藏橡皮筋；过滤误触（视口尺寸过小）；把已吸附整数像素的场景矩形上报。
void CanvasView::gestureMarqueeEnd(const QPoint& viewportPos) {
    rubber_ = false;
    const QRect vr = QRect(rubberStart_, viewportPos).normalized();
    if (rubberBand_) rubberBand_->hide();
    if (vr.width() > nums::canvas::kMarqueeMinSizePx && vr.height() > nums::canvas::kMarqueeMinSizePx) {
        emit rubberSelect(snappedMarqueeSceneRect(rubberStart_, viewportPos));
    }
}

// 框选两点（视口坐标）→ 场景矩形：两端各取整（四舍五入）后规范化，使选区四边恒为整数像素。
// 与选区拖动/缩放的整数像素口径一致（上层再钳到图内），故 L1/L2/L3 三种框选同一行为。
QRectF CanvasView::snappedMarqueeSceneRect(const QPoint& a, const QPoint& b) const {
    const QPointF p0 = mapToScene(a);
    const QPointF p1 = mapToScene(b);
    const qreal l = std::round(std::min(p0.x(), p1.x()));
    const qreal t = std::round(std::min(p0.y(), p1.y()));
    const qreal r = std::round(std::max(p0.x(), p1.x()));
    const qreal btm = std::round(std::max(p0.y(), p1.y()));
    return {l, t, r - l, btm - t};
}

// 标注选取态空白按下：纯意图转发，无任何本地状态（选中清除由上层协调器落地）。
void CanvasView::gestureAnnoBlankPress() { emit annoBlankPressed(); }

// 非绘制态悬停移动：回报光标场景坐标（状态栏）；图元事件交付由适配器 dispatchSceneMove 完成。
void CanvasView::gestureCursorMoved(const QPoint& viewportPos) {
    emit cursorScenePos(mapToScene(viewportPos));
}

// 步进缩放：适配器只给倍率；钳制与手柄重同步归 zoomBy。
void CanvasView::gestureStepZoom(const qreal factor) { zoomBy(factor); }

// 步进缩放并锚定指定视口点（桌面滚轮：以光标处为原点）。
// 手动锚定、不借 AnchorUnderMouse：其内部记录的鼠标位仅在基类 mouseMove/Press 里刷新，而平移 /
// 绘制悬停路径不经基类（适配器提前 return），读数陈旧会让缩放跳位。此处在缩放前记下锚点视口下的
// 场景坐标，缩放后解出「把视图中心挪多少，才能使该场景坐标仍落在此视口点」，再 centerOn 对齐。
void CanvasView::gestureStepZoomAt(const QPoint& anchorViewport, const qreal factor) {
    const qreal cur = transform().m11();
    if (cur <= 0.0) return;
    const auto& prefs = CanvasPrefs::instance();
    if (const qreal next = std::clamp(cur * factor, prefs.zoomMinFactor(), prefs.zoomMaxFactor()); !qFuzzyCompare(next, cur)) {                       // 已达缩放边界则不变倍。
        setTransformationAnchor(NoAnchor);                 // 缩放期关锚点，位移完全由下方 centerOn 决定。
        const QPointF anchorScene = mapToScene(anchorViewport);        // 缩放前锚点下的场景坐标。
        scale(next / cur, next / cur);
        const QPointF anchorSceneNow = mapToScene(anchorViewport);     // 缩放后同一视口点下的场景坐标。
        const QPointF centerNow = mapToScene(viewport()->geometry().center());
        centerOn(centerNow + (anchorScene - anchorSceneNow));          // 平移使锚点场景坐标重落回此视口点。
        setTransformationAnchor(AnchorUnderMouse);                     // 恢复既设锚点（工具栏 ± 等仍走原语义）。
    }
    updateHandleSize();                                    // 重报倍率并随新缩放换算手柄 / 余量。
}

// 右键菜单项意图代理：菜单 UI 在桌面适配器，此处仅补发既有意图信号（接线住在各属主旁：清除在 CanvasCoordinator、遮罩在 PreviewController、复位视图在 MainWindow）。
void CanvasView::requestClearCut() { emit clearCutRequested(); }
void CanvasView::requestResetView() { emit resetViewRequested(); }
void CanvasView::requestToggleMasks() { emit toggleMasksRequested(); }

// 无键盘补偿：步进盘/取消按钮与键盘同一信号链路（方向键微调 → nudgeSelection；
// 绘制态 Esc → annoEscape），移动端不新增任何行为分支（SPEC §8.3）。
void CanvasView::requestNudge(const int dx, const int dy) { emit nudgeSelection(dx, dy); }
void CanvasView::requestAnnoEscape() { emit annoEscape(); }

} // namespace idc::gui
