// ============================================================================
// 文件：canvas/canvas_view.h
// 作用：画布视图——承载全部画布交互（本目录 README「界面概览」与「快捷键列表」）：滚轮缩放、空格或中键平移、
//       左键框选生成选区、方向键微调、右键菜单、光标坐标回报。视图只负责「把用户手势翻译成
//       意图信号」，具体状态变更由 CanvasCoordinator / AnnotationCoordinator 落到 Document，再由 Core 计算（CONTRIBUTING.md「分层纪律」）。
// 分块依据：
//   - 输入适配器分层：原始事件的设备语义（哪个键 / 滚轮刻度 / 右键菜单 UI）
//     收拢在 ICanvasInputAdapter（桌面实现 DesktopInputAdapter），本视图只保留业务手势执行面
//     （gesture* 系列：平移 / 标注绘制 / 框选 / 缩放 / 悬停的状态机与意图信号），事件入口仅一行转发；
//   - 键盘（Esc / 空格 / 方向键）是桌面专属通道，仍留在本视图；移动端由补偿 UI 调同一 gesture* 入口；
//   - 通过信号（rubberSelect / nudgeSelection / cursorScenePos / *Requested）与外界解耦，
//     视图不认识 Document，只发意图，符合「采集交互 → 翻译 → 交上层」的 GUI 定位。
// 说明：手柄尺寸随缩放动态换算（updateHandleSize），保证选区手柄恒约 8 屏幕像素（本项目实现选择，支撑 SPEC §8.2 拖拽能力）；
//       内容外的滚动余量按**视口各边百分比**同时随缩放与视口尺寸换算（updateScrollMargins），故 resize 需重推。
// ============================================================================
#pragma once

#include <memory>

#include <QGraphicsView>
#include <QPoint>

#include "canvas/canvas_mode.h"

class QRubberBand;
class QContextMenuEvent;
class QMouseEvent;
class QResizeEvent;

namespace idc::gui {

class CanvasScene;
class ICanvasInputAdapter;

// ---------------------------------------------------------------------------
// CanvasView：画布交互视图。
// ---------------------------------------------------------------------------
class CanvasView : public QGraphicsView {
    Q_OBJECT
public:
    explicit CanvasView(CanvasScene* scene, QWidget* parent = nullptr);
    // 声明于头、定义于 .cpp：input_ 是前向声明类型的 unique_ptr，若析构在头内联实例化
    // 会因不完整类型报 sizeof 错误（mac/linux 编译器与 moc 实例化路径均会触发）。
    ~CanvasView() override;

    // 缩放控制（工具栏 / 快捷键调用）。
    void zoomIn();
    void zoomOut();
    void resetZoom();
    void fitToWindow();

    // 画布四态互斥模式：设定当前交互态并复位非绘制态手势标志、切换光标。
    // 标注创建态（AnnoCreate）即绘制态门控：其余三态左键手势不进入标注绘制。
    void setCanvasMode(CanvasMode mode);
    CanvasMode canvasMode() const { return mode_; }

    // 标注绘制态门控（SPEC §8.2 标注绘制模式）：现为「当前模式是否为标注创建」的派生查询——
    // 为 true 时左键手势路由到标注绘制（橡皮筋选区让位）；为 false 时维持框选 / 拾取 / 选取交互。
    bool annotationDrawActive() const { return mode_ == CanvasMode::AnnoCreate; }

    // ——交互状态查询（供输入适配器路由，语义与原视图内部标志一致）——
    bool panArmed() const { return spacePan_; }          // 空格按住（平移预备态）
    bool panning() const { return panning_; }            // 正在平移
    bool strokeActive() const { return annoDrawing_; }   // 标注绘制拖拽中
    bool marqueeActive() const { return rubber_; }       // 框选橡皮筋带进行中
    // 双击消歧：视口位置处是否存在接收左键的交互图元（图层落位框/标注/选区框/网格基准手柄等；
    // 底图不接鼠标）。命中 → 单击即时路由、双击略过适应窗口（按两次普通点击处理，同点连击循环不被
    // 双击打断）；仅底图/空白 → 单击延迟判定与双击适应窗口照常。
    bool interactiveItemAt(const QPointF& viewportPos) const;

    // ——场景事件交付（供适配器：把原始事件交给 QGraphicsView 默认实现，图元优先抓取）——
    void dispatchScenePress(QMouseEvent* event) { QGraphicsView::mousePressEvent(event); }
    void dispatchSceneMove(QMouseEvent* event) { QGraphicsView::mouseMoveEvent(event); }
    void dispatchSceneRelease(QMouseEvent* event) { QGraphicsView::mouseReleaseEvent(event); }

    // ——业务手势执行面（ICanvasInputAdapter 的唯一调用目标；坐标一律为视口坐标）——
    // 平移：起 / 更新（手写滚动条位移，不与图元 DragMode 争用）/ 收。
    void gesturePanBegin(const QPoint& viewportPos);
    void gesturePanUpdate(const QPoint& viewportPos);
    void gesturePanEnd();
    // 取消进行中的框选 / 平移 / 绘制拖拽 / 图元拖拽（触控捏合与长按开始前调用）：第一指合成鼠标事件
    // 可能在手势识别前已启动这些手势——不取消则缩放与它们并存，落点按新旧缩放混算、几何被乘进倍率。
    // 绘制拖拽的作废取向按工具分类（见 gestureStrokeCancel），图元拖拽的归位口径见
    // CanvasScene::cancelActiveItemDrag。
    void gestureCancelCurrent();
    // 缩放前的窄口径取消：只结束「会被缩放污染几何」的手势（绘制拖拽 + 图元拖拽），不动平移 / 框选。
    // 与 gestureCancelCurrent 分立：桌面「空格按住 + 滚轮缩放」是常用组合，一并取消平移会迫使松键重来。
    void gestureCancelForZoom();
    // 双指平移（触控）：按视口位移量直接滚动，与 gesturePanUpdate 同一执行链路。
    void gesturePanDelta(const QPoint& deltaViewport) const;
    // 标注绘制手势：起笔 / 拖拽 / 悬停（未按键）/ 释放 / 收笔（右键或等效）。
    // 拖拽态标志由本视图持着，但落点语义全在上层按工具分派：按住即成形的（两点形状/画笔）在按下起笔，
    // 点击式的（折线/文字）恒在**释放**定落点。
    void gestureStrokeBegin(const QPoint& viewportPos);
    void gestureStrokeUpdate(const QPoint& viewportPos);
    void gestureStrokeHover(const QPoint& viewportPos);
    void gestureStrokeEnd(const QPoint& viewportPos);
    void gestureStrokeFinish();
    // 绘制拖拽被外部打断（缩放抢占）：退拖拽态并发「作废本笔」意图。绝不能走 gestureStrokeEnd 的
    // 提交语义（分类落法见 AnnotationCoordinator::onAnnoDragCancel）。
    void gestureStrokeCancel();
    // 框选（橡皮筋带）：起 / 更新 / 收（释放时过滤 ≤3px 误触并映射回场景坐标）。
    // 矩形边恒吸附整数像素（场景坐标）：绘制与上报同一取整口径，做到「看到的就是得到的像素框」。
    void gestureMarqueeBegin(const QPoint& viewportPos);
    void gestureMarqueeUpdate(const QPoint& viewportPos) const;
    void gestureMarqueeEnd(const QPoint& viewportPos);
    // 标注选取态空白按下（无图元接管）：发 annoBlankPressed 供上层取消标注选中
    //（本态禁框选、无 marqueeStarted 通道，反选需专用入口）。
    void gestureAnnoBlankPress();
    // 悬停移动：回报光标场景坐标（状态栏）。
    void gestureCursorMoved(const QPoint& viewportPos);
    // 步进缩放（桌面滚轮一格 ×1.15；工具栏 ± 走 zoomIn/Out，另成一路）。
    void gestureStepZoom(qreal factor);
    // 步进缩放并锚定给定视口点（桌面滚轮专用）：显式以光标处为原点，不依赖 QGraphicsView 的
    // AnchorUnderMouse——其内部记录的鼠标位仅在基类 mouseMove/Press 里刷新，而本视图的绘制悬停 /
    // 平移路径不经基类，读数会陈旧，导致缩放跳位（有时不以鼠标为原点）。
    void gestureStepZoomAt(const QPoint& anchorViewport, qreal factor);
    // 右键菜单项意图代理（菜单 UI 见 showCanvasContextMenu；此处补发既有意图信号）。
    void requestClearCut();
    void requestResetView();
    void requestToggleMasks();
    // 画布上下文菜单（清除切割线 / 重置视图 / 切换遮罩）：桌面右键与触控长按共用同一份。
    // 非阻塞 popup() 于给定全局点弹出，命中项经 request* 补发既有意图信号（不起嵌套事件循环、
    // 不吞 Release）；QMenu 以本视图为父并置 WA_DeleteOnClose，关闭即自销毁。
    void showCanvasContextMenu(const QPoint& globalPos);
    // 无键盘补偿入口（移动端 8 向步进盘 / 「取消与完成」按钮，SPEC §8.3）：
    // 与键盘同一条信号链路——方向键微调与绘制态 Esc 的触控等价物。
    void requestNudge(int dx, int dy);
    void requestAnnoEscape();
    void requestAnnoFinish();   // 移动端常驻「完成」按钮：与右键/长按收笔同走 annoFinish（折线在此结束），不再只吊在长按一条 QPA 通道上。

signals:
    // 光标在场景（原图）坐标中的位置，供状态栏显示。
    void cursorScenePos(const QPointF& scenePos);
    // 视图缩放倍数变化（1.0 = 100%），供状态栏显示放大倍数。
    void zoomChanged(qreal factor);
    // 用户框选出一个新选区（场景/原图坐标，已规范化）。
    void rubberSelect(const QRectF& sceneRect);
    // 框选起带（空白处按下即发）：供上层清除进行中的标注选中，使标注 OBB 手柄随框选消失。
    void marqueeStarted();
    // 标注选取态下按下落在空白（未命中任何标注/选区框图元）：供上层取消标注选中（桌面/触控同构）。
    void annoBlankPressed();
    // 方向键微调选区（dx,dy 为原图像素增量，Shift 时为大步）。
    void nudgeSelection(int dx, int dy);
    // 右键菜单意图。
    void clearCutRequested();
    void resetViewRequested();
    void toggleMasksRequested();
    // 标注绘制手势（仅当前模式为 AnnoCreate 时发出，场景/原图坐标）。
    // 上层（AnnotationCoordinator）依当前工具决定语义：两点形状→按下记锚点、释放提交；
    // 画笔→按下起笔、逐帧追点；折线/文字→点击式，落点在释放才定。
    void annoDragStart(const QPointF& scenePos);
    void annoDragMove(const QPointF& scenePos);
    void annoDragEnd(const QPointF& scenePos);
    // 绘制拖拽被缩放打断：上层按工具分类作废本笔（两点形状弃预览 / 画笔收笔 / 折线保留已落顶点）。
    void annoDragCancel();
    // 绘制态下鼠标悬停（未按键移动）：供折线「橡皮筋」实时预览落点与连线（左键点击才落顶点）。
    void annoHover(const QPointF& scenePos);
    // 绘制态下右键：收笔 / 退出当前绘制手势（折线在此结束并提交，不弹视图右键菜单）。
    void annoFinish();
    // 绘制态下按 Esc：交上层收笔（折线/画笔）或取消当前预览。
    void annoEscape();

protected:
    // Qt 事件入口：全部一行转发给输入适配器（设备语义只在适配器内存在）；
    // 键盘为桌面专属通道，直接在本视图处理。
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;   // 双击：适配器路由（仅框选态空白＝适应窗口；交互图元/其余模式＝补发两次普通点击）。
                                                              // 不调基类：图元恒收不到 DblClick——将来「双击标注＝编辑文字」等图元级双击语义
                                                              // 需在适配器里按落点显式下发。
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    // 手势事件（QEvent::Gesture，触控 pinch 缩放）：交适配器消费，未消费则照常下发。
    bool event(QEvent* event) override;
    // 视口尺寸变化：滚动余量按视口各边百分比生效（与缩放无关的那一层口径），故尺寸变了就得重推场景矩形。
    void resizeEvent(QResizeEvent* event) override;

private:
    // 依当前视图缩放换算选区手柄的场景尺寸（屏幕基准由适配器提供：桌面约 8 屏幕px，
    // 触控 ≥12pt，SPEC §8.3），并下发给选区框。
    void updateHandleSize();
    // 按当前视口尺寸与缩放把滚动余量注入场景（横 = 视口宽×百分比、纵 = 视口高×百分比，再除以 m11 折算为场景单位）；
    // 缩放基准变化（updateHandleSize）与视口尺寸变化（resizeEvent）两个入口共用。
    void updateScrollMargins() const;
    // 框选两点（视口坐标）→ 场景矩形，四边取整（整数像素吸附）：非整数缩放档位下视口→场景映射带小数，
    // 不取整会框出半像素选区（面板/Core 又只能按整数像素工作）。绘制与上报共用本结果。
    QRectF snappedMarqueeSceneRect(const QPoint& a, const QPoint& b) const;
    // 按倍数缩放并钳制到 CanvasPrefs 允许区间 [zoomMin,zoomMax]，随后刷新手柄尺寸与倍率回报。
    void zoomBy(qreal factor);
    // 退出绘制拖拽态（annoDrawing_ 的唯一置否入口）：收笔 / Esc / 打断 / 离开绘制态均经此。
    // 必在上报落点或作废**之前**置否——右键与 Esc 可在左键仍按住时触发，不退态则随后的释放会再走
    // 一次 gestureStrokeEnd，而上层本笔已结算（pathDraft_ 已清）→ 落点被当成新草稿的首顶点。
    void endStrokeDrag() { annoDrawing_ = false; }
    // 把当前变换的缩放钳制到 CanvasPrefs 允许区间（供 fitToWindow 等绝对变换后收敛回区间）。
    void clampZoom();

    CanvasScene* scene_{nullptr};
    std::unique_ptr<ICanvasInputAdapter> input_;   // 输入适配器（桌面注入桌面实现；Android/iOS 换装触控实现）

    bool spacePan_{false};    // 空格是否按下（进入平移预备态）
    bool panning_{false};     // 正在平移
    QPoint lastPanPos_;       // 平移时的上一视口坐标

    bool rubber_{false};      // 正在框选
    QPoint rubberStart_;      // 框选起点（视口坐标）
    QRubberBand* rubberBand_{nullptr};

    CanvasMode mode_{CanvasMode::Marquee};  // 当前画布交互模式（四态互斥；默认框选）
    bool annoDrawing_{false};               // 正处于标注绘制拖拽手势中
};

} // namespace idc::gui
