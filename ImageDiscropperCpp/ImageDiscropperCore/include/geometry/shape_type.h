// ============================================================================
// 文件：include/geometry/shape_type.h
// 作用：定义标注形状类型枚举 ShapeType，服务于 SPEC §3.1 FR-1.3 标注能力；
//       每种类型对应 annotation 模块 ShapeFactory 的一条构造分支。
// ============================================================================
#pragma once

namespace idc::geometry {

// 标注形状类型：每一种类型对应 ShapeFactory 里的一条构造分支。
enum class ShapeType {
    LINE,               // 直线
    RECTANGLE,          // 矩形
    SQUARE,             // 正方形（落点为顶点，边长 max(dx,dy)，轴对齐朝光标象限生长）
    RHOMBUS,            // 菱形（起点为中心，dx/dy 为半对角线）
    ELLIPSE,            // 椭圆（落点为中心，dx/dy 为两半轴）
    CIRCLE,             // 圆形（起点为圆心，两点距离为半径）
    POLYLINE,           // 多线段（拖动连续追加）
    PATH,               // 自由路径（每次点击追加一段）
    TEXT,               // 文字（占位实现，输出矩形边界）
    ISOS_TRIANGLE,      // 等腰三角形（起点为底边中点）
    EQU_TRIANGLE,       // 等边三角形（落点为底边中点，底边水平，顶角朝光标竖向生长，参照等腰）
    RECT_TRIANGLE,      // 直角三角形（落点为直角顶点，两直角边取 dx、dy）
    EQU_RECT_TRIANGLE,  // 等腰直角三角形（落点为直角顶点，两直角边等长 max(dx,dy)，沿轴朝光标象限）
};

// 将枚举转换为可读字符串，便于日志与调试。
const char* shapeTypeName(ShapeType t);

} // namespace idc::geometry
