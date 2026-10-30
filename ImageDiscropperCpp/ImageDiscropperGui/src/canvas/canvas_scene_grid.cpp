// ============================================================================
// 文件：canvas/canvas_scene_grid.cpp
// 作用：实现 CanvasScene 的网格图元族——L3 诱导网格线（updateGrid/clearGrid/refreshPaletteColors）、
//       网格基准手柄（ensureGridHandles/onGridHandle{Previewed,Committed}/refreshGridHandleInteraction/
//       setGridHandleSize/applySnapEnabled）、单元编号与点选（setCellNumberVisible/cellsIntersecting）
//       与选区框同步（syncSelection）。
// 分块依据：见 canvas_scene.cpp 头注（按图元族五 TU 拆分，与 _annotations/_rects 同构）；本 TU 只
//       管理网格/选区图元与手柄交互，共享几何助手（nearest/cellSceneRect）见 canvas_scene_internal.h。
// ============================================================================
#include "canvas/canvas_scene.h"

#include <algorithm>
#include <vector>

#include <QGraphicsSceneMouseEvent>
#include <QRectF>

#include "canvas/items/grid_handle_item.h"
#include "canvas/items/z_order.h"
#include "util/gui_nums.h"

#include "canvas_scene_internal.h" // cellSceneRect（单元区域 → 场景矩形）

namespace idc::gui {

// 依 Core 诱导网格刷新 L3 网格线（委托 GridLayer，灰色虚线）；show=false 时清空。
// 同时把网格下发给单元点选图元并按 show 切换其显隐。
// 单元选择高亮属「选取边框」图层范畴：受 selectionVisible_ 门控（隐藏后点选、框选一起失效——picker 隐藏，
//   视图橡皮筋与 cellsIntersecting 另由 cellSelectionInteractive() 收敛）。
void CanvasScene::updateGrid(const engine::Grid& grid, const bool show) {
    gridActive_ = show;
    gridLayer_.rebuild(*this, grid, imgW_, imgH_, show && gridVisible_);
    // L3 基准手柄：仅 L3 显示，落位取自 Core 网格参数（即 Document 的 gridParams）；
    // 显隐随「网格线/切割线」同一开关（隐藏图层=不可交互）。
    if (show) {
        ensureGridHandles();
        gridLayer_.syncHandles(grid.params(), imgW_, imgH_, gridVisible_);
    } else {
        gridLayer_.setHandlesVisible(false);
    }
    refreshGridHandleInteraction();
    if (pickerItem_) {
        pickerItem_->setGrid(grid, imgW_, imgH_);
        pickerItem_->setVisible(show && selectionVisible_);
    }

    // 内容边界 = 各单元区域的并集（场景坐标）。补白(Pad) 策略下 Core 会保留越界单元值（`layoutAxis` 不夹取
    //   partial 单元，虚拟起点还可落在负坐标），那截越界必须**完整**进滚动范围，否则放大后可滚进来的部分
    //   越来越小、边界块永远看不全。余量策略不传进场景（三种策略共用同一套相位 `virtualStart`）：Discard
    //   （跳过残缺单元）/ KeepPartial（已夹回图内）的单元恒在原图内 → 并集就是原图，与其余模式同口径。
    QRectF bounds;
    if (show) {
        for (const engine::Cell& c : grid.cells())
            bounds = bounds.united(cellSceneRect(c));
    }
    gridBounds_ = bounds;   // show=false 时为空矩形 → 内容边界退回原图。
    applySceneExtent();
}

// 清空网格线（非 L3 模式 / 换图时），并隐藏、清空单元点选图元。
void CanvasScene::clearGrid() {
    gridActive_ = false;   // 退出 L3：单元选择交互通道随之关断（见 cellSelectionInteractive）。
    gridLayer_.clear();
    if (pickerItem_) {
        pickerItem_->setVisible(false);
        pickerItem_->setGrid(engine::Grid{}, imgW_, imgH_);
    }
    // 撤掉网格带来的内容外扩：非 L3（或换图后）内容边界退回原图，只剩外圈余量。
    gridBounds_ = QRectF();
    applySceneExtent();
}

// Palette 改色后刷新网格/诱导切割线 pen（rebuild 期烘焙、不随 update() 变色）；其余 paint 期取色图元由调用方 update() 重绘。
void CanvasScene::refreshPaletteColors() const {
    gridLayer_.recolor();
}

// ===========================================================================
// L3 网格基准手柄（做在网格线层）：拖基准点/单元尺寸，拖拽只本地预览，释放才一次性写 Document
// ===========================================================================

// 惰性创建两个手柄（由网格线层持有）并接上其拖拽信号：只在首次进入 L3 时创建一次，
// 之后 clear/重建复用（不用重连信号，也不会在拖拽中删图元）。
void CanvasScene::ensureGridHandles() {
    if (gridLayer_.handlesReady()) return;
    gridLayer_.createHandles(*this);
    gridLayer_.setHandleSize(gridHandleScene_);
    for (const GridHandleItem* it : {gridLayer_.originHandle(), gridLayer_.cellHandle()}) {
        connect(it, &GridHandleItem::previewed, this, &CanvasScene::onGridHandlePreviewed);
        connect(it, &GridHandleItem::committed, this, &CanvasScene::onGridHandleCommitted);
    }
}

// 拖拽中逐帧：按草稿参数交 Core Grid::build 重铺网格线（纯整数几何，廉价），两个手柄随网格线跟手。
// 不写 Document——故不会触发 refreshPreview 的 Core 全量预览重算（拖拽不逐帧写 Document）。
void CanvasScene::onGridHandlePreviewed(const engine::GridParams& params) {
    if (!gridActive_ || imgW_ <= 0 || imgH_ <= 0) return;
    // 单元高亮/编号仍按旧网格：拖拽期隐藏点选图元免误导（提交后由 updateGrid 依新网格重搭显隐）。
    if (pickerItem_) pickerItem_->setVisible(false);
    engine::Grid draft;
    draft.build(params, imgW_, imgH_);
    gridLayer_.rebuild(*this, draft, imgW_, imgH_, gridVisible_);
    gridLayer_.syncHandles(params, imgW_, imgH_, gridVisible_);
    refreshGridHandleInteraction();
}

// 释放：恢复单元点选图元，并把最终参数一次性上交（同一手势内再多移动也只写回这一次）。
// 值未变时 Document::setGridParams 自行早退，不产生重算。
void CanvasScene::onGridHandleCommitted(const engine::GridParams& params) {
    if (pickerItem_) pickerItem_->setVisible(gridActive_ && selectionVisible_);
    if (!gridActive_) return;
    emit gridParamsEdited(params.originX, params.originY, params.cellWidth, params.cellHeight);
}

// 手柄可交互口径：L3 + 网格线图层可见 + 不在图片拾取/标注选取态（那两态各有自己的图元要接按下）。
// 「网格线/切割线」开关关掉时手柄仍可见但不可拖的反直觉情形不存在——图层开关即手柄总开关。
void CanvasScene::refreshGridHandleInteraction() const {
    gridLayer_.setHandlesInteractive(gridActive_ && gridVisible_ && !imagePickMode_ && !annotationSelectMode_);
}

// 网格基准手柄边长（随视图缩放换算，使手柄恒约 8 屏幕px；缩放前用默认场景口径兜底）。
void CanvasScene::setGridHandleSize(const qreal sceneUnits) {
    gridHandleScene_ = std::max(1.0, sceneUnits);
    gridLayer_.setHandleSize(gridHandleScene_);
}

// 吸附运行时开关：回设单矩形选区与全部 L2 多矩形选区图元的 snapEnabled（无需重建）。
void CanvasScene::applySnapEnabled(const bool on) const {
    if (selItem_) selItem_->setSnapEnabled(on);
    for (SelectionRectItem* it : multiRectItems_) if (it) it->setSnapEnabled(on);
}

// 单元编号角标图层显隐（图层面板「单元编号」开关）：角标是单元点选图元的子图层，
// 随点选图元一起显隐；本开关只控制角标子图层自身。
void CanvasScene::setCellNumberVisible(const bool on) const {
    if (pickerItem_) pickerItem_->setBadgesVisible(on);
}

// 依 Document 选择集与排序策略刷新单元点选图元的高亮/角标（只在值时重绘，不重建图元）。
void CanvasScene::updateCellSelection(const std::vector<int>& selected,
                                      const engine::SortStrategy strategy) const {
    if (pickerItem_) pickerItem_->setSelection(selected, strategy);
}

// L3 框选命中查询：委托单元点选图元。无图元（尚无图像 / 非 L3）或单元选择不可交互时返回空——
// 后者是「隐藏图层=不可交互」在框选通道上的落点：picker 隐藏后点选已失效，命中查询必须同样收敛。
std::vector<int> CanvasScene::cellsIntersecting(const QRectF& sceneRect) const {
    if (!pickerItem_ || !cellSelectionInteractive()) return {};
    return pickerItem_->cellsIntersecting(sceneRect);
}

// 依 Document 的选区同步选区框显示。
void CanvasScene::syncSelection(const engine::RectRegion& rect, const bool hasRect) const {
    if (!selItem_) return; // 尚无图像。
    if (!hasRect) {
        selItem_->setVisible(false);
        return;
    }
    selItem_->setRect(QRectF(rect.left, rect.top,
                             rect.width(), rect.height()));
    selItem_->setVisible(true);
}


} // namespace idc::gui