// ============================================================================
// 文件：src/annotation/annotation_json.cpp
// 作用：标注 ↔ JSON 的实现（SPEC §7 annotations 段）——单条形状几何 / 样式 / 变换整体快照的双向映射，
//       段级交换函数（供 engine 组装 / 解析整份 §7 文档），以及公共的标注文本解析入口。
// 分块依据：原实现随配置存取寄生于 engine_config_json.cpp；数据层解耦后归位到 annotation 模块，
//       engine 仅经私有内部头 src/json_common.h 声明的 annotationsSegmentToJson / annotationsFromSegmentJson
//       调用本文件，公共头只暴露 parseAnnotationsFromJsonText（std 级 API，不外泄 nlohmann）。
// 说明：本次为**纯搬家重构**——函数体逐字迁移，未改任何容差 / 缺省值 / 校验语义（向后兼容旧配置文件）。
//       通用 JSON 助手（at/jInt/… / colorToHex/colorFromHex）来自 src/json_common.h（idc::jsonutil），与 engine 共用一份。
// ============================================================================
#include "annotation/annotation_json.h"

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

using json = nlohmann::json;
using geometry::ShapeType;

// 与 engine 共用的宽容读取 / 颜色助手（单一真相源见 src/json_common.h）。
using jsonutil::at;
using jsonutil::colorFromHex;
using jsonutil::colorToHex;
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
        case ShapeType::LINE: return "line";
        case ShapeType::RECTANGLE: return "rectangle";
        case ShapeType::SQUARE: return "square";
        case ShapeType::RHOMBUS: return "rhombus";
        case ShapeType::ELLIPSE: return "ellipse";
        case ShapeType::CIRCLE: return "circle";
        case ShapeType::POLYLINE: return "polyline";
        case ShapeType::PATH: return "path";
        case ShapeType::TEXT: return "text";
        case ShapeType::ISOS_TRIANGLE: return "isos-triangle";
        case ShapeType::EQU_TRIANGLE: return "equ-triangle";
        case ShapeType::RECT_TRIANGLE: return "rect-triangle";
        case ShapeType::EQU_RECT_TRIANGLE: return "equ-rect-triangle";
    }
    return "line";
}
// 严格解析 type 串：仅接受已知的标注类型名；未知或空串返回 nullopt（供标注载入的强制校验使用）。
std::optional<ShapeType> strictShapeTypeFromName(const std::string& s) {
    static const std::pair<const char*, ShapeType> kAll[] = {
        {"line", ShapeType::LINE}, {"rectangle", ShapeType::RECTANGLE}, {"square", ShapeType::SQUARE},
        {"rhombus", ShapeType::RHOMBUS}, {"ellipse", ShapeType::ELLIPSE}, {"circle", ShapeType::CIRCLE},
        {"polyline", ShapeType::POLYLINE}, {"path", ShapeType::PATH}, {"text", ShapeType::TEXT},
        {"isos-triangle", ShapeType::ISOS_TRIANGLE}, {"equ-triangle", ShapeType::EQU_TRIANGLE},
        {"rect-triangle", ShapeType::RECT_TRIANGLE}, {"equ-rect-triangle", ShapeType::EQU_RECT_TRIANGLE},
    };
    for (const auto&[name, t] : kAll)
        if (s == name) return t;
    return std::nullopt;
}

// 双锚点约束图元：几何由 buildShape(p1,p2) 派生，杜绝手填顶点不满足类型约束。
bool isAnchorConstrained(const ShapeType t) {
    switch (t) {
        case ShapeType::RECTANGLE: case ShapeType::SQUARE: case ShapeType::RHOMBUS:
        case ShapeType::ISOS_TRIANGLE: case ShapeType::EQU_TRIANGLE:
        case ShapeType::RECT_TRIANGLE: case ShapeType::EQU_RECT_TRIANGLE:
            return true;
        default:
            return false;
    }
}

// 颜色串严格校验：# 可省；长度须为 6（RRGGBB）或 8（AARRGGBB），且全为十六进制字符。
bool isValidHexColor(const std::string& raw) {
    const std::string h = !raw.empty() && raw[0] == '#' ? raw.substr(1) : raw;
    if (h.size() != 6 && h.size() != 8) return false;
    for (const char c : h) {
        if (const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); !ok) return false;
    }
    return true;
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
        return std::array<double, 4>{mnx, mny, mxx, mxy};
    };
    switch (poly.type()) {
        case ShapeType::RECTANGLE:
        case ShapeType::SQUARE: {
            // 取 pts[0]（buildShape 保留的 p1 起始角）与其对角 pts[2] 作锚点：
            //   矩形 pts[0]=(minX,minY)、pts[2]=(maxX,maxY)，与 bbox 等价；
            //   正方形 pts[0]=原 p1、pts[2]=对角，重建后顶点序/朝向严格一致（用 bbox 会致旋转错位）。
            x1 = pts[0].x; y1 = pts[0].y;
            x2 = pts[2].x; y2 = pts[2].y;
            break;
        }
        case ShapeType::RHOMBUS: {
            const auto b = boundsOf();
            const double cx = (b[0] + b[2]) / 2.0, cy = (b[1] + b[3]) / 2.0;
            x1 = cx; y1 = cy;                    // 中心
            x2 = cx + (b[2] - b[0]) / 2.0;       // 中心 + (半对角x, 半对角y)
            y2 = cy + (b[3] - b[1]) / 2.0;
            break;
        }
        case ShapeType::ISOS_TRIANGLE:
        case ShapeType::EQU_TRIANGLE: {
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
        case ShapeType::RECT_TRIANGLE:
        case ShapeType::EQU_RECT_TRIANGLE: {
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

// 路径段类型 ↔ 单字母短串（MOVE_TO/LINE_TO/QUAD_TO/CUBIC_TO/CLOSE）。
const char* segTypeName(const geometry::PathSegmentType t) {
    switch (t) {
        case geometry::PathSegmentType::MOVE_TO: return "M";
        case geometry::PathSegmentType::LINE_TO: return "L";
        case geometry::PathSegmentType::QUAD_TO: return "Q";
        case geometry::PathSegmentType::CUBIC_TO: return "C";
        case geometry::PathSegmentType::CLOSE: return "Z";
    }
    return "M";
}

// 变换状态：恒等且 OBB 参数未动时省略（返回 null），否则存矩阵 6 元 + 缩放/旋转 3 参。
json transformToJson(const geometry::Shape& s) {
    const geometry::AffineTransform& m = s.transform();
    if (m.isIdentity() && s.obbScaleX() == 1.0 && s.obbScaleY() == 1.0 && s.obbRotationDeg() == 0.0)
        return json(nullptr);
    return json{
        {"matrix", json::array({m.a, m.b, m.c, m.d, m.tx, m.ty})},
        {"scaleX", s.obbScaleX()}, {"scaleY", s.obbScaleY()}, {"rotationDeg", s.obbRotationDeg()},
    };
}

// 写回变换（缺失/null 则保持恒等默认，不调 restoreTransformState）。
void transformFromJson(const json& v, geometry::Shape& s) {
    if (!v.is_object()) return;
    geometry::AffineTransform m;
    if (const json& mat = at(v, "matrix"); mat.is_array() && mat.size() >= 6) {
        m = geometry::AffineTransform(jDouble(mat[0], 1.0), jDouble(mat[1], 0.0), jDouble(mat[2], 0.0),
                                      jDouble(mat[3], 1.0), jDouble(mat[4], 0.0), jDouble(mat[5], 0.0));
    }
    s.restoreTransformState(m, jDouble(at(v, "scaleX"), 1.0), jDouble(at(v, "scaleY"), 1.0),
                            jDouble(at(v, "rotationDeg"), 0.0));
}

// 形状 → JSON：按具体类分派几何字段（局部坐标，不含变换；变换另经 transform 字段）。
json shapeToJson(const geometry::Shape& s) {
    json o;
    o["type"] = annoShapeTypeName(s.type());
    if (const auto* text = dynamic_cast<const geometry::TextShape*>(&s)) {
        o["x"] = text->x(); o["y"] = text->y();
        o["text"] = text->text();
        o["fontSize"] = text->fontSize();
    } else if (const auto* ell = dynamic_cast<const geometry::EllipseShape*>(&s)) {
        // 圆/椭圆：反解双锚点写出（圆心 p1 + 定形点 p2），与 shapeFromJson 的锚点→buildShape 路径互逆。
        const double cx = ell->centerX(), cy = ell->centerY();
        const double rx = ell->radiusX(), ry = ell->radiusY();
        o["x1"] = cx; o["y1"] = cy;
        if (s.type() == ShapeType::CIRCLE) { o["x2"] = cx + rx; o["y2"] = cy; }   // 圆：p2 取水平向圆上点，半径 = rx
        else { o["x2"] = cx + rx; o["y2"] = cy + ry; }                             // 椭圆：p2 编码两半轴 (rx, ry)
    } else if (const auto* path = dynamic_cast<const geometry::PathShape*>(&s)) {
        json segs = json::array();
        for (const auto&[type, coords] : path->path().segments())
            segs.push_back(json{{"t", segTypeName(type)}, {"coords", coords}});
        o["segments"] = std::move(segs);
    } else if (const auto* poly = dynamic_cast<const geometry::PolygonShape*>(&s)) {
        if (isAnchorConstrained(poly->type())) {
            // 约束图元：反解双锚点写出（与 shapeFromJson 的锚点→buildShape 路径互逆）。
            double ax1 = 0, ay1 = 0, ax2 = 0, ay2 = 0;
            anchorFromConstrained(*poly, ax1, ay1, ax2, ay2);
            o["x1"] = ax1; o["y1"] = ay1; o["x2"] = ax2; o["y2"] = ay2;
            o["cornerRadius"] = poly->cornerRadius();
        } else {
            json pts = json::array();
            for (const core::Point2D& p : poly->points()) pts.push_back(json::array({p.x, p.y}));
            o["points"] = std::move(pts);
            o["closed"] = poly->closed();
            o["cornerRadius"] = poly->cornerRadius();
        }
    } else {
        return nullptr; // 未知形状类（理论不可达）：由调用方跳过。
    }
    o["transform"] = transformToJson(s);
    return o;
}

// JSON → 形状（严格校验）：非法时写 reason 返回 nullptr。双锚点图元（约束多边形 + 圆/椭圆）经 buildShape(p1,p2) 派生、恒合法，
// line/polyline 直连顶点、path/text 直连参数并逐项校验；type 缺失/未知一律报错。
std::unique_ptr<geometry::Shape> shapeFromJson(const json& v, std::string& reason) {
    const json& tv = at(v, "type");
    if (!tv.is_string()) { reason = "缺少 type 字段"; return nullptr; }
    const std::string ts = tv.get<std::string>();
    const std::optional<ShapeType> maybeType = strictShapeTypeFromName(ts);
    if (!maybeType) { reason = "未知 type：'" + ts + "'"; return nullptr; }
    const ShapeType type = *maybeType;

    // 数值存在性助手：键须为数字，否则记 reason 并返回 false。
    const auto needNum = [&](const char* key, double& out) -> bool {
        const json& jv = at(v, key);
        if (!jv.is_number()) { reason = std::string("缺少或非法数值字段 '") + key + "'"; return false; }
        out = jv.get<double>();
        return true;
    };

    std::unique_ptr<geometry::Shape> shape;
    if (isAnchorConstrained(type)) {
        double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
        if (!(needNum("x1", x1) && needNum("y1", y1) && needNum("x2", x2) && needNum("y2", y2))) return nullptr;
        const double dx = x2 - x1, dy = y2 - y1;
        constexpr double eps = 1e-3;
        bool ok = true;
        auto why = "";
        switch (type) {
            case ShapeType::RECTANGLE:      ok = std::abs(dx) > eps && std::abs(dy) > eps; why = "矩形宽高须大于 0"; break;
            case ShapeType::SQUARE:         ok = std::max(std::abs(dx), std::abs(dy)) > eps; why = "正方形边长须大于 0"; break;
            case ShapeType::RHOMBUS:        ok = std::abs(dx) > eps && std::abs(dy) > eps; why = "菱形两半对角线须大于 0"; break;
            case ShapeType::ISOS_TRIANGLE:  ok = std::abs(dx) > eps && std::abs(dy) > eps; why = "等腰三角形半底与高须大于 0"; break;
            case ShapeType::EQU_TRIANGLE:   ok = std::max(std::abs(dx), std::abs(dy) / std::sqrt(3.0)) > eps; why = "等边三角形尺寸须大于 0"; break;
            case ShapeType::RECT_TRIANGLE:  ok = std::abs(dx) > eps && std::abs(dy) > eps; why = "直角三角形两直角边须大于 0"; break;
            case ShapeType::EQU_RECT_TRIANGLE: ok = std::max(std::abs(dx), std::abs(dy)) > eps; why = "等腰直角三角形直角边须大于 0"; break;
            default: break;
        }
        if (!ok) { reason = std::string("锚点退化：") + why; return nullptr; }
        ShapeRequest req;
        req.p1 = core::Point2D{x1, y1};
        req.p2 = core::Point2D{x2, y2};
        req.type = type;
        req.cornerRadius = std::max(0.0, jDouble(at(v, "cornerRadius"), 0.0));
        shape = buildShape(req);
    } else if (type == ShapeType::LINE || type == ShapeType::POLYLINE) {
        const json& pts = at(v, "points");
        if (!pts.is_array()) { reason = "缺少 points 数组"; return nullptr; }
        std::vector<core::Point2D> points;
        for (const json& p : pts)
            if (p.is_array() && p.size() >= 2 && p[0].is_number() && p[1].is_number())
                points.emplace_back(p[0].get<double>(), p[1].get<double>());
        if (type == ShapeType::LINE && points.size() != 2) { reason = "直线 points 须恰为 2 顶点"; return nullptr; }
        if (type == ShapeType::POLYLINE && points.size() < 2) { reason = "折线 points 须 ≥2 顶点"; return nullptr; }
        shape = std::make_unique<geometry::PolygonShape>(std::move(points), type,
                                                         jBool(at(v, "closed"), false),
                                                         std::max(0.0, jDouble(at(v, "cornerRadius"), 0.0)));
    } else if (type == ShapeType::ELLIPSE || type == ShapeType::CIRCLE) {
        // 圆/椭圆改走锚点：p1=圆心、p2 定形（圆：半径=|p1p2|；椭圆：半轴=|Δx|,|Δy|），几何经 buildShape 派生恒合法。
        double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
        if (!(needNum("x1", x1) && needNum("y1", y1) && needNum("x2", x2) && needNum("y2", y2))) return nullptr;
        const double dx = x2 - x1, dy = y2 - y1;
        constexpr double eps = 1e-3;
        if (type == ShapeType::CIRCLE) {
            if (std::hypot(dx, dy) <= eps) { reason = "锚点退化：圆的半径须大于 0"; return nullptr; }
        } else { // ELLIPSE：两半轴均须 > 0（杜绝退化扁平椭圆）
            if (std::abs(dx) <= eps || std::abs(dy) <= eps) { reason = "锚点退化：椭圆两半轴须大于 0"; return nullptr; }
        }
        ShapeRequest req;
        req.type = type;
        req.p1 = core::Point2D{x1, y1};
        req.p2 = core::Point2D{x2, y2};
        shape = buildShape(req);
    } else if (type == ShapeType::PATH) {
        const json& segs = at(v, "segments");
        if (!segs.is_array() || segs.empty()) { reason = "path 缺少非空 segments 数组"; return nullptr; }
        geometry::Path path;
        int added = 0;
        for (const json& seg : segs) {
            const std::string t = jStr(at(seg, "t"), "M");
            const json& c = at(seg, "coords");
            if (!c.is_array()) continue;
            const auto d = [&](const std::size_t i) { return i < c.size() && c[i].is_number() ? c[i].get<double>() : 0.0; };
            if (t == "M" && c.size() >= 2) { path.moveTo(d(0), d(1)); ++added; }
            else if (t == "L" && c.size() >= 2) { path.lineTo(d(0), d(1)); ++added; }
            else if (t == "Q" && c.size() >= 4) { path.quadTo(d(0), d(1), d(2), d(3)); ++added; }
            else if (t == "C" && c.size() >= 6) { path.cubicTo(d(0), d(1), d(2), d(3), d(4), d(5)); ++added; }
            else if (t == "Z") { path.closePath(); ++added; }
        }
        if (added == 0) { reason = "path 无有效段"; return nullptr; }
        shape = std::make_unique<geometry::PathShape>(std::move(path), type);
    } else { // TEXT
        const json& fj = at(v, "fontSize");
        double fontSize = 200.0;
        if (fj.is_number()) { fontSize = fj.get<double>(); if (!(fontSize > 0)) { reason = "文字 fontSize 须为正"; return nullptr; } }
        else if (!fj.is_null()) { reason = "文字 fontSize 须为数值"; return nullptr; }
        shape = std::make_unique<geometry::TextShape>(jStr(at(v, "text"), "A"),
                                                      jDouble(at(v, "x"), 0.0), jDouble(at(v, "y"), 0.0), fontSize);
    }
    transformFromJson(at(v, "transform"), *shape);
    return shape;
}

// 单条标注 → JSON（几何 + 样式）；无形状/未知类返回 null（调用方 push 前过滤）。
json annotationToJson(const Annotation& a) {
    if (!a.shape) return nullptr;
    json o = shapeToJson(*a.shape);
    if (o.is_null()) return o;
    o["color"] = colorToHex(a.color);
    o["fillColor"] = colorToHex(a.fillColor);
    o["strokeWidth"] = a.strokeWidth;
    o["fill"] = a.fillType;
    o["zOrder"] = a.zOrder;   // 叠放次序（0＝最底、大＝顶）：显式落盘，与数组位置一致。
    return o;
}

// JSON → 单条标注（带样式，严格校验）：几何/颜色非法时写 reason 并返回空 shape（调用方据此报错）。
// fallbackZOrder：无 zOrder 键时的回退层位（调用方传入其数组下标，保证旧文件/手写无此字段时仍按位置定序）。
Annotation annotationFromJson(const json& v, std::string& reason, const int fallbackZOrder) {
    Annotation a;
    a.shape = shapeFromJson(v, reason);
    if (!a.shape) return a;
    a.shapeType = a.shape->type();
    if (const json& cj = at(v, "color"); cj.is_string() && !isValidHexColor(cj.get<std::string>())) {
        reason = "color 非法十六进制：'" + cj.get<std::string>() + "'"; a.shape.reset(); return a;
    }
    if (const json& fj = at(v, "fillColor"); fj.is_string() && !isValidHexColor(fj.get<std::string>())) {
        reason = "fillColor 非法十六进制：'" + fj.get<std::string>() + "'"; a.shape.reset(); return a;
    }
    if (const json& sw = at(v, "strokeWidth"); !sw.is_null() && !sw.is_number_integer()) {
        reason = "strokeWidth 须为整数"; a.shape.reset(); return a;
    }
    if (const json& zo = at(v, "zOrder"); !zo.is_null() && !zo.is_number_integer()) {
        reason = "zOrder 须为整数"; a.shape.reset(); return a;
    }
    a.color = colorFromHex(jStr(at(v, "color"), "#FF000000"));
    a.fillColor = colorFromHex(jStr(at(v, "fillColor"), "#FF000000"));
    a.strokeWidth = jInt(at(v, "strokeWidth"), 2);
    a.fillType = jBool(at(v, "fill"), false);
    a.zOrder = jInt(at(v, "zOrder"), fallbackZOrder);   // 缺字段回退为数组下标
    return a;
}

} // namespace

// ---------------------------------------------------------------------------
// 标注段 ↔ JSON 片段（跨模块私有交换，声明见 src/json_common.h）：
// engine_config_json 组装 / 解析整份 SPEC §7 文档时对 annotations 段调用之。
// ---------------------------------------------------------------------------
// 层内标注 → annotations 数组（逐条 annotationToJson，无形状 / 未知类返回 null 时跳过）。
nlohmann::json annotationsSegmentToJson(const AnnotationLayer& layer) {
    json arr = json::array();
    for (const Annotation& a : layer.annotations()) {
        if (json o = annotationToJson(a); !o.is_null()) arr.push_back(std::move(o));
    }
    return arr;
}

// annotations 数组值 → 层（严格校验，逐条重建）：任一非法即整体失败（不静默丢弃），
// annoErr 非空时逐条回传原因；段缺失 / 非数组 → 清空层（配置文件为标注真相源）；
// 成功则按 zOrder 稳定排序（等值保序）后 restoreAnnotations（其内规范化 zOrder ＝ 新下标）。
bool annotationsFromSegmentJson(const nlohmann::json& arrValue, AnnotationLayer& layer, std::string* annoErr) {
    std::vector<Annotation> items;
    std::vector<std::string> errs;
    if (arrValue.is_array()) {
        std::size_t idx = 0;
        for (const json& item : arrValue) {
            ++idx;
            std::string reason;
            if (!item.is_object()) {
                reason = "非对象条目";
            } else {
                if (Annotation a = annotationFromJson(item, reason, static_cast<int>(idx - 1)); a.shape)
                    items.push_back(std::move(a));
            }
            if (!reason.empty()) errs.push_back("第 " + std::to_string(idx) + " 个标注元素：" + reason);
        }
    }
    if (!errs.empty()) {
        if (annoErr != nullptr) {
            *annoErr = "配置 annotations 段存在非法条目：";
            for (const std::string& e : errs) *annoErr += "\n  - " + e;
        }
        return false;
    }
    // 按 zOrder 稳定排序（等值保持原数组顺序）：正常 GUI 导出 zOrder 与位置一致→恒等；
    // 手改乱序时尊重显式层位意图。随后 restoreAnnotations 会规范化 zOrder ＝ 新下标。
    std::stable_sort(items.begin(), items.end(),
                     [](const Annotation& x, const Annotation& y) { return x.zOrder < y.zOrder; });
    layer.restoreAnnotations(std::move(items));
    return true;
}

// ---------------------------------------------------------------------------
// 公共入口（annotation_json.h）：解析标注 JSON 文本（SPEC §7 annotations 段元素语法）——
// 根对象＝单条、根数组＝多条。与整档存取共用同一套重建逻辑（annotationFromJson）。
// 强制校验：逐条重建，**任一条非法即整体失败**（不静默丢弃，out 保持不变），err 逐条列出序号与原因。
// ---------------------------------------------------------------------------
bool parseAnnotationsFromJsonText(const std::string& text,
                                  std::vector<Annotation>& out,
                                  std::string& err) {
    if (text.empty()) { err = "标注 JSON 为空"; return false; }
    try {
        const json value = json::parse(text);
        std::vector<Annotation> built; // 本批临时产物：无错才整体追加到 out。
        std::vector<std::string> errs;
        std::size_t idx = 0;
        const auto addOne = [&](const json& item) {
            ++idx;
            std::string reason;
            if (!item.is_object()) {
                reason = "非对象条目";
            } else {
                if (Annotation a = annotationFromJson(item, reason, static_cast<int>(idx - 1)); a.shape)
                    built.push_back(std::move(a));
            }
            if (!reason.empty()) errs.push_back("第 " + std::to_string(idx) + " 个元素：" + reason);
        };
        if (value.is_array()) {
            for (const json& item : value) addOne(item);
        } else if (value.is_object()) {
            addOne(value);
        } else {
            err = "标注 JSON 根须为对象（单条标注）或数组（多条标注）";
            return false;
        }
        if (!errs.empty()) {
            err = "标注校验失败：";
            for (const std::string& e : errs) err += "\n  - " + e;
            return false;
        }
        if (built.empty()) { err = "标注 JSON 无有效条目"; return false; }
        // 单来源批内按 zOrder 稳定排序（等值保序）；跨批/与已有层合并的相对顺序由调用方（out 追加序）决定。
        std::stable_sort(built.begin(), built.end(),
                         [](const Annotation& x, const Annotation& y) { return x.zOrder < y.zOrder; });
        for (auto& a : built) out.push_back(std::move(a));
        return true;
    } catch (const json::exception& e) {
        err = std::string("标注 JSON 解析失败：") + e.what();
        return false;
    }
}

} // namespace idc::annotation
