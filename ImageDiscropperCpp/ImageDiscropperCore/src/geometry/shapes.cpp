// ============================================================================
// 文件：src/geometry/shapes.cpp
// 作用：实现 include/geometry/shapes.h 声明的各类具体形状，以及 shape_type.h
//       中枚举到字符串的映射函数 shapeTypeName。
// 说明：所有形状都可经 toPath() 转成 Path；命中检测与包围盒计算多数委托 Path 的实现
//       以避免重复代码，矩形 / 椭圆 / 文字等则用更直接的特化判定（见各自实现）。
// ============================================================================
#include "geometry/shapes.h"

#include <algorithm>
#include <cmath>

// MSVC 默认不定义 M_PI，这里统一给出常量。
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace idc::geometry {

// ---------------------------------------------------------------------------
// shape_type.h 中的枚举转字符串
// ---------------------------------------------------------------------------
const char* shapeTypeName(const ShapeType t) {
    switch (t) {
        case ShapeType::LINE: return "LINE";
        case ShapeType::RECTANGLE: return "RECTANGLE";
        case ShapeType::SQUARE: return "SQUARE";
        case ShapeType::RHOMBUS: return "RHOMBUS";
        case ShapeType::ROUNDRECTANGLE: return "ROUNDRECTANGLE";
        case ShapeType::ROUNDSQUARE: return "ROUNDSQUARE";
        case ShapeType::ELLIPSE: return "ELLIPSE";
        case ShapeType::CIRCLE: return "CIRCLE";
        case ShapeType::POLYLINE: return "POLYLINE";
        case ShapeType::PATH: return "PATH";
        case ShapeType::TEXT: return "TEXT";
        case ShapeType::ISOS_TRIANGLE: return "ISOS_TRIANGLE";
        case ShapeType::EQU_TRIANGLE: return "EQU_TRIANGLE";
        case ShapeType::RECT_TRIANGLE: return "RECT_TRIANGLE";
        case ShapeType::EQU_RECT_TRIANGLE: return "EQU_RECT_TRIANGLE";
    }
    return "UNKNOWN";
}

// ===========================================================================
// LineShape
// ===========================================================================

// 构造：由两个端点确定一条直线段。
LineShape::LineShape(const double x1, const double y1, const double x2, const double y2)
    : x1_(x1), y1_(y1), x2_(x2), y2_(y2) {}

// 转为路径：moveTo → lineTo。
Path LineShape::toPath() const {
    Path p;
    p.moveTo(x1_, y1_);
    p.lineTo(x2_, y2_);
    return p;
}

// 包围盒：两端点确定的最小矩形，宽高至少为 0。
BoundingBox LineShape::bounds() const {
    const double minX = std::min(x1_, x2_);
    const double minY = std::min(y1_, y2_);
    return {minX, minY, std::abs(x2_ - x1_), std::abs(y2_ - y1_)};
}

// 直线不含内部，恒返回 false（命中检测请使用 distanceToSegment）。
bool LineShape::contains(const core::Point2D&) const { return false; }

// 深拷贝。
std::unique_ptr<Shape> LineShape::clone() const {
    auto s = std::make_unique<LineShape>(x1_, y1_, x2_, y2_);
    copyXformTo(*s);   // 保留非破坏性变换（缩放/旋转/翻转），避免深拷贝丢失。
    return s;
}

// 平移：两个端点各偏移 (dx, dy)。
void LineShape::translate(const double dx, const double dy) {
    x1_ += dx; y1_ += dy;
    x2_ += dx; y2_ += dy;
}

// 控制点：两个端点。
std::vector<core::Point2D> LineShape::controlPoints() const {
    return {{x1_, y1_}, {x2_, y2_}};
}

// 计算点到线段的最短距离，用于命中检测。
double LineShape::distanceToSegment(const core::Point2D& p) const {
    const double dx = x2_ - x1_;
    const double dy = y2_ - y1_;
    const double lenSq = dx * dx + dy * dy;
    if (lenSq <= 1e-12) return std::hypot(p.x - x1_, p.y - y1_);
    double t = ((p.x - x1_) * dx + (p.y - y1_) * dy) / lenSq;
    t = std::clamp(t, 0.0, 1.0);
    return std::hypot(p.x - (x1_ + t * dx), p.y - (y1_ + t * dy));
}

// ===========================================================================
// RectShape
// ===========================================================================

// 构造：由左上角坐标与宽高定义。
RectShape::RectShape(const double x, const double y, const double w, const double h)
    : x_(x), y_(y), w_(w), h_(h) {}

// 转为路径：四条边 + closePath。
Path RectShape::toPath() const {
    Path p;
    p.moveTo(x_, y_);
    p.lineTo(x_ + w_, y_);
    p.lineTo(x_ + w_, y_ + h_);
    p.lineTo(x_, y_ + h_);
    p.closePath();
    return p;
}

BoundingBox RectShape::bounds() const { return {x_, y_, w_, h_}; }

// 内部判定：直接比较包围盒即可。
bool RectShape::contains(const core::Point2D& p) const {
    return p.x >= x_ && p.x <= x_ + w_ && p.y >= y_ && p.y <= y_ + h_;
}

std::unique_ptr<Shape> RectShape::clone() const {
    auto s = std::make_unique<RectShape>(x_, y_, w_, h_);
    copyXformTo(*s);   // 保留非破坏性变换。
    return s;
}

// 平移：左上角偏移 (dx, dy)，宽高不变。
void RectShape::translate(const double dx, const double dy) { x_ += dx; y_ += dy; }

// 控制点：四个角点。
std::vector<core::Point2D> RectShape::controlPoints() const {
    return {{x_, y_}, {x_ + w_, y_}, {x_ + w_, y_ + h_}, {x_, y_ + h_}};
}

// ===========================================================================
// RoundRectShape
// ===========================================================================

// 构造：矩形 + 圆角弧度宽高，可选类型（用于区分 ROUNDRECTANGLE / ROUNDSQUARE）。
RoundRectShape::RoundRectShape(const double x, const double y, const double w, const double h,
                               const double arcW, const double arcH, const ShapeType type)
    : x_(x), y_(y), w_(w), h_(h), arcW_(arcW), arcH_(arcH), type_(type) {}

// 转为路径：使用四段三次贝塞尔近似圆角，kappa ≈ 0.5523。
Path RoundRectShape::toPath() const {
    const double aw = std::min(arcW_, w_ / 2.0);
    const double ah = std::min(arcH_, h_ / 2.0);
    const double kx = aw * 0.5523;
    const double ky = ah * 0.5523;

    Path p;
    p.moveTo(x_ + aw, y_);
    p.lineTo(x_ + w_ - aw, y_);
    p.cubicTo(x_ + w_ - aw + kx, y_, x_ + w_, y_ + ah - ky, x_ + w_, y_ + ah);
    p.lineTo(x_ + w_, y_ + h_ - ah);
    p.cubicTo(x_ + w_, y_ + h_ - ah + ky, x_ + w_ - aw + kx, y_ + h_, x_ + w_ - aw, y_ + h_);
    p.lineTo(x_ + aw, y_ + h_);
    p.cubicTo(x_ + aw - kx, y_ + h_, x_, y_ + h_ - ah + ky, x_, y_ + h_ - ah);
    p.lineTo(x_, y_ + ah);
    p.cubicTo(x_, y_ + ah - ky, x_ + aw - kx, y_, x_ + aw, y_);
    p.closePath();
    return p;
}

BoundingBox RoundRectShape::bounds() const { return {x_, y_, w_, h_}; }

// 内部判定：使用扁平化后的路径扫描线判定，保证圆角处判定准确。
bool RoundRectShape::contains(const core::Point2D& p) const {
    return toPath().contains(p.x, p.y);
}

std::unique_ptr<Shape> RoundRectShape::clone() const {
    auto s = std::make_unique<RoundRectShape>(x_, y_, w_, h_, arcW_, arcH_, type_);
    copyXformTo(*s);   // 保留非破坏性变换。
    return s;
}

// 平移：左上角偏移 (dx, dy)，宽高与圆角不变。
void RoundRectShape::translate(const double dx, const double dy) { x_ += dx; y_ += dy; }

// 控制点：四个角点，圆角不额外提供控制点。
std::vector<core::Point2D> RoundRectShape::controlPoints() const {
    return {{x_, y_}, {x_ + w_, y_}, {x_ + w_, y_ + h_}, {x_, y_ + h_}};
}

// ===========================================================================
// EllipseShape
// ===========================================================================

// 构造：外接矩形 + 类型（ELLIPSE 或 CIRCLE）。
EllipseShape::EllipseShape(const double x, const double y, const double w, const double h, const ShapeType type)
    : x_(x), y_(y), w_(w), h_(h), type_(type) {}

// 转为路径：四段三次贝塞尔近似椭圆，kappa ≈ 0.5523。
Path EllipseShape::toPath() const {
    const double cx = x_ + w_ / 2.0;
    const double cy = y_ + h_ / 2.0;
    const double rx = w_ / 2.0;
    const double ry = h_ / 2.0;
    const double kx = rx * 0.5523;
    const double ky = ry * 0.5523;

    Path p;
    p.moveTo(cx + rx, cy);
    p.cubicTo(cx + rx, cy + ky, cx + kx, cy + ry, cx, cy + ry);
    p.cubicTo(cx - kx, cy + ry, cx - rx, cy + ky, cx - rx, cy);
    p.cubicTo(cx - rx, cy - ky, cx - kx, cy - ry, cx, cy - ry);
    p.cubicTo(cx + kx, cy - ry, cx + rx, cy - ky, cx + rx, cy);
    p.closePath();
    return p;
}

BoundingBox EllipseShape::bounds() const { return {x_, y_, w_, h_}; }

// 内部判定：使用椭圆标准方程，避免展开为折线的性能开销。
bool EllipseShape::contains(const core::Point2D& p) const {
    const double rx = w_ / 2.0;
    const double ry = h_ / 2.0;
    if (rx <= 0 || ry <= 0) return false;
    const double dx = (p.x - (x_ + rx)) / rx;
    const double dy = (p.y - (y_ + ry)) / ry;
    return dx * dx + dy * dy <= 1.0;
}

std::unique_ptr<Shape> EllipseShape::clone() const {
    auto s = std::make_unique<EllipseShape>(x_, y_, w_, h_, type_);
    copyXformTo(*s);   // 保留非破坏性变换。
    return s;
}

// 平移：外接矩形左上角偏移 (dx, dy)（圆心随之平移），半径不变。
void EllipseShape::translate(const double dx, const double dy) { x_ += dx; y_ += dy; }

// 控制点：中心 + 上下左右四点。
std::vector<core::Point2D> EllipseShape::controlPoints() const {
    const double cx = x_ + w_ / 2.0;
    const double cy = y_ + h_ / 2.0;
    return {{cx, cy}, {cx, y_}, {cx, y_ + h_}, {x_, cy}, {x_ + w_, cy}};
}

// ===========================================================================
// PolygonShape
// ===========================================================================

// 构造：顶点列表 + 类型 + 是否闭合。
PolygonShape::PolygonShape(std::vector<core::Point2D> points, const ShapeType type, const bool closed)
    : points_(std::move(points)), type_(type), closed_(closed) {}

// 转为路径：moveTo → 若干 lineTo →（可选）closePath。
Path PolygonShape::toPath() const {
    Path p;
    if (points_.empty()) return p;
    p.moveTo(points_[0].x, points_[0].y);
    for (std::size_t i = 1; i < points_.size(); ++i) {
        p.lineTo(points_[i].x, points_[i].y);
    }
    if (closed_) p.closePath();
    return p;
}

BoundingBox PolygonShape::bounds() const { return toPath().bounds(); }

// 内部判定：闭合时使用 Path::contains；开放时恒 false。
bool PolygonShape::contains(const core::Point2D& p) const {
    if (!closed_) return false;
    return toPath().contains(p.x, p.y);
}

std::unique_ptr<Shape> PolygonShape::clone() const {
    auto s = std::make_unique<PolygonShape>(points_, type_, closed_);
    copyXformTo(*s);   // 保留非破坏性变换。
    return s;
}

// 平移：逐个顶点偏移 (dx, dy)，保留多边形类型与顶点语义。
void PolygonShape::translate(const double dx, const double dy) {
    for (core::Point2D& pt : points_) { pt.x += dx; pt.y += dy; }
}

// 控制点：所有顶点。
std::vector<core::Point2D> PolygonShape::controlPoints() const { return points_; }

// ===========================================================================
// PathShape
// ===========================================================================

// 构造：直接持有一条 Path。
PathShape::PathShape(Path path, const ShapeType type)
    : path_(std::move(path)), type_(type) {}

// 内部判定：委托给 Path::contains。
bool PathShape::contains(const core::Point2D& p) const {
    return path_.contains(p.x, p.y);
}

std::unique_ptr<Shape> PathShape::clone() const {
    auto s = std::make_unique<PathShape>(path_, type_);
    copyXformTo(*s);   // 保留非破坏性变换。
    return s;
}

// 平移：委托 Path::translate（逐段偏移，保留 PATH / POLYLINE 类型）。
void PathShape::translate(const double dx, const double dy) { path_.translate(dx, dy); }

// 控制点：抽取所有 MOVE_TO / LINE_TO 端点，曲线段取终点。
std::vector<core::Point2D> PathShape::controlPoints() const {
    std::vector<core::Point2D> pts;
    for (const auto&[type, coords] : path_.segments()) {
        switch (type) {
            case PathSegmentType::MOVE_TO:
            case PathSegmentType::LINE_TO:
                pts.emplace_back(coords[0], coords[1]);
                break;
            case PathSegmentType::QUAD_TO:
                pts.emplace_back(coords[2], coords[3]);
                break;
            case PathSegmentType::CUBIC_TO:
                pts.emplace_back(coords[4], coords[5]);
                break;
            case PathSegmentType::CLOSE:
                break;
        }
    }
    return pts;
}

// ===========================================================================
// TextShape（占位实现）
// ===========================================================================

// 构造：文本内容、锚点坐标（左下角）、字号。
TextShape::TextShape(std::string text, const double x, const double y, const double fontSize)
    : text_(std::move(text)), x_(x), y_(y), fontSize_(fontSize) {}

// 估算文本宽度：无字体库时按「字符数 × 单字系数」粗略估算——
// ASCII 半宽（0.5 × fontSize）、多字节字符全宽（1.0 × fontSize，如中文）。
// 逐字节扫描 UTF-8：续字节（10xxxxxx）跳过，其余字节即字符起点。
double TextShape::estimateWidth() const {
    double units = 0.0;
    for (const unsigned char b : text_) {
        if ((b & 0xC0) == 0x80) continue;  // UTF-8 续字节：随字符起点已计数，跳过
        units += b < 0x80 ? 0.5 : 1.0;   // ASCII 半宽、多字节字符全宽
    }
    return units * fontSize_;
}

// 转为路径：以估算宽高构成矩形边界。
Path TextShape::toPath() const {
    const double w = estimateWidth();
    const double h = fontSize_;
    Path p;
    p.moveTo(x_, y_ - h);
    p.lineTo(x_ + w, y_ - h);
    p.lineTo(x_ + w, y_);
    p.lineTo(x_, y_);
    p.closePath();
    return p;
}

BoundingBox TextShape::bounds() const {
    return {x_, y_ - fontSize_, estimateWidth(), fontSize_};
}

bool TextShape::contains(const core::Point2D& p) const {
    return bounds().contains(p.x, p.y);
}

std::unique_ptr<Shape> TextShape::clone() const {
    auto s = std::make_unique<TextShape>(text_, x_, y_, fontSize_);
    copyXformTo(*s);   // 保留非破坏性变换（旋转/缩放文字时字形随之变换，不丢字）。
    return s;
}

// 平移：文本基线锚点偏移 (dx, dy)，保留文本内容与字号（不丢字形）。
void TextShape::translate(const double dx, const double dy) { x_ += dx; y_ += dy; }

// 控制点：仅给出文本基线起点。
std::vector<core::Point2D> TextShape::controlPoints() const {
    return {{x_, y_}};
}

} // namespace idc::geometry
