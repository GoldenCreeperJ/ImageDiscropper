// ============================================================================
// 文件：canvas/canvas_view.h
// 作用：画布视图——承载全部画布交互（本目录 README「界面概览」与「快捷键列表」）：滚轮缩放、空格或中键平移、
//       左键框选生成选区、方向键微调、右键菜单、光标坐标回报。视图只负责「把用户手势翻译成
//       意图信号」，具体状态变更由 MainWindow 落到 Document，再由 Core 计算（CONTRIBUTING.md「分层纪律」）。
// 分块依据：
//   - 输入适配器分层（GuideLine 阶段 2）：原始事件的设备语义（哪个键 / 滚轮刻度 / 右键菜单 UI）
//     收拢在 ICanvasInputAdapter（桌面实现 DesktopInputAdapter），本视图只保留业务手势执行面
//     （gesture* 系列：平移 / 标注绘制 / 框选 / 缩放 / 悬停的状态机与意图信号），事件入口仅一行转发；
//   - 键盘（Esc / 空格 / 方向键）是桌面专属通道，仍留在本视图；移动端由补偿 UI 调同一 gesture* 入口；
//   - 通过信号（rubberSelect / nudgeSelection / cursorScenePos / *Requested）与外界解耦，
//     视图不认识 Document，只发意图，符合「采集交互 → 翻译 → 交上层」的 GUI 定位。
// 说明：手柄尺寸随缩放动态换算（updateHandleSize），保证选区手柄恒约 8 屏幕像素（NFR-7）。
// ============================================================================
#pragma once

#include <memory>

#include <QGraphicsView>
#include <QPoint>

class QRubberBand;
class QContextMenuEvent;
class QMouseEvent;

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

    // 标注绘制态门控（G-4）：为 true 时左键手势路由到标注绘制（橡皮筋选区让位）；
    // 为 false 时维持既有选区 / 单元交互（标注 SELECT 工具下标注图元仍自行响应选中 / 拖动）。
    void setAnnotationDrawActive(bool on);
    bool annotationDrawActive() const { return annotationDrawActive_; }

    // ——交互状态查询（供输入适配器路由，语义与原视图内部标志一致）——
    bool panArmed() const { return spacePan_; }          // 空格按住（平移预备态）
    bool panning() const { return panning_; }            // 正在平移
    bool strokeActive() const { return annoDrawing_; }   // 标注绘制拖拽中
    bool marqueeActive() const { return rubber_; }       // 框选橡皮筋带进行中

    // ——场景事件交付（供适配器：把原始事件交给 QGraphicsView 默认实现，图元优先抓取）——
    void dispatchScenePress(QMouseEvent* event) { QGraphicsView::mousePressEvent(event); }
    void dispatchSceneMove(QMouseEvent* event) { QGraphicsView::mouseMoveEvent(event); }
    void dispatchSceneRelease(QMouseEvent* event) { QGraphicsView::mouseReleaseEvent(event); }

    // ——业务手势执行面（ICanvasInputAdapter 的唯一调用目标；坐标一律为视口坐标）——
    // 平移：起 / 更新（手写滚动条位移，不与图元 DragMode 争用）/ 收。
    void gesturePanBegin(const QPoint& viewportPos);
    void gesturePanUpdate(const QPoint& viewportPos);
    void gesturePanEnd();
    // 取消进行中的框选/平移（触控捏合开始前调用：第一指合成鼠标事件可能在手势
    // 识别前已启动框选——不取消则缩放与框选冲突）。标注绘制态不在此取消。
    void gestureCancelCurrent();
    // 双指平移（触控）：按视口位移量直接滚动，与 gesturePanUpdate 同一执行链路。
    void gesturePanDelta(const QPoint& deltaViewport) const;
    // 标注绘制手势：起笔 / 拖拽 / 悬停（未按键）/ 释放 / 收笔（右键或等效）。
    void gestureStrokeBegin(const QPoint& viewportPos);
    void gestureStrokeUpdate(const QPoint& viewportPos);
    void gestureStrokeHover(const QPoint& viewportPos);
    void gestureStrokeEnd(const QPoint& viewportPos);
    void gestureStrokeFinish();
    // 框选（橡皮筋带）：起 / 更新 / 收（释放时过滤 ≤3px 误触并映射回场景坐标）。
    void gestureMarqueeBegin(const QPoint& viewportPos);
    void gestureMarqueeUpdate(const QPoint& viewportPos) const;
    void gestureMarqueeEnd(const QPoint& viewportPos);
    // 悬停移动：回报光标场景坐标（状态栏）。
    void gestureCursorMoved(const QPoint& viewportPos);
    // 步进缩放（桌面滚轮一格 ×1.15；工具栏 ± 走 zoomIn/Out，另成一路）。
    void gestureStepZoom(qreal factor);
    // 右键菜单项意图代理（菜单 UI 在桌面适配器；此处补发既有意图信号）。
    void requestClearCut();
    void requestResetView();
    void requestToggleMasks();
    // 无键盘补偿入口（移动端 8 向步进盘 / 「取消与完成」按钮，SPEC §8.3）：
    // 与键盘同一条信号链路——方向键微调与绘制态 Esc 的触控等价物。
    void requestNudge(int dx, int dy);
    void requestAnnoEscape();

signals:
    // 光标在场景（原图）坐标中的位置，供状态栏显示。
    void cursorScenePos(const QPointF& scenePos);
    // 视图缩放倍数变化（1.0 = 100%），供状态栏显示放大倍数。
    void zoomChanged(qreal factor);
    // 用户框选出一个新选区（场景/原图坐标，已规范化）。
    void rubberSelect(const QRectF& sceneRect);
    // 方向键微调选区（dx,dy 为原图像素增量，Shift 时为大步）。
    void nudgeSelection(int dx, int dy);
    // 右键菜单意图。
    void clearCutRequested();
    void resetViewRequested();
    void toggleMasksRequested();
    // 标注绘制手势（仅 setAnnotationDrawActive(true) 时发出，场景/原图坐标）。
    // 上层（MainWindow）依当前工具决定语义：两点形状→拖拽成形；折线/画笔→逐点追加；文字→落点取文本。
    void annoDragStart(const QPointF& scenePos);
    void annoDragMove(const QPointF& scenePos);
    void annoDragEnd(const QPointF& scenePos);
    // 绘制态下鼠标悬停（未按键移动）：供折线「橡皮筋」实时预览落点与连线（左键点击才落顶点）。
    void annoHover(const QPointF& scenePos);
    // 绘制态下右键：收笔 / 退出当前绘制手势（折线在此结束并提交，不再弹视图右键菜单）。
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
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    // 手势事件（QEvent::Gesture，阶段 3 触控 pinch 缩放）：交适配器消费，未消费则照常下发。
    bool event(QEvent* event) override;

private:
    // 依当前视图缩放换算选区手柄的场景尺寸（屏幕基准由适配器提供：桌面约 8 屏幕px，
    // 触控 ≥12pt，SPEC §8.3），并下发给选区框。
    void updateHandleSize();
    // 按倍数缩放并钳制到允许区间 [kMinZoom, kMaxZoom]，随后刷新手柄尺寸与倍率回报。
    void zoomBy(qreal factor);
    // 把当前变换的缩放钳制回允许区间（供 fitToWindow 等绝对变换后收敛回区间）。
    void clampZoom();

    CanvasScene* scene_{nullptr};
    std::unique_ptr<ICanvasInputAdapter> input_;   // 输入适配器（构造时注入桌面实现；阶段 3 换装触控实现）

    bool spacePan_{false};    // 空格是否按下（进入平移预备态）
    bool panning_{false};     // 正在平移
    QPoint lastPanPos_;       // 平移时的上一视口坐标

    bool rubber_{false};      // 正在框选
    QPoint rubberStart_;      // 框选起点（视口坐标）
    QRubberBand* rubberBand_{nullptr};

    bool annotationDrawActive_{false};  // 标注绘制态门控（绘制工具激活时为 true）
    bool annoDrawing_{false};           // 正处于标注绘制拖拽手势中
};

} // namespace idc::gui
