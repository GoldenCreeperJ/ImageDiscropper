// ============================================================================
// 文件：util/path_qt_adapter.cpp
// 作用：实现 Core 几何/颜色 → Qt 绘图类型的适配（见同名头文件说明）。
// 分块依据：三个纯翻译函数，无状态、无副作用；坐标顺序严格对齐 Core PathSegment 约定
//           （QUAD_TO=cx,cy,x,y；CUBIC_TO=c1x,c1y,c2x,c2y,x,y）。
// ============================================================================
#include "util/path_qt_adapter.h"

namespace idc::gui {

// 逐段把 Core Path 翻译成 QPainterPath；坐标不足（异常数据）的段安全跳过。
QPainterPath toQPainterPath(const idc::geometry::Path& path) {
    using idc::geometry::PathSegmentType;
    QPainterPath qp;
    for (const idc::geometry::PathSegment& seg : path.segments()) {
        const std::vector<double>& c = seg.coords;
        switch (seg.type) {
            case PathSegmentType::MOVE_TO:
                if (c.size() >= 2) qp.moveTo(c[0], c[1]);
                break;
            case PathSegmentType::LINE_TO:
                if (c.size() >= 2) qp.lineTo(c[0], c[1]);
                break;
            case PathSegmentType::QUAD_TO:
                if (c.size() >= 4) qp.quadTo(c[0], c[1], c[2], c[3]);
                break;
            case PathSegmentType::CUBIC_TO:
                if (c.size() >= 6) qp.cubicTo(c[0], c[1], c[2], c[3], c[4], c[5]);
                break;
            case PathSegmentType::CLOSE:
                qp.closeSubpath();
                break;
        }
    }
    return qp;
}

// core::Color（RGBA 8 位）→ QColor。
QColor toQColor(const idc::core::Color& c) {
    return QColor(static_cast<int>(c.r), static_cast<int>(c.g),
                  static_cast<int>(c.b), static_cast<int>(c.a));
}

// QColor → core::Color（QColor::red()/alpha() 已返回 0..255 整数分量）。
idc::core::Color toCoreColor(const QColor& c) {
    return idc::core::Color(static_cast<std::uint8_t>(c.red()),
                            static_cast<std::uint8_t>(c.green()),
                            static_cast<std::uint8_t>(c.blue()),
                            static_cast<std::uint8_t>(c.alpha()));
}

// Core 仿射变换 → QTransform：QTransform(m11,m12,m21,m22,dx,dy) 映射为
// x'=m11*x+m21*y+dx, y'=m12*x+m22*y+dy，与 Core x'=a*x+c*y+tx, y'=b*x+d*y+ty 逐一对应。
QTransform toQTransform(const idc::geometry::AffineTransform& t) {
    return QTransform(t.a, t.b, t.c, t.d, t.tx, t.ty);
}

} // namespace idc::gui
