// ============================================================================
// 文件：src/engine/engine_config_json_internal.h（Core 私有内部头，仅 engine_config_json.cpp 包含）
// 作用：EngineConfig ↔ SPEC §7 JSON 的「枚举 ↔ 串 / 颜色 / 宽容读取」助手区：
//       tierName/generatorName/remainderName 等枚举串映射 + cut/sequence/generate/preprocess
//       各段 toJson/fromJson 小助手。单消费者内部头（同 json_common.h 先例：仅 Core 内部 .cpp
//       以相对路径包含，绝不外泄公共头）。
// 常量层：本头内的 keys:: / defs:: / nums:: 三个别名是 §7 键名 / 兜底默认串 / 数字兜底的
//       引用简写（定义见 include/i18n/core_texts.h 与 core_nums.h）；engine_config_json.cpp 经
//       `using namespace detail` 共用同一套，故不得在 .cpp 里重复定义。
// ============================================================================
#pragma once

#include "../json_common.h"   // idc::jsonutil 助手（单一真相源）

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

#include "engine/engine.h"
#include "core/core_nums.h"  // §7 兜底默认数字单一真相源（常量层）
#include "i18n/core_texts.h" // §7 JSON 键名与兜底默认串单一真相源（常量层）

namespace idc::engine::detail {

using json = nlohmann::json;

// §7 JSON 键名别名（单一真相源见 include/i18n/core_texts.h）：本头与 engine_config_json.cpp
//（经 `using namespace detail`）共用同一个 `keys::`，避免两处各引一份而漂移。
namespace keys = core::texts::jsonkey;
// §7 缺字段回退值别名（不可译数据串，同上）与数字兜底别名：同由 engine_config_json.cpp 经
// `using namespace detail` 共用，保证「结构体成员初值」与「读取端回退」两处同值。
namespace defs = core::texts::defaults;
namespace nums = core::nums;

// 与 annotation 共用的宽容读取 / 颜色助手（单一真相源见 src/json_common.h）。
using jsonutil::at;
using jsonutil::colorFromHex;
using jsonutil::colorToHex;
using jsonutil::jBool;
using jsonutil::jDouble;
using jsonutil::jInt;
using jsonutil::jStr;

// ---------------------------------------------------------------------------
// 枚举 ↔ 短横线小写串（SPEC §7：如 "row-major" / "multi-rect"）。
// 枚举标识符为 C++ 内部名；下列返回串是 §7 契约 token，逐字节冻结不可改。
// ---------------------------------------------------------------------------
inline const char* tierName(const Tier t) {
    switch (t) { case Tier::Standard: return "extract"; case Tier::Erase: return "erase"; case Tier::Grid: return "grid"; }
    return "erase";
}

inline Tier tierFromName(const std::string& s) {
    if (s == "extract") return Tier::Standard;
    if (s == "grid") return Tier::Grid;
    return Tier::Erase;
}

inline const char* generatorName(const CutGenerator g) {
    switch (g) {
        case CutGenerator::Rect: return "rect";
        case CutGenerator::HorizontalLine: return "horizontal";
        case CutGenerator::VerticalLine: return "vertical";
        case CutGenerator::MultiRect: return "multi-rect";
        case CutGenerator::Grid: return "grid";
    }
    return "rect";
}

inline CutGenerator generatorFromName(const std::string& s) {
    if (s == "horizontal") return CutGenerator::HorizontalLine;
    if (s == "vertical") return CutGenerator::VerticalLine;
    if (s == "multi-rect") return CutGenerator::MultiRect;
    if (s == "grid") return CutGenerator::Grid;
    return CutGenerator::Rect;
}

inline const char* polarityName(const Polarity p) {
    return p == Polarity::Keep ? "keep" : "remove";
}

inline Polarity polarityFromName(const std::string& s) {
    return s == "keep" ? Polarity::Keep : Polarity::Remove;
}

inline const char* strategyName(const SortStrategy s) {
    switch (s) {
        case SortStrategy::RowMajor: return "row-major";
        case SortStrategy::ColumnMajor: return "column-major";
        case SortStrategy::Custom: return "custom";
    }
    return "row-major";
}

inline SortStrategy strategyFromName(const std::string& s) {
    if (s == "column-major") return SortStrategy::ColumnMajor;
    if (s == "custom") return SortStrategy::Custom;
    return SortStrategy::RowMajor;
}

inline const char* emitModeName(const EmitMode m) {
    return m == EmitMode::Separate ? "separate" : "merged";
}

inline EmitMode emitModeFromName(const std::string& s) {
    return s == "separate" ? EmitMode::Separate : EmitMode::Merged;
}

inline const char* layoutName(const MergeLayout l) {
    return l == MergeLayout::Collapse ? "collapse" : "rearrange";
}

inline MergeLayout layoutFromName(const std::string& s) {
    return s == "rearrange" ? MergeLayout::Rearrange : MergeLayout::Collapse;
}

inline const char* formatName(const ExportFormat f) {
    switch (f) {
        case ExportFormat::Png: return "png";
        case ExportFormat::Jpeg: return "jpeg";
        case ExportFormat::Webp: return "webp";
        case ExportFormat::Bmp: return "bmp";
    }
    return "png";
}

inline ExportFormat formatFromName(const std::string& s) {
    if (s == "jpeg" || s == "jpg") return ExportFormat::Jpeg;
    if (s == "webp") return ExportFormat::Webp;
    if (s == "bmp") return ExportFormat::Bmp;
    return ExportFormat::Png;
}

inline const char* remainderName(const RemainderPolicy r) {
    switch (r) {
        case RemainderPolicy::Discard: return "discard";
        case RemainderPolicy::KeepPartial: return "keep-partial";
        case RemainderPolicy::Pad: return "pad";
    }
    return "discard";
}

inline RemainderPolicy remainderFromName(const std::string& s) {
    if (s == "keep-partial") return RemainderPolicy::KeepPartial;
    if (s == "pad") return RemainderPolicy::Pad;
    return RemainderPolicy::Discard;
}

// 图案类型 / 方向 ↔ 短横线小写串（与 CLI `--pattern` / `--orientation` 取值同一套 token）。
// 命名为 gen*Token 以避免与 generate::patternKindName 产 ADL 歧义。
inline const char* genKindToken(const generate::PatternKind k) {
    switch (k) {
        case generate::PatternKind::Solid: return "solid";
        case generate::PatternKind::LinearGradient: return "linear-gradient";
        case generate::PatternKind::RadialGradient: return "radial-gradient";
        case generate::PatternKind::Checkerboard: return "checkerboard";
        case generate::PatternKind::Stripes: return "stripes";
        case generate::PatternKind::GridLines: return "grid-lines";
        case generate::PatternKind::DotGrid: return "dot-grid";
        case generate::PatternKind::Concentric: return "concentric";
        case generate::PatternKind::RadialRays: return "radial-rays";
        case generate::PatternKind::HueGradient: return "hue-gradient";
        case generate::PatternKind::Noise: return "noise";
    }
    return "solid";
}

inline generate::PatternKind genKindFromToken(const std::string& s) {
    if (s == "linear-gradient") return generate::PatternKind::LinearGradient;
    if (s == "radial-gradient") return generate::PatternKind::RadialGradient;
    if (s == "checkerboard") return generate::PatternKind::Checkerboard;
    if (s == "stripes") return generate::PatternKind::Stripes;
    if (s == "grid-lines") return generate::PatternKind::GridLines;
    if (s == "dot-grid") return generate::PatternKind::DotGrid;
    if (s == "concentric") return generate::PatternKind::Concentric;
    if (s == "radial-rays") return generate::PatternKind::RadialRays;
    if (s == "hue-gradient") return generate::PatternKind::HueGradient;
    if (s == "noise") return generate::PatternKind::Noise;
    return generate::PatternKind::Solid;
}

inline const char* genOrientationToken(const generate::Orientation o) {
    switch (o) {
        case generate::Orientation::Horizontal: return "horizontal";
        case generate::Orientation::Vertical: return "vertical";
        case generate::Orientation::DiagonalDown: return "diagonal-down";
        case generate::Orientation::DiagonalUp: return "diagonal-up";
    }
    return "horizontal";
}

inline generate::Orientation genOrientationFromToken(const std::string& s) {
    if (s == "vertical") return generate::Orientation::Vertical;
    if (s == "diagonal-down") return generate::Orientation::DiagonalDown;
    if (s == "diagonal-up") return generate::Orientation::DiagonalUp;
    return generate::Orientation::Horizontal;
}

// 噪声配色模式 ↔ 串（仅 Noise 用）。
inline const char* genNoiseToken(const generate::NoiseMode m) {
    switch (m) {
        case generate::NoiseMode::FullColor: return "full-color";
        case generate::NoiseMode::Grayscale: return "grayscale";
        case generate::NoiseMode::TwoColor: return "two-color";
    }
    return "full-color";
}

inline generate::NoiseMode genNoiseFromToken(const std::string& s) {
    if (s == "grayscale") return generate::NoiseMode::Grayscale;
    if (s == "two-color") return generate::NoiseMode::TwoColor;
    return generate::NoiseMode::FullColor;
}

// 落位锚点 ↔ 串：图层 transform（inCanvasAnchor/layerPivot）用。
inline const char* genAnchorToken(const generate::ImageAnchor a) {
    switch (a) {
        case generate::ImageAnchor::TopLeft: return "top-left";
        case generate::ImageAnchor::TopCenter: return "top-center";
        case generate::ImageAnchor::TopRight: return "top-right";
        case generate::ImageAnchor::MiddleLeft: return "middle-left";
        case generate::ImageAnchor::Center: return "center";
        case generate::ImageAnchor::MiddleRight: return "middle-right";
        case generate::ImageAnchor::BottomLeft: return "bottom-left";
        case generate::ImageAnchor::BottomCenter: return "bottom-center";
        case generate::ImageAnchor::BottomRight: return "bottom-right";
    }
    return "center";
}

inline generate::ImageAnchor genAnchorFromToken(const std::string& s) {
    if (s == "top-left") return generate::ImageAnchor::TopLeft;
    if (s == "top-center") return generate::ImageAnchor::TopCenter;
    if (s == "top-right") return generate::ImageAnchor::TopRight;
    if (s == "middle-left") return generate::ImageAnchor::MiddleLeft;
    if (s == "middle-right") return generate::ImageAnchor::MiddleRight;
    if (s == "bottom-left") return generate::ImageAnchor::BottomLeft;
    if (s == "bottom-center") return generate::ImageAnchor::BottomCenter;
    if (s == "bottom-right") return generate::ImageAnchor::BottomRight;
    return generate::ImageAnchor::Center;
}

// ImageFit ↔ token（内层源图→图层框适应方式）。
inline const char* imageFitToken(const canvas::ImageFit f) {
    switch (f) {
        case canvas::ImageFit::Crop: return "crop";
        case canvas::ImageFit::Stretch: return "stretch";
        case canvas::ImageFit::Native: return "native";
    }
    return "crop";
}

inline canvas::ImageFit imageFitFromToken(const std::string& s) {
    if (s == "stretch") return canvas::ImageFit::Stretch;
    if (s == "native") return canvas::ImageFit::Native;
    return canvas::ImageFit::Crop;
}

// ---------------------------------------------------------------------------
// PatternParams ↔ JSON：顶层 generate 段与每个 layers[i].source.recipe 共用同一映射，
//   避免两处各写一份造成字段漂移。位图恒不入配置。
// ---------------------------------------------------------------------------
inline json patternParamsToJson(const generate::PatternParams& g) {
    json gen;
    gen[keys::common::kKind] = genKindToken(g.kind);
    gen[keys::layer::kBackground] = colorToHex(g.background);
    gen[keys::layer::kForeground] = colorToHex(g.foreground);
    gen[keys::layer::kOrientation] = genOrientationToken(g.orientation);
    gen[keys::layer::kPeriod] = g.period;
    gen[keys::layer::kLineWidth] = g.lineWidth;
    gen[keys::layer::kDotRadius] = g.dotRadius;
    gen[keys::layer::kSeed] = g.seed;
    gen[keys::layer::kNoiseMode] = genNoiseToken(g.noiseMode);
    return gen;
}

inline generate::PatternParams patternParamsFromJson(const json& gen) {
    generate::PatternParams g;
    g.kind = genKindFromToken(jStr(at(gen, keys::common::kKind), defs::kPatternKindSolid));
    g.background = colorFromHex(jStr(at(gen, keys::layer::kBackground), defs::kPatternBackground));
    g.foreground = colorFromHex(jStr(at(gen, keys::layer::kForeground), defs::kPatternForeground));
    g.orientation = genOrientationFromToken(jStr(at(gen, keys::layer::kOrientation), defs::kPatternOrientation));
    g.period = jInt(at(gen, keys::layer::kPeriod), nums::generate::kDefaultPeriod);
    g.lineWidth = jInt(at(gen, keys::layer::kLineWidth), nums::generate::kDefaultLineWidth);
    g.dotRadius = jInt(at(gen, keys::layer::kDotRadius), nums::generate::kDefaultDotRadius);
    g.seed = jInt(at(gen, keys::layer::kSeed), 0);
    g.noiseMode = genNoiseFromToken(jStr(at(gen, keys::layer::kNoiseMode), defs::kNoiseModeFullColor));
    return g;
}

// ---------------------------------------------------------------------------
// 矩形 / 网格 / optional<int> 子对象映射。
// ---------------------------------------------------------------------------
inline json rectToJson(const RectRegion& r) {
    return json{{keys::common::kX1, r.left}, {keys::common::kY1, r.top}, {keys::common::kX2, r.right}, {keys::common::kY2, r.bottom}};
}

inline RectRegion rectFromJson(const json& v) {
    return RectRegion(jInt(at(v, keys::common::kX1), 0), jInt(at(v, keys::common::kY1), 0),
                      jInt(at(v, keys::common::kX2), 0), jInt(at(v, keys::common::kY2), 0));
}

// 网格参数 ↔ JSON：仅「基准点 + 单元尺寸 + 余量策略」（SPEC §3.4.3）；
// 行列数由图像边界自动推导，不作为配置字段（无 gap / cols / rows）。
inline json gridToJson(const GridParams& g) {
    return json{
        {keys::layer::kOriginX, g.originX}, {keys::layer::kOriginY, g.originY},
        {keys::config::kCellWidth, g.cellWidth}, {keys::config::kCellHeight, g.cellHeight},
        {keys::layer::kRemainder, remainderName(g.remainder)},
    };
}

inline GridParams gridFromJson(const json& v) {
    GridParams g;
    g.originX = jInt(at(v, keys::layer::kOriginX), 0); g.originY = jInt(at(v, keys::layer::kOriginY), 0);
    g.cellWidth = jInt(at(v, keys::config::kCellWidth), 1); g.cellHeight = jInt(at(v, keys::config::kCellHeight), 1);
    g.remainder = remainderFromName(jStr(at(v, keys::layer::kRemainder), defs::kRemainderDiscard));
    return g;
}

inline json optIntToJson(const std::optional<int>& o) {
    return o.has_value() ? json(*o) : json(nullptr);
}

inline std::optional<int> jsonToOptInt(const json& v) {
    if (v.is_number_integer()) return v.get<int>();
    if (v.is_number_float()) return static_cast<int>(v.get<double>());
    return std::nullopt; // null 或缺失 → 无值。
}

// ---------------------------------------------------------------------------
// 预处理步骤 ↔ {op, ...params}（SPEC §7 preprocess 数组）。
// ---------------------------------------------------------------------------
inline json preprocessToJson(const preprocess::PreprocessConfig& cfg) {
    using namespace preprocess;
    json o;
    switch (configType(cfg)) {
        case PreprocessType::Gray:
            o[keys::pre::kOp] = "gray"; break;
        case PreprocessType::Split:
            o[keys::pre::kOp] = "split"; o[keys::pre::kMask] = std::get<SplitOp>(cfg).mask; break;
        case PreprocessType::Invert:
            o[keys::pre::kOp] = "invert"; o[keys::pre::kMask] = std::get<InvertOp>(cfg).mask; break;
        case PreprocessType::Rotate:
            o[keys::pre::kOp] = "rotate"; o[keys::pre::kDeg] = std::get<RotateOp>(cfg).angle; break;
        case PreprocessType::Flip:
            o[keys::pre::kOp] = "flip"; o[keys::pre::kAxis] = std::get<FlipOp>(cfg).horizontal ? "horizontal" : "vertical"; break;
        case PreprocessType::Resize:
            o[keys::pre::kOp] = "resize"; o[keys::common::kWidth] = std::get<ResizeOp>(cfg).width; o[keys::common::kHeight] = std::get<ResizeOp>(cfg).height; break;
    }
    return o;
}


} // namespace idc::engine::detail
