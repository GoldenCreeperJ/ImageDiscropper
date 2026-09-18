// ============================================================================
// 文件：canvas/cell_picker_item.h
// 作用：L3 网格「单元点选」交互图元（FR-L3.3：单击切换、拖拽框选）——覆盖整幅图像，
//       依 Core Grid 的单元区域做命中测试：单击某单元发 cellToggled，拖拽框选发
//       cellsMarqueeSelected（框内单元序号）；并把当前选择集高亮叠加在单元上（独立于极性）。
//       CUSTOM 排序下（FR-L3.5）额外在每个已选单元角标显示其自定义序号（点选顺序），
//       并支持把某已选单元拖到另一已选单元上调序（发 cellReordered）。
// 分块依据：
//   - 与 SelectionRectItem 并列的第二个交互图元，但二者互斥显隐（L1/L2 用选区框、L3 用点选）；
//     本图元只做「命中测试 + 手势翻译成信号」，不含网格几何——单元区域全部来自 Core Grid（A-0.1）。
//   - 选择集的集合运算（toggle / union）落在 Document；本图元不认识 Document，只发意图信号，
//     与 CanvasView「采集交互 → 翻译 → 交上层」的定位一致。
//   - 派生自 QGraphicsObject（而非 QGraphicsItem）以获得 Q_OBJECT 信号能力（同 SelectionRectItem）。
// 说明：非 CUSTOM 排序不显示角标（输出序由 Core 按行/列主序计算，网格上直观可见）。
// ============================================================================
#pragma once

#include <vector>

#include <QGraphicsObject>
#include <QRectF>

#include "engine/engine.h"

namespace idc::gui {

// ---------------------------------------------------------------------------
// CellPickerItem：L3 单元点选/框选交互图元（并高亮当前选择集）。
// ---------------------------------------------------------------------------
class CellPickerItem : public QGraphicsObject {
    Q_OBJECT
public:
    explicit CellPickerItem(QGraphicsItem* parent = nullptr);

    // 依 Core 网格更新可点选的单元几何（cells 的区域与序号）；width/height 为原图尺寸。
    void setGrid(const engine::Grid& grid, int width, int height);
    // 依 Document 更新当前选择集（有序；CUSTOM 下其顺序即自定义序）与排序策略。
    void setSelection(const std::vector<int>& selected, engine::SortStrategy strategy);
    // 视图缩放换算的覆盖层尺度（≈8 屏幕px 对应的场景单位）：用于单击/拖拽阈值与边框观感。
    void setOverlayScale(qreal sceneUnits);

    // 是否正处于拖拽框选手势中。
    bool isMarqueeing() const { return marquee_; }

    // 框选命中查询：返回与给定场景矩形相交的全部单元序号（纯几何查询，不含网格推导）。
    // 供 CanvasView 的橡皮筋框选（可能从图像外起拖，未被本图元 grab）在 L3 下路由到单元选择。
    std::vector<int> cellsIntersecting(const QRectF& sceneRect) const;

    // QGraphicsItem 接口。
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

signals:
    // 单击某单元（Core 序号），交上层在 Document 里切换其选中态。
    void cellToggled(int index);
    // 拖拽框选命中的单元序号集合，交上层并入 Document 选择集。
    void cellsMarqueeSelected(const std::vector<int>& indices);
    // CUSTOM 拖拽调序：把 fromIndex 单元移到 toIndex 单元所在的序位（交上层重排 Document 选择集）。
    void cellReordered(int fromIndex, int toIndex);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void hoverMoveEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;

private:
    // 命中测试：场景点 → 单元序号（落在某单元区域内）；无则 -1。
    int cellIndexAt(const QPointF& scenePos) const;
    // 依 selected_ 重建 O(1) 选中查表（selFlag_）与序号位查表（selPos_，CUSTOM 角标用）。
    void rebuildSelectionLookup();
    // 局部重绘某序号单元（悬停反馈用；避免大选集下整层重绘）。
    void updateCell(int index);

    std::vector<engine::Cell> cells_;  // Core 单元（区域 + 序号）：命中测试与高亮的唯一几何来源。
    std::vector<int> selected_;             // 当前选择集（有序）。
    std::vector<char> selFlag_;             // 按单元序号索引的选中标记（O(1) 查询）。
    std::vector<int> selPos_;               // 按单元序号索引的自定义序位（1-based；0=未选）。
    engine::SortStrategy strategy_{engine::SortStrategy::ROW_MAJOR};
    int imgW_{0};
    int imgH_{0};
    qreal overlayScale_{8.0};

    int hoverIndex_{-1};    // 悬停单元序号（高亮反馈）；无则 -1。
    bool pressing_{false};  // 左键按下中。
    bool marquee_{false};   // 已进入拖拽框选（移动超过阈值）。
    int pressIndex_{-1};    // 按下点命中的单元序号（无则 -1）。
    bool reorderDrag_{false}; // 已进入 CUSTOM 拖拽调序。
    int reorderTarget_{-1};   // 调序拖拽的当前目标单元序号。
    QPointF pressPos_;      // 按下点（场景坐标）。
    QPointF curPos_;        // 当前拖拽点（场景坐标）。
};

} // namespace idc::gui
