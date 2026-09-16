// ============================================================================
// 文件：canvas/canvas_scene.h
// 作用：画布场景——按 guideline §4.2 的 z 序图层化组织：底图、遮罩、切割线、选区框。
//       场景坐标统一为「原图像素坐标」；底图用降采样 pixmap 经变换铺到原图尺寸，
//       于是遮罩/切割线/选区都能直接按原图坐标叠加（无需到处做坐标换算）。
// 分块依据：
//   - CanvasScene 只做「图层装配与刷新调度」，把遮罩交给 MaskLayer、选区与切割线交给 SelectionRectItem
//     （切割线即选区标记边的贯穿延伸，同一橙色图元、可直接拖边），自身不实现任何切割/几何（A-0.1）。
//   - 所有数据来自 Core（EngineResult.kept / CutLineSet）与 Document（选区矩形），场景只渲染。
// 说明：交互（缩放/平移/框选/微调/右键）在 CanvasView；场景只持有并刷新图元。
// ============================================================================
#pragma once

#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QPixmap>

#include "canvas/mask_layer.h"
#include "canvas/selection_rect_item.h"
#include "engine/engine.h"

namespace idc::gui {

// ---------------------------------------------------------------------------
// CanvasScene：图层化画布场景。
// ---------------------------------------------------------------------------
class CanvasScene : public QGraphicsScene {
    Q_OBJECT
public:
    explicit CanvasScene(QObject* parent = nullptr);

    // 设置底图：preview 为降采样 pixmap，scaleX/Y 为预览→原图放大系数，fullW/H 为原图尺寸。
    // 会同步设定场景矩形与选区框的图像边界。
    void setBaseImage(const QPixmap& preview, double scaleX, double scaleY, int fullW, int fullH);

    // 清空画布全部内容（换图前 / 关闭图像时）。
    void clearAll();

    // 依 Core 结果刷新保留(绿)/删除(红)遮罩。
    void updateMasks(const idc::engine::EngineResult& result);
    // 遮罩显隐（右键菜单「切换预览遮罩」）。
    void setMasksVisible(bool visible);
    bool masksVisible() const { return masksVisible_; }

    // 依 Core 切割线集合，判定选区哪几条边有内部贯穿切割线，下发给选区框绘制并允许直接拖动。
    // rect 为当前选区（原图像素）；切割线即选区边的贯穿延伸，与选区同为一个橙色图元（不双重表示）。
    void updateCutLines(const idc::engine::CutLineSet& lines, const idc::engine::RectRegion& rect);
    void clearCutLines();

    // 选区框：依 Document 的矩形显示；无选区时隐藏。
    void syncSelection(const idc::engine::RectRegion& rect, bool hasRect);
    SelectionRectItem* selectionItem() const { return selItem_; }
    // 选区框是否正被用户拖拽（供上层在拖拽期间跳过回设/面板回同步）。
    bool isDraggingSelection() const { return selItem_ && selItem_->isDragging(); }

    int imageWidth() const { return imgW_; }
    int imageHeight() const { return imgH_; }

signals:
    // 选区框被用户移动/缩放/拖边（转发自 SelectionRectItem::rectChanged），参数为原图像素坐标矩形。
    // 拖动选区边＝移动对应切割线（二者同一图元），故只需这一个稳定信号。
    void selectionEdited(const QRectF& sceneRect);

private:
    QGraphicsPixmapItem* baseItem_{nullptr};
    MaskLayer maskLayer_;
    SelectionRectItem* selItem_{nullptr};

    int imgW_{0};
    int imgH_{0};
    bool masksVisible_{true};
};

} // namespace idc::gui
