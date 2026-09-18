// ============================================================================
// 文件：canvas/mask_layer.h
// 作用：管理画布上的「保留/删除预览遮罩」（guideline §4.2 / §5.3）——保留块绿色半透明、
//       删除区红色半透明，让用户一眼看懂反向剔除。遮罩区域完全来自 Core 的 EngineResult，
//       本层只负责按结果创建/销毁 QGraphicsRectItem，不做任何区域布尔运算（A-0.1）。
// 分块依据：把遮罩这一图层的生命周期与 CanvasScene 的其它图层（底图/切割线/选区）解耦，
//           单独成类，避免 CanvasScene 变成上帝类。
// 说明（渲染法）：Core 的 EngineResult 只给「保留集 kept」。为在 GUI 不做几何求补，
//       采用「先铺满全图的红色删除底，再在其上叠加绿色保留块」——绿覆盖处为保留、
//       透出红处即删除。z 序见 canvas/z_order.h（kDelete < kKeep）。
// ============================================================================
#pragma once

#include <vector>

#include <QGraphicsRectItem>

#include "engine/engine.h"

class QGraphicsScene;

namespace idc::gui {

// ---------------------------------------------------------------------------
// MaskLayer：保留(绿)/删除(红)遮罩图层。
// ---------------------------------------------------------------------------
class MaskLayer {
public:
    // 依 Core 结果重建遮罩。visible=false 或结果不成功时清空（不显示遮罩）。
    // W/H 为原图尺寸（场景坐标范围），kept 区域按原图像素坐标叠加。
    void rebuild(QGraphicsScene& scene, const engine::EngineResult& result,
                 int width, int height, bool visible);

    // 显隐切换（不重建，仅设可见性；对应右键菜单「切换预览遮罩」）。
    void setVisible(bool visible) const;

    // 清空全部遮罩图元（从场景移除并释放）。
    void clear();

private:
    QGraphicsRectItem* deleteBase_{nullptr};          // 铺满全图的红色删除底
    std::vector<QGraphicsRectItem*> keepItems_;       // 绿色保留块（每个 kept 片段一个）
};

} // namespace idc::gui
