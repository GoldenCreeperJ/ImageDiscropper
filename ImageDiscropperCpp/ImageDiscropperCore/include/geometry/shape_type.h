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
    SQUARE,             // 正方形（以起点到当前点的向量为一条边）
    RHOMBUS,            // 菱形（起点为中心，dx/dy 为半对角线）
    ROUNDRECTANGLE,     // 圆角矩形
    ROUNDSQUARE,        // 圆角正方形
    ELLIPSE,            // 椭圆（两点确定外接矩形）
    CIRCLE,             // 圆形（起点为圆心，两点距离为半径）
    POLYLINE,           // 多线段（拖动连续追加）
    PATH,               // 自由路径（每次点击追加一段）
    TEXT,               // 文字（占位实现，输出矩形边界）
    ISOS_TRIANGLE,      // 等腰三角形（起点为底边中点）
    EQU_TRIANGLE,       // 等边三角形（起点到当前点为一条边）
    RECT_TRIANGLE,      // 直角三角形（起点、当前点、(x1,y2) 三点）
    EQU_RECT_TRIANGLE,  // 等腰直角三角形
};

// 将枚举转换为可读字符串，便于日志与调试。
const char* shapeTypeName(ShapeType t);

} // namespace idc::geometry
