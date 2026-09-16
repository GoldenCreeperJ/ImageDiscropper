// ============================================================================
// 文件：canvas/canvas_view.h
// 作用：画布视图——承载全部画布交互（guideline §4.2 / §4.10）：滚轮缩放、空格或中键平移、
//       左键框选生成选区、方向键微调、右键菜单、光标坐标回报。视图只负责「把用户手势翻译成
//       意图信号」，具体状态变更由 MainWindow 落到 Document，再由 Core 计算（A-0.1）。
// 分块依据：
//   - 交互事件处理集中在本视图；图层渲染在 CanvasScene；两者职责分离，避免上帝类。
//   - 通过信号（rubberSelect / nudgeSelection / cursorScenePos / *Requested）与外界解耦，
//     视图不认识 Document，只发意图，符合「采集交互 → 翻译 → 交上层」的 GUI 定位。
// 说明：手柄尺寸随缩放动态换算（updateHandleSize），保证选区手柄恒约 8 屏幕像素（NFR-7）。
// ============================================================================
#pragma once

#include <QGraphicsView>
#include <QPoint>

class QRubberBand;

namespace idc::gui {

class CanvasScene;

// ---------------------------------------------------------------------------
// CanvasView：画布交互视图。
// ---------------------------------------------------------------------------
class CanvasView : public QGraphicsView {
    Q_OBJECT
public:
    explicit CanvasView(CanvasScene* scene, QWidget* parent = nullptr);

    // 缩放控制（工具栏 / 快捷键调用）。
    void zoomIn();
    void zoomOut();
    void resetZoom();
    void fitToWindow();

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

protected:
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    // 依当前视图缩放换算选区手柄的场景尺寸（约 8 屏幕px），并下发给选区框。
    void updateHandleSize();
    // 按倍数缩放并钳制到允许区间 [kMinZoom, kMaxZoom]，随后刷新手柄尺寸与倍率回报。
    void zoomBy(qreal factor);
    // 把当前变换的缩放钳制回允许区间（供 fitToWindow 等绝对变换后兜底）。
    void clampZoom();
    // 开始平移（中键或空格+左键）。
    void beginPan(const QPoint& viewportPos);

    CanvasScene* scene_{nullptr};

    bool spacePan_{false};    // 空格是否按下（进入平移预备态）
    bool panning_{false};     // 正在平移
    QPoint lastPanPos_;       // 平移时的上一视口坐标

    bool rubber_{false};      // 正在框选
    QPoint rubberStart_;      // 框选起点（视口坐标）
    QRubberBand* rubberBand_{nullptr};
};

} // namespace idc::gui
