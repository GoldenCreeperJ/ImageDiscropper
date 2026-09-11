// ============================================================================
// 文件：include/annotation/shape_factory.h
// 作用：根据两个锚点 (p1, p2) 与 ShapeType，构造对应的 Shape 对象。
// ============================================================================
#pragma once

#include <memory>
#include <string>

#include "core/point.h"
#include "geometry/shape_type.h"
#include "geometry/path.h"
#include "geometry/shapes.h"

namespace idc::annotation {

// 构造形状所需的参数集合。
struct ShapeRequest {
    core::Point2D p1{0, 0};       // 起点（鼠标按下位置）
    core::Point2D p2{0, 0};       // 终点（鼠标当前位置）
    geometry::ShapeType type{geometry::ShapeType::LINE}; // 形状类型
    std::string text{"A"};        // TEXT 类型时使用的文本
    double fontSize{200.0};       // TEXT 类型时的字号
    geometry::Path path;          // POLYLINE / PATH 类型时使用的既有路径
};

// 根据 ShapeRequest 构造具体 Shape 实例；返回智能指针便于多态持有。
// 对于 POLYLINE / PATH，将 request.path 直接封装为 PathShape。
std::unique_ptr<geometry::Shape> buildShape(const ShapeRequest& req);

} // namespace idc::annotation
