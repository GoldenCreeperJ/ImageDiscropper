// ============================================================================
// 文件：src/annotation/annotation_json_parse.cpp
// 作用：标注 JSON 的读侧实现——单条形状严格重建（五个域解析器 + shapeFromJson）、标注几何
//       百分比坐标的就地换算（resolveAnnoPercentKeys）、标注集合的段级还原
//       （annotationsFromSegmentJson，供 engine 解析整份 §7 文档）与公共文本解析入口
//       （parseAnnotationsFromJsonText）。逐条强制校验，任一非法即整体失败（不静默丢弃）。
// 分块依据：读侧（严格校验 + 百分比换算 + 还原）与写侧（几何快照，annotation_json.cpp）职责
//       正交；两 TU 共用的类型名映射 / 约束判定 / 变换写回助手集中 annotation_json_internal.h
//       （inline，定义单份不漂移）。
// 说明：通用 JSON 助手（at/jInt/…）来自 src/json_common.h（idc::jsonutil），与 engine 共用一份；
//       校验报因一律走门面 IDC_KTR / IDC_KTRF（缺译文自动回退常量层）。
// ============================================================================
#include "annotation/annotation_json.h"

#include "core/core_nums.h"  // 字号 / 描边 / 退化容差默认值单一真相源（常量层）
#include "core/percent.h"    // parsePercent / resolvePercent：标注几何的百分比换算（与 engine 读侧同口径）
#include "i18n/core_i18n.h"   // IDC_KTR / IDC_KTRF：报因取译文（缺译文回退常量层）
#include "i18n/core_texts.h" // §7 JSON 键名单一真相源（常量层）

#include "../json_common.h" // idc::jsonutil 助手 + 片段函数声明（本文件给出实现）

#include "annotation/shape_factory.h" // buildShape / ShapeRequest（约束图元复用 GUI 同款构造器）
#include "annotation_json_internal.h" // strictShapeTypeFromName / isAnchorConstrained / transformFromJson（与写侧共用）
#include "geometry/path.h"
#include "geometry/shapes.h"

#include <algorithm>
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
using jsonutil::isValidHexColor;  // 颜色串严格校验（engine/annotation 共用单一真相源）
using jsonutil::jBool;
using jsonutil::jDouble;
using jsonutil::jInt;
using jsonutil::jStr;

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
// 标注段 ↔ 集合（跨模块私有交换，声明见 src/json_common.h）：engine_config_json 解析
// 整份 SPEC §7 文档时对 annotations 段调用之；公共文本解析入口 parseAnnotationsFromJsonText
// 声明见 annotation/annotation_json.h（std 级 API，不外泄 nlohmann）。
// ---------------------------------------------------------------------------
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
