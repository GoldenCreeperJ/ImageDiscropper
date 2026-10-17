// ============================================================================
// 文件：src/geometry/shapes.cpp
// 作用：实现 include/geometry/shapes.h 声明的各类具体形状，以及 shape_type.h
//       中枚举到字符串的映射函数 shapeTypeName。
// 说明：所有形状都可经 toPath() 转成 Path；命中检测与包围盒计算多数委托 Path 的实现
//       以避免重复代码，矩形 / 椭圆 / 文字等则用更直接的特化判定（见各自实现）。
// ============================================================================
#include "geometry/shapes.h"

#include "core/core_nums.h" // 零长边阈值单一真相源（常量层）

#include <algorithm>
#include <cmath>

// MSVC 默认不定义 M_PI，这里统一给出常量。
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace idc::geometry {

namespace {

// 对折线 / 多边形顶点做角度自适应圆角（fillet）：每个参与倒角的顶点以二次贝塞尔替换，
// 控制点取原顶点，两个切点分别落在相邻两边上、距顶点 ri = min(r, 半相邻边长) 处；
// 半径自动夹到相邻两边各半以避免自交。开放折线跳过首末两端，闭合则处理全部顶点。
// r<=0 或顶点不足（<3）时退化为直角折线（等同原有 moveTo/lineTo/closePath 构造）。
Path buildFilletPath(const std::vector<core::Point2D>& pts, const bool closed, const double r) {
    const std::size_t n = pts.size();
    if (r <= 0.0 || n < 3) {
        Path p;
        if (n == 0) return p;
        p.moveTo(pts[0].x, pts[0].y);
        for (std::size_t i = 1; i < n; ++i) p.lineTo(pts[i].x, pts[i].y);
        if (closed) p.closePath();
        return p;
    }

    // 由顶点 V 指向 to 的单位向量，并通过 len 回传边长（零长边返回零向量）。
    auto unitTo = [](const core::Point2D& from, const core::Point2D& to, double& len) {
        const double dx = to.x - from.x;
        const double dy = to.y - from.y;
        len = std::hypot(dx, dy);
        if (len <= core::nums::geometry::kZeroLengthEps) return core::Point2D{0.0, 0.0}; // 零长边：返回零向量
        return core::Point2D{dx / len, dy / len};
    };

    // 计算顶点 idx 的入边切点 A 与出边切点 B（闭合按取模取前后顶点；开放由调用方仅对内部顶点调用）。
    auto tangents = [&](const std::size_t idx, core::Point2D& A, core::Point2D& B) {
        const core::Point2D& V = pts[idx];
        const std::size_t pi = closed ? (idx + n - 1) % n : idx - 1;
        const std::size_t ni = closed ? (idx + 1) % n : idx + 1;
        double lenPrev = 0.0;
        double lenNext = 0.0;
        const core::Point2D uPrev = unitTo(V, pts[pi], lenPrev);   // 指向前驱
        const core::Point2D uNext = unitTo(V, pts[ni], lenNext);   // 指向后继
        const double ri = std::max(0.0, std::min({r, lenPrev * 0.5, lenNext * 0.5}));
        A = {V.x + uPrev.x * ri, V.y + uPrev.y * ri};
        B = {V.x + uNext.x * ri, V.y + uNext.y * ri};
    };

    Path p;
    if (closed) {
        core::Point2D a0{};
        core::Point2D b0{};
        tangents(0, a0, b0);
        p.moveTo(b0.x, b0.y);                         // 起于顶点 0 出边切点
        for (std::size_t i = 1; i < n; ++i) {
            core::Point2D A{};
            core::Point2D B{};
            tangents(i, A, B);
            p.lineTo(A.x, A.y);                        // 直线连到入边切点
            p.quadTo(pts[i].x, pts[i].y, B.x, B.y);    // 二次贝塞尔绕过顶点
        }
        p.lineTo(a0.x, a0.y);                          // 收尾回到顶点 0 入边切点
        p.quadTo(pts[0].x, pts[0].y, b0.x, b0.y);
        p.closePath();
    } else {
        p.moveTo(pts[0].x, pts[0].y);                  // 首端点保持锐角
        for (std::size_t i = 1; i + 1 < n; ++i) {       // 仅内部顶点倒角
            core::Point2D A{};
            core::Point2D B{};
            tangents(i, A, B);
            p.lineTo(A.x, A.y);
            p.quadTo(pts[i].x, pts[i].y, B.x, B.y);
        }
        p.lineTo(pts[n - 1].x, pts[n - 1].y);          // 末端点保持锐角
    }
    return p;
}

} // namespace

// ---------------------------------------------------------------------------
// shape_type.h 中的枚举转字符串（返回串供日志与调试显示）
// ---------------------------------------------------------------------------
const char* shapeTypeName(const ShapeType t) {
    switch (t) {
        case ShapeType::Line: return "LINE";
        case ShapeType::Rectangle: return "RECTANGLE";
        case ShapeType::Square: return "SQUARE";
        case ShapeType::Rhombus: return "RHOMBUS";
        case ShapeType::Ellipse: return "ELLIPSE";
        case ShapeType::Circle: return "CIRCLE";
        case ShapeType::Polyline: return "POLYLINE";
        case ShapeType::Path: return "PATH";
        case ShapeType::Text: return "TEXT";
        case ShapeType::IsosTriangle: return "ISOS_TRIANGLE";
        case ShapeType::EquTriangle: return "EQU_TRIANGLE";
        case ShapeType::RectTriangle: return "RECT_TRIANGLE";
        case ShapeType::EquRectTriangle: return "EQU_RECT_TRIANGLE";
    }
    return "UNKNOWN";
}

// ===========================================================================
// EllipseShape
// ===========================================================================

// 构造：外接矩形 + 类型（Ellipse 或 Circle）。
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

// 构造：顶点列表 + 类型 + 是否闭合 + 转角圆角半径（px，0＝直角）。
PolygonShape::PolygonShape(std::vector<core::Point2D> points, const ShapeType type, const bool closed,
                           const double cornerRadius)
    : points_(std::move(points)), type_(type), closed_(closed),
      cornerRadius_(cornerRadius < 0.0 ? 0.0 : cornerRadius) {}

// 转为路径：无圆角时 moveTo → 若干 lineTo →（可选）closePath；半径>0 时各顶点二次贝塞尔倒角。
Path PolygonShape::toPath() const {
    if (cornerRadius_ > 0.0) return buildFilletPath(points_, closed_, cornerRadius_);
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
    auto s = std::make_unique<PolygonShape>(points_, type_, closed_, cornerRadius_);
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
// PathShape（仅画笔自由路径）
// ===========================================================================

// 构造：直接持有一条 Path + 类型（toPath 已内联返回原路径，无倒角）。
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

// 平移：委托 Path::translate（逐段偏移，保留 Path / Polyline 类型）。
void PathShape::translate(const double dx, const double dy) { path_.translate(dx, dy); }

// 控制点：抽取所有 MoveTo / LineTo 端点，曲线段取终点。
std::vector<core::Point2D> PathShape::controlPoints() const {
    std::vector<core::Point2D> pts;
    for (const auto&[type, coords] : path_.segments()) {
        switch (type) {
            case PathSegmentType::MoveTo:
            case PathSegmentType::LineTo:
                pts.emplace_back(coords[0], coords[1]);
                break;
            case PathSegmentType::QuadTo:
                pts.emplace_back(coords[2], coords[3]);
                break;
            case PathSegmentType::CubicTo:
                pts.emplace_back(coords[4], coords[5]);
                break;
            case PathSegmentType::Close:
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

// 控制点：仅给出文本基线锚点。
std::vector<core::Point2D> TextShape::controlPoints() const {
    return {{x_, y_}};
}

} // namespace idc::geometry
