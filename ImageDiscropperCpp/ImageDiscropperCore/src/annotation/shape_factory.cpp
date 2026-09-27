// ============================================================================
// 文件：src/annotation/shape_factory.cpp
// 作用：实现 include/annotation/shape_factory.h 声明的 buildShape 函数：
//       依据 ShapeType 与两个锚点 (p1, p2) 构造对应 Shape。
// ============================================================================
#include "annotation/shape_factory.h"

#include <algorithm>
#include <vector>

// MSVC 默认不定义 M_PI，这里统一给出常量。
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace idc::annotation {

namespace {

// 从路径草稿抽取有序顶点（各段终点），供折线转 PolygonShape 表达。
std::vector<core::Point2D> pathVertices(const geometry::Path& path) {
    std::vector<core::Point2D> pts;
    for (const geometry::PathSegment& seg : path.segments()) {
        switch (seg.type) {
            case geometry::PathSegmentType::MOVE_TO:
            case geometry::PathSegmentType::LINE_TO:
                pts.emplace_back(seg.coords[0], seg.coords[1]);
                break;
            case geometry::PathSegmentType::QUAD_TO:
                pts.emplace_back(seg.coords[2], seg.coords[3]);
                break;
            case geometry::PathSegmentType::CUBIC_TO:
                pts.emplace_back(seg.coords[4], seg.coords[5]);
                break;
            case geometry::PathSegmentType::CLOSE:
                break;
        }
    }
    return pts;
}

// 退化占位：输入尺寸过小时返回一条退化开放直线（2 顶点 PolygonShape，类型 LINE）。
std::unique_ptr<geometry::Shape> degenerateLine(const double x1, const double y1,
                                                const double x2, const double y2) {
    return std::make_unique<geometry::PolygonShape>(
        std::vector<core::Point2D>{{x1, y1}, {x2, y2}}, geometry::ShapeType::LINE, false);
}

} // namespace

// 根据 ShapeRequest 构造具体形状实例。
// 说明：
//   - PATH（画笔）封装为 PathShape；POLYLINE / LINE 由顶点列表转 PolygonShape；
//   - 对 TEXT，用 (p1.x, p1.y) 作为文本基线起点；
//   - 其余类型均从 (p1, p2) 计算几何参数，cornerRadius>0 时顶点倒角。
std::unique_ptr<geometry::Shape> buildShape(const ShapeRequest& req) {
    using geometry::ShapeType;
    const double x1 = req.p1.x, y1 = req.p1.y;
    const double x2 = req.p2.x, y2 = req.p2.y;
    const double dx = x2 - x1;
    const double dy = y2 - y1;
    const double distance = std::hypot(dx, dy);

    switch (req.type) {
        case ShapeType::LINE:
            // 直线＝2 顶点开放多边形（端点无夹角，不倒角）。
            return std::make_unique<geometry::PolygonShape>(
                std::vector<core::Point2D>{{x1, y1}, {x2, y2}}, ShapeType::LINE, false);

        case ShapeType::PATH:
            // 画笔自由路径：仅 PathShape 承载，无转角倒角。
            return std::make_unique<geometry::PathShape>(req.path, req.type);

        case ShapeType::POLYLINE:
            // 折线：抽取路径顶点转为开放 PolygonShape，cornerRadius>0 时内部顶点倒角。
            return std::make_unique<geometry::PolygonShape>(
                pathVertices(req.path), ShapeType::POLYLINE, false, req.cornerRadius);

        case ShapeType::CIRCLE: {
            // 起点为圆心，两点距离为半径
            return std::make_unique<geometry::EllipseShape>(
                x1 - distance, y1 - distance, 2 * distance, 2 * distance,
                ShapeType::CIRCLE);
        }

        case ShapeType::ELLIPSE: {
            // 两点确定外接矩形
            const double minX = std::min(x1, x2);
            const double minY = std::min(y1, y2);
            return std::make_unique<geometry::EllipseShape>(
                minX, minY, std::abs(dx), std::abs(dy), ShapeType::ELLIPSE);
        }

        case ShapeType::RECTANGLE: {
            // 并入 PolygonShape：四点闭合多边形，cornerRadius>0 时转角倒角。
            const double minX = std::min(x1, x2);
            const double minY = std::min(y1, y2);
            const double w = std::abs(dx);
            const double h = std::abs(dy);
            std::vector<core::Point2D> pts = {
                {minX, minY}, {minX + w, minY}, {minX + w, minY + h}, {minX, minY + h},
            };
            return std::make_unique<geometry::PolygonShape>(
                std::move(pts), ShapeType::RECTANGLE, true, req.cornerRadius);
        }

        case ShapeType::SQUARE: {
            // 以 (p1 → p2) 向量为一条边，构造正方形
            if (distance < 1e-3) {
                return degenerateLine(x1, y1, x1, y1);
            }
            std::vector<core::Point2D> pts = {
                {x1 + dx, y1 + dy},
                {x1 - dy, y1 + dx},
                {x1 - dx, y1 - dy},
                {x1 + dy, y1 - dx},
            };
            return std::make_unique<geometry::PolygonShape>(
                std::move(pts), ShapeType::SQUARE, true, req.cornerRadius);
        }

        case ShapeType::RHOMBUS: {
            // 起点为中心，|dx|、|dy| 为半对角线
            const double halfD1 = std::abs(dx);
            const double halfD2 = std::abs(dy);
            if (halfD1 < 1e-3 && halfD2 < 1e-3) {
                return degenerateLine(x1, y1, x1, y1);
            }
            std::vector<core::Point2D> pts = {
                {x1 + halfD1, y1},
                {x1, y1 - halfD2},
                {x1 - halfD1, y1},
                {x1, y1 + halfD2},
            };
            return std::make_unique<geometry::PolygonShape>(
                std::move(pts), ShapeType::RHOMBUS, true, req.cornerRadius);
        }

        case ShapeType::ISOS_TRIANGLE: {
            // 起点为底边中点，|dx| 为半底边，|dy| 为高
            const double halfBase = std::abs(dx);
            const double height = std::abs(dy);
            if (halfBase < 1e-3 && height < 1e-3) {
                return degenerateLine(x1, y1, x1, y1);
            }
            const double signY = y2 >= y1 ? -1.0 : 1.0;
            std::vector<core::Point2D> pts = {
                {x1 - halfBase, y1},
                {x1 + halfBase, y1},
                {x1, y1 - signY * height},
            };
            return std::make_unique<geometry::PolygonShape>(
                std::move(pts), ShapeType::ISOS_TRIANGLE, true, req.cornerRadius);
        }

        case ShapeType::EQU_TRIANGLE: {
            // 起点到当前点为一条边，构造等边三角形
            if (distance < 1.0) {
                return degenerateLine(x1, y1, x1, y1);
            }
            const double half = 2.0 / std::sqrt(3.0) * distance / 2.0;
            const double perpX = -dy / distance;
            const double perpY = dx / distance;
            std::vector<core::Point2D> pts = {
                {x1 + perpX * half, y1 + perpY * half},
                {x1 - perpX * half, y1 - perpY * half},
                {x2, y2},
            };
            return std::make_unique<geometry::PolygonShape>(
                std::move(pts), ShapeType::EQU_TRIANGLE, true, req.cornerRadius);
        }

        case ShapeType::RECT_TRIANGLE: {
            // 直角三角形：p1、p2、(x1, y2) 三点
            std::vector<core::Point2D> pts = {
                {x1, y1}, {x2, y2}, {x1, y2},
            };
            return std::make_unique<geometry::PolygonShape>(
                std::move(pts), ShapeType::RECT_TRIANGLE, true, req.cornerRadius);
        }

        case ShapeType::EQU_RECT_TRIANGLE: {
            // 等腰直角三角形：p1、p2、(x1 - dy, y1 + dx)
            if (std::abs(dx) + std::abs(dy) < 1e-3) {
                return degenerateLine(x1, y1, x1, y1);
            }
            std::vector<core::Point2D> pts = {
                {x1, y1}, {x2, y2}, {x1 - dy, y1 + dx},
            };
            return std::make_unique<geometry::PolygonShape>(
                std::move(pts), ShapeType::EQU_RECT_TRIANGLE, true, req.cornerRadius);
        }

        case ShapeType::TEXT: {
            return std::make_unique<geometry::TextShape>(req.text, x1, y1, req.fontSize);
        }
    }
    // 兜底：返回退化直线
    return degenerateLine(x1, y1, x2, y2);
}

} // namespace idc::annotation
