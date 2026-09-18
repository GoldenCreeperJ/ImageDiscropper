// ============================================================================
// 文件：src/geometry/path.cpp
// 作用：实现 include/geometry/path.h 声明的 Path / PathSegment / BoundingBox。
//       核心算法包括：贝塞尔曲线离散化、扫描线内部判定、点到折线的最短距离。
// ============================================================================
#include "geometry/path.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace idc::geometry {

// 判断点是否位于包围盒内部（含边界）。
bool BoundingBox::contains(const double px, const double py) const {
    return px >= x && px <= x + width && py >= y && py <= y + height;
}

// 合并两个包围盒，返回包含它们的最小矩形。
BoundingBox BoundingBox::united(const BoundingBox& o) const {
    if (isEmpty()) return o;
    if (o.isEmpty()) return *this;
    const double minX = std::min(x, o.x);
    const double minY = std::min(y, o.y);
    const double maxX = std::max(x + width, o.x + o.width);
    const double maxY = std::max(y + height, o.y + o.height);
    return {minX, minY, maxX - minX, maxY - minY};
}

// ---------------------------------------------------------------------------
// Path 构建接口
// ---------------------------------------------------------------------------

// 抬笔移动：追加一段 MOVE_TO。
void Path::moveTo(double x, double y) {
    segments_.push_back({PathSegmentType::MOVE_TO, {x, y}});
}

// 直线段：追加一段 LINE_TO。
void Path::lineTo(double x, double y) {
    segments_.push_back({PathSegmentType::LINE_TO, {x, y}});
}

// 二次贝塞尔：追加一段 QUAD_TO。
void Path::quadTo(double cx, double cy, double x, double y) {
    segments_.push_back({PathSegmentType::QUAD_TO, {cx, cy, x, y}});
}

// 三次贝塞尔：追加一段 CUBIC_TO。
void Path::cubicTo(double c1x, double c1y, double c2x, double c2y, double x, double y) {
    segments_.push_back({PathSegmentType::CUBIC_TO, {c1x, c1y, c2x, c2y, x, y}});
}

// 闭合当前子路径。
void Path::closePath() {
    segments_.push_back({PathSegmentType::CLOSE, {}});
}

// 追加另一路径的所有段。
void Path::append(const Path& other) {
    segments_.insert(segments_.end(), other.segments_.begin(), other.segments_.end());
}

// 平移路径所有坐标。
void Path::translate(const double dx, const double dy) {
    for (auto&[type, coords] : segments_) {
        for (std::size_t i = 0; i + 1 < coords.size(); i += 2) {
            coords[i] += dx;
            coords[i + 1] += dy;
        }
    }
}

// ---------------------------------------------------------------------------
// 包围盒计算：对所有控制点求最小 / 最大坐标，粗略但足以覆盖曲线范围。
// ---------------------------------------------------------------------------
BoundingBox Path::bounds() const {
    if (segments_.empty()) return {0, 0, 0, 0};
    double minX = std::numeric_limits<double>::infinity();
    double minY = std::numeric_limits<double>::infinity();
    double maxX = -std::numeric_limits<double>::infinity();
    double maxY = -std::numeric_limits<double>::infinity();

    for (const auto&[type, coords] : segments_) {
        for (std::size_t i = 0; i + 1 < coords.size(); i += 2) {
            minX = std::min(minX, coords[i]);
            maxX = std::max(maxX, coords[i]);
            minY = std::min(minY, coords[i + 1]);
            maxY = std::max(maxY, coords[i + 1]);
        }
    }
    if (minX > maxX) return {0, 0, 0, 0};
    return {minX, minY, maxX - minX, maxY - minY};
}

// ---------------------------------------------------------------------------
// 内部工具：把 QUAD_TO 离散化为若干 LINE_TO
// 说明：使用固定步长（依据 flatness 计算），保证曲率越大分段越多。
// ---------------------------------------------------------------------------
static void flattenQuad(Path& out, const double cx, const double cy, const double x0, const double y0,
                        const double x1, const double y1, const double flatness) {
    // 用控制点到中点的距离估算需要的分段数
    const double mx = (x0 + x1) / 2.0;
    const double my = (y0 + y1) / 2.0;
    const double dev = std::hypot(cx - mx, cy - my);
    int steps = static_cast<int>(std::ceil(std::sqrt(dev / std::max(flatness, 1e-6))));
    steps = std::clamp(steps, 2, 64);
    for (int i = 1; i <= steps; ++i) {
        const double t = static_cast<double>(i) / steps;
        const double u = 1.0 - t;
        const double px = u * u * x0 + 2 * u * t * cx + t * t * x1;
        const double py = u * u * y0 + 2 * u * t * cy + t * t * y1;
        out.lineTo(px, py);
    }
}

// 把 CUBIC_TO 离散化为若干 LINE_TO。
static void flattenCubic(Path& out, const double c1x, const double c1y, const double c2x, const double c2y,
                         const double x0, const double y0, const double x1, const double y1, const double flatness) {
    const double dev = std::max(
        std::hypot(c1x - (2 * x0 + x1) / 3.0, c1y - (2 * y0 + y1) / 3.0),
        std::hypot(c2x - (x0 + 2 * x1) / 3.0, c2y - (y0 + 2 * y1) / 3.0));
    int steps = static_cast<int>(std::ceil(std::sqrt(dev / std::max(flatness, 1e-6))));
    steps = std::clamp(steps, 2, 96);
    for (int i = 1; i <= steps; ++i) {
        const double t = static_cast<double>(i) / steps;
        const double u = 1.0 - t;
        const double px = u * u * u * x0 + 3 * u * u * t * c1x + 3 * u * t * t * c2x + t * t * t * x1;
        const double py = u * u * u * y0 + 3 * u * u * t * c1y + 3 * u * t * t * c2y + t * t * t * y1;
        out.lineTo(px, py);
    }
}

// 扁平化：把所有 QUAD_TO / CUBIC_TO 展开为 LINE_TO。
Path Path::flattened(const double flatness) const {
    Path out;
    double curX = 0.0, curY = 0.0;
    for (const auto&[type, coords] : segments_) {
        switch (type) {
            case PathSegmentType::MOVE_TO:
                out.moveTo(coords[0], coords[1]);
                curX = coords[0]; curY = coords[1];
                break;
            case PathSegmentType::LINE_TO:
                out.lineTo(coords[0], coords[1]);
                curX = coords[0]; curY = coords[1];
                break;
            case PathSegmentType::QUAD_TO:
                flattenQuad(out, coords[0], coords[1], curX, curY,
                            coords[2], coords[3], flatness);
                curX = coords[2]; curY = coords[3];
                break;
            case PathSegmentType::CUBIC_TO:
                flattenCubic(out, coords[0], coords[1], coords[2], coords[3],
                             curX, curY, coords[4], coords[5], flatness);
                curX = coords[4]; curY = coords[5];
                break;
            case PathSegmentType::CLOSE:
                out.closePath();
                break;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// 内部工具：从扁平化后的 Path 中提取所有折线（每个子路径独立）
// ---------------------------------------------------------------------------
static std::vector<std::vector<core::Point2D>> extractPolylines(const Path& flat) {
    std::vector<std::vector<core::Point2D>> polylines;
    std::vector<core::Point2D> current;
    core::Point2D first{0, 0};
    for (const auto&[type, coords] : flat.segments()) {
        if (type == PathSegmentType::MOVE_TO) {
            if (!current.empty()) polylines.push_back(current);
            current.clear();
            first = {coords[0], coords[1]};
            current.push_back(first);
        } else if (type == PathSegmentType::LINE_TO) {
            current.emplace_back(coords[0], coords[1]);
        } else if (type == PathSegmentType::CLOSE) {
            if (!current.empty() && (current.back().x != first.x || current.back().y != first.y)) {
                current.push_back(first);
            }
        }
    }
    if (!current.empty()) polylines.push_back(current);
    return polylines;
}

// 内部判定：使用偶奇规则的扫描线算法。
bool Path::contains(const double px, const double py, const double flatness) const {
    const Path flat = flattened(flatness);
    const auto polys = extractPolylines(flat);

    int crossings = 0;
    for (const auto& poly : polys) {
        for (std::size_t i = 0; i + 1 < poly.size(); ++i) {
            const double x1 = poly[i].x, y1 = poly[i].y;
            const double x2 = poly[i + 1].x;
            if (const double y2 = poly[i + 1].y; y1 > py != y2 > py) {
                if (const double xIntersect = x1 + (py - y1) * (x2 - x1) / (y2 - y1); px < xIntersect) ++crossings;
            }
        }
    }
    return crossings % 2 == 1;
}

// 点到线段的最短距离（内部工具）。
static double pointToSegmentDistance(const double px, const double py,
                                     const double x1, const double y1, const double x2, const double y2) {
    const double dx = x2 - x1;
    const double dy = y2 - y1;
    const double lenSq = dx * dx + dy * dy;
    if (lenSq <= 1e-12) return std::hypot(px - x1, py - y1);
    double t = ((px - x1) * dx + (py - y1) * dy) / lenSq;
    t = std::clamp(t, 0.0, 1.0);
    const double projX = x1 + t * dx;
    const double projY = y1 + t * dy;
    return std::hypot(px - projX, py - projY);
}

// 命中检测：返回点到路径所有线段的最小距离。
double Path::distanceToOutline(const double px, const double py, const double flatness) const {
    const Path flat = flattened(flatness);
    const auto polys = extractPolylines(flat);
    double best = std::numeric_limits<double>::infinity();
    for (const auto& poly : polys) {
        for (std::size_t i = 0; i + 1 < poly.size(); ++i) {
            best = std::min(best, pointToSegmentDistance(
                px, py, poly[i].x, poly[i].y, poly[i + 1].x, poly[i + 1].y));
        }
    }
    return best;
}

} // namespace idc::geometry
