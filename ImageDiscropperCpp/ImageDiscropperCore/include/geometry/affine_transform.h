// ============================================================================
// 文件：include/geometry/affine_transform.h
// 作用：定义 2D 仿射变换 AffineTransform——为标注形状提供**非破坏性**的缩放 / 拉伸 /
//       旋转 / 翻转能力（header-only，无对应 .cpp，故不改动 CMake 源清单）。
// 分块依据：
//   - 采用列向量约定：x' = a*x + c*y + tx，y' = b*x + d*y + ty；
//   - 提供工厂（平移 / 缩放 / 旋转）、复合（operator*）、点 / 路径映射、行列式、逆、单位判定；
//   - 与 QTransform 的布局一一对应（见 GUI util/path_qt_adapter 的 toQTransform），便于画布矢量渲染。
// 说明：仿射变换对贝塞尔曲线具**不变性**——直接变换控制点即得精确结果，故 applyToPath 无需离散化，
//       旋转 / 缩放后的曲线仍保持矢量精度（这也是「无需退化、无需像素兜底」的几何依据）。
// ============================================================================
#pragma once

#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

#include "core/point.h"
#include "geometry/path.h"

namespace idc::geometry {

// ---------------------------------------------------------------------------
// AffineTransform：2D 仿射变换矩阵（末行恒为 [0 0 1]，省略存储）。
// ---------------------------------------------------------------------------
class AffineTransform {
public:
    // 矩阵分量：x' = a*x + c*y + tx；y' = b*x + d*y + ty。默认构造为单位变换。
    double a{1.0}, b{0.0}, c{0.0}, d{1.0}, tx{0.0}, ty{0.0};

    AffineTransform() = default;
    AffineTransform(const double a_, const double b_, const double c_, const double d_,
                    const double tx_, const double ty_)
        : a(a_), b(b_), c(c_), d(d_), tx(tx_), ty(ty_) {}

    // ---- 工厂 ----
    static AffineTransform identity() { return {}; }
    static AffineTransform translation(const double x, const double y) { return {1, 0, 0, 1, x, y}; }
    static AffineTransform scaling(const double sx, const double sy) { return {sx, 0, 0, sy, 0, 0}; }
    // 绕原点逆时针旋转（角度制）。屏幕 y 轴向下时视觉为顺时针，与常见图像编辑器一致。
    static AffineTransform rotation(const double degrees) {
        constexpr double kPi = 3.14159265358979323846;
        const double r = degrees * kPi / 180.0;
        const double cs = std::cos(r);
        const double sn = std::sin(r);
        return {cs, sn, -sn, cs, 0, 0};
    }

    // ---- 复合：返回「先应用 o，再应用 this」的复合变换（this ∘ o）。----
    AffineTransform operator*(const AffineTransform& o) const {
        return {
            a * o.a + c * o.b,
            b * o.a + d * o.b,
            a * o.c + c * o.d,
            b * o.c + d * o.d,
            a * o.tx + c * o.ty + tx,
            b * o.tx + d * o.ty + ty,
        };
    }

    // ---- 映射 ----
    core::Point2D applyToPoint(const core::Point2D& p) const {
        return {a * p.x + c * p.y + tx, b * p.x + d * p.y + ty};
    }

    // 对路径所有段坐标（含二次 / 三次贝塞尔控制点）应用变换，返回新路径（不改原路径）。
    Path applyToPath(const Path& path) const {
        Path out;
        for (const auto&[type, coords] : path.segments()) {
            // 结构化解绑的 coords 为 const 引用，需复制一份方可原地写入；副本改名避免遮蔽。
            std::vector<double> mapped = coords;
            for (std::size_t i = 0; i + 1 < mapped.size(); i += 2) {
                const double x = mapped[i];
                const double y = mapped[i + 1];
                mapped[i]     = a * x + c * y + tx;
                mapped[i + 1] = b * x + d * y + ty;
            }
            out.segments().push_back(PathSegment{type, std::move(mapped)});
        }
        return out;
    }

    // ---- 属性 ----
    double determinant() const { return a * d - b * c; }

    bool isIdentity() const {
        constexpr double kEps = 1e-12;
        return std::fabs(a - 1.0) < kEps && std::fabs(d - 1.0) < kEps &&
               std::fabs(b) < kEps && std::fabs(c) < kEps &&
               std::fabs(tx) < kEps && std::fabs(ty) < kEps;
    }

    // 逆变换（行列式近 0 时退化返回单位矩阵，避免除零；用于把世界坐标点映回局部做命中判定）。
    AffineTransform inverse() const {
        const double det = determinant();
        if (std::fabs(det) < 1e-12) return identity();
        return {
            d / det, -b / det, -c / det, a / det,
            (c * ty - d * tx) / det, (b * tx - a * ty) / det,
        };
    }
};

} // namespace idc::geometry
