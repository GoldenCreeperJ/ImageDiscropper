// ============================================================================
// 文件：util/path_qt_adapter.h
// 作用：Core 几何/颜色类型与 Qt 绘图类型之间的适配转换（GUI 视图关注点，非引擎逻辑）：
//       geometry::Path → QPainterPath（供 QGraphicsItem 矢量渲染标注）、
//       core::Color ↔ QColor（供标注属性面板取色与画布画笔/画刷着色）。
// 分块依据：仅承载「Core 纯数据 ↔ Qt 绘图数据」的单向/双向翻译，不含任何几何计算或
//           光栅化——曲线离散化、命中、填充全在 Core（guideline §0 A-0.1）。
// 说明：QPainterPath 原生支持二次/三次贝塞尔，故逐段直接映射，无需先扁平化（保留精度）。
// ============================================================================
#pragma once

#include <QColor>
#include <QPainterPath>
#include <QTransform>

#include "core/color.h"
#include "geometry/affine_transform.h"
#include "geometry/path.h"

namespace idc::gui {

// 将 Core 的 geometry::Path 逐段翻译为 QPainterPath：
// MOVE_TO→moveTo、LINE_TO→lineTo、QUAD_TO→quadTo、CUBIC_TO→cubicTo、CLOSE→closeSubpath。
QPainterPath toQPainterPath(const geometry::Path& path);

// core::Color（RGBA 8 位）→ QColor。
QColor toQColor(const core::Color& c);

// QColor → core::Color（取 RGBA 分量，越界分量由 QColor 自身钳制）。
core::Color toCoreColor(const QColor& c);

// Core 仿射变换 → QTransform（供文字标注在画布上以矢量方式旋转/缩放/翻转字形）。
// 两者布局一致：Core x'=a*x+c*y+tx, y'=b*x+d*y+ty ↔ QTransform(a,b,c,d,tx,ty)。
QTransform toQTransform(const geometry::AffineTransform& t);

} // namespace idc::gui
