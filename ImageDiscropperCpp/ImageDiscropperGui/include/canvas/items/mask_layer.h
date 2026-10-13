// ============================================================================
// 文件：canvas/items/mask_layer.h
// 作用：管理画布上的「保留/删除预览遮罩」（本目录 README「画布视觉规范」）——保留块绿色半透明、
//       删除区红色半透明，让用户一眼看懂反向剔除。遮罩区域完全来自 Core 的 EngineResult，
//       本层只负责按结果创建/销毁单一遮罩图元，不做任何区域布尔运算（CONTRIBUTING.md「分层纪律」）。
// 分块依据：把遮罩这一图层的生命周期与 CanvasScene 的其它图层（底图/切割线/选区）解耦，
//           单独成类，避免 CanvasScene 变成上帝类。
// 说明（渲染法，单层合并）：Core 的 EngineResult 只给「保留集 kept」。为在 GUI 不做几何求补，
//       MaskLayerItem 在**单一图元、单一 z 层（kMask）**内先铺满全图的红色删除底、再在其上叠绿色
//       保留块——绿覆盖处为保留、透出红处即删除。保留/删除不再拆成两个图元/两层（旧 kDelete/kKeep 合并）。
// ============================================================================
#pragma once

#include <QGraphicsItem>
#include <QRectF>

#include <vector>

#include "engine/engine.h"

class QGraphicsScene;

namespace idc::gui {

// ---------------------------------------------------------------------------
// MaskLayerItem：单一遮罩图元——paint 内先铺红色删除底（整幅），再叠绿色保留块。
//   纯显示、不接收鼠标、不可选中（避免遮挡选区交互）。
// ---------------------------------------------------------------------------
class MaskLayerItem : public QGraphicsItem {
public:
    // 载入整幅尺寸（原图像素坐标）与保留块矩形集合；变化时重置几何缓存并重绘。
    void setGeometry(int width, int height, std::vector<QRectF> keeps);

    // QGraphicsItem 接口。
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

private:
    int width_{0};
    int height_{0};
    std::vector<QRectF> keeps_;
    // 包围盒 = 整幅原图 ∪ 全部保留块。L3 的 PAD 余量策略下 Core 保留块可伸出图像边界，
    //   不并入则那部分不算重绘区域，需缩放触发全量重绘才显示（与 CellPickerItem 同理）。
    QRectF bounds_;
};

// ---------------------------------------------------------------------------
// MaskLayer：保留(绿)/删除(红)遮罩图层（单一 MaskLayerItem 承载）。
// ---------------------------------------------------------------------------
class MaskLayer {
public:
    // 依 Core 结果重建遮罩。visible=false 或结果不成功时清空（不显示遮罩）。
    // W/H 为原图尺寸（场景坐标范围），kept 区域按原图像素坐标叠加。
    void rebuild(QGraphicsScene& scene, const engine::EngineResult& result,
                 int width, int height, bool visible);

    // 显隐切换（不重建，仅设可见性；对应右键菜单「切换预览遮罩」）。
    void setVisible(bool visible) const;

    // 清空遮罩图元（从场景移除并释放）。
    void clear();

private:
    MaskLayerItem* item_{nullptr};   // 单一遮罩图元（红色删除底 + 绿色保留块合成于一层）。
};

} // namespace idc::gui
