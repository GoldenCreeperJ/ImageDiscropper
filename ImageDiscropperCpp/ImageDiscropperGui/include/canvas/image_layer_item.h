// ============================================================================
// 文件：canvas/image_layer_item.h
// 作用：多层画布上「单个图片图层」的落位框图元——以 canvas::layerPlacement 得到的画布坐标
//       矩形，用青色虚线框叠加渲染（不改底图像素，标注在底图之上、标注之下，见 z_order）。
//       默认仅示意（不可交互，Qt::NoButton，不抢既有框选/标注/选区交互）；
//       图层拾取模式开启时经 setInteractive(true) 允许左键抓取：按下发 pressed(id) 选中，
//       拖拽本地图元位移作视觉反馈，释放发 moved(id, dxPx, dyPx) 交上层把偏移累加进 ImageTransform。
// 分块依据：图元只做「渲染一条落位框 + 拖动它」，落位几何来自 Core layerPlacement
//          （GUI 不自算锚点/枢轴/缩放，CONTRIBUTING.md「分层纪律」）；提交后由上层改声明态重组展平。
// 说明：仿 SelectionRectItem 的 QGraphicsObject 范式（自绘 + 自管拖拽）；拖拽安全——拖动期只 setPos
//       视觉反馈，释放先复位 pos 再一次性发 moved，场景以增量方式刷新（数量不变不删正在拖者）。
// ============================================================================
#pragma once

#include <QGraphicsObject>
#include <QPointF>
#include <QRectF>

namespace idc::gui {

// ---------------------------------------------------------------------------
// ImageLayerItem：单条图片图层的落位框（可拾取拖拽，默认关）。
// ---------------------------------------------------------------------------
class ImageLayerItem : public QGraphicsObject {
    Q_OBJECT
public:
    explicit ImageLayerItem(QGraphicsItem* parent = nullptr);

    void setLayerId(const int id) { id_ = id; }
    int layerId() const { return id_; }

    // 载入落位矩形（场景/画布坐标，左上原点）；变化时重置几何缓存并重绘。
    void setPlacement(const QRectF& rect);
    QRectF placement() const { return rect_; }

    // 选中态：描边加粗高亮（当前预处理/编辑目标）。
    void setSelectedFrame(bool on);
    bool selectedFrame() const { return selected_; }

    // 交互态（图层拾取模式）：true 时由场景拾取命中作为候选可拖；false 时不作拾取候选。
    //   注：图元自身不再直接接受鼠标（恒 Qt::NoButton），拾取/拖拽统一由 CanvasScene 路由（支持重叠循环选层）。
    void setInteractive(bool on);
    bool interactive() const { return interactive_; }

    // 是否正处于拖拽手势中（供上层在拖拽期跳过对本图元的回设）。
    bool isDragging() const { return dragging_; }

    // ---- 拖拽手势三段（由 CanvasScene 在拾取模式下驱动，不再依赖图元自身鼠标事件）----
    // beginDrag：按下即选中本层（发 pressed(id)）并复位任何遗留位移，从本次场景坐标起拖。
    void beginDrag(const QPointF& scenePos);
    // dragMove：逐帧累加视觉位移（setPos 反馈，不回写模型）。
    void dragMove(const QPointF& scenePos);
    // dragEnd：复位视觉位移并一次性发出 moved(id, dx, dy) 交上层累加进 transform。
    void dragEnd(const QPointF& scenePos);

    // QGraphicsItem 接口。
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

signals:
    // 图层被拾取按下：上报图层 id，交上层选中（doc_.selectLayer）。
    void pressed(int id);
    // 拖拽结束：请求把该图层落位平移 (dxPx, dyPx) 个画布像素（累加进 ImageTransform.x/y）。
    void moved(int id, int dxPx, int dyPx);

private:
    int id_{0};
    QRectF rect_{};        // 落位矩形（item 坐标＝场景坐标，pos 恒 0，除拖拽临时位移）。
    bool selected_{false};
    bool interactive_{false};

    bool dragging_{false};
    QPointF lastScenePos_{};   // 上次场景坐标（拖拽增量用）。
    QPointF accumDelta_{};     // 本次手势累计位移（setPos 视觉反馈）。
};

} // namespace idc::gui
