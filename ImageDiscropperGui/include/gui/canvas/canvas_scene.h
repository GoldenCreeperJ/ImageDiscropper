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
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QPixmap>
#include <QVector>

#include "canvas/mask_layer.h"
#include "canvas/grid_layer.h"
#include "canvas/cell_picker_item.h"
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

    // 依 Core 诱导网格刷新 L3 网格线（show=false 或非 L3 模式时清空）。
    // 网格线纯显示，与遮罩/选区解耦；单元区域均来自 Core Grid（A-0.1）。
    // 同时把网格下发给单元点选图元（L3 交互）并按 show 切换其显隐。
    void updateGrid(const idc::engine::Grid& grid, bool show);
    void clearGrid();

    // 仅刷新网格线（不动单元点选图元）——供 L2 多矩形显示 Core 诱导网格，
    // 但不显示 picker（picker 会 grab 鼠标、阻断画布框选追加矩形）。
    void updateGridLines(const idc::engine::Grid& grid, bool show);

    // 依 Document 的多矩形列表刷新可交互的橙色选区框（L2 MULTI_RECT）：每个矩形一个
    // SelectionRectItem（可整体移动 / 四角缩放 / 拖边微调），与单矩形选区体验一致。
    // 增量维护（按数量增删末位图元、逐个刷新几何），拖拽中跳过对正在拖图元的回设。
    void updateMultiRects(const std::vector<idc::engine::RectRegion>& rects);
    void clearMultiRects();
    // 多矩形选区框的手柄尺寸（随视图缩放换算，使手柄恒约 8 屏幕px）。
    void setMultiRectHandleSize(qreal sceneUnits);
    // 是否有任一多矩形选区框正被拖拽（供上层在拖拽期间跳过面板回同步）。
    bool isDraggingMultiRect() const;
    // 设置当前高亮（选中）的多矩形下标（-1 = 无）：对应选区框颜色略微加强，其余恢复普通。
    void setActiveMultiRect(int index);

    // 依 Document 的选择集与排序策略刷新单元点选图元的高亮（L3）。
    void updateCellSelection(const std::vector<int>& selected, idc::engine::SortStrategy strategy);
    CellPickerItem* cellPickerItem() const { return pickerItem_; }

    // L3 框选命中查询：委托单元点选图元返回与场景矩形相交的单元序号（无图元时返回空）。
    // 供 CanvasView 橡皮筋框选从图像外起拖（未被 picker grab）时，仍能在 L3 选中单元。
    std::vector<int> cellsIntersecting(const QRectF& sceneRect) const;

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
    // L3 单元点选（转发自 CellPickerItem）：单击切换 / 拖拽框选，交 MainWindow 写回 Document。
    void cellToggled(int index);
    void cellsMarqueeSelected(const std::vector<int>& indices);
    // L3 CUSTOM 拖拽调序（转发自 CellPickerItem）：把 fromIndex 移到 toIndex 序位。
    void cellReordered(int fromIndex, int toIndex);
    // L2 多矩形：某个选区框被拖动/缩放（转发自对应 SelectionRectItem），index 为其在列表中的下标。
    void multiRectEdited(int index, const QRectF& sceneRect);

private:
    QGraphicsPixmapItem* baseItem_{nullptr};
    MaskLayer maskLayer_;
    GridLayer gridLayer_;
    SelectionRectItem* selItem_{nullptr};
    CellPickerItem* pickerItem_{nullptr};  // L3 单元点选图元（与选区框互斥显隐）
    QVector<SelectionRectItem*> multiRectItems_;  // L2 多矩形可拖拽选区框（下标与 Document::rects() 一致）
    qreal multiHandleSize_{8.0};           // 多矩形选区框手柄尺寸（场景单位，随缩放换算）
    int multiBoundsW_{-1};                 // 上次下发给多矩形图元的图像宽（变化时才重推边界）
    int multiBoundsH_{-1};                 // 上次下发给多矩形图元的图像高
    int activeMultiRect_{-1};              // 当前高亮（选中）的多矩形下标（-1 = 无）

    int imgW_{0};
    int imgH_{0};
    bool masksVisible_{true};
};

} // namespace idc::gui
