// ============================================================================
// 文件：canvas/selection_rect_item.h
// 作用：画布上的「选区框」图元——用户拖拽出的橙色矩形（guideline §4.2：橙色实线 2px），
//       并把被标记的选区边向全图延伸绘制为橙色「贯穿切割线」。它是画布上唯一的橙色交互图元：
//       既能整体移动、四角手柄对角缩放，也能直接抓取任一条边（含其延伸到图像边界的那段）
//       沿法向拖动＝移动该边＝移动对应切割线（线与选区本为一体，绝不双重表示/错位/分离）。
//       移动/缩放/拖边时实时发出 rectChanged，驱动 Core 重算与遮罩即时刷新（NFR-6 / A-0.8）。
// 分块依据：
//   - 本图元只处理「用户如何拖动这个框/这些边」的交互几何（移动/缩放/拖边/吸附），不含切割逻辑；
//     它产出的矩形交给 Document，再由 Core 决定诱导哪些贯穿切割线（A-0.1）；
//   - 哪几条边需延伸为贯穿切割线由场景依 Core CutLineSet 下发（setCutEdges），本图元不自算几何。
//   - 派生自 QGraphicsObject（而非 QGraphicsRectItem）以获得 Q_OBJECT 信号能力与自绘手柄。
// 说明：吸附目标为图像边缘(0/W/H)与中心(W/2,H/2)，阈值随视图缩放动态设定（见 setHandleSize）。
// ============================================================================
#pragma once

#include <QGraphicsObject>
#include <QRectF>

namespace idc::gui {

// ---------------------------------------------------------------------------
// SelectionRectItem：可移动、可缩放、可直接拖边的选区框（并绘制标记边的贯穿切割线）。
// ---------------------------------------------------------------------------
class SelectionRectItem : public QGraphicsObject {
    Q_OBJECT
public:
    explicit SelectionRectItem(QGraphicsItem* parent = nullptr);

    // 设置/读取选区（原图像素坐标，内部规范化为左上-右下）。
    void setRect(const QRectF& r);
    QRectF rect() const { return rect_.normalized(); }

    // 是否正处于用户拖拽手势中（移动/缩放/拖边）。
    // 供上层在拖拽期间跳过对选区框的回设与面板回同步（避免逐帧量化抖动与卡顿）。
    bool isDragging() const { return mode_ != DragMode::None; }

    // 设置图像边界（用于移动/缩放钳制、吸附，以及贯穿切割线的延伸长度）。
    void setImageBounds(int width, int height);

    // 设置手柄边长、抓边条带半宽与吸附阈值（场景单位）；由 CanvasView 依缩放换算，使手柄恒约 8 屏幕px。
    void setHandleSize(qreal sceneUnits);

    // 依 Core 切割线集合下发：哪几条边需延伸绘制为贯穿切割线（并可被直接抓取拖动）。
    // 单矩形/十字(RECT)四边皆有；横带(HLINE)仅上/下；竖带(VLINE)仅左/右。
    void setCutEdges(bool left, bool right, bool top, bool bottom);

    // 是否启用坐标吸附（默认开启，NFR-7）。
    void setSnapEnabled(const bool on) { snapEnabled_ = on; }

    // 高亮态（多矩形场景）：标记本选区框为「当前选中」，绘制时颜色/线宽略微加强以示区分。
    // 单矩形选区不使用（恒 false），外观不变。仅在值变化时重绘（避免拖拽期逐帧无谓 update）。
    void setHighlighted(const bool on);

    // QGraphicsItem 接口。
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

signals:
    // 选区变化（已规范化、已钳制到图像内），参数为原图像素坐标矩形。
    void rectChanged(const QRectF& sceneRect);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

private:
    // 拖拽模式：整体移动 / 四角对角缩放 / 单边法向拖动（＝移动对应切割线）。
    enum class DragMode { None, Move, Resize, Edge };
    // 边序号：0=Left,1=Right,2=Top,3=Bottom。
    enum EdgeIndex { EdgeLeft = 0, EdgeRight = 1, EdgeTop = 2, EdgeBottom = 3 };

    // 返回四个角手柄中心点（顺序：TL, TR, BL, BR）。
    void cornerPoints(QPointF& tl, QPointF& tr, QPointF& bl, QPointF& br) const;
    // 命中测试：返回被点中的角手柄序号(0..3)，未命中返回 -1。
    int hitHandle(const QPointF& scenePos) const;
    // 命中测试：返回被点中的边序号(EdgeLeft..EdgeBottom)，未命中返回 -1。竖边比 x、横边比 y，
    // 落在 ±handleSize_ 抓边条带内且沿线绘制跨度（标记边贯穿全图、否则仅选区那段）即命中。
    int hitEdge(const QPointF& scenePos) const;
    // 把矩形平移钳制到图像边界内。
    void clampToImage(QRectF& r) const;
    // 对当前 rect_ 做边缘/中心吸附。preserveSize=true（整体移动）时**保持宽高不变**，
    // 只按最接近目标的那条边把整个矩形平移到位；false（缩放/拖边）时四边各自独立吸附（本就改变尺寸）。
    void applySnap(bool preserveSize);

    QRectF rect_{};
    int imgW_{0};
    int imgH_{0};
    bool hasBounds_{false};
    qreal handleSize_{8.0};
    bool snapEnabled_{true};
    bool highlighted_{false};  // 多矩形下是否为当前选中项（绘制颜色/线宽略微加强）。
    bool cutEdge_[4]{false, false, false, false}; // 哪几条边延伸为贯穿切割线（场景依 Core 下发）

    DragMode mode_{DragMode::None};
    int dragEdge_{-1};       // Edge 模式下正在拖动的边序号
    QPointF fixedPoint_;     // 缩放时固定不动的对角
    QPointF lastPos_;        // 上次场景坐标（移动增量用）
};

} // namespace idc::gui
