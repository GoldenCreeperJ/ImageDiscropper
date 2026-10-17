// ============================================================================
// 文件：src/annotation/shape_factory.cpp
// 作用：实现 include/annotation/shape_factory.h 声明的 buildShape 函数：
//       依据 ShapeType 与两个锚点 (p1, p2) 构造对应 Shape。
// ============================================================================
#include "annotation/shape_factory.h"

#include "core/core_nums.h" // 锚点退化容差单一真相源（常量层）

#include <algorithm>
#include <cmath>
#include <vector>

// MSVC 默认不定义 M_PI，这里统一给出常量。
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace idc::annotation {

namespace nums = core::nums; // 简写常量层命名空间（仅本文件内可见）

namespace {

// 从路径草稿抽取有序顶点（各段终点），供折线转 PolygonShape 表达。
std::vector<core::Point2D> pathVertices(const geometry::Path& path) {
    std::vector<core::Point2D> pts;
    for (const auto&[type, coords] : path.segments()) {
        switch (type) {
            case geometry::PathSegmentType::MoveTo:
            case geometry::PathSegmentType::LineTo:
                pts.emplace_back(coords[0], coords[1]);
                break;
            case geometry::PathSegmentType::QuadTo:
                pts.emplace_back(coords[2], coords[3]);
                break;
            case geometry::PathSegmentType::CubicTo:
                pts.emplace_back(coords[4], coords[5]);
                break;
            case geometry::PathSegmentType::Close:
                break;
        }
    }
    return pts;
}

// 退化占位：输入尺寸过小时返回一条退化开放直线（2 顶点 PolygonShape，类型 Line）。
std::unique_ptr<geometry::Shape> degenerateLine(const double x1, const double y1,
                                                const double x2, const double y2) {
    return std::make_unique<geometry::PolygonShape>(
        std::vector<core::Point2D>{{x1, y1}, {x2, y2}}, geometry::ShapeType::Line, false);
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
    // 锚点退化容差：取自常量层，与各形状的尺寸判废阈值同一处调参。
    constexpr double kEps = nums::annotation::kDegenerateEpsPx;

    switch (req.type) {
        case ShapeType::Line:
            // 直线＝2 顶点开放多边形（端点无夹角，不倒角）。
            return std::make_unique<geometry::PolygonShape>(
                std::vector<core::Point2D>{{x1, y1}, {x2, y2}}, ShapeType::Line, false);

        case ShapeType::Path:
            // 画笔自由路径：仅 PathShape 承载，无转角倒角。
            return std::make_unique<geometry::PathShape>(req.path, req.type);

        case ShapeType::Polyline:
            // 折线：抽取路径顶点转为开放 PolygonShape，cornerRadius>0 时内部顶点倒角。
            return std::make_unique<geometry::PolygonShape>(
                pathVertices(req.path), ShapeType::Polyline, false, req.cornerRadius);

        case ShapeType::Circle: {
            // 起点为圆心，两点距离为半径
            return std::make_unique<geometry::EllipseShape>(
                x1 - distance, y1 - distance, 2 * distance, 2 * distance,
                ShapeType::Circle);
        }

        case ShapeType::Ellipse: {
            // 落点为中心，|dx|、|dy| 为两半轴。
            const double a = std::abs(dx);
            const double b = std::abs(dy);
            if (a < kEps && b < kEps) {
                return degenerateLine(x1, y1, x1, y1);
            }
            return std::make_unique<geometry::EllipseShape>(
                x1 - a, y1 - b, 2 * a, 2 * b, ShapeType::Ellipse);
        }

        case ShapeType::Rectangle: {
            // 并入 PolygonShape：四点闭合多边形，cornerRadius>0 时转角倒角。
            const double minX = std::min(x1, x2);
            const double minY = std::min(y1, y2);
            const double w = std::abs(dx);
            const double h = std::abs(dy);
            std::vector<core::Point2D> pts = {
                {minX, minY}, {minX + w, minY}, {minX + w, minY + h}, {minX, minY + h},
            };
            return std::make_unique<geometry::PolygonShape>(
                std::move(pts), ShapeType::Rectangle, true, req.cornerRadius);
        }

        case ShapeType::Square: {
            // 落点为正方形的一个顶点，边长取 max(|dx|,|dy|)，轴对齐、朝光标所在象限生长（不随拖动旋转）。
            const double side = std::max(std::abs(dx), std::abs(dy));
            if (side < kEps) {
                return degenerateLine(x1, y1, x1, y1);
            }
            const double gx = dx >= 0 ? 1.0 : -1.0;
            const double gy = dy >= 0 ? 1.0 : -1.0;
            std::vector<core::Point2D> pts = {
                {x1, y1},
                {x1 + gx * side, y1},
                {x1 + gx * side, y1 + gy * side},
                {x1, y1 + gy * side},
            };
            return std::make_unique<geometry::PolygonShape>(
                std::move(pts), ShapeType::Square, true, req.cornerRadius);
        }

        case ShapeType::Rhombus: {
            // 起点为中心，|dx|、|dy| 为半对角线
            const double halfD1 = std::abs(dx);
            const double halfD2 = std::abs(dy);
            if (halfD1 < kEps && halfD2 < kEps) {
                return degenerateLine(x1, y1, x1, y1);
            }
            std::vector<core::Point2D> pts = {
                {x1 + halfD1, y1},
                {x1, y1 - halfD2},
                {x1 - halfD1, y1},
                {x1, y1 + halfD2},
            };
            return std::make_unique<geometry::PolygonShape>(
                std::move(pts), ShapeType::Rhombus, true, req.cornerRadius);
        }

        case ShapeType::IsosTriangle: {
            // 起点为底边中点，|dx| 为半底边，|dy| 为高
            const double halfBase = std::abs(dx);
            const double height = std::abs(dy);
            if (halfBase < kEps && height < kEps) {
                return degenerateLine(x1, y1, x1, y1);
            }
            const double signY = y2 >= y1 ? -1.0 : 1.0;
            std::vector<core::Point2D> pts = {
                {x1 - halfBase, y1},
                {x1 + halfBase, y1},
                {x1, y1 - signY * height},
            };
            return std::make_unique<geometry::PolygonShape>(
                std::move(pts), ShapeType::IsosTriangle, true, req.cornerRadius);
        }

        case ShapeType::EquTriangle: {
            // 等边三角形：参照等腰——落点为底边中点，底边水平，顶角朝光标竖向生长（不随拖动旋转）。
            // 等边只有一个自由度：取 |dx|（半底边）与 |dy|/√3（由高反推的半底边）较大者，使形状跟随光标主方向放大。
            const double halfBase = std::max(std::abs(dx), std::abs(dy) / std::sqrt(3.0));
            if (halfBase < kEps) {
                return degenerateLine(x1, y1, x1, y1);
            }
            const double height = std::sqrt(3.0) * halfBase;             // 等边高 = √3/2·边长 = √3·半底边
            const double signY = y2 >= y1 ? -1.0 : 1.0;                 // 与等腰一致：顶角朝光标方向生长
            std::vector<core::Point2D> pts = {
                {x1 - halfBase, y1},
                {x1 + halfBase, y1},
                {x1, y1 - signY * height},
            };
            return std::make_unique<geometry::PolygonShape>(
                std::move(pts), ShapeType::EquTriangle, true, req.cornerRadius);
        }

        case ShapeType::RectTriangle: {
            // 直角三角形：落点为直角顶点，两直角边分别沿轴取 dx、dy（不用斜边点）。
            if (std::abs(dx) < kEps && std::abs(dy) < kEps) {
                return degenerateLine(x1, y1, x1, y1);
            }
            std::vector<core::Point2D> pts = {
                {x1, y1}, {x2, y1}, {x1, y2},
            };
            return std::make_unique<geometry::PolygonShape>(
                std::move(pts), ShapeType::RectTriangle, true, req.cornerRadius);
        }

        case ShapeType::EquRectTriangle: {
            // 等腰直角三角形：落点为直角顶点，两直角边等长 = max(|dx|,|dy|)，沿轴朝光标象限（不随拖动旋转）。
            const double leg = std::max(std::abs(dx), std::abs(dy));
            if (leg < kEps) {
                return degenerateLine(x1, y1, x1, y1);
            }
            const double gx = dx >= 0 ? 1.0 : -1.0;
            const double gy = dy >= 0 ? 1.0 : -1.0;
            std::vector<core::Point2D> pts = {
                {x1, y1},
                {x1 + gx * leg, y1},
                {x1, y1 + gy * leg},
            };
            return std::make_unique<geometry::PolygonShape>(
                std::move(pts), ShapeType::EquRectTriangle, true, req.cornerRadius);
        }

        case ShapeType::Text: {
            return std::make_unique<geometry::TextShape>(req.text, x1, y1, req.fontSize);
        }
    }
    // 兜底：返回退化直线
    return degenerateLine(x1, y1, x2, y2);
}

} // namespace idc::annotation
