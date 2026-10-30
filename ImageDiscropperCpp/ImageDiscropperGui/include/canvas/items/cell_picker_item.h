// ============================================================================
// 文件：canvas/items/cell_picker_item.h
// 作用：L3 网格「单元点选」交互图元（FR-L3.3：单击切换、拖拽框选）——覆盖整幅图像，
//       依 Core Grid 的单元区域做命中测试：单击某单元发 cellToggled，拖拽框选发
//       cellsMarqueeSelected（框内单元序号）；并把当前选择集高亮叠加在单元上（独立于极性）。
//       Custom 排序下（FR-L3.5）额外在每个已选单元角标显示其自定义序号（点选顺序），
//       并支持把某已选单元拖到任一单元上调序/替换（发 cellReordered）：已选目标互换位次、未选目标原位替换。
// 分块依据：
//   - 与 SelectionRectItem 并列的第二个交互图元，但二者互斥显隐（L1/L2 用选区框、L3 用点选）；
//     本图元只做「命中测试 + 手势翻译成信号」，不含网格几何——单元区域全部来自 Core Grid（CONTRIBUTING.md「分层纪律」）。
//   - 选择集的集合运算（toggle / union）落在 Document；本图元不认识 Document，只发意图信号，
//     与 CanvasView「采集交互 → 翻译 → 交上层」的定位一致。
//   - 派生自 QGraphicsObject（而非 QGraphicsItem）以获得 Q_OBJECT 信号能力（同 SelectionRectItem）。
// 说明：非 Custom 排序不显示角标（输出序由 Core 按行/列主序计算，网格上直观可见）；
//       「单元编号」图层隐藏时角标不绘制，Custom 拖动调序一并停用。
// ============================================================================
#pragma once

#include <vector>

#include <QGraphicsObject>
#include <QRectF>

#include "engine/engine.h"
#include "util/gui_nums.h"   // nums::canvas（覆盖带尺寸的场景口径兜底初值）

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
    // 依 Document 更新当前选择集（有序；Custom 下其顺序即自定义序）与排序策略。
    void setSelection(const std::vector<int>& selected, engine::SortStrategy strategy);
    // 视图缩放换算的覆盖层尺度（≈8 屏幕px 对应的场景单位）：用于单击/拖拽阈值与重绘余量。
    void setOverlayScale(qreal sceneUnits);

    // 单元编号角标子图层显隐（图层面板「单元编号」开关）；角标是本图元的子图层，随本图元一起显隐。
    // 角标隐藏时 Custom 拖动调序一并失效：自定义序只由角标显示，隐藏后拖动既无序号可辨也无调序反馈。
    void setBadgesVisible(bool visible);

    // 是否正处于拖拽框选手势中。
    bool isMarqueeing() const { return marquee_; }

    // 外部打断（缩放抢占）取消本序列：清按下 / 框选 / 调序态并重绘，不发任何提交信号
    //（打断点既非有效落点也非点击，与释放路径的三条结算分支互斥）。
    void cancelDrag();

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
    // Custom 拖拽调序：把 fromIndex 交上层重排到 toIndex 的位次（toIndex 已选中则互换位次、未选中则原位替换）。
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
    // 依 selected_ 重建 O(1) 选中查表（selFlag_）与序号位查表（selPos_，Custom 角标用）。
    void rebuildSelectionLookup();
    // 局部重绘某序号单元（悬停反馈用；避免大选集下整层重绘）。
    void updateCell(int index);

    // ---------------------------------------------------------------------------
    // CellNumberLayer：L3 Custom 序的单元编号角标子图层（QGraphicsItem，非交互，不抢鼠标事件）。
    // 作为 CellPickerItem 的子图元（选区图层子图层）随其一起显隐；子 z = kCellNumber − kSelection，
    // 叠加在本图元的选区绘制之上，等效全局 z 序即 kCellNumber——展示为独立图层，逻辑归属选区图层。
    // ---------------------------------------------------------------------------
    class CellNumberLayer : public QGraphicsItem {
    public:
        explicit CellNumberLayer(CellPickerItem* owner);
        QRectF boundingRect() const override;
        void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    private:
        CellPickerItem* owner_;
    };
    CellNumberLayer* numberLayer_{nullptr};  // 角标子图层（构造时创建；Custom 序下才绘制内容）。

    std::vector<engine::Cell> cells_;  // Core 单元（区域 + 序号）：命中测试与高亮的唯一几何来源。
    std::vector<int> selected_;             // 当前选择集（有序）。
    std::vector<char> selFlag_;             // 按单元序号索引的选中标记（O(1) 查询）。
    std::vector<int> selPos_;               // 按单元序号索引的自定义序位（1-based；0=未选）。
    engine::SortStrategy strategy_{engine::SortStrategy::RowMajor};
    int imgW_{0};
    int imgH_{0};
    // 图元包围盒 = 整幅原图 ∪ 全部单元区域。Pad 余量策略下跨界残缺单元会伸出图像边界
    //   （甚至落在负坐标），boundingRect 必须覆盖之，否则那部分不被纳入重绘区域、
    //   exposedRect 也取不到，表现为「选中边界块时画布外的覆盖要缩放一下才出现」。
    QRectF bounds_;
    qreal overlayScale_{nums::canvas::kFallbackHandleScenePx};   // 视图下发前的场景兜底初值（手柄同源；兼作点选扩边与单击/拖拽手势阈值）。

    int hoverIndex_{-1};    // 悬停单元序号（高亮反馈）；无则 -1。
    bool pressing_{false};  // 左键按下中。
    bool marquee_{false};   // 已进入拖拽框选（移动超过阈值）。
    int pressIndex_{-1};    // 按下点命中的单元序号（无则 -1）。
    bool reorderDrag_{false}; // 已进入 Custom 拖拽调序。
    int reorderTarget_{-1};   // 调序拖拽的当前目标单元序号。
    bool badgesVisible_{true}; // 单元编号角标是否可见（图层面板开关；隐藏时禁 Custom 拖拽调序）。
    QPointF pressPos_;      // 按下点（场景坐标）。
    QPointF curPos_;        // 当前拖拽点（场景坐标）。
};

} // namespace idc::gui
