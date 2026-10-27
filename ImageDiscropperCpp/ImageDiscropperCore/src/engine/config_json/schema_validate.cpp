// ============================================================================
// 文件：src/engine/config_json/schema_validate.cpp
// 作用：schema_validate.h 的实现——SPEC §7 配置 schema 的读侧严格校验。仅拦「键显式写出且值非 null」
//       而类型 / 枚举 / 颜色非法的 present-but-invalid 情形——这类值若静默落默认值，会产出与意图相左
//       却 exit 0 的结果（如 "mode":"sepreate"→merged），故一律报错。缺失 / null 仍走宽容路径填默认
//       （向后兼容不变）；所有数值键一律整数（浮点即报 kErrConfigNotInteger，不静默截断）；例外是
//       「有基准尺寸的几何坐标」（矩形 / 网格 / 合并单元尺寸 / 图层 transform / generate 落位 /
//       resize 目标尺寸），它们另接受百分比串 "50%"（checkCoord 判据，像素换算见 percent_coords.cpp）。
//       数值越界仅图层 opacity（0..255）由 validateLayerSeg 拦截，其余越界数值仍由读侧 clamp。
//       合法别名（format "jpg"）与各枚举默认 token（tier "erase"、generator "rect"…）一律放行——
//       只有真正拼错才报。逐段镜像 engineConfigFromJson 的读取分派，保证「校验判据 == 读取判据」不漂移。
//       首处失败即停，reason 携 JSON 路径定位。
// 分块依据：严格校验与 codec（engine_config_json.cpp）的字段映射正交，且体量可观（二十余个
//       check*/validate* 助手），独立成文件避免 engine_config_json.cpp 沦为上帝文件；仅 validateSchema /
//       validateLayerSeg 两个入口被序列化侧调用，其余助手全部内聚于本文件匿名命名空间。
// ============================================================================
#include "schema_validate.h"

#include "../../json_common.h"   // jsonutil::isValidHexColor（颜色严格校验唯一口径）
#include "engine_config_json_internal.h" // detail: keys/defs/at/jStr + generatorFromName 等枚举映射
#include "core/percent.h"     // core::parsePercent（checkCoord / 轴权重百分比判据）
#include "i18n/core_i18n.h"   // IDC_KTRF：严格校验定位报因取译文（缺译文回退常量层）

#include <cstddef>
#include <initializer_list>
#include <string>

namespace idc::engine {
namespace {
using namespace detail;   // keys/defs/at/jStr 等简写（单一真相源见 engine_config_json_internal.h）

// ---------------------------------------------------------------------------
// 叶子取值助手：键缺失或值为 null → 放行（交默认）；present 且类型/取值不符 → 写定位报因、返回 false。
// ---------------------------------------------------------------------------
bool checkObject(const json& obj, const char* key, const std::string& path, std::string& reason) {
    if (!obj.contains(key)) return true;
    if (const json& v = obj.at(key); v.is_null() || v.is_object()) return true;
    reason = IDC_KTRF(msg, kErrConfigNotObject, path);
    return false;
}
bool checkArray(const json& obj, const char* key, const std::string& path, std::string& reason) {
    if (!obj.contains(key)) return true;
    if (const json& v = obj.at(key); v.is_null() || v.is_array()) return true;
    reason = IDC_KTRF(msg, kErrConfigNotArray, path);
    return false;
}
// 整数严格校验（唯一数值判据）：引擎数值字段恒为整数，浮点即报错——覆盖 source / opacity / order / cut / export /
// layers / generate 的全部数值键；缺失或 null 放行（交默认），越界仍由读取点 clamp（如 opacity 0..255）或 build 侧拒绝。
bool checkInt(const json& obj, const char* key, const std::string& path, std::string& reason) {
    if (!obj.contains(key)) return true;
    if (const json& v = obj.at(key); v.is_null() || v.is_number_integer()) return true;
    reason = IDC_KTRF(msg, kErrConfigNotInteger, path);
    return false;
}
// 坐标严格校验：整数或百分比串（"50%"）——仅用于「有明确基准尺寸」的几何键：矩形 / 网格 / 合并单元
//   尺寸 / 图层 transform / generate 落位 / resize 目标尺寸。百分比在解析前由 percent_coords.cpp 的
//   resolvePercentCoords 就地换算为像素，故各段解析器始终只见整数；其余数值键仍走 checkInt。
bool checkCoord(const json& obj, const char* key, const std::string& path, std::string& reason) {
    if (!obj.contains(key)) return true;
    const json& v = obj.at(key);
    if (v.is_null() || v.is_number_integer()) return true;
    if (v.is_string() && core::parsePercent(v.get_ref<const std::string&>()).has_value()) return true;
    reason = IDC_KTRF(msg, kErrConfigNotCoord, path, v.dump());
    return false;
}
// 整数 + 闭区间（如 opacity 0..255）：越界报错。
bool checkIntRange(const json& obj, const char* key, const std::string& path, const int lo, const int hi, std::string& reason) {
    if (!obj.contains(key)) return true;
    const json& v = obj.at(key);
    if (v.is_null()) return true;
    if (!v.is_number_integer()) { reason = IDC_KTRF(msg, kErrConfigNotInteger, path); return false; }
    if (const int n = v.get<int>(); n < lo || n > hi) { reason = IDC_KTRF(msg, kErrConfigRange, path, std::to_string(lo), std::to_string(hi)); return false; }
    return true;
}
bool checkBool(const json& obj, const char* key, const std::string& path, std::string& reason) {
    if (!obj.contains(key)) return true;
    if (const json& v = obj.at(key); v.is_null() || v.is_boolean()) return true;
    reason = IDC_KTRF(msg, kErrConfigNotBool, path);
    return false;
}
bool checkString(const json& obj, const char* key, const std::string& path, std::string& reason) {
    if (!obj.contains(key)) return true;
    if (const json& v = obj.at(key); v.is_null() || v.is_string()) return true;
    reason = IDC_KTRF(msg, kErrConfigNotString, path);
    return false;
}
bool checkEnum(const json& obj, const char* key, const std::string& path,
                      const std::initializer_list<const char*>& allowed, std::string& reason) {
    if (!obj.contains(key)) return true;
    const json& v = obj.at(key);
    if (v.is_null()) return true;
    if (!v.is_string()) { reason = IDC_KTRF(msg, kErrConfigBadEnum, path, v.dump()); return false; }
    const std::string token = v.get<std::string>();
    for (const char* a : allowed) if (token == a) return true;
    reason = IDC_KTRF(msg, kErrConfigBadEnum, path, token);
    return false;
}
bool checkColor(const json& obj, const char* key, const std::string& path, std::string& reason) {
    if (!obj.contains(key)) return true;
    const json& v = obj.at(key);
    if (v.is_null()) return true;
    if (!v.is_string()) { reason = IDC_KTRF(msg, kErrConfigNotString, path); return false; }
    if (const std::string raw = v.get<std::string>(); !jsonutil::isValidHexColor(raw)) { reason = IDC_KTRF(msg, kErrConfigBadColor, path, raw); return false; }
    return true;
}
// 九向锚点枚举（transform.inCanvasAnchor/layerPivot 与 imageAnchor/imagePivot 共用）：抽为助手避免命名数组无法转 initializer_list。
bool checkAnchor(const json& obj, const char* key, const std::string& path, std::string& reason) {
    return checkEnum(obj, key, path,
                     {"top-left", "top-center", "top-right", "middle-left", "center",
                      "middle-right", "bottom-left", "bottom-center", "bottom-right"}, reason);
}

// 数组元素逐个校验：null 数组→放行；非数组由调用方先行 checkArray；逐元素调 visitor(elem, path[i])。
template <class Fn>
bool validateNumberArray(const json& arr, const std::string& path, std::string& reason, Fn&& visitElem) {
    if (arr.is_null()) return true;
    for (std::size_t i = 0; i < arr.size(); ++i) {
        if (!visitElem(arr[i], path + "[" + std::to_string(i) + "]", reason)) return false;
    }
    return true;
}

// PatternParams（配方图案）键集校验：顶层 generate 段、recipe 子对象、backdrop 子对象共用。
bool validatePatternParams(const json& o, const std::string& path, std::string& reason) {
    if (!checkEnum(o, keys::common::kKind, path + "." + keys::common::kKind,
                   {defs::kPatternKindSolid, "linear-gradient", "radial-gradient", "checkerboard",
                    "stripes", "grid-lines", "dot-grid", "concentric", "radial-rays", "hue-gradient", "noise"}, reason)) return false;
    if (!checkColor(o, keys::layer::kBackground, path + "." + keys::layer::kBackground, reason)) return false;
    if (!checkColor(o, keys::layer::kForeground, path + "." + keys::layer::kForeground, reason)) return false;
    if (!checkEnum(o, keys::layer::kOrientation, path + "." + keys::layer::kOrientation,
                   {defs::kPatternOrientation, "vertical", "diagonal-down", "diagonal-up"}, reason)) return false;
    if (!checkEnum(o, keys::layer::kNoiseMode, path + "." + keys::layer::kNoiseMode,
                   {defs::kNoiseModeFullColor, "grayscale", "two-color"}, reason)) return false;
    if (!checkInt(o, keys::layer::kPeriod, path + "." + keys::layer::kPeriod, reason)) return false;
    if (!checkInt(o, keys::layer::kLineWidth, path + "." + keys::layer::kLineWidth, reason)) return false;
    if (!checkInt(o, keys::layer::kDotRadius, path + "." + keys::layer::kDotRadius, reason)) return false;
    if (!checkInt(o, keys::layer::kSeed, path + "." + keys::layer::kSeed, reason)) return false;
    return true;
}

// 预处理步：{op, ...params}。op 未知会被 preprocessFromJson 静默丢弃（present-but-invalid），故必拦；
//   各参数键仅在对应 op 出现，统一按类型校验（mask 串 / deg 数 / axis 枚举 / width·height 数）。
bool validatePreprocessStep(const json& sv, const std::string& path, std::string& reason) {
    if (!sv.is_object()) { reason = IDC_KTRF(msg, kErrConfigNotObject, path); return false; }
    if (!checkEnum(sv, keys::pre::kOp, path + "." + keys::pre::kOp,
                   {"gray", "split", "invert", "rotate", "flip", "resize"}, reason)) return false;
    if (!checkString(sv, keys::pre::kMask, path + "." + keys::pre::kMask, reason)) return false;
    if (!checkInt(sv, keys::pre::kDeg, path + "." + keys::pre::kDeg, reason)) return false;
    if (!checkEnum(sv, keys::pre::kAxis, path + "." + keys::pre::kAxis,
                   {defs::kFlipAxisHorizontal, "vertical"}, reason)) return false;
    // resize 目标尺寸是几何量：可写百分比（相对 source 宽高），其余步不读这两键。
    if (!checkCoord(sv, keys::common::kWidth, path + "." + keys::common::kWidth, reason)) return false;
    if (!checkCoord(sv, keys::common::kHeight, path + "." + keys::common::kHeight, reason)) return false;
    return true;
}

bool validatePreprocessArray(const json& arr, const std::string& path, std::string& reason) {
    if (arr.is_null()) return true;
    if (!arr.is_array()) { reason = IDC_KTRF(msg, kErrConfigNotArray, path); return false; }
    for (std::size_t i = 0; i < arr.size(); ++i) {
        if (!validatePreprocessStep(arr[i], path + "[" + std::to_string(i) + "]", reason)) return false;
    }
    return true;
}

// 矩形（RectRegion）：x1,y1,x2,y2 皆为整数或百分比（缺→0；小数/类型不符→报）。
bool validateRectObj(const json& o, const std::string& path, std::string& reason) {
    if (!o.is_object()) { reason = IDC_KTRF(msg, kErrConfigNotObject, path); return false; }
    for (const char* k : {keys::common::kX1, keys::common::kY1, keys::common::kX2, keys::common::kY2}) {
        if (!checkCoord(o, k, path + "." + k, reason)) return false;
    }
    return true;
}

// 分轴定义（cut.params.x / .y）：周期（origin/cell/remainder）、分割数（count）、比例（weights）
//   三选一；键组混写即报错（严格模式，不静默取其中之一）。权重可为正数或百分比串（只取相对比例）。
bool validateAxisObj(const json& o, const std::string& path, std::string& reason) {
    if (!o.is_object()) { reason = IDC_KTRF(msg, kErrConfigNotObject, path); return false; }
    const bool hasCount = o.contains(keys::layer::kAxisCount);
    const bool hasWeights = o.contains(keys::layer::kAxisWeights);
    const bool hasPitch = o.contains(keys::layer::kAxisOrigin) || o.contains(keys::layer::kAxisCell) ||
                          o.contains(keys::layer::kRemainder);
    if ((hasCount ? 1 : 0) + (hasWeights ? 1 : 0) + (hasPitch ? 1 : 0) > 1) {
        reason = IDC_KTRF(msg, kErrConfigGridAxisMode, path);
        return false;
    }
    if (hasCount) return checkInt(o, keys::layer::kAxisCount, path + "." + keys::layer::kAxisCount, reason);
    if (hasWeights) {
        const std::string wp = path + "." + keys::layer::kAxisWeights;
        if (!checkArray(o, keys::layer::kAxisWeights, wp, reason)) return false;
        const json& arr = at(o, keys::layer::kAxisWeights);
        if (!arr.is_array()) return true;   // null → 缺省（不展开任何单元，由引擎报无网格）。
        if (arr.empty()) { reason = IDC_KTRF(msg, kErrConfigGridWeight, wp, "[]"); return false; }
        for (std::size_t i = 0; i < arr.size(); ++i) {
            const json& e = arr[i];
            bool ok = e.is_number() && e.get<double>() > 0.0;
            if (!ok && e.is_string()) {
                const auto ratio = core::parsePercent(e.get_ref<const std::string&>());
                ok = ratio.has_value() && *ratio > 0.0;
            }
            if (!ok) {
                reason = IDC_KTRF(msg, kErrConfigGridWeight, wp + "[" + std::to_string(i) + "]", e.dump());
                return false;
            }
        }
        return true;
    }
    if (!checkCoord(o, keys::layer::kAxisOrigin, path + "." + keys::layer::kAxisOrigin, reason)) return false;
    if (!checkCoord(o, keys::layer::kAxisCell, path + "." + keys::layer::kAxisCell, reason)) return false;
    return checkEnum(o, keys::layer::kRemainder, path + "." + keys::layer::kRemainder,
                     {defs::kRemainderDiscard, "keep-partial", "pad"}, reason);
}

// 网格参数：基准点与单元尺寸皆为整数或百分比（基准点可为负，只作相位；小数即报错，与图层/配方同口径）。
//   两种形态互斥：平铺四键（两轴皆周期）或分轴 x / y；混写即报错。
bool validateGridObj(const json& o, const std::string& path, std::string& reason) {
    if (!o.is_object()) { reason = IDC_KTRF(msg, kErrConfigNotObject, path); return false; }
    if (o.contains(keys::layer::kAxisX) || o.contains(keys::layer::kAxisY)) {
        const char* axisKey = o.contains(keys::layer::kAxisX) ? keys::layer::kAxisX : keys::layer::kAxisY;
        for (const char* k : {keys::layer::kOriginX, keys::layer::kOriginY, keys::config::kCellWidth,
                              keys::config::kCellHeight, keys::layer::kRemainder}) {
            if (o.contains(k)) {
                reason = IDC_KTRF(msg, kErrConfigGridMixed, path + "." + k, path + "." + axisKey);
                return false;
            }
        }
        for (const char* k : {keys::layer::kAxisX, keys::layer::kAxisY}) {
            const std::string ap = path + "." + k;
            if (!checkObject(o, k, ap, reason)) return false;
            if (const json& ax = at(o, k); ax.is_object() && !validateAxisObj(ax, ap, reason)) return false;
        }
        return true;
    }
    for (const char* k : {keys::layer::kOriginX, keys::layer::kOriginY, keys::config::kCellWidth, keys::config::kCellHeight}) {
        if (!checkCoord(o, k, path + "." + k, reason)) return false;
    }
    return checkEnum(o, keys::layer::kRemainder, path + "." + keys::layer::kRemainder,
                     {defs::kRemainderDiscard, "keep-partial", "pad"}, reason);
}

bool validateCut(const json& root, std::string& reason) {
    if (!checkObject(root, keys::config::kCut, keys::config::kCut, reason)) return false;
    const json& cut = at(root, keys::config::kCut);
    if (cut.is_null() || !cut.is_object()) return true;
    constexpr const char* p = keys::config::kCut;
    if (!checkEnum(cut, keys::config::kTier, std::string(p) + "." + keys::config::kTier,
                   {defs::kTierErase, "extract", "grid"}, reason)) return false;
    if (!checkEnum(cut, keys::config::kGenerator, std::string(p) + "." + keys::config::kGenerator,
                   {defs::kGeneratorRect, "horizontal", "vertical", "multi-rect", "grid"}, reason)) return false;
    if (!checkEnum(cut, keys::config::kPolarity, std::string(p) + "." + keys::config::kPolarity,
                   {defs::kPolarityRemove, "keep"}, reason)) return false;
    if (!checkObject(cut, keys::config::kParams, std::string(p) + "." + keys::config::kParams, reason)) return false;
    const json& params = at(cut, keys::config::kParams);
    if (params.is_null() || !params.is_object()) return true;
    // 依 generator 枚举分派（与读取一致）：非法 generator 已在上方拦截，此处枚举必为合法值。
    const CutGenerator gen = generatorFromName(jStr(at(cut, keys::config::kGenerator), defs::kGeneratorRect));
    const std::string pp = std::string(p) + "." + keys::config::kParams;
    if (gen == CutGenerator::MultiRect) {
        if (!checkArray(params, keys::config::kRects, pp + "." + keys::config::kRects, reason)) return false;
        if (const json& rs = at(params, keys::config::kRects); rs.is_array()) {
            for (std::size_t i = 0; i < rs.size(); ++i) {
                if (!validateRectObj(rs[i], pp + "." + keys::config::kRects + "[" + std::to_string(i) + "]", reason)) return false;
            }
        }
    } else if (gen == CutGenerator::Grid) {
        if (!validateGridObj(params, pp, reason)) return false;
    } else {
        if (!validateRectObj(params, pp, reason)) return false;
    }
    return true;
}

bool validateOrder(const json& root, std::string& reason) {
    if (!checkObject(root, keys::config::kOrder, keys::config::kOrder, reason)) return false;
    const json& order = at(root, keys::config::kOrder);
    if (order.is_null() || !order.is_object()) return true;
    constexpr const char* p = keys::config::kOrder;
    if (!checkArray(order, keys::config::kSelect, std::string(p) + "." + keys::config::kSelect, reason)) return false;
    if (const json& sel = at(order, keys::config::kSelect); !validateNumberArray(
            sel, std::string(p) + "." + keys::config::kSelect, reason,
            [](const json& e, const std::string& ep, std::string& er) {
                if (e.is_null() || e.is_number_integer()) return true;   // 单元序号须为整数，小数/字符串即报。
                er = IDC_KTRF(msg, kErrConfigNotInteger, ep);
                return false;
            })) return false;
    if (!checkEnum(order, keys::config::kStrategy, std::string(p) + "." + keys::config::kStrategy,
                   {defs::kStrategyRowMajor, "column-major", "custom"}, reason)) return false;
    if (!checkBool(order, keys::config::kReverse, std::string(p) + "." + keys::config::kReverse, reason)) return false;
    if (!checkBool(order, keys::config::kSnake, std::string(p) + "." + keys::config::kSnake, reason)) return false;
    return true;
}

bool validateExport(const json& root, std::string& reason) {
    if (!checkObject(root, keys::config::kExport, keys::config::kExport, reason)) return false;
    const json& ex = at(root, keys::config::kExport);
    if (ex.is_null() || !ex.is_object()) return true;
    constexpr const char* p = keys::config::kExport;
    if (!checkEnum(ex, keys::config::kMode, std::string(p) + "." + keys::config::kMode,
                   {defs::kExportModeMerged, "separate"}, reason)) return false;
    if (!checkEnum(ex, keys::config::kFormat, std::string(p) + "." + keys::config::kFormat,
                   {defs::kFormatPng, "jpeg", "jpg", "webp", "bmp"}, reason)) return false;
    if (!checkString(ex, keys::config::kNaming, std::string(p) + "." + keys::config::kNaming, reason)) return false;
    if (!checkInt(ex, keys::config::kQuality, std::string(p) + "." + keys::config::kQuality, reason)) return false;
    if (!checkBool(ex, keys::config::kKeepMetadata, std::string(p) + "." + keys::config::kKeepMetadata, reason)) return false;
    if (!checkObject(ex, keys::config::kMerge, std::string(p) + "." + keys::config::kMerge, reason)) return false;
    if (const json& mg = at(ex, keys::config::kMerge); !mg.is_null() && mg.is_object()) {
        const std::string mp = std::string(p) + "." + keys::config::kMerge;
        if (!checkEnum(mg, keys::config::kLayout, mp + "." + keys::config::kLayout,
                       {defs::kLayoutCollapse, "rearrange"}, reason)) return false;
        // 行列数只有绝对含义（整数）；单元尺寸是几何量，可写百分比（相对画布宽高）。
        for (const char* k : {keys::config::kCols, keys::config::kRows}) {
            if (!checkInt(mg, k, mp + "." + k, reason)) return false;   // null 亦容（optional int）；小数即报。
        }
        for (const char* k : {keys::config::kCellWidth, keys::config::kCellHeight}) {
            if (!checkCoord(mg, k, mp + "." + k, reason)) return false;
        }
        if (!checkColor(mg, keys::config::kPadColor, mp + "." + keys::config::kPadColor, reason)) return false;
        if (!checkObject(mg, keys::config::kArrange, mp + "." + keys::config::kArrange, reason)) return false;
        if (const json& ar = at(mg, keys::config::kArrange); !ar.is_null() && ar.is_object()) {
            const std::string ap = mp + "." + keys::config::kArrange;
            if (!checkEnum(ar, keys::config::kStrategy, ap + "." + keys::config::kStrategy,
                           {defs::kStrategyRowMajor, "column-major", "custom"}, reason)) return false;
            if (!checkBool(ar, keys::config::kReverse, ap + "." + keys::config::kReverse, reason)) return false;
            if (!checkBool(ar, keys::config::kSnake, ap + "." + keys::config::kSnake, reason)) return false;
        }
    }
    return true;
}

// 图片元素键（image 对象内：fit / anchor / pivot / x / y [+box*]）：generate 段与 layer.image 共用。
//   allowPercent：generate 的落位框恒＝画布（基准已知）故接受百分比；layer.image 的框由 boxWidth/boxHeight
//   决定、可小于画布，无统一基准 → 只收整数。allowBox：仅 layers 持久化 box*（generate 不落盘）。
bool validateImageElement(const json& o, const std::string& path, const bool allowPercent,
                          const bool allowBox, std::string& reason) {
    if (!checkEnum(o, keys::layer::kFit, path + "." + keys::layer::kFit,
                   {defs::kImageFitCrop, "stretch", "native"}, reason)) return false;
    if (!checkAnchor(o, keys::layer::kAnchor, path + "." + keys::layer::kAnchor, reason)) return false;
    if (!checkAnchor(o, keys::layer::kPivot, path + "." + keys::layer::kPivot, reason)) return false;
    const auto checkXY = [&](const char* k) {
        return allowPercent ? checkCoord(o, k, path + "." + k, reason) : checkInt(o, k, path + "." + k, reason);
    };
    if (!checkXY(keys::common::kX)) return false;
    if (!checkXY(keys::common::kY)) return false;
    if (allowBox) {
        for (const char* k : {keys::layer::kBoxWidth, keys::layer::kBoxHeight}) {
            if (!checkInt(o, k, path + "." + k, reason)) return false;
        }
    }
    if (!checkArray(o, keys::common::kPreprocess, path + "." + keys::common::kPreprocess, reason)) return false;
    if (!validatePreprocessArray(at(o, keys::common::kPreprocess), path + "." + keys::common::kPreprocess, reason)) return false;
    if (!checkIntRange(o, keys::common::kOpacity, path + "." + keys::common::kOpacity, 0, 255, reason)) return false;
    return true;
}

// 图案元素键（backdrop 对象内：图案键族 + preprocess + opacity）。
bool validateBackdropElement(const json& o, const std::string& path, std::string& reason) {
    if (!validatePatternParams(o, path, reason)) return false;
    if (!checkArray(o, keys::common::kPreprocess, path + "." + keys::common::kPreprocess, reason)) return false;
    if (!validatePreprocessArray(at(o, keys::common::kPreprocess), path + "." + keys::common::kPreprocess, reason)) return false;
    if (!checkIntRange(o, keys::common::kOpacity, path + "." + keys::common::kOpacity, 0, 255, reason)) return false;
    return true;
}

// 底图 generate 段：{ backdrop?, image?, opacity, preprocess }。
bool validateGenerateSeg(const json& gen, const std::string& path, std::string& reason) {
    if (!gen.is_object()) { reason = IDC_KTRF(msg, kErrConfigNotObject, path); return false; }
    if (!checkInt(gen, keys::common::kOpacity, path + "." + keys::common::kOpacity, reason)) return false;
    if (!checkArray(gen, keys::common::kPreprocess, path + "." + keys::common::kPreprocess, reason)) return false;
    if (!validatePreprocessArray(at(gen, keys::common::kPreprocess), path + "." + keys::common::kPreprocess, reason)) return false;
    if (!checkObject(gen, keys::layer::kBackdrop, path + "." + keys::layer::kBackdrop, reason)) return false;
    if (const json& bd = at(gen, keys::layer::kBackdrop); !bd.is_null() && bd.is_object()) {
        if (!validateBackdropElement(bd, path + "." + keys::layer::kBackdrop, reason)) return false;
    }
    if (!checkObject(gen, keys::layer::kImage, path + "." + keys::layer::kImage, reason)) return false;
    if (const json& im = at(gen, keys::layer::kImage); !im.is_null() && im.is_object()) {
        // generate 不落盘 box*：allowBox=false；落位框恒＝画布：百分比可收（allowPercent=true）。
        if (!validateImageElement(im, path + "." + keys::layer::kImage, true, false, reason)) return false;
        // file/box* 不是本段合法键（SPEC §7：底图不落路径、框恒＝画布 source）——显式写出即报因，防静默丢字段。
        for (const char* k : {keys::common::kFile, keys::layer::kBoxWidth, keys::layer::kBoxHeight}) {
            if (im.contains(k)) {
                reason = IDC_KTRF(msg, kErrConfigIllegalKey, path + "." + keys::layer::kImage + "." + k);
                return false;
            }
        }
    }
    return true;
}

} // namespace

// 单层段严格校验（见 schema_validate.h）：与整档同口径，逐键镜像 layerFromJson 的读取判据。
bool validateLayerSeg(const jsonutil::json& lv, const std::string& path, std::string& reason) {
    using namespace detail;
    if (!lv.is_object()) { reason = IDC_KTRF(msg, kErrConfigNotObject, path); return false; }
    if (!checkObject(lv, keys::layer::kImage, path + "." + keys::layer::kImage, reason)) return false;
    if (const json& im = at(lv, keys::layer::kImage); !im.is_null() && im.is_object()) {
        if (!checkString(im, keys::common::kFile, path + "." + keys::layer::kImage + "." + keys::common::kFile, reason)) return false;
        // layers 持久化 box*：allowBox=true；框可小于画布无统一基准：只收整数（allowPercent=false）。
        if (!validateImageElement(im, path + "." + keys::layer::kImage, false, true, reason)) return false;
    }
    if (!checkObject(lv, keys::layer::kBackdrop, path + "." + keys::layer::kBackdrop, reason)) return false;
    if (const json& bd = at(lv, keys::layer::kBackdrop); !bd.is_null() && bd.is_object()) {
        if (!validateBackdropElement(bd, path + "." + keys::layer::kBackdrop, reason)) return false;
    }
    if (!checkObject(lv, keys::common::kTransform, path + "." + keys::common::kTransform, reason)) return false;
    if (const json& tf = at(lv, keys::common::kTransform); !tf.is_null() && tf.is_object()) {
        const std::string tp = path + "." + keys::common::kTransform;
        if (!checkAnchor(tf, keys::layer::kInCanvasAnchor, tp + "." + keys::layer::kInCanvasAnchor, reason)) return false;
        if (!checkAnchor(tf, keys::layer::kLayerPivot, tp + "." + keys::layer::kLayerPivot, reason)) return false;
        // transform 是画布坐标：位置与尺寸均可写百分比（相对画布宽高）。
        for (const char* k : {keys::common::kX, keys::common::kY, keys::common::kWidth, keys::common::kHeight}) {
            if (!checkCoord(tf, k, tp + "." + k, reason)) return false;
        }
        // opacity：整数且 0..255（alpha 乘子上限 255，越界报错）。
        if (!checkIntRange(tf, keys::common::kOpacity, tp + "." + keys::common::kOpacity, 0, 255, reason)) return false;
    }
    if (!checkInt(lv, keys::common::kZOrder, path + "." + keys::common::kZOrder, reason)) return false;
    if (!checkArray(lv, keys::common::kPreprocess, path + "." + keys::common::kPreprocess, reason)) return false;
    if (!validatePreprocessArray(at(lv, keys::common::kPreprocess), path + "." + keys::common::kPreprocess, reason)) return false;
    return true;
}

// 整档严格校验（见 schema_validate.h）：逐段镜像 engineConfigFromJson 的读取分派。
bool validateSchema(const jsonutil::json& root, std::string& reason) {
    using namespace detail;
    // source / 顶层 opacity / preprocess / annotations 的容器类型。
    if (!checkObject(root, keys::config::kSource, keys::config::kSource, reason)) return false;
    if (const json& src = at(root, keys::config::kSource); !src.is_null() && src.is_object()) {
        if (!checkInt(src, keys::common::kWidth, std::string(keys::config::kSource) + "." + keys::common::kWidth, reason)) return false;
        if (!checkInt(src, keys::common::kHeight, std::string(keys::config::kSource) + "." + keys::common::kHeight, reason)) return false;
    }
    if (!checkInt(root, keys::common::kOpacity, keys::common::kOpacity, reason)) return false;
    if (!checkArray(root, keys::common::kPreprocess, keys::common::kPreprocess, reason)) return false;
    if (!validatePreprocessArray(at(root, keys::common::kPreprocess), keys::common::kPreprocess, reason)) return false;
    // annotations：present 且非 null 须为数组（逐条严格校验交由 annotation 模块，此处只判容器类型）。
    if (!checkArray(root, keys::config::kAnnotations, keys::config::kAnnotations, reason)) return false;
    // 各段。
    if (!validateCut(root, reason)) return false;
    if (!validateOrder(root, reason)) return false;
    if (!validateExport(root, reason)) return false;
    if (!checkObject(root, keys::config::kGenerate, keys::config::kGenerate, reason)) return false;
    if (const json& gen = at(root, keys::config::kGenerate); !gen.is_null() && gen.is_object()) {
        if (!validateGenerateSeg(gen, keys::config::kGenerate, reason)) return false;
    }
    if (!checkString(root, keys::config::kAssetsDir, keys::config::kAssetsDir, reason)) return false;
    if (!checkArray(root, keys::config::kLayers, keys::config::kLayers, reason)) return false;
    if (const json& ls = at(root, keys::config::kLayers); ls.is_array()) {
        for (std::size_t i = 0; i < ls.size(); ++i) {
            if (!validateLayerSeg(ls[i], std::string(keys::config::kLayers) + "[" + std::to_string(i) + "]", reason)) return false;
        }
    }
    return true;
}

} // namespace idc::engine
