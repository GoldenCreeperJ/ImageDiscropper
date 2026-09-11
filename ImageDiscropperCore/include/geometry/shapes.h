// ============================================================================
// 文件：include/geometry/shapes.h
// 作用：定义具体形状类型（LineShape / RectShape / EllipseShape / ArcShape /
//       RoundRectShape / PolygonShape / PathShape / TextShape），提供统一的
//       Shape 抽象接口。
// ============================================================================
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/point.h"
#include "geometry/shape_type.h"
#include "geometry/path.h"

namespace idc::geometry {

// ---------------------------------------------------------------------------
// Shape：所有几何形状的抽象基类
// ---------------------------------------------------------------------------
class Shape {
public:
    virtual ~Shape() = default;

    // 返回该形状的类型枚举，便于分派。
    virtual ShapeType type() const = 0;

    // 转换为 Path 表示（曲线保留原精度）。
    virtual Path toPath() const = 0;

    // 计算包围盒。
    virtual BoundingBox bounds() const = 0;

    // 判断点是否位于形状内部（对开放形状如 LINE 返回 false）。
    virtual bool contains(const core::Point2D& p) const = 0;

    // 深拷贝。
    virtual std::unique_ptr<Shape> clone() const = 0;

    // 返回控制点列表，用于编辑模式下的可拖拽锚点。
    // 对应 AnnotationLayer.kt: getControlPoint(pshape)
    virtual std::vector<core::Point2D> controlPoints() const = 0;
};

// ---------------------------------------------------------------------------
// LineShape：直线段
// ---------------------------------------------------------------------------
class LineShape : public Shape {
public:
    LineShape(double x1, double y1, double x2, double y2);

    ShapeType type() const override { return ShapeType::LINE; }
    Path toPath() const override;
    BoundingBox bounds() const override;
    bool contains(const core::Point2D& p) const override; // 直线不含内部，恒 false
    std::unique_ptr<Shape> clone() const override;
    std::vector<core::Point2D> controlPoints() const override;

    // 端点访问。
    core::Point2D p1() const { return {x1_, y1_}; }
    core::Point2D p2() const { return {x2_, y2_}; }

    // 计算点到线段的最短距离，用于命中检测。
    double distanceToSegment(const core::Point2D& p) const;

private:
    double x1_, y1_, x2_, y2_;
};

// ---------------------------------------------------------------------------
// RectShape：矩形
// ---------------------------------------------------------------------------
class RectShape : public Shape {
public:
    RectShape(double x, double y, double w, double h);

    ShapeType type() const override { return ShapeType::RECTANGLE; }
    Path toPath() const override;
    BoundingBox bounds() const override;
    bool contains(const core::Point2D& p) const override;
    std::unique_ptr<Shape> clone() const override;
    std::vector<core::Point2D> controlPoints() const override;

    double x() const { return x_; }
    double y() const { return y_; }
    double width() const { return w_; }
    double height() const { return h_; }

private:
    double x_, y_, w_, h_;
};

// ---------------------------------------------------------------------------
// RoundRectShape：圆角矩形
// ---------------------------------------------------------------------------
class RoundRectShape : public Shape {
public:
    RoundRectShape(double x, double y, double w, double h, double arcW, double arcH,
                   ShapeType type = ShapeType::ROUNDRECTANGLE);

    ShapeType type() const override { return type_; }
    Path toPath() const override;
    BoundingBox bounds() const override;
    bool contains(const core::Point2D& p) const override;
    std::unique_ptr<Shape> clone() const override;
    std::vector<core::Point2D> controlPoints() const override;

private:
    double x_, y_, w_, h_, arcW_, arcH_;
    ShapeType type_;
};

// ---------------------------------------------------------------------------
// EllipseShape：椭圆 / 圆
// ---------------------------------------------------------------------------
class EllipseShape : public Shape {
public:
    EllipseShape(double x, double y, double w, double h,
                 ShapeType type = ShapeType::ELLIPSE);

    ShapeType type() const override { return type_; }
    Path toPath() const override; // 使用四段三次贝塞尔近似
    BoundingBox bounds() const override;
    bool contains(const core::Point2D& p) const override;
    std::unique_ptr<Shape> clone() const override;
    std::vector<core::Point2D> controlPoints() const override;

    double centerX() const { return x_ + w_ / 2.0; }
    double centerY() const { return y_ + h_ / 2.0; }
    double radiusX() const { return w_ / 2.0; }
    double radiusY() const { return h_ / 2.0; }

private:
    double x_, y_, w_, h_;
    ShapeType type_;
};

// ---------------------------------------------------------------------------
// ArcType：扇形闭合方式
// ---------------------------------------------------------------------------
enum class ArcType {
    OPEN,   // 只绘制弧线，不闭合
    CHORD,  // 用弦闭合
    PIE,    // 用两条半径闭合（扇形）
};

// ---------------------------------------------------------------------------
// ArcShape：圆弧 / 扇形 / 弓形
// 角度约定：angleStart 与 angleExtent 单位为度，逆时针为正，
// ---------------------------------------------------------------------------
class ArcShape : public Shape {
public:
    ArcShape(double x, double y, double w, double h,
             double angleStart, double angleExtent, ArcType arcType);

    ShapeType type() const override; // 根据 arcType_ 返回 ARC / CHORD / PIE
    Path toPath() const override;
    BoundingBox bounds() const override;
    bool contains(const core::Point2D& p) const override;
    std::unique_ptr<Shape> clone() const override;
    std::vector<core::Point2D> controlPoints() const override;

    double angleStart() const { return angleStart_; }
    double angleExtent() const { return angleExtent_; }
    ArcType arcType() const { return arcType_; }

private:
    double x_, y_, w_, h_, angleStart_, angleExtent_;
    ArcType arcType_;
};

// ---------------------------------------------------------------------------
// PolygonShape：任意多边形 / 折线
// 用途：RHOMBUS / SQUARE / 各类三角形 / POLYLINE 等由顶点列表描述的形状。
// ---------------------------------------------------------------------------
class PolygonShape : public Shape {
public:
    PolygonShape(std::vector<core::Point2D> points, ShapeType type, bool closed);

    ShapeType type() const override { return type_; }
    Path toPath() const override;
    BoundingBox bounds() const override;
    bool contains(const core::Point2D& p) const override;
    std::unique_ptr<Shape> clone() const override;
    std::vector<core::Point2D> controlPoints() const override;

    // 顶点访问，主要用于序列化与调试。
    const std::vector<core::Point2D>& points() const { return points_; }
    bool closed() const { return closed_; }

private:
    std::vector<core::Point2D> points_;
    ShapeType type_;
    bool closed_;
};

// ---------------------------------------------------------------------------
// PathShape：自由路径
//           直接持有一条 Path，行为等价于 java.awt.geom.Path2D。
// ---------------------------------------------------------------------------
class PathShape : public Shape {
public:
    PathShape(Path path, ShapeType type);

    ShapeType type() const override { return type_; }
    Path toPath() const override { return path_; }
    BoundingBox bounds() const override { return path_.bounds(); }
    bool contains(const core::Point2D& p) const override;
    std::unique_ptr<Shape> clone() const override;
    std::vector<core::Point2D> controlPoints() const override;

    // 直接访问内部路径。
    const Path& path() const { return path_; }
    Path& path() { return path_; }

private:
    Path path_;
    ShapeType type_;
};

// ---------------------------------------------------------------------------
// TextShape：文字形状（占位实现）
// 说明：由于不引入外部字体库，这里将文字近似为一个矩形边界；
//       真实渲染由上层 Rasterizer 决定，可以绘制文本框或调用系统字体接口。
// ---------------------------------------------------------------------------
class TextShape : public Shape {
public:
    TextShape(std::string text, double x, double y, double fontSize);

    ShapeType type() const override { return ShapeType::TEXT; }
    Path toPath() const override; // 返回矩形边界路径
    BoundingBox bounds() const override;
    bool contains(const core::Point2D& p) const override;
    std::unique_ptr<Shape> clone() const override;
    std::vector<core::Point2D> controlPoints() const override;

    const std::string& text() const { return text_; }
    double fontSize() const { return fontSize_; }

private:
    std::string text_;
    double x_, y_, fontSize_;

    // 估算文本宽度：无字体库时按字符数 * fontSize * 0.5 粗略计算。
    double estimateWidth() const;
};

} // namespace idc::geometry
