// ============================================================================
// 文件：canvas/grid_layer.h
// 作用：网格线渲染层——把 Core 诱导网格 Grid 的单元边界画成贯穿线。两种样式（asCutLines）：
//       灰色虚线（L3 网格，本目录 README「画布视觉规范」：1px、z=kGrid）或橙色实线（L2 多矩形诱导切割线，
//       与选区框同一橙色）。与 MaskLayer 同构：纯显示、不接收鼠标、不含几何计算，
//       只按 Core 给出的单元区域绘线（CONTRIBUTING.md「分层纪律」）。
// 分块依据：网格线渲染独立成层，与遮罩(MaskLayer)、选区(SelectionRectItem)解耦，避免
//           CanvasScene 变成上帝类；rebuild 每次先 clear 再按 Grid 重画，setVisible 只切显隐。
// 说明：本层只画贯穿全图的线；单元编号角标（自定义排序）与单元点选交互由 CellPickerItem 承担。
// ============================================================================
#pragma once

#include <vector>

#include <QGraphicsLineItem>

#include "engine/engine.h"

class QGraphicsScene;

namespace idc::gui {

// ---------------------------------------------------------------------------
// GridLayer：网格线层（被动显示；灰虚线网格或橙色切割线）。
// ---------------------------------------------------------------------------
class GridLayer {
public:
    // 依 Core 网格重建线；visible=false 或网格为空时清空（不画）。
    // width/height 为原图尺寸（线贯穿全图，越界的残缺单元边被裁剪忽略）。
    // asCutLines=true 时画为橙色实线（L2 多矩形诱导切割线），否则灰色虚线（L3 网格）。
    void rebuild(QGraphicsScene& scene, const engine::Grid& grid,
                 int width, int height, bool visible, bool asCutLines = false);

    // 显隐切换（不重建，仅设可见性）。
    void setVisible(bool visible) const;

    // 清空全部网格线图元（从场景移除并释放）。
    void clear();

private:
    std::vector<QGraphicsLineItem*> lineItems_;  // 竖线 + 横线（贯穿全图）
};

} // namespace idc::gui
