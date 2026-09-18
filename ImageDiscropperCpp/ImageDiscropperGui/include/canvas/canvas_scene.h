// ============================================================================
// 文件：canvas/canvas_scene.h
// 作用：画布场景——按 z 序图层化组织：底图、遮罩、切割线、选区框。
//       场景坐标统一为「原图像素坐标」；底图用降采样 pixmap 经变换铺到原图尺寸，
//       于是遮罩/切割线/选区都能直接按原图坐标叠加（无需到处做坐标换算）。
// 分块依据：
//   - CanvasScene 只做「图层装配与刷新调度」，把遮罩交给 MaskLayer、选区与切割线交给 SelectionRectItem
//     （切割线即选区标记边的贯穿延伸，同一橙色图元、可直接拖边），自身不实现任何切割/几何（CONTRIBUTING.md「分层纪律」）。
//   - 所有数据来自 Core（EngineResult.kept / CutLineSet）与 Document（选区矩形），场景只渲染。
// 说明：交互（缩放/平移/框选/微调/右键）在 CanvasView；场景只持有并刷新图元。
// ============================================================================
#pragma once

#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QPixmap>
#include <QVector>

#include "canvas/mask_layer.h"
#include "canvas/grid_layer.h"
#include "canvas/cell_picker_item.h"
#include "canvas/selection_rect_item.h"
#include "canvas/annotation_item.h"
#include "engine/engine.h"

namespace idc::gui {

class AnnotationBridge;   // 前置声明：updateAnnotations 只取 const 引用（避免 canvas→model 头耦合）

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
    void updateMasks(const engine::EngineResult& result);
    // 遮罩显隐（右键菜单「切换预览遮罩」）。
    void setMasksVisible(bool visible);
    bool masksVisible() const { return masksVisible_; }

    // 依 Core 切割线集合，判定选区哪几条边有内部贯穿切割线，下发给选区框绘制并允许直接拖动。
    // rect 为当前选区（原图像素）；切割线即选区边的贯穿延伸，与选区同为一个橙色图元（不双重表示）。
    void updateCutLines(const engine::CutLineSet& lines, const engine::RectRegion& rect) const;
    void clearCutLines() const;

    // 依 Core 诱导网格刷新 L3 网格线（show=false 或非 L3 模式时清空）。
    // 网格线纯显示，与遮罩/选区解耦；单元区域均来自 Core Grid（CONTRIBUTING.md「分层纪律」）。
    // 同时把网格下发给单元点选图元（L3 交互）并按 show 切换其显隐。
    void updateGrid(const engine::Grid& grid, bool show);
    void clearGrid();

    // 仅刷新 L2 多矩形的诱导切割线（不动单元点选图元）：以橙色切割线样式画各矩形十字带
    // 并集的诱导线，受 cutLinesVisible_ 门控；不显示 picker（picker 会 grab 鼠标、阻断画布框选追加矩形）。
    void updateMultiRectCutLines(const engine::Grid& grid, bool show);

    // 依 Document 的多矩形列表刷新可交互的橙色选区框（L2 MULTI_RECT）：每个矩形一个
    // SelectionRectItem（可整体移动 / 四角缩放 / 拖边微调），与单矩形选区体验一致。
    // 增量维护（按数量增删末位图元、逐个刷新几何），拖拽中跳过对正在拖图元的回设。
    void updateMultiRects(const std::vector<engine::RectRegion>& rects);
    void clearMultiRects();
    // 多矩形选区框的手柄尺寸（随视图缩放换算，使手柄恒约 8 屏幕px）。
    void setMultiRectHandleSize(qreal sceneUnits);
    // 是否有任一多矩形选区框正被拖拽（供上层在拖拽期间跳过面板回同步）。
    bool isDraggingMultiRect() const;
    // 设置当前高亮（选中）的多矩形下标（-1 = 无）：对应选区框颜色略微加强，其余恢复普通。
    void setActiveMultiRect(int index);

    // 依 Document 的选择集与排序策略刷新单元点选图元的高亮（L3）。
    void updateCellSelection(const std::vector<int>& selected, engine::SortStrategy strategy) const;
    CellPickerItem* cellPickerItem() const { return pickerItem_; }
    // 单元编号角标图层显隐（图层面板「单元编号」开关；角标为单元点选图元的子图层，随其一起显隐）。
    void setCellNumberVisible(bool on) const;

    // L3 框选命中查询：委托单元点选图元返回与场景矩形相交的单元序号（无图元时返回空）。
    // 供 CanvasView 橡皮筋框选从图像外起拖（未被 picker grab）时，仍能在 L3 选中单元。
    std::vector<int> cellsIntersecting(const QRectF& sceneRect) const;

    // 选区框：依 Document 的矩形显示；无选区时隐藏。
    void syncSelection(const engine::RectRegion& rect, bool hasRect) const;
    SelectionRectItem* selectionItem() const { return selItem_; }
    // 选区框是否正被用户拖拽（供上层在拖拽期间跳过回设/面板回同步）。
    bool isDraggingSelection() const { return selItem_ && selItem_->isDragging(); }

    // ---- 标注图层 ----
    // 依 AnnotationBridge 增量维护标注矢量图元（每个 Core Annotation 一个 AnnotationItem）+ 拖拽预览图元。
    // 增量维护（按数量增删末位、逐个刷新几何），绝不 clear+重建（否则拖拽中删除正在处理事件的图元→崩溃）。
    void updateAnnotations(const AnnotationBridge& model);
    // 仅刷新拖拽预览图元（绘制手势逐帧调用），不触碰已提交标注图元（避免逐帧重建/克隆）。
    void updatePendingAnnotation(const AnnotationBridge& model);
    // 标注图层显隐（图层面板开关）：只切图元可见性，不删除。
    void setAnnotationsVisible(bool on);
    // 清空全部标注图元（换图 / 维度变化预处理 / 关闭图像时）。
    void clearAnnotations();
    // 标注控制点手柄尺寸（随视图缩放换算，使其屏幕观感恒定）。
    void setAnnotationHandleSize(qreal sceneUnits);
    // 底图图层显隐（图层面板开关）：仅切换 base 图元可见性，不影响导出。
    void setBaseVisible(bool on) const;

    // ---- 其余图层显隐（图层面板开关；遵循「隐藏图层=不可交互」）----
    // 网格线图层：门控 updateGrid 的 L3 灰色网格可见性（仅置标志，切换后由上层 refreshPreview 重建落地）。
    void setGridVisible(bool on);
    bool gridVisible() const { return gridVisible_; }
    // 切割线图层：影响选区框贯穿延伸段绘制 + L2 多矩形诱导切割线（后者需上层 refreshPreview 重建）。
    void setCutLinesVisible(bool on);
    bool cutLinesVisible() const { return cutLinesVisible_; }
    // 选取边框图层：影响选区框矩形描边/填充/手柄绘制（隐藏同时禁用其拖拽交互）+ L3 单元选择高亮显隐。
    void setSelectionVisible(bool on);
    bool selectionVisible() const { return selectionVisible_; }

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
    // 标注：某标注图元被按下（转发自 AnnotationItem），交上层用 Core hitTest 选中（场景坐标）。
    void annotationSelectRequested(const QPointF& scenePos);
    // 标注：某选中图元被拖动平移（转发自 AnnotationItem），index 为其在标注列表中的下标。
    void annotationMoved(int index, double dx, double dy);
    // 标注：某选中图元的定向包围盒手柄被拖拽（转发自 AnnotationItem），请求缩放(sx,sy)+旋转(deg)。
    void annotationTransformed(int index, double sx, double sy, double rotateDeg);
    // 标注：手柄拖拽**进行中**逐帧预览值（转发自 AnnotationItem），仅供属性面板数值实时回显，不写模型。
    void annotationTransformPreview(int index, double sx, double sy, double rotateDeg);

private:
    QGraphicsPixmapItem* baseItem_{nullptr};
    MaskLayer maskLayer_;
    GridLayer gridLayer_;
    SelectionRectItem* selItem_{nullptr};
    CellPickerItem* pickerItem_{nullptr};  // L3 单元点选图元（与选区框互斥显隐）
    QVector<SelectionRectItem*> multiRectItems_;  // L2 多矩形可拖拽选区框（下标与 Document::rects() 一致）
    QVector<AnnotationItem*> annoItems_;   // 标注矢量图元（下标与 AnnotationBridge::annotations() 一致）
    AnnotationItem* pendingAnnoItem_{nullptr};  // 绘制拖拽预览图元（橡皮筋矢量，不进 Core 历史）
    bool annotationsVisible_{true};        // 标注图层可见性（图层面板开关）
    qreal annoHandleSize_{8.0};            // 标注控制点手柄尺寸（场景单位，随缩放换算）
    qreal multiHandleSize_{8.0};           // 多矩形选区框手柄尺寸（场景单位，随缩放换算）
    int multiBoundsW_{-1};                 // 上次下发给多矩形图元的图像宽（变化时才重推边界）
    int multiBoundsH_{-1};                 // 上次下发给多矩形图元的图像高
    int activeMultiRect_{-1};              // 当前高亮（选中）的多矩形下标（-1 = 无）

    int imgW_{0};
    int imgH_{0};
    bool masksVisible_{true};
    bool gridVisible_{true};       // 网格线图层可见性（图层面板开关）
    bool cutLinesVisible_{true};   // 切割线图层可见性（图层面板开关）
    bool selectionVisible_{true};  // 选取边框图层可见性（图层面板开关）
};

} // namespace idc::gui
