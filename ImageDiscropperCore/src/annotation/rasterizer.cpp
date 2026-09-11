// ============================================================================
// 文件：src/annotation/rasterizer.cpp
// 作用：实现 include/annotation/rasterizer.h 中的光栅化算法。
//       核心步骤：
//         1. 将任意 Shape 展平为折线（Path::flattened）
//         2. 填充：扫描线算法（偶奇规则）
//         3. 描边：沿折线逐段绘制粗线（圆盘笔刷）
//         4. Alpha 混合：按 src-over 合成到目标像素
// ============================================================================
#include "annotation/rasterizer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace idc::annotation {

// ---------------------------------------------------------------------------
// 内部工具：Alpha 混合（src-over）
//   out = src * srcA + dst * (1 - srcA)
// ---------------------------------------------------------------------------
static core::Color blendSrcOver(const core::Color& dst, const core::Color& src) {
    if (src.a == 255) return src;
    if (src.a == 0) return dst;
    const double sa = src.a / 255.0;
    const double da = dst.a / 255.0;
    const double outA = sa + da * (1.0 - sa);
    if (outA <= 0.0) return core::kTransparent;
    const auto mix = [&](const std::uint8_t s, const std::uint8_t d) -> std::uint8_t {
        const double v = (s * sa + d * da * (1.0 - sa)) / outA;
        return static_cast<std::uint8_t>(std::clamp(v, 0.0, 255.0));
    };
    return core::Color(mix(src.r, dst.r), mix(src.g, dst.g), mix(src.b, dst.b),
                       static_cast<std::uint8_t>(std::clamp(outA * 255.0, 0.0, 255.0)));
}

// 在图像上安全地写入一个像素（自动做 alpha 混合与越界裁剪）。
static void blendPixel(core::Image& img, const int x, const int y, const core::Color& c, const double coverage = 1.0) {
    if (!img.inBounds(x, y)) return;
    if (coverage <= 0.0) return;
    core::Color src = c;
    if (coverage < 1.0) {
        src.a = static_cast<std::uint8_t>(std::clamp(c.a * coverage, 0.0, 255.0));
    }
    const core::Color dst = img.getPixel(x, y);
    img.setPixel(x, y, blendSrcOver(dst, src));
}

// ---------------------------------------------------------------------------
// 内部工具：从扁平化路径中提取折线列表（每个子路径独立）
// ---------------------------------------------------------------------------
static std::vector<std::vector<core::Point2D>> extractPolylines(const geometry::Path& flat) {
    std::vector<std::vector<core::Point2D>> polylines;
    std::vector<core::Point2D> current;
    core::Point2D first{0, 0};
    for (const auto&[type, coords] : flat.segments()) {
        if (type == geometry::PathSegmentType::MOVE_TO) {
            if (!current.empty()) polylines.push_back(current);
            current.clear();
            first = {coords[0], coords[1]};
            current.push_back(first);
        } else if (type == geometry::PathSegmentType::LINE_TO) {
            current.emplace_back(coords[0], coords[1]);
        } else if (type == geometry::PathSegmentType::CLOSE) {
            if (!current.empty() &&
                (current.back().x != first.x || current.back().y != first.y)) {
                current.push_back(first);
            }
        }
    }
    if (!current.empty()) polylines.push_back(current);
    return polylines;
}

// ---------------------------------------------------------------------------
// 内部工具：扫描线多边形填充（偶奇规则）
// 对每条扫描线 y，收集所有与多边形边的交点，按 x 排序后成对填充。
// ---------------------------------------------------------------------------
static void fillPolygon(core::Image& img,
                        const std::vector<std::vector<core::Point2D>>& polys,
                        const core::Color& color) {
    if (polys.empty()) return;

    // 计算全局 y 范围
    double minY = std::numeric_limits<double>::infinity();
    double maxY = -std::numeric_limits<double>::infinity();
    for (const auto& poly : polys) {
        for (const auto& p : poly) {
            minY = std::min(minY, p.y);
            maxY = std::max(maxY, p.y);
        }
    }
    const int yStart = std::max(0, static_cast<int>(std::floor(minY)));
    const int yEnd = std::min(img.height() - 1, static_cast<int>(std::ceil(maxY)));

    std::vector<double> xs;
    for (int y = yStart; y <= yEnd; ++y) {
        xs.clear();
        const double py = y + 0.5; // 像素中心
        for (const auto& poly : polys) {
            for (std::size_t i = 0; i + 1 < poly.size(); ++i) {
                const double y1 = poly[i].y, y2 = poly[i + 1].y;
                if ((y1 > py) == (y2 > py)) continue; // 不与扫描线相交
                const double x1 = poly[i].x, x2 = poly[i + 1].x;
                const double t = (py - y1) / (y2 - y1);
                xs.push_back(x1 + t * (x2 - x1));
            }
        }
        std::sort(xs.begin(), xs.end());
        // 成对填充
        for (std::size_t i = 0; i + 1 < xs.size(); i += 2) {
            const int xa = std::max(0, static_cast<int>(std::ceil(xs[i] - 0.5)));
            const int xb = std::min(img.width() - 1, static_cast<int>(std::floor(xs[i + 1] - 0.5)));
            for (int x = xa; x <= xb; ++x) {
                blendPixel(img, x, y, color);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 内部工具：绘制粗线段（圆盘笔刷）
// 沿线段方向以 step 步长前进，在每个采样点画一个半径 = strokeWidth / 2 的圆盘。
// ---------------------------------------------------------------------------
static void drawThickLine(core::Image& img,
                          const core::Point2D& a, const core::Point2D& b,
                          const core::Color& color, const int strokeWidth, const bool antialias) {
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double len = std::hypot(dx, dy);
    const double radius = std::max(0.5, strokeWidth / 2.0);
    const int steps = std::max(1, static_cast<int>(std::ceil(len / std::max(0.5, radius * 0.5))));

    for (int i = 0; i <= steps; ++i) {
        const double t = static_cast<double>(i) / steps;
        const double cx = a.x + dx * t;
        const double cy = a.y + dy * t;
        const int minX = static_cast<int>(std::floor(cx - radius));
        const int maxX = static_cast<int>(std::ceil(cx + radius));
        const int minY = static_cast<int>(std::floor(cy - radius));
        const int maxY = static_cast<int>(std::ceil(cy + radius));
        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                // 计算像素中心到圆盘中心的距离
                const double px = x + 0.5 - cx;
                const double py = y + 0.5 - cy;
                if (const double dist = std::hypot(px, py); dist <= radius - 0.5) {
                    blendPixel(img, x, y, color, 1.0);
                } else if (antialias && dist <= radius + 0.5) {
                    // 边缘像素按覆盖率做抗锯齿
                    const double coverage = std::clamp(radius + 0.5 - dist, 0.0, 1.0);
                    blendPixel(img, x, y, color, coverage);
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 内部工具：沿折线绘制描边
// ---------------------------------------------------------------------------
static void strokePolylines(core::Image& img,
                            const std::vector<std::vector<core::Point2D>>& polys,
                            const core::Color& color, const int strokeWidth, const bool antialias) {
    for (const auto& poly : polys) {
        for (std::size_t i = 0; i + 1 < poly.size(); ++i) {
            drawThickLine(img, poly[i], poly[i + 1], color, strokeWidth, antialias);
        }
    }
}

// ---------------------------------------------------------------------------
// 公共接口：将 Shape 光栅化到 Image
// ---------------------------------------------------------------------------
void rasterize(core::Image& image, const geometry::Shape& shape, const PaintStyle& style) {
    if (image.empty()) return;

    // 1. 展平路径
    const geometry::Path path = shape.toPath();
    const geometry::Path flat = path.flattened(0.5);
    const auto polys = extractPolylines(flat);
    if (polys.empty()) return;

    const bool isLine = shape.type() == geometry::ShapeType::LINE;
    const int strokeW = std::max(1, style.strokeWidth);

    // 2. 填充（LINE 不填充）
    if (style.fill && !isLine) {
        fillPolygon(image, polys, style.color);
        // 填充模式下不再描边，避免边缘过深；如需同时描边可在此追加
        return;
    }

    // 3. 描边
    strokePolylines(image, polys, style.color, strokeW, style.antialias);
}

} // namespace idc::annotation
