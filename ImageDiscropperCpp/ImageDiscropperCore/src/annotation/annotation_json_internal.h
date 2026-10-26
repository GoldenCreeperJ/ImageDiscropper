// ============================================================================
// 文件：src/annotation/annotation_json_internal.h（Core 私有内部头，仅标注 JSON 两个 TU 包含）
// 作用：标注 JSON 写侧（annotation_json.cpp）与读侧（annotation_json_parse.cpp）共用的三个小助手：
//       strictShapeTypeFromName（严格 type 串解析，读侧强制校验判据）、isAnchorConstrained
//       （双锚点约束图元判定，写侧反解与读侧分派共用）、transformFromJson（变换快照写回，读侧施加）。
//       全部 inline：体量小、无独立编译单元价值（同 json_common.h 先例，仅 Core 内部 .cpp
//       以相对路径包含，绝不外泄公共头）。
// ============================================================================
#pragma once

#include <optional>
#include <string>
#include <utility>

#include "../json_common.h"      // jsonutil::json / at / jDouble（宽容读取单一真相源）
#include "geometry/affine_transform.h"
#include "geometry/shapes.h"     // geometry::Shape / ShapeType
#include "i18n/core_texts.h"     // keys::anno（§7 键名单一真相源）

namespace idc::annotation {

// 严格解析 type 串：仅接受已知的标注类型名；未知或空串返回 nullopt（供标注载入的强制校验使用）。
inline std::optional<geometry::ShapeType> strictShapeTypeFromName(const std::string& s) {
    using geometry::ShapeType;
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
inline bool isAnchorConstrained(const geometry::ShapeType t) {
    using geometry::ShapeType;
    switch (t) {
        case ShapeType::Rectangle: case ShapeType::Square: case ShapeType::Rhombus:
        case ShapeType::IsosTriangle: case ShapeType::EquTriangle:
        case ShapeType::RectTriangle: case ShapeType::EquRectTriangle:
            return true;
        default:
            return false;
    }
}

// 写回变换（缺失/null 则保持恒等默认，不调 restoreTransformState）。
inline void transformFromJson(const jsonutil::json& v, geometry::Shape& s) {
    namespace keys = core::texts::jsonkey;
    using jsonutil::at;
    using jsonutil::jDouble;
    if (!v.is_object()) return;
    geometry::AffineTransform m;
    if (const jsonutil::json& mat = at(v, keys::anno::kMatrix); mat.is_array() && mat.size() >= 6) {
        m = geometry::AffineTransform(jDouble(mat[0], 1.0), jDouble(mat[1], 0.0), jDouble(mat[2], 0.0),
                                      jDouble(mat[3], 1.0), jDouble(mat[4], 0.0), jDouble(mat[5], 0.0));
    }
    s.restoreTransformState(m, jDouble(at(v, keys::anno::kScaleX), 1.0), jDouble(at(v, keys::anno::kScaleY), 1.0),
                            jDouble(at(v, keys::anno::kRotationDeg), 0.0));
}

} // namespace idc::annotation
