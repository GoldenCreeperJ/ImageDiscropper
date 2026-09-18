// ============================================================================
// 文件：include/geometry/shapes.h
// 作用：定义具体形状类型（LineShape / RectShape / EllipseShape /
//       RoundRectShape / PolygonShape / PathShape / TextShape），提供统一的
//       Shape 抽象接口。
// ============================================================================
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/point.h"
#include "geometry/affine_transform.h"
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
    virtual std::vector<core::Point2D> controlPoints() const = 0;

    // 就地平移形状：各子类偏移自身存储的参数（而非「展平为 Path 再重建」），
    // 从而**保留具体类型与参数化身份**——避免标注移动后类型退化为通用 PATH
    // （退化会导致 TEXT 丢失字形渲染、参数化元数据与控制点语义丢失）。
    virtual void translate(double dx, double dy) = 0;

    // ---- 非破坏性变换（缩放 / 拉伸 / 旋转 / 翻转）----
    // 变换以矩阵 xform_ 累积存储，**不改动子类的参数化几何**（故类型 / 文字字形永不退化）；
    // 渲染、命中、导出统一经 worldPath() / worldBounds() / controlPointsWorld() 取「已变换」结果，
    // 默认单位矩阵时三者与 toPath() / bounds() / controlPoints() 完全一致（向后兼容）。
    // 说明：仿射对贝塞尔具不变性，旋转 / 缩放后仍是精确矢量，无需像素兜底。
    // xform_ 的写入统一经 applyObbTransform（缩放/旋转/翻转，同步累积 OBB 参数）；平移走 translateWorld
    // （就地偏移子类几何、不改 xform_）。故不提供裸 setTransform/applyTransform——绕过 applyObbTransform
    // 直接改 xform_ 会使累积 OBB 参数与矩阵失配，导致属性面板回显错误。
    const AffineTransform& transform() const { return xform_; }

    // 世界系平移：把世界位移 (dx,dy) 换算到形状局部系后再调子类 translate（就地偏移参数、保留类型）。
    // 存在旋转/缩放时，直接调 translate(dx,dy) 会沿**局部轴**移动（方向与光标不一致）；本方法用逆变换的
    // 线性部分把世界向量映回局部，使拖动方向始终跟随光标。单位矩阵时等价于 translate(dx,dy)。
    void translateWorld(const double dx, const double dy) {
        if (xform_.isIdentity()) { translate(dx, dy); return; }
        const AffineTransform inv = xform_.inverse();
        translate(inv.a * dx + inv.c * dy, inv.b * dx + inv.d * dy);
    }

    // 已应用变换的世界路径 / 包围盒 / 控制点（单位矩阵时回落到未变换版本，零额外开销）。
    Path worldPath() const { return xform_.isIdentity() ? toPath() : xform_.applyToPath(toPath()); }
    BoundingBox worldBounds() const { return worldPath().bounds(); }
    std::vector<core::Point2D> controlPointsWorld() const {
        std::vector<core::Point2D> pts = controlPoints();
        if (!xform_.isIdentity()) {
            for (core::Point2D& p : pts) p = xform_.applyToPoint(p);
        }
        return pts;
    }

    // ---- 坐标映射（供交互式定向包围盒手柄）----
    // localToWorld / worldToLocal 在形状局部系与世界系之间映射点（worldToLocal 用 xform_ 的逆）。
    // GUI 手柄拖拽据此把世界光标映回局部系以沿形状自身轴算缩放/旋转，不在 GUI 做矩阵运算（A-0.1）。
    core::Point2D localToWorld(const core::Point2D& p) const { return xform_.applyToPoint(p); }
    core::Point2D worldToLocal(const core::Point2D& p) const { return xform_.inverse().applyToPoint(p); }

    // ---- 定向包围盒（OBB）交互变换：缩放 / 拉伸 / 旋转 / 翻转 ----
    // 复合语义分处左右两侧：先沿形状**自身轴**做局部缩放（右乘，绕 bounds() 局部盒中心），再对当前世界
    // 外观做**绕世界中心的刚性旋转**（左乘）。这样缩放不产生世界轴剪切、旋转也不与非均匀缩放复合成剪切——
    // 即便形状此前已被非均匀拉伸，再旋转仍是刚性旋转（不变形）。缩放系数为负即翻转（拖手柄越过对边）。
    // 关键：旋转必须走世界系左乘。若把旋转也塞进局部右乘，则「非均匀缩放 ∘ 旋转」= 剪切，会让已拉伸的
    // 形状在旋转时彻底变形。obbPreviewTransform 只算不改状态供拖拽预览；applyObbTransform 写入与之完全
    // 一致的结果，保证「所见即所得」（预览帧与释放后 Core 提交帧逐像素一致）。
    AffineTransform obbPreviewTransform(const double sx, const double sy, const double degrees) const {
        const auto [x, y, width, height] = bounds();   // 局部（未变换）轴对齐包围盒
        const core::Point2D cl{x + width / 2.0, y + height / 2.0};
        // 1) 局部缩放：绕局部盒中心沿形状自身轴缩放，右乘叠加到现有变换（不产生世界轴剪切）。
        AffineTransform out = xform_ * (AffineTransform::translation(cl.x, cl.y) *
                                        AffineTransform::scaling(sx, sy) *
                                        AffineTransform::translation(-cl.x, -cl.y));
        // 2) 世界旋转：绕当前世界盒中心把整体外观刚性旋转，左乘（与既有非均匀缩放复合也不剪切）。
        //    局部缩放绕 cl 进行、不移动 cl，故世界中心 cw = xform_(cl) 在缩放前后一致，旋转中心稳定。
        if (degrees != 0.0) {
            const core::Point2D cw = xform_.applyToPoint(cl);
            out = AffineTransform::translation(cw.x, cw.y) * AffineTransform::rotation(degrees) *
                  AffineTransform::translation(-cw.x, -cw.y) * out;
        }
        return out;
    }
    void applyObbTransform(const double sx, const double sy, const double degrees) {
        xform_ = obbPreviewTransform(sx, sy, degrees);
        // 累积记录 OBB 参数（供 GUI 面板忠实回显「底层真实变换」）：缩放连乘、旋转累加。
        // 与 xform_ 的线性部分 R(Σdeg)·diag(Πsx, Πsy) 一致（缩放绕局部中心、旋转绕世界中心，中心恒定不变）。
        obbScaleX_ *= sx;
        obbScaleY_ *= sy;
        obbRotationDeg_ += degrees;
    }
    // 累积的 OBB 变换参数（带符号；缩放为负即翻转）。供 GUI 属性面板显示绝对缩放/旋转，
    // 与 xform_ 同步维护（applyObbTransform 累积、clone 经 copyXformTo 复制）；无变换时为恒等 (1,1,0)。
    double obbScaleX() const { return obbScaleX_; }
    double obbScaleY() const { return obbScaleY_; }
    double obbRotationDeg() const { return obbRotationDeg_; }

protected:
    // 复制非破坏性变换状态（矩阵 + 累积 OBB 参数）到另一形状，供各 clone() 调用，
    // 避免深拷贝（含 GUI 持有的 Annotation 副本、撤销重做快照）丢失变换或面板回显基准。
    void copyXformTo(Shape& o) const {
        o.xform_ = xform_;
        o.obbScaleX_ = obbScaleX_;
        o.obbScaleY_ = obbScaleY_;
        o.obbRotationDeg_ = obbRotationDeg_;
    }

    // 累积的非破坏性变换（默认单位＝几何不变）。
    AffineTransform xform_{};
    // 累积的 OBB 交互参数（带符号），与 xform_ 线性部分对应；仅由 applyObbTransform 累积、copyXformTo 复制。
    double obbScaleX_{1.0};
    double obbScaleY_{1.0};
    double obbRotationDeg_{0.0};
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
    void translate(double dx, double dy) override;

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
    void translate(double dx, double dy) override;

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
    void translate(double dx, double dy) override;

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
    void translate(double dx, double dy) override;

    double centerX() const { return x_ + w_ / 2.0; }
    double centerY() const { return y_ + h_ / 2.0; }
    double radiusX() const { return w_ / 2.0; }
    double radiusY() const { return h_ / 2.0; }

private:
    double x_, y_, w_, h_;
    ShapeType type_;
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
    void translate(double dx, double dy) override;

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
//           直接持有一条 Path（对应 FR-1.3 的画笔 / 自由路径标注）。
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
    void translate(double dx, double dy) override;

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
    void translate(double dx, double dy) override;

    const std::string& text() const { return text_; }
    double fontSize() const { return fontSize_; }

private:
    std::string text_;
    double x_, y_, fontSize_;

    // 估算文本宽度：无字体库时按字符数 * fontSize * 0.5 粗略计算。
    double estimateWidth() const;
};

} // namespace idc::geometry
