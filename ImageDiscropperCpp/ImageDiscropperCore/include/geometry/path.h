// ============================================================================
// 文件：include/geometry/path.h
// 作用：定义路径 Path 与路径段 PathSegment。路径是所有形状的通用表示，
//       由 moveTo / lineTo / quadTo / cubicTo / closePath 五类指令组成。
//       同时提供扁平化（曲线离散化）与包围盒计算，便于光栅化与命中检测。
// ============================================================================
#pragma once

#include <vector>

#include "core/core_nums.h"
#include "core/point.h"

namespace idc::geometry {

// ---------------------------------------------------------------------------
// PathSegmentType：路径段类型
// ---------------------------------------------------------------------------
enum class PathSegmentType {
    MoveTo,    // 抬笔移动
    LineTo,    // 直线段
    QuadTo,    // 二次贝塞尔
    CubicTo,   // 三次贝塞尔
    Close,     // 闭合到当前子路径起点
};

// ---------------------------------------------------------------------------
// PathSegment：路径段
// 说明：坐标统一存放在 coords 中，数量取决于 type：
//   MoveTo  / LineTo ：2 个（x, y）
//   QuadTo           ：4 个（cx, cy, x, y）
//   CubicTo          ：6 个（c1x, c1y, c2x, c2y, x, y）
//   Close            ：0 个
// ---------------------------------------------------------------------------
struct PathSegment {
    PathSegmentType type{PathSegmentType::MoveTo};
    std::vector<double> coords;
};

// ---------------------------------------------------------------------------
// BoundingBox：轴对齐包围盒
// ---------------------------------------------------------------------------
struct BoundingBox {
    double x{0.0};
    double y{0.0};
    double width{0.0};
    double height{0.0};

    // 判断点是否位于包围盒内。
    bool contains(double px, double py) const;
    // 与另一包围盒合并（返回并集）。
    BoundingBox united(const BoundingBox& o) const;
    // 是否为空（宽高 <= 0）。
    bool isEmpty() const { return width <= 0.0 || height <= 0.0; }
    double right() const { return x + width; }
    double bottom() const { return y + height; }
};

// ---------------------------------------------------------------------------
// Path：路径
// ---------------------------------------------------------------------------
class Path {
public:
    Path() = default;

    // 抬笔移动到 (x, y)，开始新的子路径。
    void moveTo(double x, double y);
    // 从当前点画直线到 (x, y)。
    void lineTo(double x, double y);
    // 二次贝塞尔：控制点 (cx, cy)，终点 (x, y)。
    void quadTo(double cx, double cy, double x, double y);
    // 三次贝塞尔：控制点 (c1x, c1y) (c2x, c2y)，终点 (x, y)。
    void cubicTo(double c1x, double c1y, double c2x, double c2y, double x, double y);
    // 闭合当前子路径，绘制一条回到起点的隐式直线。
    void closePath();

    // 追加另一条路径的所有段。
    void append(const Path& other);

    // 是否为空（没有任何段）。
    bool empty() const { return segments_.empty(); }
    // 段数量。
    std::size_t size() const { return segments_.size(); }
    // 只读访问段列表。
    const std::vector<PathSegment>& segments() const { return segments_; }
    // 可写访问段列表，主要用于扁平化改写。
    std::vector<PathSegment>& segments() { return segments_; }

    // 计算包围盒（对曲线取控制点包围盒，粗略但足够）。
    BoundingBox bounds() const;

    // 将所有曲线段离散化为折线段，flatness 越小折线越精细。
    // 返回新的 Path，不修改原路径。默认容差取常量层 geometry::kDefaultFlatnessPx。
    Path flattened(double flatness = core::nums::geometry::kDefaultFlatnessPx) const;

    // 判断点是否位于路径内部（偶奇规则）。仅对闭合路径有意义。
    // 内部先扁平化，再做经典扫描线判定。
    bool contains(double px, double py, double flatness = core::nums::geometry::kHitFlatnessPx) const;

    // 填充语义用：若路径未闭合（末段非 Close 且末点 ≠ 首点），追加一条回到首点的直线段。
    // 对齐 Qt 对开放路径「填充时隐式闭合」的语义——fill 的 contains / 命中 / 光栅化一律用它；
    // 描边保持原路径（开放描边不画闭合边）。已闭合 / 空路径原样返回副本。
    Path closedForFill() const;

    // 计算点到路径最近线段的距离（扁平化后按线段距离取最小）。
    // 用于命中检测：返回实际距离，由调用方自行与容差比较。
    double distanceToOutline(double px, double py, double flatness = core::nums::geometry::kHitFlatnessPx) const;

    // 平移整条路径。
    void translate(double dx, double dy);

private:
    std::vector<PathSegment> segments_;
};

} // namespace idc::geometry
