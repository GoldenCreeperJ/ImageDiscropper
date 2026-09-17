// ============================================================================
// 文件：include/core/point.h
// 作用：定义二维坐标类型——浮点坐标 Point2D（含加减、数乘、数除等算术运算符，供
//       geometry / annotation 的连续几何计算使用）与整数坐标 Point2I（供像素索引等
//       离散场景使用）。二者均为 core 地基类型，Point2I 预留给上层 CLI/GUI 直接引用。
// ============================================================================
#pragma once

namespace idc::core {

// ---------------------------------------------------------------------------
// Point2D：双精度二维坐标
// ---------------------------------------------------------------------------
struct Point2D {
    double x{0.0};
    double y{0.0};

    Point2D() = default;
    Point2D(const double x_, const double y_) : x(x_), y(y_) {}

    // 加法：两点坐标分量相加。
    Point2D operator+(const Point2D& o) const { return {x + o.x, y + o.y}; }
    // 减法：两点坐标分量相减。
    Point2D operator-(const Point2D& o) const { return {x - o.x, y - o.y}; }
    // 数乘：所有分量乘以标量。
    Point2D operator*(const double s) const { return {x * s, y * s}; }
    // 数除：所有分量除以标量。调用方保证 s != 0。
    Point2D operator/(const double s) const { return {x / s, y / s}; }

    Point2D& operator+=(const Point2D& o) { x += o.x; y += o.y; return *this; }
    Point2D& operator-=(const Point2D& o) { x -= o.x; y -= o.y; return *this; }
    Point2D& operator*=(const double s) { x *= s; y *= s; return *this; }
    Point2D& operator/=(const double s) { x /= s; y /= s; return *this; }

    bool operator==(const Point2D& o) const { return x == o.x && y == o.y; }
    bool operator!=(const Point2D& o) const { return !(*this == o); }
};

// 标量在左侧的乘法：允许写作 2.0 * p。
inline Point2D operator*(const double s, const Point2D& p) { return p * s; }

// 二维欧氏距离，用于命中检测、半径计算等场景。
double distance(const Point2D& a, const Point2D& b);

// 整数点，主要用于像素坐标索引。
struct Point2I {
    int x{0};
    int y{0};
};

} // namespace idc::core
