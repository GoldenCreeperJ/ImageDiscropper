// ============================================================================
// 文件：canvas/cell_picker_item.cpp
// 作用：实现 L3 单元点选/框选交互图元（见同名头文件说明）。
// 分块依据：命中测试只用 Core 单元的 area（contains / 矩形重叠），不含任何网格几何推导；
//           手势（单击 vs 拖拽）以 overlayScale_ 为阈值区分，释放时才发一次信号（避免逐帧
//           回写 Document 造成抖动/卡顿——见拖拽图元的通用陷阱）。
// ============================================================================
#include "canvas/cell_picker_item.h"

#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QPen>
#include <QStyleOptionGraphicsItem>
#include <QTransform>

#include "canvas/z_order.h"

namespace idc::gui {
namespace {

// 视觉：选择高亮用半透明蓝（独立于极性的绿/红遮罩，清晰标示「已选单元」）。
constexpr QColor kSelFill(0, 150, 255, 48);
constexpr QColor kSelBorder(0, 150, 255, 210);
constexpr QColor kHoverBorder(255, 255, 255, 190);
constexpr QColor kMarqueeFill(0, 150, 255, 32);
constexpr QColor kMarqueeBorder(0, 150, 255, 230);
// CUSTOM 角标：深色底 + 白字（保证在任意底图/遮罩上可读）。
constexpr QColor kBadgeBg(0, 0, 0, 175);
constexpr QColor kBadgeFg(255, 255, 255);
// 调序反馈：被拖单元橙色粗边框、目标单元黄色虚线边框。
constexpr QColor kDragBorder(230, 126, 34);
constexpr QColor kDropBorder(241, 196, 15);

// 单元区域 → QRectF（原图像素坐标；RectRegion 左闭右开，width/height 即跨度）。
QRectF cellRect(const engine::Cell& c) {
    // static_cast：int→qreal 的隐式转换在花括号初始化列表中是 narrowing，须显式转换。
    return {static_cast<qreal>(c.area.left), static_cast<qreal>(c.area.top),
            static_cast<qreal>(c.area.width()), static_cast<qreal>(c.area.height())};
}

// 单元区域与场景矩形是否相交（严格重叠，左闭右开语义下的开区间判定）。
bool overlaps(const engine::RectRegion& a, const QRectF& b) {
    return static_cast<qreal>(a.left) < b.right() && static_cast<qreal>(a.right) > b.left() &&
           static_cast<qreal>(a.top) < b.bottom() && static_cast<qreal>(a.bottom) > b.top();
}

// 1px cosmetic 画笔（线宽不随缩放变化）。
QPen cosmeticPen(const QColor& c, const Qt::PenStyle style = Qt::SolidLine) {
    QPen p(c);
    p.setWidth(1);
    p.setCosmetic(true);
    p.setStyle(style);
    return p;
}

} // namespace

// 构造：只接收左键、开启悬停、置于选择层（L3 下选区框隐藏，本图元为最上层可交互项）。
CellPickerItem::CellPickerItem(QGraphicsItem* parent) : QGraphicsObject(parent) {
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
    setZValue(zorder::kSelection);
    // 角标子图层：随本图元显隐；子 z = kCellNumber − kSelection，使角标叠加在本图元选区绘制之上。
    numberLayer_ = new CellNumberLayer(this);
    numberLayer_->setZValue(zorder::kCellNumber - zorder::kSelection);
}

// 依 Core 网格更新单元几何（并重置悬停、重建选中查表）。
void CellPickerItem::setGrid(const engine::Grid& grid, const int width, const int height) {
    prepareGeometryChange();
    cells_ = grid.cells();
    imgW_ = width;
    imgH_ = height;
    hoverIndex_ = -1;
    rebuildSelectionLookup();
    update();
    if (numberLayer_) numberLayer_->update();   // 角标子图层随网格变化刷新。
}

// 依 Document 更新选择集与排序策略。
void CellPickerItem::setSelection(const std::vector<int>& selected,
                                  const engine::SortStrategy strategy) {
    selected_ = selected;
    strategy_ = strategy;
    rebuildSelectionLookup();
    update();
    if (numberLayer_) numberLayer_->update();   // 选择集/排序变化 → 角标内容变化。
}

// 覆盖层尺度（≈8 屏幕px 的场景单位）。
void CellPickerItem::setOverlayScale(const qreal sceneUnits) {
    if (sceneUnits > 0.0) overlayScale_ = sceneUnits;
}

// 单元编号角标子图层显隐（图层面板开关）。
void CellPickerItem::setBadgesVisible(const bool visible) const {
    if (numberLayer_) numberLayer_->setVisible(visible);
}

// 场景矩形 = 整幅原图。
QRectF CellPickerItem::boundingRect() const {
    // 显式转换：int→qreal 的隐式转换在花括号初始化列表中是 narrowing（成员非常量表达式），
    // clangd 按标准会报错，故逐个 static_cast（与 applySnap 的写法一致）。
    return {0.0, 0.0, static_cast<qreal>(imgW_), static_cast<qreal>(imgH_)};
}

// 依 selected_ 重建按序号索引的选中标记与自定义序位（O(1) 查询，避免 paint 里 O(n²) 扫描）。
void CellPickerItem::rebuildSelectionLookup() {
    selFlag_.assign(cells_.size(), 0);
    selPos_.assign(cells_.size(), 0);
    for (std::size_t k = 0; k < selected_.size(); ++k) {
        if (const int idx = selected_[k]; idx >= 0 && static_cast<std::size_t>(idx) < selFlag_.size()) {
            selFlag_[static_cast<std::size_t>(idx)] = 1;
            selPos_[static_cast<std::size_t>(idx)] = static_cast<int>(k) + 1; // 1-based 自定义序位。
        }
    }
}

// 命中测试：点 → 单元序号（Core 单元区域 contains）；无则 -1。
int CellPickerItem::cellIndexAt(const QPointF& scenePos) const {
    const int x = static_cast<int>(scenePos.x());
    const int y = static_cast<int>(scenePos.y());
    for (const engine::Cell& c : cells_) {
        if (c.area.contains(x, y)) return c.index;
    }
    return -1;
}

// 框选：与场景矩形相交的全部单元序号。
std::vector<int> CellPickerItem::cellsIntersecting(const QRectF& sceneRect) const {
    std::vector<int> out;
    for (const engine::Cell& c : cells_) {
        if (overlaps(c.area, sceneRect)) out.push_back(c.index);
    }
    return out;
}

// 局部重绘某序号单元（含边框余量）；Core 单元行主序连续编号，可直接下标取区域。
void CellPickerItem::updateCell(const int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= cells_.size()) return;
    QRectF r = cellRect(cells_[static_cast<std::size_t>(index)]);
    r.adjust(-overlayScale_, -overlayScale_, overlayScale_, overlayScale_);
    update(r);
}

// 左键按下：记录起点与命中单元，进入待判定态（单击 / 框选 / CUSTOM 调序）。
void CellPickerItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (event->button() != Qt::LeftButton) { event->ignore(); return; }
    pressing_ = true;
    marquee_ = false;
    reorderDrag_ = false;
    reorderTarget_ = -1;
    pressPos_ = event->scenePos();
    curPos_ = pressPos_;
    pressIndex_ = cellIndexAt(pressPos_);
    event->accept();
}

// 左键移动：CUSTOM 下按在已选单元上拖动→调序；否则超阈值→框选并实时重绘。
void CellPickerItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    if (!pressing_) { event->ignore(); return; }
    curPos_ = event->scenePos();
    const bool moved = (curPos_ - pressPos_).manhattanLength() > overlayScale_;

    // CUSTOM 下按在已选单元上拖动 → 拖拽调序（优先于框选）。
    const bool pressSelected =
        pressIndex_ >= 0 && static_cast<std::size_t>(pressIndex_) < selFlag_.size() &&
        selFlag_[static_cast<std::size_t>(pressIndex_)];
    if (strategy_ == engine::SortStrategy::CUSTOM && pressSelected && moved) {
        reorderDrag_ = true;
        reorderTarget_ = cellIndexAt(curPos_);
        update();
        event->accept();
        return;
    }

    if (!marquee_ && moved) marquee_ = true;
    if (marquee_) update();
    event->accept();
}

// 左键释放：调序 → 发 cellReordered；框选 → 发命中单元集合；单击 → 发命中单元序号。
void CellPickerItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    if (event->button() != Qt::LeftButton) { event->ignore(); return; }
    if (reorderDrag_) {
        reorderDrag_ = false;
        pressing_ = false;
        const int from = pressIndex_, to = reorderTarget_;
        reorderTarget_ = -1;
        update();
        // 目标须为另一已选单元，方构成有效调序。
        if (from >= 0 && to >= 0 && to != from &&
            static_cast<std::size_t>(to) < selFlag_.size() && selFlag_[static_cast<std::size_t>(to)]) {
            emit cellReordered(from, to);
        }
        event->accept();
        return;
    }
    if (marquee_) {
        const QRectF band = QRectF(pressPos_, curPos_).normalized();
        marquee_ = false;
        pressing_ = false;
        update();
        if (const std::vector<int> idx = cellsIntersecting(band); !idx.empty()) emit cellsMarqueeSelected(idx);
    } else {
        pressing_ = false;
        if (const int idx = cellIndexAt(pressPos_); idx >= 0) emit cellToggled(idx);
    }
    event->accept();
}

// 悬停移动：更新悬停单元并局部重绘旧/新单元（大选集下避免整层重绘）。
void CellPickerItem::hoverMoveEvent(QGraphicsSceneHoverEvent* event) {
    if (const int idx = cellIndexAt(event->scenePos()); idx != hoverIndex_) {
        const int old = hoverIndex_;
        hoverIndex_ = idx;
        updateCell(old);
        updateCell(hoverIndex_);
    }
    event->accept();
}

// 悬停离开：清除悬停高亮。
void CellPickerItem::hoverLeaveEvent(QGraphicsSceneHoverEvent* event) {
    if (hoverIndex_ != -1) {
        const int old = hoverIndex_;
        hoverIndex_ = -1;
        updateCell(old);
    }
    event->accept();
}

// 绘制：已选单元高亮 → 悬停单元边框 → 框选带（仅绘暴露区内的单元，兼顾大选集性能）。
// 编号角标由角标子图层 CellNumberLayer 绘制（不在此层，见文件尾实现）。
void CellPickerItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget*) {
    if (cells_.empty() || imgW_ <= 0 || imgH_ <= 0) return;
    const QRectF exposed = option ? option->exposedRect : boundingRect();

    // 已选单元：半透明蓝填充 + 蓝边框。
    const QPen selPen = cosmeticPen(kSelBorder);
    painter->setPen(selPen);
    painter->setBrush(kSelFill);
    for (const engine::Cell& c : cells_) {
        if (!selFlag_.empty() && static_cast<std::size_t>(c.index) < selFlag_.size() &&
            selFlag_[static_cast<std::size_t>(c.index)]) {
            if (const QRectF r = cellRect(c); exposed.intersects(r)) painter->drawRect(r);
        }
    }

    // 悬停单元：白色边框反馈（未框选时）。
    if (hoverIndex_ >= 0 && !marquee_ &&
        static_cast<std::size_t>(hoverIndex_) < cells_.size()) {
        if (const QRectF r = cellRect(cells_[static_cast<std::size_t>(hoverIndex_)]); exposed.intersects(r)) {
            painter->setPen(cosmeticPen(kHoverBorder));
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(r);
        }
    }

    // 框选带：半透明蓝 + 虚线边框。
    if (marquee_) {
        const QRectF band = QRectF(pressPos_, curPos_).normalized();
        painter->setPen(cosmeticPen(kMarqueeBorder, Qt::DashLine));
        painter->setBrush(kMarqueeFill);
        painter->drawRect(band);
    }

    // 调序拖拽反馈：被拖单元橙色粗边框、目标单元黄色虚线边框（cosmetic，屏幕线宽恒定）。
    if (reorderDrag_) {
        painter->setBrush(Qt::NoBrush);
        if (pressIndex_ >= 0 && static_cast<std::size_t>(pressIndex_) < cells_.size()) {
            QPen dp = cosmeticPen(kDragBorder);
            dp.setWidth(2);
            painter->setPen(dp);
            painter->drawRect(cellRect(cells_[static_cast<std::size_t>(pressIndex_)]));
        }
        if (reorderTarget_ >= 0 && reorderTarget_ != pressIndex_ &&
            static_cast<std::size_t>(reorderTarget_) < cells_.size()) {
            painter->setPen(cosmeticPen(kDropBorder, Qt::DashLine));
            painter->drawRect(cellRect(cells_[static_cast<std::size_t>(reorderTarget_)]));
        }
    }
}

// ---------------------------------------------------------------------------
// CellNumberLayer（单元编号角标子图层）实现
// ---------------------------------------------------------------------------

// 构造：作为 owner 的子图元挂载（选区图层子图层），纯展示、不接收鼠标事件。
CellPickerItem::CellNumberLayer::CellNumberLayer(CellPickerItem* owner) : owner_(owner) {
    setParentItem(owner);
    setAcceptedMouseButtons(Qt::NoButton);
}

// 场景矩形与父图元同域（整幅原图）。
QRectF CellPickerItem::CellNumberLayer::boundingRect() const {
    return owner_->boundingRect();
}

// 绘制 CUSTOM 序的单元编号角标（1-based）：每个已选单元左上角，深色圆角底 + 白字。
// 以设备像素绘制（字号恒定屏幕大小），避免高倍缩放下场景单位字号取整为 0。
void CellPickerItem::CellNumberLayer::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget*) {
    if (owner_->strategy_ != engine::SortStrategy::CUSTOM || owner_->selPos_.empty()) return;
    const QRectF exposed = option ? option->exposedRect : boundingRect();
    painter->save();
    const QTransform wt = painter->worldTransform();
    painter->resetTransform();
    QFont f = painter->font();
    f.setPixelSize(13);
    f.setBold(true);
    painter->setFont(f);
    for (const engine::Cell& c : owner_->cells_) {
        if (static_cast<std::size_t>(c.index) >= owner_->selPos_.size()) continue;
        const int num = owner_->selPos_[static_cast<std::size_t>(c.index)];
        if (num <= 0) continue;
        const QRectF r = cellRect(c);
        if (!exposed.intersects(r)) continue;
        const QPointF dev = wt.map(r.topLeft());
        // 单元在屏幕上过小则跳过角标，避免缩小时糊成一片。
        if (const QPointF devBR = wt.map(r.bottomRight()); devBR.x() - dev.x() < 14.0 || devBR.y() - dev.y() < 14.0) continue;
        const QString text = QString::number(num);
        const qreal w = 8.0 * static_cast<qreal>(text.length()) + 6.0;
        const QRectF badge(dev.x(), dev.y(), w, 16.0);
        painter->setPen(Qt::NoPen);
        painter->setBrush(kBadgeBg);
        painter->drawRoundedRect(badge, 3.0, 3.0);
        painter->setPen(kBadgeFg);
        painter->drawText(badge, Qt::AlignCenter, text);
    }
    painter->restore();
}

} // namespace idc::gui
