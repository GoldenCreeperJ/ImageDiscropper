// ============================================================================
// 文件：canvas/canvas_scene.cpp
// 作用：实现 CanvasScene 的图层装配与刷新（见同名头文件说明）。
// 分块依据：底图/遮罩/切割线/选区四类图元各由专门方法管理；场景不含切割或几何逻辑，
//           仅按 Core 结果与 Document 状态渲染（A-0.1）。
// ============================================================================
#include "canvas/canvas_scene.h"

#include <cstdlib>
#include <vector>

#include <QColor>
#include <QPen>
#include <QTransform>

#include "canvas/z_order.h"

namespace idc::gui {
namespace {

// 在候选坐标集 vals 中取最接近 target 者；vals 为空返回 -1。
// 用于把 Core 的切割线（xs/ys 含 0/W 边界）关联到最近的选区边。
int nearest(const std::vector<int>& vals, const int target) {
    int best = -1;
    int bestD = -1;
    for (const int v : vals) {
        const int d = std::abs(v - target);
        if (bestD < 0 || d < bestD) { bestD = d; best = v; }
    }
    return best;
}

} // namespace

// 构造：空场景。底图与选区框在 setBaseImage 时惰性创建。
CanvasScene::CanvasScene(QObject* parent) : QGraphicsScene(parent) {}

// 设置底图并同步场景矩形与选区边界。
void CanvasScene::setBaseImage(const QPixmap& preview, const double scaleX, const double scaleY,
                               const int fullW, const int fullH) {
    imgW_ = fullW;
    imgH_ = fullH;
    setSceneRect(0.0, 0.0, static_cast<qreal>(fullW), static_cast<qreal>(fullH));

    // 底图：预览 pixmap 经放大变换铺满原图尺寸（快速变换，预览足够）。
    if (!baseItem_) {
        baseItem_ = addPixmap(QPixmap());
        baseItem_->setZValue(zorder::kBase);
        baseItem_->setAcceptedMouseButtons(Qt::NoButton);
    }
    baseItem_->setTransformationMode(Qt::FastTransformation);
    baseItem_->setTransform(QTransform::fromScale(scaleX, scaleY));
    baseItem_->setPixmap(preview);

    // 选区框：惰性创建，设定图像边界（供钳制与吸附）。
    if (!selItem_) {
        selItem_ = new SelectionRectItem();
        addItem(selItem_);
        selItem_->setZValue(zorder::kSelection);
        selItem_->setVisible(false);
        // 选区框的编辑事件转发为场景信号，供 MainWindow 写回 Document（视图/场景不认识 Document）。
        connect(selItem_, &SelectionRectItem::rectChanged, this, &CanvasScene::selectionEdited);
    }
    selItem_->setImageBounds(fullW, fullH);

    // 单元点选图元：惰性创建（L3 交互）；仅 L3 显示（updateGrid 时切换）。
    // 其点选/框选事件同样转发为场景信号，供 MainWindow 写回 Document。
    if (!pickerItem_) {
        pickerItem_ = new CellPickerItem();
        addItem(pickerItem_);
        pickerItem_->setVisible(false);
        connect(pickerItem_, &CellPickerItem::cellToggled, this, &CanvasScene::cellToggled);
        connect(pickerItem_, &CellPickerItem::cellsMarqueeSelected,
                this, &CanvasScene::cellsMarqueeSelected);
        connect(pickerItem_, &CellPickerItem::cellReordered, this, &CanvasScene::cellReordered);
    }
}

// 清空画布全部内容。
void CanvasScene::clearAll() {
    maskLayer_.clear();
    gridLayer_.clear();
    clearCutLines();
    clearMultiRects();
    if (baseItem_) { removeItem(baseItem_); delete baseItem_; baseItem_ = nullptr; }
    if (selItem_) { removeItem(selItem_); delete selItem_; selItem_ = nullptr; }
    if (pickerItem_) { removeItem(pickerItem_); delete pickerItem_; pickerItem_ = nullptr; }
    imgW_ = 0;
    imgH_ = 0;
}

// 刷新保留/删除遮罩（委托 MaskLayer）。
void CanvasScene::updateMasks(const idc::engine::EngineResult& result) {
    maskLayer_.rebuild(*this, result, imgW_, imgH_, masksVisible_);
}

// 遮罩显隐切换。
void CanvasScene::setMasksVisible(const bool visible) {
    masksVisible_ = visible;
    maskLayer_.setVisible(visible);
}

// 依 Core 切割线集合，判定选区四条边各自是否有「内部贯穿切割线」，把结果下发给选区框：
// 选区框据此把这些边延伸绘制为橙色贯穿线，并允许直接抓取拖动（拖动该边＝移动切割线）。
// 单矩形/十字(RECT)四边皆有；横带(HLINE)仅上/下；竖带(VLINE)仅左/右；落在图像边界(0/W/H)
// 的线不算「内部切割线」（此时选区边即图像边，内部无切割可言），对应边不延伸。
void CanvasScene::updateCutLines(const idc::engine::CutLineSet& lines, const idc::engine::RectRegion& rect) {
    if (!selItem_ || imgW_ <= 0 || imgH_ <= 0) { clearCutLines(); return; }

    // 每条边取 Core 线集中最接近该边者，落在图像内部才视为有效贯穿切割线。
    const int lx = nearest(lines.xs, rect.left);
    const int rx = nearest(lines.xs, rect.right);
    const int ty = nearest(lines.ys, rect.top);
    const int by = nearest(lines.ys, rect.bottom);
    const bool left   = (lx > 0 && lx < imgW_);
    const bool right  = (rx > 0 && rx < imgW_);
    const bool top    = (ty > 0 && ty < imgH_);
    const bool bottom = (by > 0 && by < imgH_);
    selItem_->setCutEdges(left, right, top, bottom);
}

// 清空切割线（换图/关闭图像/无选区时）：取消选区框的边延伸标记。
void CanvasScene::clearCutLines() {
    if (selItem_) selItem_->setCutEdges(false, false, false, false);
}

// 依 Core 诱导网格刷新 L3 网格线（委托 GridLayer）；show=false 时清空。
// 同时把网格下发给单元点选图元并按 show 切换其显隐。
void CanvasScene::updateGrid(const idc::engine::Grid& grid, const bool show) {
    gridLayer_.rebuild(*this, grid, imgW_, imgH_, show);
    if (pickerItem_) {
        pickerItem_->setGrid(grid, imgW_, imgH_);
        pickerItem_->setVisible(show);
    }
}

// 清空网格线（非 L3 模式 / 换图时），并隐藏、清空单元点选图元。
void CanvasScene::clearGrid() {
    gridLayer_.clear();
    if (pickerItem_) {
        pickerItem_->setVisible(false);
        pickerItem_->setGrid(idc::engine::Grid{}, imgW_, imgH_);
    }
}

// 仅刷新网格线（委托 GridLayer），不触碰单元点选图元——供 L2 多矩形显示诱导网格。
// 多矩形靠画布框选追加，若显示 picker 会 grab 鼠标、阻断框选，故此处不启动 picker。
void CanvasScene::updateGridLines(const idc::engine::Grid& grid, const bool show) {
    gridLayer_.rebuild(*this, grid, imgW_, imgH_, show);
}

// 依 Document 多矩形列表刷新可交互的橙色选区框（L2 MULTI_RECT）。每个矩形一个 SelectionRectItem，
// 可整体移动 / 四角缩放 / 拖边微调，与单矩形选区一致。坐标即原图像素（场景坐标），无需换算。
// 【拖拽安全】增量维护：按数量增删末位图元、逐个刷新几何；绝不 clear+重建（否则会在拖拽中
// 删除正在处理鼠标事件的图元→崩溃）；且跳过对正在拖拽图元的回设（避免逐帧量化抖动）。
// 多矩形不画贯穿切割线（各矩形十字带的并集由遮罩呈现），故 cutEdges 全 false，boundingRect
// 仅覆盖各自矩形，不阻断在空白处框选追加新矩形。
void CanvasScene::updateMultiRects(const std::vector<idc::engine::RectRegion>& rects) {
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
        addItem(it);
        connect(it, &SelectionRectItem::rectChanged, this, [this, it](const QRectF& r) {
            const int idx = multiRectItems_.indexOf(it);
            if (idx >= 0) emit multiRectEdited(idx, r);
        });
        multiRectItems_.append(it);
    }
    // 刷新几何：跳过正在拖拽者（其位置/边界由手势维护，释放时才回设，避免逐帧 prepareGeometryChange 抖动）。
    // 非拖拽者也只在边界变化/矩形变化时才重推，避免拖拽期间对其余矩形的无谓重绘。
    const bool boundsChanged = (multiBoundsW_ != imgW_ || multiBoundsH_ != imgH_);
    for (int i = 0; i < multiRectItems_.size(); ++i) {
        SelectionRectItem* it = multiRectItems_[i];
        it->setVisible(true);
        it->setHighlighted(i == activeMultiRect_);   // 高亮当前选中项（仅变化时重绘）。
        if (i < n && !it->isDragging()) {
            if (boundsChanged) it->setImageBounds(imgW_, imgH_);
            const idc::engine::RectRegion& r = rects[static_cast<std::size_t>(i)];
            const QRectF nr(static_cast<qreal>(r.left), static_cast<qreal>(r.top),
                            static_cast<qreal>(r.width()), static_cast<qreal>(r.height()));
            if (it->rect() != nr) it->setRect(nr);
        }
    }
    multiBoundsW_ = imgW_;
    multiBoundsH_ = imgH_;
}

// 清空多矩形选区框（非 MULTI_RECT / 换图时）。
void CanvasScene::clearMultiRects() {
    for (auto* item : multiRectItems_) { removeItem(item); delete item; }
    multiRectItems_.clear();
    multiBoundsW_ = -1;   // 重置边界缓存，下次重建时重推图像边界。
    multiBoundsH_ = -1;
    activeMultiRect_ = -1; // 无矩形时无高亮。
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
void CanvasScene::setActiveMultiRect(const int index) {
    activeMultiRect_ = index;
    for (int i = 0; i < multiRectItems_.size(); ++i)
        multiRectItems_[i]->setHighlighted(i == index);
}

// 依 Document 的选择集与排序策略刷新单元点选图元的高亮。
void CanvasScene::updateCellSelection(const std::vector<int>& selected,
                                      const idc::engine::SortStrategy strategy) {
    if (pickerItem_) pickerItem_->setSelection(selected, strategy);
}

// L3 框选命中查询：委托单元点选图元。无图元（尚无图像 / 非 L3）时返回空。
std::vector<int> CanvasScene::cellsIntersecting(const QRectF& sceneRect) const {
    if (!pickerItem_) return {};
    return pickerItem_->cellsIntersecting(sceneRect);
}

// 依 Document 的选区同步选区框显示。
void CanvasScene::syncSelection(const idc::engine::RectRegion& rect, const bool hasRect) {
    if (!selItem_) return; // 尚无图像。
    if (!hasRect) {
        selItem_->setVisible(false);
        return;
    }
    selItem_->setRect(QRectF(static_cast<qreal>(rect.left), static_cast<qreal>(rect.top),
                             static_cast<qreal>(rect.width()), static_cast<qreal>(rect.height())));
    selItem_->setVisible(true);
}

} // namespace idc::gui
