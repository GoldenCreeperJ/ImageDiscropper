// ============================================================================
// 文件：src/annotation/annotation_json.cpp
// 作用：标注 ↔ JSON 的写侧实现（SPEC §7 annotations 段）——单条形状几何 / 样式 / 变换整体快照的
//       序列化（shapeToJson / annotationToJson / annotationsSegmentToJson）。
// 分块依据：标注 JSON 序列化归属 annotation 模块；engine 仅经私有内部头 src/json_common.h 声明的
//       annotationsSegmentToJson / annotationsFromSegmentJson 调用本文件。读侧（严格重建 + 百分比
//       换算 + 段级还原 + 文本入口）独立为 annotation_json_parse.cpp；两 TU 共用的类型名映射 /
//       约束判定 / 变换写回助手集中 annotation_json_internal.h（inline，定义单份不漂移）。
//       公共头只暴露 parseAnnotationsFromJsonText（std 级 API，不外泄 nlohmann）。
// 说明：通用 JSON 助手（at/jInt/… / colorToHex/colorFromHex）来自 src/json_common.h（idc::jsonutil），与 engine 共用一份。
// 常量层：键名 keys:: / 兜底默认串 defs:: / 默认数字 nums:: 三个别名仅在本文件匿名
//       命名空间内可见；文本值为对外契约（含全角标点与尾随空格），改动须同步 include/i18n/core_texts.h。
// ============================================================================
#include "annotation/annotation_json.h"

#include "core/core_nums.h"  // 兜底默认数字单一真相源（常量层，nums:: 别名引用）
#include "i18n/core_texts.h" // §7 JSON 键名单一真相源（常量层）

#include "../json_common.h" // idc::jsonutil 助手 + 片段函数声明（本文件给出实现）

#include "annotation_json_internal.h" // strictShapeTypeFromName / isAnchorConstrained / transformFromJson（与读侧共用）
#include "geometry/affine_transform.h"
#include "geometry/path.h"
#include "geometry/shapes.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>

namespace idc::annotation {
namespace {

using json = jsonutil::json; // 与 engine/json_common 单一类型对齐（ordered_json，保插入序）。
using geometry::ShapeType;

// §7 annotations 段键名 / 兜底默认串 / 默认数字的别名（单一真相源见 include/i18n/core_texts.h）。
namespace keys = core::texts::jsonkey;
namespace defs = core::texts::defaults;
namespace nums = core::nums;

// 与 engine 共用的宽容读取 / 颜色助手（单一真相源见 src/json_common.h）。
using jsonutil::at;
using jsonutil::colorFromHex;
using jsonutil::colorToHex;
using jsonutil::isValidHexColor;  // 颜色串严格校验（engine/annotation 共用单一真相源）
using jsonutil::jBool;
using jsonutil::jDouble;
using jsonutil::jInt;
using jsonutil::jStr;

// ---------------------------------------------------------------------------
// 标注 ↔ JSON（SPEC §7 annotations 段）：形状按具体类分派几何，样式逐条携带，
// 非恒等变换存「矩阵 + 累积 OBB 参数」整体快照（加载经 restoreTransformState 一次写回）。
// ---------------------------------------------------------------------------
// ShapeType ↔ 短横线小写串（与其余枚举串风格一致；与 shapeTypeName 的大写日志串分开维护）。
const char* annoShapeTypeName(const ShapeType t) {
    switch (t) {
        case ShapeType::Line: return "line";
        case ShapeType::Rectangle: return "rectangle";
        case ShapeType::Square: return "square";
        case ShapeType::Rhombus: return "rhombus";
        case ShapeType::Ellipse: return "ellipse";
        case ShapeType::Circle: return "circle";
        case ShapeType::Polyline: return "polyline";
        case ShapeType::Path: return "path";
        case ShapeType::Text: return "text";
        case ShapeType::IsosTriangle: return "isos-triangle";
        case ShapeType::EquTriangle: return "equ-triangle";
        case ShapeType::RectTriangle: return "rect-triangle";
        case ShapeType::EquRectTriangle: return "equ-rect-triangle";
    }
    return "line";
}

// 约束图元的规范顶点 → 双锚点 (x1,y1,x2,y2)：各类型反解规则与 buildShape 一一对应，保证往返顶点级稳定。
// 说明：调用前提为 poly 顶点确由 buildShape 生成（GUI 绘制 / CLI 载入皆然）。
void anchorFromConstrained(const geometry::PolygonShape& poly,
                           double& x1, double& y1, double& x2, double& y2) {
    const auto& pts = poly.points();
    constexpr double eps = 1e-6;
    const auto boundsOf = [&] {
        double mnx = pts[0].x, mxx = pts[0].x, mny = pts[0].y, mxy = pts[0].y;
        for (const auto& p : pts) {
            mnx = std::min(mnx, p.x); mxx = std::max(mxx, p.x);
            mny = std::min(mny, p.y); mxy = std::max(mxy, p.y);
        }
        return std::array{mnx, mny, mxx, mxy};
    };
    switch (poly.type()) {
        case ShapeType::Rectangle:
        case ShapeType::Square: {
            // 取 pts[0]（buildShape 保留的 p1 起始角）与其对角 pts[2] 作锚点：
            //   矩形 pts[0]=(minX,minY)、pts[2]=(maxX,maxY)，与 bbox 等价；
            //   正方形 pts[0]=原 p1、pts[2]=对角，重建后顶点序/朝向严格一致（用 bbox 会致旋转错位）。
            x1 = pts[0].x; y1 = pts[0].y;
            x2 = pts[2].x; y2 = pts[2].y;
            break;
        }
        case ShapeType::Rhombus: {
            const auto b = boundsOf();
            const double cx = (b[0] + b[2]) / 2.0, cy = (b[1] + b[3]) / 2.0;
            x1 = cx; y1 = cy;                    // 中心
            x2 = cx + (b[2] - b[0]) / 2.0;       // 中心 + (半对角x, 半对角y)
            y2 = cy + (b[3] - b[1]) / 2.0;
            break;
        }
        case ShapeType::IsosTriangle:
        case ShapeType::EquTriangle: {
            // 底边＝两条等 y 顶点；顶点＝第三点。底边中点为 p1，(中点x+半底, 顶点y) 为 p2。
            int bi0 = -1, bi1 = -1, ai = -1;
            for (int i = 0; i < 3 && bi0 < 0; ++i)
                for (int j = i + 1; j < 3; ++j)
                    if (std::abs(pts[i].y - pts[j].y) < eps) { bi0 = i; bi1 = j; ai = 3 - i - j; }
            const double baseMidX = (pts[bi0].x + pts[bi1].x) / 2.0;
            const double halfBase = std::abs(pts[bi1].x - pts[bi0].x) / 2.0;
            x1 = baseMidX; y1 = pts[bi0].y;
            x2 = baseMidX + halfBase; y2 = pts[ai].y;   // y2 落在顶点行，编码高度与朝向
            break;
        }
        case ShapeType::RectTriangle:
        case ShapeType::EquRectTriangle: {
            // 直角顶点＝与另两点分别共享 x / y 的那一点；p2＝(另一点x, 第三点y)。
            int corner = -1, hb = -1, vc = -1;
            for (int i = 0; i < 3; ++i) {
                int h = -1, v = -1;                        // h: 同 y 的邻居; v: 同 x 的邻居
                for (int k = 0; k < 3; ++k) if (k != i) {
                    if (std::abs(pts[k].y - pts[i].y) < eps) h = k;
                    if (std::abs(pts[k].x - pts[i].x) < eps) v = k;
                }
                if (h >= 0 && v >= 0 && h != v) { corner = i; hb = h; vc = v; break; }
            }
            x1 = pts[corner].x; y1 = pts[corner].y;
            x2 = pts[hb].x;     y2 = pts[vc].y;
            break;
        }
        default: {
            const auto b = boundsOf();
            x1 = b[0]; y1 = b[1]; x2 = b[2]; y2 = b[3];    // 理论不可达兜底
            break;
        }
    }
    (void)eps;
}

// 路径段类型 ↔ 单字母短串（MoveTo/LineTo/QuadTo/CubicTo/Close）。
const char* segTypeName(const geometry::PathSegmentType t) {
    switch (t) {
        case geometry::PathSegmentType::MoveTo: return "M";
        case geometry::PathSegmentType::LineTo: return "L";
        case geometry::PathSegmentType::QuadTo: return "Q";
        case geometry::PathSegmentType::CubicTo: return "C";
        case geometry::PathSegmentType::Close: return "Z";
    }
    return "M";
}

// 变换状态：恒等且 OBB 参数未动时省略（返回 null），否则存矩阵 6 元 + 缩放/旋转 3 参。
json transformToJson(const geometry::Shape& s) {
    const geometry::AffineTransform& m = s.transform();
    if (m.isIdentity() && s.obbScaleX() == 1.0 && s.obbScaleY() == 1.0 && s.obbRotationDeg() == 0.0)
        return {nullptr};
    return json{
        {keys::anno::kMatrix, json::array({m.a, m.b, m.c, m.d, m.tx, m.ty})},
        {keys::anno::kScaleX, s.obbScaleX()}, {keys::anno::kScaleY, s.obbScaleY()}, {keys::anno::kRotationDeg, s.obbRotationDeg()},
    };
}

// 形状 → JSON：按具体类分派几何字段（局部坐标，不含变换；变换另经 transform 字段）。
json shapeToJson(const geometry::Shape& s) {
    json o;
    o[keys::common::kType] = annoShapeTypeName(s.type());
    if (const auto* text = dynamic_cast<const geometry::TextShape*>(&s)) {
        o[keys::common::kX] = text->x(); o[keys::common::kY] = text->y();
        o[keys::anno::kText] = text->text();
        o[keys::anno::kFontSize] = text->fontSize();
    } else if (const auto* ell = dynamic_cast<const geometry::EllipseShape*>(&s)) {
        // 圆/椭圆：反解双锚点写出（圆心 p1 + 定形点 p2），与 shapeFromJson 的锚点→buildShape 路径互逆。
        const double cx = ell->centerX(), cy = ell->centerY();
        const double rx = ell->radiusX(), ry = ell->radiusY();
        o[keys::common::kX1] = cx; o[keys::common::kY1] = cy;
        if (s.type() == ShapeType::Circle) { o[keys::common::kX2] = cx + rx; o[keys::common::kY2] = cy; }   // 圆：p2 取水平向圆上点，半径 = rx
        else { o[keys::common::kX2] = cx + rx; o[keys::common::kY2] = cy + ry; }                             // 椭圆：p2 编码两半轴 (rx, ry)
    } else if (const auto* path = dynamic_cast<const geometry::PathShape*>(&s)) {
        json segs = json::array();
        for (const auto&[type, coords] : path->path().segments())
            segs.push_back(json{{keys::anno::kSegType, segTypeName(type)}, {keys::anno::kCoords, coords}});
        o[keys::anno::kSegments] = std::move(segs);
    } else if (const auto* poly = dynamic_cast<const geometry::PolygonShape*>(&s)) {
        if (isAnchorConstrained(poly->type())) {
            // 约束图元：反解双锚点写出（与 shapeFromJson 的锚点→buildShape 路径互逆）。
            double ax1 = 0, ay1 = 0, ax2 = 0, ay2 = 0;
            anchorFromConstrained(*poly, ax1, ay1, ax2, ay2);
            o[keys::common::kX1] = ax1; o[keys::common::kY1] = ay1; o[keys::common::kX2] = ax2; o[keys::common::kY2] = ay2;
            o[keys::common::kCornerRadius] = poly->cornerRadius();
        } else {
            json pts = json::array();
            for (const core::Point2D& p : poly->points()) pts.push_back(json::array({p.x, p.y}));
            o[keys::common::kPoints] = std::move(pts);
            o[keys::common::kClosed] = poly->closed();
            o[keys::common::kCornerRadius] = poly->cornerRadius();
        }
    } else {
        return nullptr; // 未知形状类（理论不可达）：由调用方跳过。
    }
    o[keys::common::kTransform] = transformToJson(s);
    return o;
}

// 单条标注 → JSON（几何 + 样式）；无形状/未知类返回 null（调用方 push 前过滤）。
json annotationToJson(const Annotation& a) {
    if (!a.shape) return nullptr;
    json o = shapeToJson(*a.shape);
    if (o.is_null()) return o;
    o[keys::anno::kColor] = colorToHex(a.style.color);
    o[keys::anno::kFillColor] = colorToHex(a.style.fillColor);
    o[keys::anno::kStrokeWidth] = a.style.strokeWidth;
    o[keys::common::kFill] = a.style.fill;
    o[keys::common::kZOrder] = a.zOrder;   // 叠放次序（0＝最底、大＝顶）：显式落盘，与数组位置一致。
    return o;
}

} // namespace

// ---------------------------------------------------------------------------
// 标注段 ↔ JSON 片段（跨模块私有交换，声明见 src/json_common.h）：
// engine_config_json 组装 / 解析整份 SPEC §7 文档时对 annotations 段调用之。
// ---------------------------------------------------------------------------
// 集合内标注 → annotations 数组（逐条 annotationToJson，无形状 / 未知类返回 null 时跳过）。
jsonutil::json annotationsSegmentToJson(const AnnotationSet& set) {
    json arr = json::array();
    for (const Annotation& a : set.annotations()) {
        if (json o = annotationToJson(a); !o.is_null()) arr.push_back(std::move(o));
    }
    return arr;
}

} // namespace idc::annotation
