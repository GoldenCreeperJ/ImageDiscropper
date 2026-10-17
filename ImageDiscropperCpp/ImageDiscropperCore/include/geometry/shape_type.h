// ============================================================================
// 文件：include/geometry/shape_type.h
// 作用：定义标注形状类型枚举 ShapeType，服务于 SPEC §3.1 FR-1.3 标注能力；
//       每种类型对应 annotation 模块 ShapeFactory 的一条构造分支。
// ============================================================================
#pragma once

namespace idc::geometry {

// 标注形状类型：每一种类型对应 ShapeFactory 里的一条构造分支。
enum class ShapeType {
    Line,               // 直线
    Rectangle,          // 矩形
    Square,             // 正方形（落点为顶点，边长 max(dx,dy)，轴对齐朝光标象限生长）
    Rhombus,            // 菱形（起点为中心，dx/dy 为半对角线）
    Ellipse,            // 椭圆（落点为中心，dx/dy 为两半轴）
    Circle,             // 圆形（起点为圆心，两点距离为半径）
    Polyline,           // 多线段（拖动连续追加）
    Path,               // 自由路径（每次点击追加一段）
    Text,               // 文字（占位实现，输出矩形边界）
    IsosTriangle,       // 等腰三角形（起点为底边中点）
    EquTriangle,        // 等边三角形（落点为底边中点，底边水平，顶角朝光标竖向生长，参照等腰）
    RectTriangle,       // 直角三角形（落点为直角顶点，两直角边取 dx、dy）
    EquRectTriangle,    // 等腰直角三角形（落点为直角顶点，两直角边等长 max(dx,dy)，沿轴朝光标象限）
};

// 将枚举转换为可读字符串，便于日志与调试。
const char* shapeTypeName(ShapeType t);

} // namespace idc::geometry
