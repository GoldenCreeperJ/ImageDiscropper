// ============================================================================
// 文件：src/canvas/annotation_item.cpp
// 作用：实现单个标注的矢量渲染与拖动平移（见同名头文件说明）。
// 分块依据：
//   1. 几何重建    setAnnotation / rebuildPath —— 由 Core Shape::worldPath 经适配器翻译为 QPainterPath
//   2. 绘制        boundingRect / shape / paint —— 描边 + 可选填充；TEXT 直接绘字形；选中叠加 OBB 与手柄
//   3. 拖动平移    mousePress/Move/Release —— setPos 实时反馈，释放先复位再发 moveRequested（拖拽安全）
//   4. 手柄变换    computeObb / hitHandle / begin|updateHandleDrag —— 定向包围盒 8 手柄缩放 + 旋转手柄，
//                  拖拽中仅本地矢量预览（Core obbPreviewTransform），释放发 transformRequested（拖拽安全）
// 说明：颜色/坐标翻译走 util/path_qt_adapter；包围盒手柄坐标、缩放/旋转矩阵均由 Core 提供（A-0.1）。
// ============================================================================
#include "canvas/annotation_item.h"

#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QPainterPathStroker>
#include <QPen>

#include "canvas/z_order.h"
#include "geometry/shapes.h"
#include "util/annotation_qt_painter.h"
#include "util/path_qt_adapter.h"

namespace idc::gui {

namespace {
// 把缩放系数钳制到安全范围：限幅 [-50,50] 防失控；避免近 0（行列式为 0 会使逆变换退化、形状塌缩）。
double clampScale(double f) {
    if (f > 50.0) f = 50.0;
    else if (f < -50.0) f = -50.0;
    if (f > -0.02 && f < 0.02) f = f < 0.0 ? -0.02 : 0.02;
    return f;
}
} // namespace

// 构造：z 序固定为标注层；默认不接受悬停，仅左键用于拖动。
AnnotationItem::AnnotationItem(QGraphicsItem* parent) : QGraphicsObject(parent) {
    setZValue(zorder::kAnnotation);
    setAcceptedMouseButtons(Qt::LeftButton);
}

// 载入标注拷贝并重建渲染路径。
void AnnotationItem::setAnnotation(const annotation::Annotation& ann) {
    prepareGeometryChange();
    ann_ = ann;   // Annotation 拷贝构造会深拷贝 shape
    rebuildPath();
    update();
}

// 由 ann_.shape 重建 path_ 与 bounds_（含线宽 / 手柄余量，保证选中高亮与旋转手柄不被裁切）。
void AnnotationItem::rebuildPath() {
    // 取世界路径（已套用形状的非破坏性变换），故缩放/旋转/翻转后的矢量渲染与导出一致。
    path_ = ann_.shape ? toQPainterPath(ann_.shape->worldPath()) : QPainterPath();
    // 余量需容纳旋转手柄（顶边中点外推 handleSize_*3 + 手柄半径/抓取区），故取 handleSize_*5。
    const qreal margin = static_cast<qreal>(ann_.strokeWidth) + handleSize_ * 5.0 + 4.0;
    bounds_ = path_.boundingRect().adjusted(-margin, -margin, margin, margin);
}

void AnnotationItem::setSelectedState(const bool on) {
    if (selected_ == on) return;
    selected_ = on;
    prepareGeometryChange();   // 选中会额外绘制控制点，需扩展/收缩包围盒
    rebuildPath();
    update();
}

void AnnotationItem::setHandleSize(const qreal sceneUnits) {
    handleSize_ = sceneUnits;
    prepareGeometryChange();
    rebuildPath();
    update();
}

QRectF AnnotationItem::boundingRect() const { return bounds_; }

// 命中轮廓：填充形状返回路径本身（含内部），否则返回按线宽加粗的描边轮廓；
// 选中时额外并入 OBB 手柄的抓取区（手柄在包围盒上、可能远离笔画，否则点不到）。
QPainterPath AnnotationItem::shape() const {
    QPainterPath s;
    if (!path_.isEmpty()) {
        if (ann_.fillType && ann_.shapeType != geometry::ShapeType::LINE) {
            s = path_;
        } else {
            QPainterPathStroker stroker;
            stroker.setWidth(static_cast<qreal>(ann_.strokeWidth) + handleSize_);
            s = stroker.createStroke(path_);
        }
    }
    if (selected_ && ann_.shape) {
        const ObbFrame f = computeObb(ann_.shape->transform());
        const qreal r = handleSize_ * 2.0;   // 抓取区略大于可视手柄，手柄较小时也易点中
        auto add = [&s, r](const QPointF& p) { s.addRect(QRectF(p.x() - r, p.y() - r, 2 * r, 2 * r)); };
        for (int i = 0; i < 4; ++i) { add(f.corner[i]); add(f.edge[i]); }
        add(f.rotate);
    }
    return s;
}

void AnnotationItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    const bool isText = ann_.shapeType == geometry::ShapeType::TEXT;
    if (path_.isEmpty() && !isText) return;
    painter->setRenderHint(QPainter::Antialiasing, true);

    const bool previewing = activeHandle_ != Handle::None && ann_.shape;

    // 有效世界变换：拖拽手柄时用预览变换（起点变换 ∘ 局部缩放旋转），否则用形状当前变换。
    geometry::AffineTransform xf;
    if (ann_.shape) {
        xf = previewing ? ann_.shape->obbPreviewTransform(previewSx_, previewSy_, previewRot_)
                        : ann_.shape->transform();
    }

    // 几何绘制（描边 / 填充 / 文字字形）委托公共渲染函数 util/annotation_qt_painter——与导出输出预览
    // 的标注烘焙同一实现，消除重复、保证两处外观一致。普通态用缓存的 path_（＝worldPath 翻译）；拖拽
    // 预览态用 xf.applyToPath(toPath()) 的实时路径（线宽仍为世界单位，与提交后一致）。
    QPainterPath drawPath = path_;
    if (previewing && !isText && ann_.shape) drawPath = toQPainterPath(xf.applyToPath(ann_.shape->toPath()));
    paintAnnotation(*painter, ann_, drawPath, xf);

    // 选中高亮：定向包围盒（虚线）+ 8 个缩放手柄 + 1 个旋转手柄。
    if (selected_ && ann_.shape) {
        const ObbFrame f = computeObb(xf);
        QPen hi(QColor(0, 160, 230));
        hi.setWidth(1);
        hi.setStyle(Qt::DashLine);
        hi.setCosmetic(true);   // 高亮线宽恒定 1 屏幕px，不随缩放变粗
        painter->setPen(hi);
        painter->setBrush(Qt::NoBrush);
        QPainterPath box;
        box.moveTo(f.corner[0]);
        for (int i = 1; i < 4; ++i) box.lineTo(f.corner[i]);
        box.closeSubpath();
        painter->drawPath(box);
        painter->drawLine(f.edge[0], f.rotate);   // 旋转手柄连到顶边中点

        const qreal h = handleSize_ * 1.35;   // 略放大手柄，便于看清与点中
        painter->setBrush(QColor(0, 160, 230));
        for (int i = 0; i < 4; ++i) {   // 8 个缩放手柄（方块）
            painter->drawRect(QRectF(f.corner[i].x() - h / 2, f.corner[i].y() - h / 2, h, h));
            painter->drawRect(QRectF(f.edge[i].x() - h / 2, f.edge[i].y() - h / 2, h, h));
        }
        painter->setBrush(QColor(255, 255, 255));   // 旋转手柄（白底蓝边圆形，与缩放手柄区分）
        painter->drawEllipse(QRectF(f.rotate.x() - h * 0.6, f.rotate.y() - h * 0.6, h * 1.2, h * 1.2));
    }
}

// 依世界变换 xf 计算 OBB 的9 个手柄世界坐标：将局部包围盒四角经 xf 映射，再取边中点与中心；
// 旋转手柄置于顶边中点沿「中心→顶边」方向外推固定场景距离处。
AnnotationItem::ObbFrame AnnotationItem::computeObb(const geometry::AffineTransform& xf) const {
    ObbFrame f;
    const auto [x, y, width, height] = ann_.shape->bounds();   // 局部（未变换）轴对齐盒
    const core::Point2D ltl{x, y};
    const core::Point2D ltr{x + width, y};
    const core::Point2D lbr{x + width, y + height};
    const core::Point2D lbl{x, y + height};
    auto W = [&xf](const core::Point2D& p) {
        const core::Point2D q = xf.applyToPoint(p);
        return QPointF(q.x, q.y);
    };
    f.corner[0] = W(ltl); f.corner[1] = W(ltr); f.corner[2] = W(lbr); f.corner[3] = W(lbl);
    f.edge[0] = (f.corner[0] + f.corner[1]) / 2.0;   // T
    f.edge[1] = (f.corner[1] + f.corner[2]) / 2.0;   // R
    f.edge[2] = (f.corner[2] + f.corner[3]) / 2.0;   // B
    f.edge[3] = (f.corner[3] + f.corner[0]) / 2.0;   // L
    f.center = W(core::Point2D{x + width / 2.0, y + height / 2.0});
    QPointF up = f.edge[0] - f.center;               // 中心→顶边中点（OBB 的「上」方向）
    if (const qreal len = std::hypot(up.x(), up.y()); len > 1e-6) up /= len; else up = QPointF(0, -1);
    f.rotate = f.edge[0] + up * (handleSize_ * 3.0);
    return f;
}

// 命中测试：优先旋转手柄，再 4 角、再 4 边中点；均不在半径内则 None。
AnnotationItem::Handle AnnotationItem::hitHandle(const QPointF& scenePos, const ObbFrame& f) const {
    const qreal r = handleSize_ * 2.0;   // 与 shape() 的抓取区一致（略大于可视手柄）
    auto near = [&](const QPointF& p) { return std::hypot(p.x() - scenePos.x(), p.y() - scenePos.y()) <= r; };
    if (near(f.rotate)) return Handle::Rotate;
    if (near(f.corner[0])) return Handle::TL;
    if (near(f.corner[1])) return Handle::TR;
    if (near(f.corner[2])) return Handle::BR;
    if (near(f.corner[3])) return Handle::BL;
    if (near(f.edge[0])) return Handle::T;
    if (near(f.edge[1])) return Handle::R;
    if (near(f.edge[2])) return Handle::B;
    if (near(f.edge[3])) return Handle::L;
    return Handle::None;
}

// 按下：已选中时优先检测 OBB 手柄（命中则进入缩放/旋转拖拽）；否则上报场景坐标交上层用
// Core hitTest 选中（同步骤回灌 selected_），并进入拖动平移预备态。
void AnnotationItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (event->button() != Qt::LeftButton) { event->ignore(); return; }
    if (selected_ && ann_.shape) {
        const ObbFrame f = computeObb(ann_.shape->transform());
        if (const Handle h = hitHandle(event->scenePos(), f); h != Handle::None) { beginHandleDrag(h, event->scenePos()); event->accept(); return; }
    }
    emit pressed(event->scenePos());   // 同步链路：上层 selectAt → updateAnnotations → setSelectedState
    dragging_ = true;
    lastScenePos_ = event->scenePos();
    accumDelta_ = QPointF();
    event->accept();
}

// 移动：手柄拖拽时走缩放/旋转预览（不改 Core，仅重绘）；否则累计位移并 setPos 实时反馈。
void AnnotationItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    if (!dragging_ || !selected_) { event->ignore(); return; }
    if (activeHandle_ != Handle::None) { updateHandleDrag(event->scenePos()); event->accept(); return; }
    const QPointF d = event->scenePos() - lastScenePos_;
    lastScenePos_ = event->scenePos();
    accumDelta_ += d;
    setPos(accumDelta_);
    event->accept();
}

// 释放：手柄拖拽——复位预览态后发 transformRequested（拖拽安全：此后不再触碰 this，上层改 Core 后回灌）；
// 平移——先复位 pos（几何由模型重建后回灌）再发 moveRequested。
void AnnotationItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    if (!dragging_) { event->ignore(); return; }
    dragging_ = false;
    if (activeHandle_ != Handle::None) {
        const double sx = previewSx_, sy = previewSy_, rot = previewRot_;
        activeHandle_ = Handle::None;
        previewSx_ = 1.0; previewSy_ = 1.0; previewRot_ = 0.0;
        event->accept();
        if (std::fabs(sx - 1.0) > 1e-6 || std::fabs(sy - 1.0) > 1e-6 || std::fabs(rot) > 1e-6) {
            emit transformRequested(sx, sy, rot);   // 上层 → model.transformSelected → Core applyObbTransform
        } else {
            prepareGeometryChange(); rebuildPath(); update();   // 无变化：复位预览包围盒
        }
        return;
    }
    const QPointF d = accumDelta_;
    accumDelta_ = QPointF();
    setPos(0.0, 0.0);
    event->accept();
    if (selected_ && !d.isNull()) emit moveRequested(d.x(), d.y());
}

// 进入手柄拖拽：捕获起点参考（世界中心；旋转手柄额外记起点方向），复位预览参数。
void AnnotationItem::beginHandleDrag(const Handle h, const QPointF& scenePos) {
    Q_UNUSED(scenePos);
    activeHandle_ = h;
    dragging_ = true;                 // 复用：令 updateAnnotations 在手势期间跳过对本图元的几何回设
    accumDelta_ = QPointF();
    setPos(0.0, 0.0);
    previewSx_ = 1.0; previewSy_ = 1.0; previewRot_ = 0.0;
    const auto [x, y, width, height] = ann_.shape->bounds();
    const core::Point2D cw =
        ann_.shape->localToWorld(core::Point2D{x + width / 2.0, y + height / 2.0});
    dragCenterWorld_ = QPointF(cw.x, cw.y);
    if (h == Handle::Rotate) {
        const ObbFrame f = computeObb(ann_.shape->transform());
        rotateStartVec_ = f.rotate - dragCenterWorld_;   // 起点时旋转手柄相对中心的方向
    }
}

// 拖拽：旋转手柄→算相对起点的角度增量；缩放手柄→把世界光标映回局部系、沿对应轴算有符号缩放系数
// （越过中心即负＝翻转）。仅更新预览参数与包围盒并重绘，不改 Core。
void AnnotationItem::updateHandleDrag(const QPointF& scenePos) {
    if (!ann_.shape) return;
    if (activeHandle_ == Handle::Rotate) {
        const QPointF v = scenePos - dragCenterWorld_;
        const double a0 = std::atan2(rotateStartVec_.y(), rotateStartVec_.x());
        const double a1 = std::atan2(v.y(), v.x());
        double deg = (a1 - a0) * 180.0 / 3.14159265358979323846;
        while (deg > 180.0) deg -= 360.0;
        while (deg <= -180.0) deg += 360.0;
        previewRot_ = deg;
    } else {
        const auto [x, y, width, height] = ann_.shape->bounds();
        const double halfW = width / 2.0, halfH = height / 2.0;
        const core::Point2D cl{x + halfW, y + halfH};
        const core::Point2D lc =
            ann_.shape->worldToLocal(core::Point2D{scenePos.x(), scenePos.y()});
        // 角手柄同时缩放两轴；边中点手柄只缩放垂直于该边的一轴。
        const bool corner = activeHandle_ == Handle::TL || activeHandle_ == Handle::TR ||
                            activeHandle_ == Handle::BR || activeHandle_ == Handle::BL;
        const bool useX = corner || activeHandle_ == Handle::L || activeHandle_ == Handle::R;
        const bool useY = corner || activeHandle_ == Handle::T || activeHandle_ == Handle::B;
        double sx = 1.0, sy = 1.0;
        if (useX && halfW > 1e-6) {
            const bool rightSide = activeHandle_ == Handle::TR || activeHandle_ == Handle::R ||
                                   activeHandle_ == Handle::BR;
            sx = rightSide ? (lc.x - cl.x) / halfW : (cl.x - lc.x) / halfW;
        }
        if (useY && halfH > 1e-6) {
            const bool bottomSide = activeHandle_ == Handle::BR || activeHandle_ == Handle::B ||
                                    activeHandle_ == Handle::BL;
            sy = bottomSide ? (lc.y - cl.y) / halfH : (cl.y - lc.y) / halfH;
        }
        previewSx_ = clampScale(sx);
        previewSy_ = clampScale(sy);
    }
    // 逐帧上报**绝对**预览值供属性面板数值联动（当前累积 ∘ 本次手势增量；仅回显，不写模型，释时才提交）。
    emit transformPreview(ann_.shape->obbScaleX() * previewSx_,
                          ann_.shape->obbScaleY() * previewSy_,
                          ann_.shape->obbRotationDeg() + previewRot_);
    updatePreviewBounds();
    update();
}

// 按预览形状（可能已放大/旋转）扩展 bounds_，避免拖拽中手柄与形状被旧包围盒裁剪。
void AnnotationItem::updatePreviewBounds() {
    if (!ann_.shape) return;
    const geometry::AffineTransform xf =
        ann_.shape->obbPreviewTransform(previewSx_, previewSy_, previewRot_);
    const QPainterPath pp = toQPainterPath(xf.applyToPath(ann_.shape->toPath()));
    const qreal margin = static_cast<qreal>(ann_.strokeWidth) + handleSize_ * 5.0 + 4.0;
    prepareGeometryChange();
    bounds_ = pp.boundingRect().adjusted(-margin, -margin, margin, margin);
}

} // namespace idc::gui
