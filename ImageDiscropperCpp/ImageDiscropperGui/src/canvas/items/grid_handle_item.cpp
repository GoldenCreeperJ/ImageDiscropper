// ============================================================================
// 文件：canvas/items/grid_handle_item.cpp
// 作用：实现网格基准手柄的落位、绘制与拖动（见同名头文件说明）。
// 分块依据：拖动几何集中在本文件（整数像素吸附 + 图内钳制），绘制只取 Palette 网格色，
//           不含任何网格铺设计算（那在 Core Grid::build）。
// ============================================================================
#include "canvas/items/grid_handle_item.h"

#include <algorithm>
#include <cmath>

#include <QCursor>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>

#include "app/palette.h"

namespace idc::gui {

// 构造：按角色定光标；不吃鼠标（由 setEditable 打开，随网格线图层显隐门控）。
GridHandleItem::GridHandleItem(const Role role, QGraphicsItem* parent)
    : QGraphicsObject(parent), role_(role) {
    setAcceptedMouseButtons(Qt::NoButton);
    setCursor(role_ == Role::Origin ? Qt::SizeAllCursor : Qt::SizeFDiagCursor);
    setVisible(false);
}

// 包围盒：手柄方框外扩 2px（抓取区与描边溢出）；以 (0,0) 为中心，落点由 pos 决定。
QRectF GridHandleItem::boundingRect() const {
    constexpr qreal m = 2.0;
    const qreal h = handleSize_ / 2.0 + m;
    return {-h, -h, 2.0 * h, 2.0 * h};
}

// 绘制：白底方框 + 网格色描边（与选区手柄同一观感）；Origin 内画十字（相位锚），
// Cell 内画斜角（与缩放类手柄同形）。取色期在 paint 内，改色只需场景重绘。
void GridHandleItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* /*option*/, QWidget* /*widget*/) {
    QPen pen(Palette::instance().gridColor());
    pen.setWidth(2);
    pen.setCosmetic(true);
    painter->setPen(pen);
    painter->setBrush(Qt::white);
    const qreal half = handleSize_ / 2.0;
    painter->drawRect(QRectF(-half, -half, handleSize_, handleSize_));

    painter->setBrush(Qt::NoBrush);
    if (role_ == Role::Origin) {
        painter->drawLine(QPointF(-half, 0.0), QPointF(half, 0.0));
        painter->drawLine(QPointF(0.0, -half), QPointF(0.0, half));
    } else {
        const qreal d = half / 2.0;
        painter->drawLine(QPointF(-d, d), QPointF(d, -d));
    }
}

// 图像尺寸：落位与钳制的边界。
void GridHandleItem::setImageBounds(const int width, const int height) {
    imgW_ = width;
    imgH_ = height;
}

// 已提交参数：仅回灌落位（拖拽基线 dragBase_ 只在按下时快照，故拖拽中被回灌不会打乱增量）。
void GridHandleItem::setParams(const engine::GridParams& params) {
    params_ = params;
    setPos(handleScenePos());
}

// 两轴是否周期：分割数/比例轴没有可拖的基准点与单元尺寸。
// 两轴皆非周期时两个手柄都退场（无可拖之物），否则重新参与显隐/可交互判定。
void GridHandleItem::setPitchAxes(const bool xPitch, const bool yPitch) {
    pitchX_ = xPitch;
    pitchY_ = yPitch;
    setPos(handleScenePos());
    if (!draggable()) {
        setVisible(false);   // 两轴皆非周期：无可拖之物，退场（重新周期性由 setEditable 显回）
        setInteractive(false);
    }
}

// 手柄边长（场景单位）：抓取区与绘制尺寸同步，故须 prepareGeometryChange。
void GridHandleItem::setHandleSize(const qreal sceneUnits) {
    handleSize_ = std::max(1.0, sceneUnits);
    prepareGeometryChange();
    update();
}

// 显隐 + 可交互：网格线图层隐藏 / 非 L3 时整体退场（遵循「隐藏图层=不可交互」）。
// 两轴皆非周期（无可拖之物）时也不显示——即使图层可见。
void GridHandleItem::setEditable(const bool on) {
    setVisible(on && draggable());
    setInteractive(on);
}

// 仅切鼠标：四态模式门控用（图片拾取 / 标注选取态让位，手柄仍可见）。
void GridHandleItem::setInteractive(const bool on) {
    setAcceptedMouseButtons(on && draggable() && imgW_ > 0 && imgH_ > 0 ? Qt::LeftButton : Qt::NoButton);
}

// 手柄在场景中的落点：Origin → 基准点；Cell → 基准点 + 单元尺寸。
// 非周期轴（分割数/比例）自 0 起铺、也无可调单元尺寸，故该轴坐标按 0 计——
// 单元尺寸手柄在该轴上于是落在切割线上，与「无单元可缩放」的语义一致。
QPointF GridHandleItem::handleScenePos() const {
    const qreal x0 = pitchX_ ? static_cast<qreal>(params_.originX) : 0.0;
    const qreal y0 = pitchY_ ? static_cast<qreal>(params_.originY) : 0.0;
    if (role_ == Role::Origin) return {x0, y0};
    return {x0 + (pitchX_ ? params_.cellWidth : 0), y0 + (pitchY_ ? params_.cellHeight : 0)};
}

// 按下：快照基线、记按点位；无图则不接管（交场景既有交互）。
void GridHandleItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (imgW_ <= 0 || imgH_ <= 0) { event->ignore(); return; }
    dragging_ = true;
    dragBase_ = params_;
    pressScenePos_ = event->scenePos();
    event->accept();
}

// 移动：以按下基线 + 取整后的增量为准，故拖动不会因中途回灌而漂移。
void GridHandleItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    if (!dragging_) { event->ignore(); return; }
    applyDrag(event->scenePos());
    event->accept();
}

// 释放：结束手势并一次性提交（值未变也发——Document 侧早退，不含重算）。
void GridHandleItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    if (!dragging_) { event->ignore(); return; }
    dragging_ = false;
    event->accept();
    emit committed(params_);
}

// 外部打断（缩放抢占）：拖拽中的参数是预览草稿（Document 未动），回落到按下基线后走同一
// committed 通道——发的是原值，Document 侧早退无重算，但场景得以据此重铺网格线并恢复点选图元显隐
//（previewed 拖拽期把 pickerItem_ 隐了，不归位就一直看不到单元高亮）。
void GridHandleItem::cancelDrag() {
    if (!dragging_) return;
    dragging_ = false;
    params_ = dragBase_;
    update();
    emit committed(params_);
}

// 拖动几何：整数像素吸附（不做边界/中线磁吸——基准与单元尺寸本就是整数像素量），
// 越界一律拒绝：基准点钳在 [0, W-cw]×[0, H-ch]（单元尺寸不变，故上界同时保证另一手柄
// (x0+cw, y0+ch) 留在图内）、单元宽高钳在 [1, W-x0]×[1, H-y0]，
// 于是两个手柄恒留在图内、单元恒为正尺寸（Core Grid::build 的要求）。
// 非周期轴不参与：基准点手柄在该轴不动、单元尺寸手柄也不改该轴（分割数/比例无周期可调）。
void GridHandleItem::applyDrag(const QPointF& scenePos) {
    if (imgW_ <= 0 || imgH_ <= 0 || !draggable()) return;
    const QPointF delta = scenePos - pressScenePos_;
    const int dx = static_cast<int>(std::lround(delta.x()));
    const int dy = static_cast<int>(std::lround(delta.y()));

    engine::GridParams next = dragBase_;
    if (role_ == Role::Origin) {
        // 单元尺寸不变，故基准点上界受右/下端点约束（W-1 仅在单元为 0 宽时才会成为更紧的界）。
        if (pitchX_) {
            const int maxX = std::clamp(imgW_ - dragBase_.cellWidth, 0, imgW_ - 1);
            next.originX = std::clamp(dragBase_.originX + dx, 0, maxX);
        }
        if (pitchY_) {
            const int maxY = std::clamp(imgH_ - dragBase_.cellHeight, 0, imgH_ - 1);
            next.originY = std::clamp(dragBase_.originY + dy, 0, maxY);
        }
    } else {
        if (pitchX_)
            next.cellWidth = std::clamp(dragBase_.cellWidth + dx, 1, std::max(1, imgW_ - dragBase_.originX));
        if (pitchY_)
            next.cellHeight = std::clamp(dragBase_.cellHeight + dy, 1, std::max(1, imgH_ - dragBase_.originY));
    }
    if (next.originX == params_.originX && next.originY == params_.originY &&
        next.cellWidth == params_.cellWidth && next.cellHeight == params_.cellHeight) {
        return;   // 未变（含越界顶到底）：不重画、不发信号。
    }
    params_ = next;
    setPos(handleScenePos());   // 自身跟手（另一个手柄由场景按预览参数同步）。
    emit previewed(params_);
}

} // namespace idc::gui
