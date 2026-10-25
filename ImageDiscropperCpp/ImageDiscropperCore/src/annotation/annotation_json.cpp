// ============================================================================
// 文件：src/annotation/annotation_json.cpp
// 作用：标注 ↔ JSON 的实现（SPEC §7 annotations 段）——单条形状几何 / 样式 / 变换整体快照的双向映射，
//       段级交换函数（供 engine 组装 / 解析整份 §7 文档），以及公共的标注文本解析入口。
// 分块依据：标注 JSON 序列化归属 annotation 模块；engine 仅经私有内部头 src/json_common.h 声明的
//       annotationsSegmentToJson / annotationsFromSegmentJson 调用本文件，公共头只暴露
//       parseAnnotationsFromJsonText（std 级 API，不外泄 nlohmann）。
// 说明：通用 JSON 助手（at/jInt/… / colorToHex/colorFromHex）来自 src/json_common.h（idc::jsonutil），与 engine 共用一份。
// 常量层：键名 keys:: / 兜底默认串 defs:: / 默认数字 nums:: 三个别名仅在本文件匿名
//       命名空间内可见；校验报因一律走门面 IDC_KTR / IDC_KTRF（缺译文自动回退常量层）。
//       文本值为对外契约（含全角标点与尾随空格），改动须同步 include/i18n/core_texts.h。
// ============================================================================
#include "annotation/annotation_json.h"

#include "core/core_nums.h"  // 字号 / 描边 / 退化容差默认值单一真相源（常量层）
#include "core/percent.h"    // parsePercent / resolvePercent：标注几何的百分比换算（与 engine 读侧同口径）
#include "i18n/core_i18n.h"   // IDC_KTR / IDC_KTRF：报因取译文（缺译文回退常量层）
#include "i18n/core_texts.h" // §7 JSON 键名单一真相源（常量层）

#include "../json_common.h" // idc::jsonutil 助手 + 片段函数声明（本文件给出实现）

#include "annotation/shape_factory.h" // buildShape / ShapeRequest（约束图元复用 GUI 同款构造器）
#include "geometry/affine_transform.h"
#include "geometry/path.h"
#include "geometry/shapes.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

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
// 严格解析 type 串：仅接受已知的标注类型名；未知或空串返回 nullopt（供标注载入的强制校验使用）。
std::optional<ShapeType> strictShapeTypeFromName(const std::string& s) {
    static const std::pair<const char*, ShapeType> kAll[] = {
        {"line", ShapeType::Line}, {"rectangle", ShapeType::Rectangle}, {"square", ShapeType::Square},
        {"rhombus", ShapeType::Rhombus}, {"ellipse", ShapeType::Ellipse}, {"circle", ShapeType::Circle},
        {"polyline", ShapeType::Polyline}, {"path", ShapeType::Path}, {"text", ShapeType::Text},
        {"isos-triangle", ShapeType::IsosTriangle}, {"equ-triangle", ShapeType::EquTriangle},
        {"rect-triangle", ShapeType::RectTriangle}, {"equ-rect-triangle", ShapeType::EquRectTriangle},
    };
    for (const auto&[name, t] : kAll)
        if (s == name) return t;
    return std::nullopt;
}

// 双锚点约束图元：几何由 buildShape(p1,p2) 派生，杜绝手填顶点不满足类型约束。
bool isAnchorConstrained(const ShapeType t) {
    switch (t) {
        case ShapeType::Rectangle: case ShapeType::Square: case ShapeType::Rhombus:
        case ShapeType::IsosTriangle: case ShapeType::EquTriangle:
        case ShapeType::RectTriangle: case ShapeType::EquRectTriangle:
            return true;
        default:
            return false;
    }
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

// 写回变换（缺失/null 则保持恒等默认，不调 restoreTransformState）。
void transformFromJson(const json& v, geometry::Shape& s) {
    if (!v.is_object()) return;
    geometry::AffineTransform m;
    if (const json& mat = at(v, keys::anno::kMatrix); mat.is_array() && mat.size() >= 6) {
        m = geometry::AffineTransform(jDouble(mat[0], 1.0), jDouble(mat[1], 0.0), jDouble(mat[2], 0.0),
                                      jDouble(mat[3], 1.0), jDouble(mat[4], 0.0), jDouble(mat[5], 0.0));
    }
    s.restoreTransformState(m, jDouble(at(v, keys::anno::kScaleX), 1.0), jDouble(at(v, keys::anno::kScaleY), 1.0),
                            jDouble(at(v, keys::anno::kRotationDeg), 0.0));
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

// JSON → 形状（严格校验）：非法时写 reason 返回 nullptr。双锚点图元（约束多边形 + 圆/椭圆）经 buildShape(p1,p2) 派生、恒合法，
// line/polyline 直连顶点、path/text 直连参数并逐项校验；type 缺失/未知一律报错。
// 数值存在性助手：键须为数字，否则记 reason 并返回 false。
bool needNumField(const json& v, const char* key, double& out, std::string& reason) {
    const json& jv = at(v, key);
    if (!jv.is_number()) { reason = IDC_KTRF(msg, kErrMissingNumField, key); return false; }
    out = jv.get<double>();
    return true;
}

// 可变子节点：非对象 / 键缺失 / 值为 null → nullptr（百分比换算需就地改写）。
json* mutableField(json& obj, const char* key) {
    if (!obj.is_object()) return nullptr;
    const auto it = obj.find(key);
    return it == obj.end() || it->is_null() ? nullptr : &*it;
}

// 单个几何叶子上的百分比换算：非字符串（整数）不动；"N%" 按 basis 四舍五入为像素。
//   非百分比串仍按「缺少或非法数值字段」报错；基准未知（<= 0）而写了百分比 → 报基准未知，不静默归零。
bool resolveAnnoCoord(json& node, const int basis, const std::string& field, std::string& reason) {
    if (!node.is_string()) return true;
    const auto ratio = core::parsePercent(node.get_ref<const std::string&>());
    if (!ratio) { reason = IDC_KTRF(msg, kErrMissingNumField, field); return false; }
    if (basis <= 0) { reason = IDC_KTRF(msg, kErrAnnoPercentNoBasis, field); return false; }
    node = core::resolvePercent(*ratio, basis);
    return true;
}

// 标注几何键的百分比换算（与 engine 读侧 resolveAnnotationCoords 同口径）：x/y（文字落点）、
//   x1..y2（锚点图元 / 圆 / 椭圆）、points[i][j]（直线 / 折线顶点）、segments[i].coords[j]
//   （path 控制点，偶索引 x、奇索引 y）。样式与 transform 快照是相对量，不参与换算。
bool resolveAnnoPercentKeys(json& item, const int width, const int height, std::string& reason) {
    const auto one = [&](const char* key, const int basis) {
        json* node = mutableField(item, key);
        return node == nullptr || resolveAnnoCoord(*node, basis, key, reason);
    };
    if (!one(keys::common::kX, width) || !one(keys::common::kY, height)) return false;
    if (!one(keys::common::kX1, width) || !one(keys::common::kY1, height)) return false;
    if (!one(keys::common::kX2, width) || !one(keys::common::kY2, height)) return false;
    if (json* pts = mutableField(item, keys::common::kPoints); pts != nullptr && pts->is_array()) {
        for (std::size_t i = 0; i < pts->size(); ++i) {
            json& p = (*pts)[i];
            if (!p.is_array()) continue;
            for (std::size_t j = 0; j < p.size() && j < 2; ++j) {
                const std::string field = std::string(keys::common::kPoints) + "[" + std::to_string(i) + "][" +
                                          std::to_string(j) + "]";
                if (!resolveAnnoCoord(p[j], j == 0 ? width : height, field, reason)) return false;
            }
        }
    }
    if (json* segs = mutableField(item, keys::anno::kSegments); segs != nullptr && segs->is_array()) {
        for (std::size_t i = 0; i < segs->size(); ++i) {
            json* coords = mutableField((*segs)[i], keys::anno::kCoords);
            if (coords == nullptr || !coords->is_array()) continue;
            for (std::size_t j = 0; j < coords->size(); ++j) {
                const std::string field = std::string(keys::anno::kSegments) + "[" + std::to_string(i) + "]." +
                                          keys::anno::kCoords + "[" + std::to_string(j) + "]";
                if (!resolveAnnoCoord((*coords)[j], j % 2 == 0 ? width : height, field, reason)) return false;
            }
        }
    }
    return true;
}

// 锚点约束形状（矩形/正方形/菱形/等腰/等边/直角/等腰直角三角）：x1,y1,x2,y2 两锚点 + 逐类型退化校验
// + 可选 cornerRadius，几何经 buildShape 派生。
std::unique_ptr<geometry::Shape> anchoredShapeFromJson(const json& v, const ShapeType type, std::string& reason) {
    double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    if (!(needNumField(v, keys::common::kX1, x1, reason) && needNumField(v, keys::common::kY1, y1, reason)
          && needNumField(v, keys::common::kX2, x2, reason) && needNumField(v, keys::common::kY2, y2, reason))) return nullptr;
    const double dx = x2 - x1, dy = y2 - y1;
    constexpr double eps = nums::annotation::kDegenerateEpsPx;
    bool ok = true;
    std::string why;   // 取译文后为值类型（非 const char*）：下面与 kErrAnchorDegenerate 拼接。
    switch (type) {
        case ShapeType::Rectangle:      ok = std::abs(dx) > eps && std::abs(dy) > eps; why = IDC_KTR(msg, kErrRectSize); break;
        case ShapeType::Square:         ok = std::max(std::abs(dx), std::abs(dy)) > eps; why = IDC_KTR(msg, kErrSquareSize); break;
        case ShapeType::Rhombus:        ok = std::abs(dx) > eps && std::abs(dy) > eps; why = IDC_KTR(msg, kErrRhombusSize); break;
        case ShapeType::IsosTriangle:   ok = std::abs(dx) > eps && std::abs(dy) > eps; why = IDC_KTR(msg, kErrIsosTriangleSize); break;
        case ShapeType::EquTriangle:    ok = std::max(std::abs(dx), std::abs(dy) / std::sqrt(3.0)) > eps; why = IDC_KTR(msg, kErrEquTriangleSize); break;
        case ShapeType::RectTriangle:   ok = std::abs(dx) > eps && std::abs(dy) > eps; why = IDC_KTR(msg, kErrRectTriangleSize); break;
        case ShapeType::EquRectTriangle: ok = std::max(std::abs(dx), std::abs(dy)) > eps; why = IDC_KTR(msg, kErrEquRectTriangleSize); break;
        default: break;
    }
    if (!ok) { reason = IDC_KTR(msg, kErrAnchorDegenerate) + why; return nullptr; }
    ShapeRequest req;
    req.p1 = core::Point2D{x1, y1};
    req.p2 = core::Point2D{x2, y2};
    req.type = type;
    req.cornerRadius = std::max(0.0, jDouble(at(v, keys::common::kCornerRadius), 0.0));
    return buildShape(req);
}

// 直线 / 折线：points 顶点数组（Line 恰 2 顶点、Polyline ≥2）+ 可选 closed/cornerRadius。
std::unique_ptr<geometry::Shape> lineShapeFromJson(const json& v, const ShapeType type, std::string& reason) {
    const json& pts = at(v, keys::common::kPoints);
    if (!pts.is_array()) { reason = IDC_KTR(msg, kErrNoPoints); return nullptr; }
    std::vector<core::Point2D> points;
    for (const json& p : pts)
        if (p.is_array() && p.size() >= 2 && p[0].is_number() && p[1].is_number())
            points.emplace_back(p[0].get<double>(), p[1].get<double>());
    if (type == ShapeType::Line && points.size() != 2) { reason = IDC_KTR(msg, kErrLineNeeds2Points); return nullptr; }
    if (type == ShapeType::Polyline && points.size() < 2) { reason = IDC_KTR(msg, kErrPolylineNeedsPoints); return nullptr; }
    return std::make_unique<geometry::PolygonShape>(std::move(points), type,
                                                    jBool(at(v, keys::common::kClosed), false),
                                                    std::max(0.0, jDouble(at(v, keys::common::kCornerRadius), 0.0)));
}

// 圆/椭圆改走锚点：p1=圆心、p2 定形（圆：半径=|p1p2|；椭圆：半轴=|Δx|,|Δy|），几何经 buildShape 派生恒合法。
std::unique_ptr<geometry::Shape> ellipseShapeFromJson(const json& v, const ShapeType type, std::string& reason) {
    double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    if (!(needNumField(v, keys::common::kX1, x1, reason) && needNumField(v, keys::common::kY1, y1, reason)
          && needNumField(v, keys::common::kX2, x2, reason) && needNumField(v, keys::common::kY2, y2, reason))) return nullptr;
    const double dx = x2 - x1, dy = y2 - y1;
    constexpr double eps = nums::annotation::kDegenerateEpsPx;
    if (type == ShapeType::Circle) {
        if (std::hypot(dx, dy) <= eps) { reason = IDC_KTR(msg, kErrCircleDegenerate); return nullptr; }
    } else { // Ellipse：两半轴均须 > 0（杜绝退化扁平椭圆）
        if (std::abs(dx) <= eps || std::abs(dy) <= eps) { reason = IDC_KTR(msg, kErrEllipseDegenerate); return nullptr; }
    }
    ShapeRequest req;
    req.type = type;
    req.p1 = core::Point2D{x1, y1};
    req.p2 = core::Point2D{x2, y2};
    return buildShape(req);
}

// Path：M/L/Q/C/Z 段序列（逐段校验坐标个数，全部无效才判错）。
std::unique_ptr<geometry::Shape> pathShapeFromJson(const json& v, const ShapeType type, std::string& reason) {
    const json& segs = at(v, keys::anno::kSegments);
    if (!segs.is_array() || segs.empty()) { reason = IDC_KTR(msg, kErrPathNoSegments); return nullptr; }
    geometry::Path path;
    int added = 0;
    for (const json& seg : segs) {
        const std::string t = jStr(at(seg, keys::anno::kSegType), "M");
        const json& c = at(seg, keys::anno::kCoords);
        if (!c.is_array()) continue;
        const auto d = [&](const std::size_t i) { return i < c.size() && c[i].is_number() ? c[i].get<double>() : 0.0; };
        if (t == "M" && c.size() >= 2) { path.moveTo(d(0), d(1)); ++added; }
        else if (t == "L" && c.size() >= 2) { path.lineTo(d(0), d(1)); ++added; }
        else if (t == "Q" && c.size() >= 4) { path.quadTo(d(0), d(1), d(2), d(3)); ++added; }
        else if (t == "C" && c.size() >= 6) { path.cubicTo(d(0), d(1), d(2), d(3), d(4), d(5)); ++added; }
        else if (t == "Z") { path.closePath(); ++added; }
    }
    if (added == 0) { reason = IDC_KTR(msg, kErrPathNoSegment); return nullptr; }
    return std::make_unique<geometry::PathShape>(std::move(path), type);
}

// Text：text 内容 + x/y 落点 + 正 fontSize。
std::unique_ptr<geometry::Shape> textShapeFromJson(const json& v, std::string& reason) {
    const json& fj = at(v, keys::anno::kFontSize);
    double fontSize = nums::annotation::kDefaultFontSizePx;
    if (fj.is_number()) { fontSize = fj.get<double>(); if (!(fontSize > 0)) { reason = IDC_KTR(msg, kErrFontSizeNotPositive); return nullptr; } }
    else if (!fj.is_null()) { reason = IDC_KTR(msg, kErrFontSizeNotNumber); return nullptr; }
    return std::make_unique<geometry::TextShape>(jStr(at(v, keys::anno::kText), defs::kAnnotationText),
                                                 jDouble(at(v, keys::common::kX), 0.0), jDouble(at(v, keys::common::kY), 0.0), fontSize);
}

// JSON → 形状（按 type 分派到上面五个域解析器；几何退化/字段非法时写 reason 返回 nullptr）。
// 成功后再施加可选 transform（缩放/旋转）。
std::unique_ptr<geometry::Shape> shapeFromJson(const json& v, std::string& reason) {
    const json& tv = at(v, keys::common::kType);
    if (!tv.is_string()) { reason = IDC_KTR(msg, kErrNoTypeField); return nullptr; }
    const std::string ts = tv.get<std::string>();
    const std::optional<ShapeType> maybeType = strictShapeTypeFromName(ts);
    if (!maybeType) { reason = IDC_KTR(msg, kErrUnknownType) + ts + "'"; return nullptr; }
    const ShapeType type = *maybeType;

    std::unique_ptr<geometry::Shape> shape;
    if (isAnchorConstrained(type)) shape = anchoredShapeFromJson(v, type, reason);
    else if (type == ShapeType::Line || type == ShapeType::Polyline) shape = lineShapeFromJson(v, type, reason);
    else if (type == ShapeType::Ellipse || type == ShapeType::Circle) shape = ellipseShapeFromJson(v, type, reason);
    else if (type == ShapeType::Path) shape = pathShapeFromJson(v, type, reason);
    else shape = textShapeFromJson(v, reason);   // Text
    if (!shape) return nullptr;
    transformFromJson(at(v, keys::common::kTransform), *shape);
    return shape;
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

// JSON → 单条标注（带样式，严格校验）：几何/颜色非法时写 reason 并返回空 shape（调用方据此报错）。
// fallbackZOrder：无 zOrder 键时的回退层位（调用方传入其数组下标，保证旧文件/手写无此字段时仍按位置定序）。
Annotation annotationFromJson(const json& v, std::string& reason, const int fallbackZOrder) {
    Annotation a;
    a.shape = shapeFromJson(v, reason);
    if (!a.shape) return a;
    if (const json& cj = at(v, keys::anno::kColor); cj.is_string() && !isValidHexColor(cj.get<std::string>())) {
        reason = IDC_KTR(msg, kErrBadColor) + cj.get<std::string>() + "'"; a.shape.reset(); return a;
    }
    if (const json& fj = at(v, keys::anno::kFillColor); fj.is_string() && !isValidHexColor(fj.get<std::string>())) {
        reason = IDC_KTR(msg, kErrBadFillColor) + fj.get<std::string>() + "'"; a.shape.reset(); return a;
    }
    if (const json& sw = at(v, keys::anno::kStrokeWidth); !sw.is_null() && !sw.is_number_integer()) {
        reason = IDC_KTR(msg, kErrStrokeWidthNotInt); a.shape.reset(); return a;
    }
    if (const json& zo = at(v, keys::common::kZOrder); !zo.is_null() && !zo.is_number_integer()) {
        reason = IDC_KTR(msg, kErrZOrderNotInt); a.shape.reset(); return a;
    }
    a.style.color = colorFromHex(jStr(at(v, keys::anno::kColor), defs::kAnnotationColor));
    a.style.fillColor = colorFromHex(jStr(at(v, keys::anno::kFillColor), defs::kAnnotationColor));
    a.style.strokeWidth = jInt(at(v, keys::anno::kStrokeWidth), nums::annotation::kDefaultStrokeWidth);
    a.style.fill = jBool(at(v, keys::common::kFill), false);
    a.zOrder = jInt(at(v, keys::common::kZOrder), fallbackZOrder);   // 缺字段回退为数组下标
    return a;
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

// annotations 数组值 → 集合（严格校验，逐条重建）：任一非法即整体失败（不静默丢弃），
// annoErr 非空时逐条回传原因；段缺失 / 非数组 → 清空集合（配置文件为标注真相源）；
// 成功则按 zOrder 稳定排序（等值保序）后 restore（其内规范化 zOrder ＝ 新下标）。
bool annotationsFromSegmentJson(const jsonutil::json& arrValue, AnnotationSet& set, std::string* annoErr) {
    std::vector<Annotation> items;
    std::vector<std::string> errs;
    if (arrValue.is_array()) {
        // 下标循环即可；std::views::enumerate 对 NDK/Apple libc++<19 不可用。
        for (std::size_t idx = 0; idx < arrValue.size(); ++idx) {
            const auto& item = arrValue[idx];
            std::string reason;
            if (!item.is_object()) {
                reason = IDC_KTR(msg, kErrNotAnObject);
            } else {
                if (Annotation a = annotationFromJson(item, reason, static_cast<int>(idx)); a.shape)
                    items.push_back(std::move(a));
            }
            if (!reason.empty()) errs.push_back(IDC_KTRF(msg, kErrAnnoItemPrefix, std::to_string(idx + 1), reason));
        }
    }
    if (!errs.empty()) {
        if (annoErr != nullptr) {
            *annoErr = IDC_KTR(msg, kErrAnnoSectionInvalid);
            for (const std::string& e : errs) *annoErr += "\n  - " + e;
        }
        return false;
    }
    // 按 zOrder 稳定排序（等值保持原数组顺序）：正常 GUI 导出 zOrder 与位置一致→恒等；
    // 手改乱序时尊重显式层位意图。随后 restore 会规范化 zOrder ＝ 新下标。
    std::ranges::stable_sort(items,
                             [](const Annotation& x, const Annotation& y) { return x.zOrder < y.zOrder; });
    set.restore(std::move(items));
    return true;
}

// ---------------------------------------------------------------------------
// 公共入口（annotation_json.h）：解析标注 JSON 文本（SPEC §7 annotations 段元素语法）——
// 根对象＝单条、根数组＝多条。与整档存取共用同一套重建逻辑（annotationFromJson）。
// 强制校验：逐条重建，**任一条非法即整体失败**（不静默丢弃，out 保持不变），err 逐条列出序号与原因。
// 几何坐标可写百分比串：逐条先按 basisWidth/basisHeight 分方向换算为像素（拷贝后改写，不动调用方视图），
//   再交同一重建逻辑；基准未知而写百分比 → 该条报错（不静默归零）。
// ---------------------------------------------------------------------------
bool parseAnnotationsFromJsonText(const std::string& text,
                                  std::vector<Annotation>& out,
                                  std::string& err,
                                  const int basisWidth,
                                  const int basisHeight) {
    if (text.empty()) { err = IDC_KTR(msg, kErrAnnoTextEmpty); return false; }
    try {
        const json value = json::parse(text);
        std::vector<Annotation> built; // 本批临时产物：无错才整体追加到 out。
        std::vector<std::string> errs;
        std::size_t idx = 0;
        const auto addOne = [&](const json& item) {
            ++idx;
            std::string reason;
            if (!item.is_object()) {
                reason = IDC_KTR(msg, kErrNotAnObject);
            } else {
                if (json resolved = item; resolveAnnoPercentKeys(resolved, basisWidth, basisHeight, reason)) {
                    if (Annotation a = annotationFromJson(resolved, reason, static_cast<int>(idx - 1)); a.shape)
                        built.push_back(std::move(a));
                }
            }
            if (!reason.empty()) errs.push_back(IDC_KTRF(msg, kErrAnnoElementPrefix, std::to_string(idx), reason));
        };
        if (value.is_array()) {
            for (const json& item : value) addOne(item);
        } else if (value.is_object()) {
            addOne(value);
        } else {
            err = IDC_KTR(msg, kErrAnnoRootType);
            return false;
        }
        if (!errs.empty()) {
            err = IDC_KTR(msg, kErrAnnoValidationFailed);
            for (const std::string& e : errs) err += "\n  - " + e;
            return false;
        }
        if (built.empty()) { err = IDC_KTR(msg, kErrAnnoNoValidEntry); return false; }
        // 单来源批内按 zOrder 稳定排序（等值保序）；跨批/与已有集合合并的相对顺序由调用方（out 追加序）决定。
        std::ranges::stable_sort(built,
                                 [](const Annotation& x, const Annotation& y) { return x.zOrder < y.zOrder; });
        for (auto& a : built) out.push_back(std::move(a));
        return true;
    } catch (const json::exception& e) {
        err = IDC_KTR(msg, kErrAnnoParseFailed) + e.what();
        return false;
    }
}

} // namespace idc::annotation
