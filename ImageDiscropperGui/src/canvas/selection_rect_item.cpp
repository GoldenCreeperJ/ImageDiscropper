// ============================================================================
// 文件：canvas/selection_rect_item.cpp
// 作用：实现选区框的移动 / 四角缩放 / 单边拖动（＝移动切割线）/ 吸附 / 自绘
//       （含把被标记的边延伸为橙色贯穿切割线），见同名头文件说明。
// 分块依据：
//   - 交互几何（命中手柄、命中边、平移钳制、对角缩放、单边拖动、吸附）集中在鼠标事件与私有工具；
//   - 每次改动 rect_ 前调用 prepareGeometryChange()，保证场景索引与重绘区域正确；
//   - 移动/缩放/拖边过程中实时 emit rectChanged，实现「拖拽即刷新遮罩」（A-0.8）。
// 说明：本图元不做切割；产出的矩形交 Document → Core 决定贯穿切割线与保留/删除集。
//       切割线就是被标记边的贯穿延伸，与选区框同为一支橙色画笔、同一个图元（唯一橙色交互体），
//       因此「拖动橙线」与「拖动选区边」是同一件事，物理上不可能错位/分离。
// ============================================================================
#include "canvas/selection_rect_item.h"

#include <algorithm>
#include <array>
#include <cmath>

#include <QBrush>
#include <QColor>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QPen>
#include <QStyleOptionGraphicsItem>

namespace idc::gui {
namespace {

// 吸附工具：若 v 距任一目标不超过阈值 th，则吸附到该目标，否则原样返回。
qreal snap1(const qreal v, const qreal th, const std::array<qreal, 3>& targets) {
    for (const qreal t : targets) {
        if (std::abs(v - t) <= th) return t;
    }
    return v;
}

// 把点钳制到图像范围 [0,W]×[0,H] 内。
QPointF clampPoint(const QPointF& p, const int w, const int h) {
    return QPointF(std::clamp(p.x(), 0.0, static_cast<qreal>(w)),
                   std::clamp(p.y(), 0.0, static_cast<qreal>(h)));
}

} // namespace

// 构造：默认无选区、无边界；接收左键（移动/缩放/拖边）。
SelectionRectItem::SelectionRectItem(QGraphicsItem* parent) : QGraphicsObject(parent) {
    setAcceptedMouseButtons(Qt::LeftButton);
    setZValue(0); // 具体 z 由 CanvasScene 统一设置。
}

// 设置选区（规范化）。
void SelectionRectItem::setRect(const QRectF& r) {
    prepareGeometryChange();
    rect_ = r.normalized();
    update();
}

// 设置图像边界。
void SelectionRectItem::setImageBounds(const int width, const int height) {
    imgW_ = width;
    imgH_ = height;
    hasBounds_ = (width > 0 && height > 0);
    prepareGeometryChange();
    update();
}

// 设置手柄边长（同时作为抓边条带半宽与吸附阈值）。
void SelectionRectItem::setHandleSize(const qreal sceneUnits) {
    handleSize_ = std::max(1.0, sceneUnits);
    prepareGeometryChange();
    update();
}

// 下发哪几条边延伸为贯穿切割线（依 Core CutLineSet；见 CanvasScene::updateCutLines）。
void SelectionRectItem::setCutEdges(const bool left, const bool right, const bool top, const bool bottom) {
    if (cutEdge_[EdgeLeft] == left && cutEdge_[EdgeRight] == right &&
        cutEdge_[EdgeTop] == top && cutEdge_[EdgeBottom] == bottom) {
        return;
    }
    cutEdge_[EdgeLeft] = left;
    cutEdge_[EdgeRight] = right;
    cutEdge_[EdgeTop] = top;
    cutEdge_[EdgeBottom] = bottom;
    prepareGeometryChange();
    update();
}

// 设置高亮态（多矩形下标记当前选中项）：仅值变化时重绘，避免拖拽期逐帧无谓 update。
void SelectionRectItem::setHighlighted(const bool on) {
    if (highlighted_ == on) return;
    highlighted_ = on;
    update();
}

// 包围盒：选区外扩手柄尺寸；若有边被标记为贯穿切割线，则并入整幅图像范围（线贯穿全图）。
QRectF SelectionRectItem::boundingRect() const {
    const qreal m = handleSize_ + 2.0;
    QRectF b = rect_.normalized().adjusted(-m, -m, m, m);
    if (hasBounds_ && (cutEdge_[EdgeLeft] || cutEdge_[EdgeRight] ||
                       cutEdge_[EdgeTop] || cutEdge_[EdgeBottom])) {
        b = b.united(QRectF(-m, -m, imgW_ + 2.0 * m, imgH_ + 2.0 * m));
    }
    return b;
}

// 计算四角手柄中心（TL, TR, BL, BR）。
void SelectionRectItem::cornerPoints(QPointF& tl, QPointF& tr, QPointF& bl, QPointF& br) const {
    const QRectF r = rect_.normalized();
    tl = QPointF(r.left(), r.top());
    tr = QPointF(r.right(), r.top());
    bl = QPointF(r.left(), r.bottom());
    br = QPointF(r.right(), r.bottom());
}

// 绘制：橙色贯穿切割线（标记边的延伸）+ 橙色半透明选区 + 四角白底橙框手柄。
void SelectionRectItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* /*option*/,
                              QWidget* /*widget*/) {
    const QRectF r = rect_.normalized();

    // 统一橙色画笔（§4.2：橙色实线 2px；cosmetic 使线宽不随缩放变化）。切割线与选区框共用此笔，
    // 故二者同色、同图元、天然合一。高亮态（多矩形选中项）颜色略深、线宽略粗以示区分。
    QPen pen(highlighted_ ? QColor(255, 90, 0) : QColor(255, 140, 0));
    pen.setWidth(highlighted_ ? 3 : 2);
    pen.setCosmetic(true);
    painter->setPen(pen);

    // 先画贯穿切割线：把被标记的边沿法向延伸贯穿全图（竖边贯穿全高、横边贯穿全宽）。
    if (hasBounds_) {
        painter->setBrush(Qt::NoBrush);
        const qreal W = static_cast<qreal>(imgW_);
        const qreal H = static_cast<qreal>(imgH_);
        if (cutEdge_[EdgeLeft])   painter->drawLine(QPointF(r.left(), 0.0),  QPointF(r.left(), H));
        if (cutEdge_[EdgeRight])  painter->drawLine(QPointF(r.right(), 0.0), QPointF(r.right(), H));
        if (cutEdge_[EdgeTop])    painter->drawLine(QPointF(0.0, r.top()),    QPointF(W, r.top()));
        if (cutEdge_[EdgeBottom]) painter->drawLine(QPointF(0.0, r.bottom()), QPointF(W, r.bottom()));
    }

    // 选区填充与边框（叠在切割线之上）；高亮态填充略深。
    painter->setBrush(QColor(255, 165, 0, highlighted_ ? 60 : 30));
    painter->setPen(pen);
    painter->drawRect(r);

    // 四角手柄：白底 + 橙色细框，边长 = handleSize_（场景单位，随缩放换算保持约 8 屏幕px）。
    QPointF tl, tr, bl, br;
    cornerPoints(tl, tr, bl, br);
    QPen handlePen(QColor(255, 140, 0));
    handlePen.setWidth(1);
    handlePen.setCosmetic(true);
    painter->setPen(handlePen);
    painter->setBrush(Qt::white);
    const qreal half = handleSize_ / 2.0;
    for (const QPointF& c : {tl, tr, bl, br}) {
        painter->drawRect(QRectF(c.x() - half, c.y() - half, handleSize_, handleSize_));
    }
}

// 命中角手柄：返回 0=TL,1=TR,2=BL,3=BR，未命中 -1。
int SelectionRectItem::hitHandle(const QPointF& scenePos) const {
    QPointF tl, tr, bl, br;
    cornerPoints(tl, tr, bl, br);
    const std::array<QPointF, 4> cs{tl, tr, bl, br};
    const qreal th = handleSize_; // 略大于手柄半径，便于抓取。
    for (int i = 0; i < 4; ++i) {
        if (std::abs(scenePos.x() - cs[i].x()) <= th &&
            std::abs(scenePos.y() - cs[i].y()) <= th) {
            return i;
        }
    }
    return -1;
}

// 命中边：返回 EdgeLeft/EdgeRight/EdgeTop/EdgeBottom，未命中 -1。
// 竖边比较 x、横边比较 y，落在 ±handleSize_ 抓边条带内、且沿线绘制跨度（标记边贯穿全图，
// 否则仅选区那段）即命中；命中多条时取法向距离最近者。角手柄已在 hitHandle 优先处理。
int SelectionRectItem::hitEdge(const QPointF& p) const {
    if (!hasBounds_) return -1;
    const QRectF r = rect_.normalized();
    const qreal th = handleSize_;
    const qreal W = static_cast<qreal>(imgW_);
    const qreal H = static_cast<qreal>(imgH_);
    int best = -1;
    qreal bestD = th; // 仅接受法向距离 ≤ th 的命中

    // 竖边（Left/Right）：沿线 y 跨度 = 标记边贯穿全高 [0,H]，否则仅选区高 [top,bottom]。
    const struct VEdge { int edge; qreal x; bool cut; } vs[2] = {
        {EdgeLeft, r.left(), cutEdge_[EdgeLeft]},
        {EdgeRight, r.right(), cutEdge_[EdgeRight]},
    };
    for (const VEdge& v : vs) {
        const qreal y0 = v.cut ? 0.0 : r.top();
        const qreal y1 = v.cut ? H : r.bottom();
        if (p.y() >= y0 - th && p.y() <= y1 + th) {
            const qreal d = std::abs(p.x() - v.x);
            if (d <= bestD) { bestD = d; best = v.edge; }
        }
    }
    // 横边（Top/Bottom）：沿线 x 跨度 = 标记边贯穿全宽 [0,W]，否则仅选区宽 [left,right]。
    const struct HEdge { int edge; qreal y; bool cut; } hs[2] = {
        {EdgeTop, r.top(), cutEdge_[EdgeTop]},
        {EdgeBottom, r.bottom(), cutEdge_[EdgeBottom]},
    };
    for (const HEdge& h : hs) {
        const qreal x0 = h.cut ? 0.0 : r.left();
        const qreal x1 = h.cut ? W : r.right();
        if (p.x() >= x0 - th && p.x() <= x1 + th) {
            const qreal d = std::abs(p.y() - h.y);
            if (d <= bestD) { bestD = d; best = h.edge; }
        }
    }
    return best;
}

// 平移钳制到图像内。
void SelectionRectItem::clampToImage(QRectF& r) const {
    if (!hasBounds_) return;
    if (r.left() < 0) r.translate(-r.left(), 0);
    if (r.top() < 0) r.translate(0, -r.top());
    if (r.right() > imgW_) r.translate(imgW_ - r.right(), 0);
    if (r.bottom() > imgH_) r.translate(0, imgH_ - r.bottom());
}

// 边缘/中心吸附。preserveSize=true（整体移动）：保持宽高不变，只取「最接近目标的某条边」
// 所需的整体平移量（位移最小且 ≤ 阈值），避免逐边独立吸附把移动变成尺寸突变。
// preserveSize=false（缩放/拖边）：四边各自独立吸附（本就在改尺寸）。
void SelectionRectItem::applySnap(const bool preserveSize) {
    if (!hasBounds_) return;
    const QRectF r = rect_.normalized();
    const qreal th = handleSize_;
    const std::array<qreal, 3> xs{0.0, imgW_ / 2.0, static_cast<qreal>(imgW_)};
    const std::array<qreal, 3> ys{0.0, imgH_ / 2.0, static_cast<qreal>(imgH_)};

    if (preserveSize) {
        // 水平：左/右边中找一个到某 x 目标位移最小的吸附，整体平移 dx（宽高不变）。
        const std::array<qreal, 2> xedges{r.left(), r.right()};
        const std::array<qreal, 2> yedges{r.top(), r.bottom()};
        qreal bestDx = th + 1.0;
        for (const qreal e : xedges)
            for (const qreal t : xs) {
                const qreal d = t - e;
                if (std::abs(d) <= th && std::abs(d) < std::abs(bestDx)) bestDx = d;
            }
        qreal bestDy = th + 1.0;
        for (const qreal e : yedges)
            for (const qreal t : ys) {
                const qreal d = t - e;
                if (std::abs(d) <= th && std::abs(d) < std::abs(bestDy)) bestDy = d;
            }
        const qreal dx = (std::abs(bestDx) <= th) ? bestDx : 0.0;
        const qreal dy = (std::abs(bestDy) <= th) ? bestDy : 0.0;
        QRectF nr = r.translated(dx, dy);
        clampToImage(nr);
        rect_ = nr;
        return;
    }

    const qreal l = snap1(r.left(), th, xs);
    const qreal rr = snap1(r.right(), th, xs);
    const qreal t = snap1(r.top(), th, ys);
    const qreal b = snap1(r.bottom(), th, ys);
    rect_ = QRectF(QPointF(l, t), QPointF(rr, b)).normalized();
}

// 按下：四角手柄缩放 / 抓边拖动（＝移动切割线）/ 框内整体移动 / 框外忽略（交视图新建选区）。
void SelectionRectItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (!hasBounds_) { event->ignore(); return; }
    const QPointF p = event->scenePos();
    const int h = hitHandle(p);
    if (h >= 0) {
        // 四角缩放：固定对角不动。TL<->BR、TR<->BL。
        QPointF tl, tr, bl, br;
        cornerPoints(tl, tr, bl, br);
        mode_ = DragMode::Resize;
        switch (h) {
            case 0: fixedPoint_ = br; break;
            case 1: fixedPoint_ = bl; break;
            case 2: fixedPoint_ = tr; break;
            default: fixedPoint_ = tl; break;
        }
    } else {
        const int e = hitEdge(p);
        if (e >= 0) {
            mode_ = DragMode::Edge;   // 抓边（或其贯穿延伸段）：沿法向拖动该边＝移动对应切割线。
            dragEdge_ = e;
        } else if (rect_.normalized().contains(p)) {
            mode_ = DragMode::Move;   // 框内且未压边：整体平移选区。
        } else {
            event->ignore();          // 点在框外且未压线：让视图处理为「框选新选区」。
            return;
        }
    }
    lastPos_ = p;
    event->accept();
}

// 移动：整体平移 / 对角缩放 / 单边法向拖动，实时钳制并发信号。
void SelectionRectItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    const QPointF p = event->scenePos();
    if (mode_ == DragMode::Move) {
        const QPointF d = p - lastPos_;
        QRectF r = rect_.normalized().translated(d);
        clampToImage(r);
        prepareGeometryChange();
        rect_ = r;
        lastPos_ = p;
        update();
        emit rectChanged(rect_);
    } else if (mode_ == DragMode::Resize) {
        const QPointF c = clampPoint(p, imgW_, imgH_);
        prepareGeometryChange();
        rect_ = QRectF(fixedPoint_, c).normalized();
        update();
        emit rectChanged(rect_);
    } else if (mode_ == DragMode::Edge) {
        // 单边拖动：只改被拖的那条边，对边固定；钳制到图像内且保持 ≥1 的最小尺寸（不退化）。
        const QPointF c = clampPoint(p, imgW_, imgH_);
        QRectF r = rect_.normalized();
        switch (dragEdge_) {
            case EdgeLeft:   r.setLeft(std::min(c.x(), r.right() - 1.0));  break;
            case EdgeRight:  r.setRight(std::max(c.x(), r.left() + 1.0));  break;
            case EdgeTop:    r.setTop(std::min(c.y(), r.bottom() - 1.0));  break;
            case EdgeBottom: r.setBottom(std::max(c.y(), r.top() + 1.0));   break;
            default: break;
        }
        prepareGeometryChange();
        rect_ = r.normalized();
        update();
        emit rectChanged(rect_);
    }
    event->accept();
}

// 释放：应用吸附、先退出拖拽态再发最终信号（使上层这一次做完整回设与面板回同步）。
void SelectionRectItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    const bool wasDragging = (mode_ != DragMode::None);
    // 整体移动须保持宽高：吸附时只平移不逐边独立吸附（否则移动会变成尺寸突变）。
    const bool preserveSize = (mode_ == DragMode::Move);
    if (wasDragging && snapEnabled_) applySnap(preserveSize);
    prepareGeometryChange();
    // 先置拖拽态为 None、再 emit：这样释放这一次 isDragging()==false，上层会照常回设选区框
    // 并同步面板；而拖拽过程中（mouseMoveEvent 的 emit）isDragging()==true，上层跳过回设。
    mode_ = DragMode::None;
    dragEdge_ = -1;
    if (wasDragging) {
        update();
        emit rectChanged(rect_.normalized());
    }
    event->accept();
}

} // namespace idc::gui
