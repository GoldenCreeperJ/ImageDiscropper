// ============================================================================
// 文件：canvas/canvas_scene_rects.cpp
// 作用：CanvasScene 的「L2 多矩形」图元域成员实现（同类分 TU 编译）——多矩形诱导切割线/可拖拽
//       选区框组/手柄尺寸/拖拽态与高亮。CanvasScene 类不拆分，仅按职责分 TU 编译。
// ============================================================================
#include "canvas/canvas_scene.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

#include <QGraphicsSceneMouseEvent>
#include <QTransform>

#include "canvas/items/z_order.h"
#include "geometry/shapes.h"

namespace idc::gui {

// 仅刷新 L2 多矩形的诱导切割线（委托 GridLayer 以橙色切割线样式重画），不触碰单元点选图元——
// 多矩形靠画布框选追加，若显示 picker 会 grab 鼠标、阻断框选，故此处不启动 picker。
// 这些诱导线本质是各矩形十字带并集的切割线，故画为橙色（asCutLines）并受 cutLinesVisible_ 门控。
void CanvasScene::updateMultiRectCutLines(const engine::Grid& grid, const bool show) {
    gridLayer_.rebuild(*this, grid, imgW_, imgH_, show && cutLinesVisible_, /*asCutLines=*/true);
}

// 依 Document 多矩形列表刷新可交互的橙色选区框（L2 MultiRect）。每个矩形一个 SelectionRectItem，
// 可整体移动 / 四角缩放 / 拖边微调，与单矩形选区一致。坐标即原图像素（场景坐标），无需换算。
// 【拖拽安全】增量维护：按数量增删末位图元、逐个刷新几何；绝不 clear+重建（否则会在拖拽中
// 删除正在处理鼠标事件的图元→崩溃）；且跳过对正在拖拽图元的回设（避免逐帧量化抖动）。
// 多矩形不画贯穿切割线（各矩形十字带的并集由遮罩呈现），故 cutEdges 全 false，boundingRect
// 仅覆盖各自矩形，不阻断在空白处框选追加新矩形。
void CanvasScene::updateMultiRects(const std::vector<engine::RectRegion>& rects) {
    const int n = static_cast<int>(rects.size());
    // 收缩：删除多余末位图元（拖拽期间不会发生删减，防御性跳过正在拖拽者）。
    while (multiRectItems_.size() > n) {
        SelectionRectItem* it = multiRectItems_.last();
        if (it->isDragging()) break;   // 防御：绝不删除正在拖拽的图元。
        multiRectItems_.removeLast();
        removeItem(it);
        delete it;
    }
    // 扩张：为新增矩形创建图元并连接编辑信号（发射时按指针查当前下标，避免下标漂移）。
    while (multiRectItems_.size() < n) {
        auto* it = new SelectionRectItem();
        it->setZValue(zorder::kSelection);
        it->setImageBounds(imgW_, imgH_);
        it->setHandleSize(multiHandleSize_);
        it->setCutLinesVisible(cutLinesVisible_);   // 继承图层面板网格线/切割线同一开关。
        it->setBorderVisible(selectionVisible_);    // 继承图层面板选取边框显隐开关。
        addItem(it);
        connect(it, &SelectionRectItem::rectChanged, this, [this, it](const QRectF& r) {
            if (const int idx = multiRectItems_.indexOf(it); idx >= 0) emit multiRectEdited(idx, r);
        });
        // 多矩形拖框开始同样转发交互信号（任一选区框被拖动都清除标注选中）。
        connect(it, &SelectionRectItem::dragStarted, this, &CanvasScene::selectionInteractionStarted);
        multiRectItems_.append(it);
    }
    // 刷新几何：跳过正在拖拽者（其位置/边界由手势维护，释放时才回设，避免逐帧 prepareGeometryChange 抖动）。
    // 非拖拽者也只在边界变化/矩形变化时才重推，避免拖拽期间对其余矩形的无谓重绘。
    const bool boundsChanged = multiBoundsW_ != imgW_ || multiBoundsH_ != imgH_;
    for (int i = 0; i < multiRectItems_.size(); ++i) {
        SelectionRectItem* it = multiRectItems_[i];
        it->setVisible(true);
        it->setHighlighted(i == activeMultiRect_);   // 高亮当前选中项（仅变化时重绘）。
        if (i < n && !it->isDragging()) {
            if (boundsChanged) it->setImageBounds(imgW_, imgH_);
            const engine::RectRegion& r = rects[static_cast<std::size_t>(i)];
            const QRectF nr(r.left, r.top,
                            (r.width()), (r.height()));
            if (it->rect() != nr) it->setRect(nr);
        }
    }
    multiBoundsW_ = imgW_;
    multiBoundsH_ = imgH_;
}

// 收集 scenePos 处命中的多矩形选区框下标，按叠放序「顶在前」返回（与 layerStackAt 同口径）：
// zValue 降序，同 z（多矩形全取 zorder::kSelection）则后加入者（绘制在上）在前。
// 只收可见且不拒左键的候选（「隐藏图层=不可交互」与标注选取态的让位均由 acceptedMouseButtons 反映）；
// 命中口径由图元自己回答（与它接按下的判定一致），本处不做几何计算。
std::vector<int> CanvasScene::multiRectStackAt(const QPointF& scenePos) const {
    struct Hit { int index; qreal z; };
    std::vector<Hit> hits;
    for (int i = 0; i < multiRectItems_.size(); ++i) {
        const SelectionRectItem* it = multiRectItems_[i];
        if (!it->isVisible() || !(it->acceptedMouseButtons() & Qt::LeftButton)) continue;
        if (!it->hitTestPress(scenePos)) continue;
        hits.push_back({i, it->zValue()});
    }
    std::ranges::sort(hits, [](const Hit& a, const Hit& b) {
        if (a.z != b.z) return a.z > b.z;
        return a.index > b.index;
    });
    std::vector<int> stack;
    stack.reserve(hits.size());
    for (const auto&[index, z] : hits) stack.push_back(index);
    return stack;
}

// 清空多矩形选区框（非 MultiRect / 换图时）。
void CanvasScene::clearMultiRects() {
    for (auto* item : multiRectItems_) { removeItem(item); delete item; }
    multiRectItems_.clear();
    multiBoundsW_ = -1;   // 重置边界缓存，下次重建时重推图像边界。
    multiBoundsH_ = -1;
    activeMultiRect_ = -1; // 无矩形时无高亮。
    multiRectPressIndex_ = -1;   // 旧图元已销毁：按下归属与循环态一并归零（下标不再可解引用）。
    multiRectCycle_.reset();
}

// 设置多矩形选区框的手柄尺寸（同时缓存供后续新建图元使用）。
void CanvasScene::setMultiRectHandleSize(const qreal sceneUnits) {
    multiHandleSize_ = sceneUnits;
    for (SelectionRectItem* it : multiRectItems_) it->setHandleSize(sceneUnits);
}

// 是否有任一多矩形选区框正被拖拽。
bool CanvasScene::isDraggingMultiRect() const {
    for (const SelectionRectItem* it : multiRectItems_)
        if (it->isDragging()) return true;
    return false;
}

// 设置当前高亮（选中）的多矩形下标：对应选区框颜色略微加强，其余恢复普通。
// 选中态兼作重叠区按下的路由基准（CanvasScene::mousePressEvent）：面板行选中或画布连击下探都经此回灌。
void CanvasScene::setActiveMultiRect(const int index) {
    activeMultiRect_ = index;
    for (int i = 0; i < multiRectItems_.size(); ++i)
        multiRectItems_[i]->setHighlighted(i == index);
}

// 依 Document 的选择集与排序策略刷新单元点选图元的高亮。

} // namespace idc::gui
