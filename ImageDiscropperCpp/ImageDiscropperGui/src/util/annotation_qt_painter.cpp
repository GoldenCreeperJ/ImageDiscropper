// ============================================================================
// 文件：src/util/annotation_qt_painter.cpp
// 作用：实现 paintAnnotation——把一条 Core Annotation 以矢量方式绘到 QPainter（见同名头说明）。
//       此为画布图元 AnnotationItem::paint 与导出预览标注烘焙（output_preview_renderer）的公共实现，
//       两处渲染外观因此保持一致（同一描边/填充/字形规则）。
// 说明：颜色/路径/变换翻译复用 util/path_qt_adapter；文字字形按局部坐标绘制后套用有效世界变换。
// ============================================================================
#include "util/annotation_qt_painter.h"

#include <QBrush>
#include <QFont>
#include <QPainter>
#include <QPen>

#include "annotation/annotation_layer.h"   // Annotation（几何 + 样式）
#include "geometry/shapes.h"               // Shape::toPath / TextShape / ShapeType
#include "util/path_qt_adapter.h"          // toQColor / toQTransform / toQPainterPath

namespace idc::gui {

void paintAnnotation(QPainter& painter,
                     const idc::annotation::Annotation& ann,
                     const QPainterPath& worldDrawPath,
                     const idc::geometry::AffineTransform& textXf) {
    if (!ann.shape) return;
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QColor color = toQColor(ann.color);

    // 文字：Core TextShape 的 toPath 仅给矩形边界，这里直接绘真实字形（渲染关注点）。
    if (ann.shapeType == idc::geometry::ShapeType::TEXT) {
        const auto* ts = dynamic_cast<const idc::geometry::TextShape*>(ann.shape.get());
        if (!ts) return;
        painter.save();
        // 字形在**局部坐标**绘制，套用有效世界变换（含拖拽预览）——旋转/缩放/翻转由 QPainter 矢量完成，
        // 字形不失真、不丢字。叠加（combine）在调用方已设的世界→设备变换之上。
        if (!textXf.isIdentity()) painter.setTransform(toQTransform(textXf), /*combine=*/true);
        QFont f = painter.font();
        f.setPixelSize(static_cast<int>(ts->fontSize()));
        painter.setFont(f);
        painter.setPen(color);
        const QRectF r = toQPainterPath(ann.shape->toPath()).boundingRect();
        painter.drawText(r.topLeft() + QPointF(0, r.height() * 0.8), QString::fromStdString(ts->text()));
        painter.restore();
        return;
    }

    // 非文字：描边（+ 可选填充）调用方给出的世界坐标路径。
    if (worldDrawPath.isEmpty()) return;
    QPen pen(color);
    pen.setWidthF(static_cast<qreal>(ann.strokeWidth));   // 世界单位线宽，随 painter 变换缩放到设备
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);
    painter.setBrush(ann.fillType ? QBrush(color) : Qt::NoBrush);
    painter.drawPath(worldDrawPath);
}

} // namespace idc::gui
