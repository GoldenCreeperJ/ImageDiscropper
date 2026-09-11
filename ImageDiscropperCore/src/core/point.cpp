// ============================================================================
// 文件：src/core/point.cpp
// 作用：实现 include/core/point.h 中声明的自由函数（如两点欧氏距离）。
// ============================================================================
#include "core/point.h"

#include <cmath>

namespace idc::core {

// 计算两点欧氏距离。
double distance(const Point2D& a, const Point2D& b) {
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    return std::sqrt(dx * dx + dy * dy);
}

} // namespace idc::core
