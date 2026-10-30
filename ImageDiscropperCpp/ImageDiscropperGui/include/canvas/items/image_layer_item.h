// ============================================================================
// 文件：canvas/items/image_layer_item.h
// 作用：多层画布上「单个图片图层」的落位框图元——以 canvas::layerPlacement 得到的画布坐标
//       矩形，用青色虚线框叠加渲染（不改底图像素，本图层位于底图之上、标注之下，见 z_order）。
//       默认仅示意（不可交互，Qt::NoButton，不抢既有框选/标注/选区交互）；
//       图层拾取模式开启时经 setInteractive(true) 允许左键抓取：选中由场景按下链路发 imageLayerPressed
//       （本图元不发按下信号），拖拽本地图元位移作视觉反馈，释放发 moved(id, dxPx, dyPx) 交上层把偏移
//       累加进 ImageTransform。
// 分块依据：图元只做「渲染一条落位框 + 拖动它」，落位几何来自 Core layerPlacement
//          （GUI 不自算锚点/枢轴/缩放，CONTRIBUTING.md「分层纪律」）；提交后由上层改声明态重组展平。
//          平移手势循环与 setPos 视觉反馈在基类 DraggableCanvasItem，本图元经 onDragFinished
//          翻译成 moved 信号（拖拽安全：释放先复位 pos 再送达，由基类保证）。
// 说明：落位框渲染部分仿 SelectionRectItem 的 QGraphicsObject 范式（自绘）。
// ============================================================================
#pragma once

#include <QPointF>
#include <QRectF>

#include "canvas/items/draggable_canvas_item.h"   // 平移拖拽手势基类（手势循环 + setPos 视觉反馈；本类留送达翻译）

namespace idc::gui {

// ---------------------------------------------------------------------------
// ImageLayerItem：单条图片图层的落位框（可拾取拖拽，默认关）。
// ---------------------------------------------------------------------------
class ImageLayerItem : public DraggableCanvasItem {
    Q_OBJECT
public:
    explicit ImageLayerItem(QGraphicsItem* parent = nullptr);

    void setLayerId(const int id) { id_ = id; }
    int layerId() const { return id_; }

    // 层序堆叠键（由场景逐帧刷新）：Core 的 zOrder（叠放次序**属性**，越大越靠顶）与该层在
    //   Document::layers() 数组中的位序（同 zOrder 时靠后者在上，SPEC §7）——两个正交维度，
    //   与 Qt z 无关：本图元不解释其含义，只供 CanvasScene::layerStackAt 作两级排序判据消费。
    void setStackKeys(const int zOrder, const int arrayIndex) { zOrder_ = zOrder; arrayIndex_ = arrayIndex; }
    int stackZOrder() const { return zOrder_; }
    int stackArrayIndex() const { return arrayIndex_; }

    // 载入落位矩形（场景/画布坐标，左上原点）；变化时重置几何缓存并重绘。
    void setPlacement(const QRectF& rect);
    QRectF placement() const { return rect_; }

    // 选中态：描边加粗高亮（当前预处理/编辑目标）。
    void setSelectedFrame(bool on);
    bool selectedFrame() const { return selected_; }

    // 交互态（图层拾取模式）：true 时由场景拾取命中作为候选可拖；false 时不作拾取候选。
    //   注：拾取/拖拽由 CanvasScene 统一路由（场景按下分支在基类分发前处理），但图元仍须吃左键——
    //   输入适配器按「接受左键」判定交互图元并即时路由，不吃则图层按下被单击延迟判定扣住（双击窗口）。
    void setInteractive(bool on);
    bool interactive() const { return interactive_; }

    // 平移拖拽手势在基类 DraggableCanvasItem（beginTranslationDrag / dragMoveTo / endTranslationDrag，
    // 由 CanvasScene 在拾取模式下驱动）；按下即选中本层由 pressed 信号链路完成，见 signals 注释。

    // QGraphicsItem 接口。
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

signals:
    // 拖拽结束：请求把该图层落位平移 (dxPx, dyPx) 个画布像素（累加进 ImageTransform.x/y）。
    void moved(int id, int dxPx, int dyPx);

protected:
    // 平移手势送达（基类钩子）：非原地点击→一次性发 moved(id, dxPx, dyPx) 交上层累加进 transform
    //（先复位视觉位移再送达，由基类 endTranslationDrag 保证）；点击仅作循环选层，不发 moved。
    void onDragFinished(bool wasClick, const QPointF& scenePos, double dx, double dy) override;

private:
    int id_{0};
    int zOrder_{0};       // Core 叠放次序属性（层序一级判据，与 Qt z 无关）。
    int arrayIndex_{0};   // 在 layers_ 数组中的位序（同 zOrder 时破平，与 zOrder 正交）。
    QRectF rect_{};        // 落位矩形（item 坐标＝场景坐标，pos 恒 0，除拖拽临时位移）。
    bool selected_{false};
    bool interactive_{false};
};

} // namespace idc::gui
