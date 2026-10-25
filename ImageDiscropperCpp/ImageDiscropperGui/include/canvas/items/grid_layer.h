// ============================================================================
// 文件：canvas/items/grid_layer.h
// 作用：网格线渲染层——把 Core 诱导网格 Grid 的单元边界画成贯穿线。两种样式（asCutLines）：
//       灰色虚线（L3 网格，本目录 README「画布视觉规范」：1px、z=kGrid）或橙色实线（L2 多矩形诱导切割线，
//       与选区框同一橙色）。与 MaskLayer 同构：线图元纯显示、不接收鼠标、不含几何计算，
//       只按 Core 给出的单元区域绘线（CONTRIBUTING.md「分层纪律」）。
// 分块依据：网格线渲染独立成层，与遮罩(MaskLayer)、选区(SelectionRectItem)解耦，避免
//           CanvasScene 变成上帝类；rebuild 每次先 clear 再按 Grid 重画，setVisible 只切显隐。
// 说明：本层只画贯穿全图的线；单元编号角标（自定义排序）与单元点选交互由 CellPickerItem 承担。
//       L3 另持有两个基准手柄（GridHandleItem：基准点 / 单元尺寸），随本层显隐门控：
//       手柄是该层唯一的可交互物（线图元仍纯显示）。
// ============================================================================
#pragma once

#include <vector>

#include <QGraphicsLineItem>

#include "engine/engine.h"

class QGraphicsScene;

namespace idc::gui {

class GridHandleItem;

// ---------------------------------------------------------------------------
// GridLayer：网格线层（线图元被动显示；L3 额外持两个可拖拽的基准手柄）。
// ---------------------------------------------------------------------------
class GridLayer {
public:
    ~GridLayer();

    // 依 Core 网格重建线；网格为空（或尺寸非正）时清空不画。
    // 线图元恒建出，visible 只决定其可见性（显隐开关直接 setVisible 即可，无需重建）。
    // width/height 为原图尺寸（线贯穿全图，越界的残缺单元边被裁剪忽略）。
    // asCutLines=true 时画为橙色实线（L2 多矩形诱导切割线），否则灰色虚线（L3 网格）；
    // 非 L3（切割线样式）时基准手柄一并退场——基准只在 L3 有意义。
    void rebuild(QGraphicsScene& scene, const engine::Grid& grid,
                 int width, int height, bool visible, bool asCutLines = false);

    // 显隐切换（不重建，仅设可见性；同时切基准手柄）。
    void setVisible(bool visible) const;

    // 改色后重新取 Palette 笔触并回设到已有线图元（pen 在 rebuild 期烘焙、不随 update() 自动变色）。
    void recolor() const;

    // 清空全部网格线图元（从场景移除并释放）；基准手柄保留但退场（下次 L3 复用，免重建/重连信号）。
    void clear();

    // ---- L3 网格基准手柄（见 grid_handle_item.h）----
    // 惰性创建两个手柄（Origin / Cell）加入场景并置顶；创建后由 CanvasScene 连接其 previewed/committed。
    void createHandles(QGraphicsScene& scene);
    bool handlesReady() const { return originHandle_ && cellHandle_; }
    GridHandleItem* originHandle() const { return originHandle_; }
    GridHandleItem* cellHandle() const { return cellHandle_; }
    // 按给定网格参数落位两个手柄并切显隐（visible=false → 隐藏且不可交互）。
    // 拖拽预览时以草稿参数调用，两个手柄随网格线一起跟手。
    void syncHandles(const engine::GridParams& params, int width, int height, bool visible) const;
    void setHandlesVisible(bool visible) const;
    // 四态模式门控：图片拾取 / 标注选取态让位（手柄仍可见，只不吃鼠标）。
    void setHandlesInteractive(bool on) const;
    // 手柄边长（场景单位，随视图缩放换算）。
    void setHandleSize(qreal sceneUnits) const;

private:
    // 只清线图元（不动基准手柄）：rebuild 重建时用——拖拽预览逐帧重铺，若连带隐藏手柄会把正在拖的手柄抽掉。
    void clearLines();

    std::vector<QGraphicsLineItem*> lineItems_;  // 竖线 + 横线（贯穿全图）
    bool lastAsCutLines_{false};                 // 本批样式（true=橙色切割线，false=灰色网格）；供 recolor 取正确笔触。
    GridHandleItem* originHandle_{nullptr};       // 基准点手柄 (x0,y0)（L3）
    GridHandleItem* cellHandle_{nullptr};         // 单元尺寸手柄 (x0+cw, y0+ch)（L3）
};

} // namespace idc::gui
