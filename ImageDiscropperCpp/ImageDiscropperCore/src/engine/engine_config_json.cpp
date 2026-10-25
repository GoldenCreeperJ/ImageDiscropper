// ============================================================================
// 文件：src/engine/engine_config_json.cpp
// 作用：engine_config_json.h 的实现——EngineConfig 与 SPEC §7 schema JSON 的双向映射与文件存取。
// 分块依据：集中承载“配置字段 ↔ JSON 键”的语义映射（枚举串、颜色十六进制、矩形/网格
//       子对象、预处理步骤数组）；JSON 语法委托第三方库 nlohmann/json，本文件不手写解析器。
//       转换函数（engineConfigToJson / engineConfigFromJson）为文件内私有实现，仅由
//       saveEngineConfig / loadEngineConfig 调用，故 nlohmann/json 依赖不泄漏到公共头（PRIVATE）。
//       annotations 段（标注列表）的 JSON 实现归属 annotation 模块：本文件组装 / 解析整档时对
//       src/json_common.h 声明的 annotation::annotationsSegmentToJson / annotationsFromSegmentJson
//       发起跨模块调用（公共 save/load 签名仍以可选 AnnotationSet* 传递，向后兼容不变）。
// 说明：读取先过一道严格 schema 守卫（validateSchema）——「键写出且值非 null」但枚举拼错 / 颜色非法 /
//       类型不符一律拒绝（回传 CoreError.message 定位报因）；缺失 / null 仍取默认值、越界仍 clamp（向后兼容不变，
//       绝不抛异常给调用方）。写出侧严格对齐 SPEC §7 示例的键名与嵌套结构；JSON 采用 nlohmann::ordered_json
//       （保插入序），序列化按写入顺序输出。
// ============================================================================
#include "engine/engine_config_json.h"

#include "preprocess/preprocess_config.h"
#include "annotation/annotation_set.h"
#include "core/path_utf8.h" // pathFromUtf8（取代 C++23 已移除的 u8path）

#include "../json_common.h" // idc::jsonutil 助手 + 标注段交换函数声明（实现在 annotation 模块）
#include "engine_config_json_internal.h" // 枚举串/颜色/各段 toJson/fromJson 助手区（detail 命名空间）
#include "i18n/core_i18n.h"  // IDC_KTR / IDC_KTRF：严格校验定位报因取译文（缺译文回退常量层）

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

namespace idc::engine {
namespace {
using namespace detail;   // 枚举串与各段 JSON 助手（engine_config_json_internal.h）

bool preprocessFromJson(const json& v, preprocess::PreprocessConfig& out) {
    using namespace preprocess;
    const std::string op = jStr(at(v, keys::pre::kOp), "");
    if (op == "gray") { out = GrayOp{}; return true; }
    if (op == "split") { SplitOp s; s.mask = jStr(at(v, keys::pre::kMask), defs::kMaskAllOn); out = s; return true; }
    if (op == "invert") { InvertOp s; s.mask = jStr(at(v, keys::pre::kMask), defs::kMaskAllOn); out = s; return true; }
    if (op == "rotate") { RotateOp s; s.angle = jInt(at(v, keys::pre::kDeg), 0); out = s; return true; }
    if (op == "flip") { FlipOp s; s.horizontal = jStr(at(v, keys::pre::kAxis), defs::kFlipAxisHorizontal) == defs::kFlipAxisHorizontal; out = s; return true; }
    if (op == "resize") { ResizeOp s; s.width = jInt(at(v, keys::common::kWidth), 0); s.height = jInt(at(v, keys::common::kHeight), 0); out = s; return true; }
    // opacity 不是预处理步（不透明度由图层 transform.opacity 承载）：未知 op 返回 false，调用方跳过。
    return false;
}

// ---------------------------------------------------------------------------
// std::vector<canvas::ImageLayer> ↔ SPEC §7 layers 段（可选，直接以数组承载）。每层：{ source, transform, zOrder, preprocess }。
//   source：{ recipe: {...PatternParams...} }（配方源）| { file: "相对名" }（文件源）| {}（空占位）。
//   来源纯由 source 携 recipe 对象 / file 名派生；decodedPixels 不入配置。
//   transform：{ inCanvasAnchor, layerPivot, x, y, width, height }。
// ---------------------------------------------------------------------------
json layerToJson(const canvas::ImageLayer& layer) {
    json o;
    json source;
    if (layer.recipe)
        source[keys::layer::kRecipe] = patternParamsToJson(*layer.recipe);   // 配方源：写 recipe 对象。
    else if (!layer.fileName.empty())
        source[keys::common::kFile] = layer.fileName;   // 文件源：仅存相对名；像素不入配置（缺失时上层降级为占位）。
    o[keys::config::kSource] = source;   // 二者皆无 → 空 source（空占位）。

    json t;
    t[keys::layer::kInCanvasAnchor] = genAnchorToken(layer.transform.inCanvasAnchor);
    t[keys::layer::kLayerPivot] = genAnchorToken(layer.transform.layerPivot);
    t[keys::common::kX] = layer.transform.x;
    t[keys::common::kY] = layer.transform.y;
    t[keys::common::kWidth] = layer.transform.width;
    t[keys::common::kHeight] = layer.transform.height;
    t[keys::common::kOpacity] = layer.transform.opacity;   // 合成期 alpha 乘子（0..255）。
    o[keys::common::kTransform] = t;

    o[keys::common::kZOrder] = layer.zOrder;

    // 内层两级落位（仅启用时写入；缺省段读取端默认 hasImagePlacement=false）。
    if (layer.hasImagePlacement) {
        json ip;
        ip[keys::layer::kFit] = imageFitToken(layer.imageFit);
        ip[keys::layer::kBoxWidth] = layer.boxWidth;
        ip[keys::layer::kBoxHeight] = layer.boxHeight;
        ip[keys::layer::kImageAnchor] = genAnchorToken(layer.imageAnchor);
        ip[keys::layer::kImagePivot] = genAnchorToken(layer.imagePivot);
        ip[keys::common::kX] = layer.imageX;
        ip[keys::common::kY] = layer.imageY;
        if (layer.boxBackdrop) ip[keys::layer::kBackdrop] = patternParamsToJson(*layer.boxBackdrop);
        o[keys::layer::kImagePlacement] = ip;
    }

    json pre = json::array();
    for (const preprocess::PreprocessConfig& step : layer.preprocess.items())
        pre.push_back(preprocessToJson(step));
    o[keys::common::kPreprocess] = pre;
    return o;
}

canvas::ImageLayer layerFromJson(const json& v) {
    canvas::ImageLayer layer;
    const json& source = at(v, keys::config::kSource);
    // 来源纯由 source 携的 recipe 对象 / file 名派生：
    if (const json& r = at(source, keys::layer::kRecipe); r.is_object()) {
        layer.recipe = patternParamsFromJson(r);   // recipe 对象 → 配方源
    } else {
        layer.fileName = jStr(at(source, keys::common::kFile), "");   // file 名 → 文件源（缺则空＝空占位）
    }

    const json& t = at(v, keys::common::kTransform);
    layer.transform.inCanvasAnchor = genAnchorFromToken(jStr(at(t, keys::layer::kInCanvasAnchor), defs::kAnchorCenter));
    layer.transform.layerPivot = genAnchorFromToken(jStr(at(t, keys::layer::kLayerPivot), defs::kAnchorCenter));
    layer.transform.x = jInt(at(t, keys::common::kX), 0);
    layer.transform.y = jInt(at(t, keys::common::kY), 0);
    layer.transform.width = jInt(at(t, keys::common::kWidth), 0);
    layer.transform.height = jInt(at(t, keys::common::kHeight), 0);
    layer.transform.opacity = std::clamp(jInt(at(t, keys::common::kOpacity), nums::engine::kDefaultOpacity), 0, 255);

    layer.zOrder = jInt(at(v, keys::common::kZOrder), 0);

    // 内层两级落位（可选段）：存在则启用 hasImagePlacement，缺失保持关闭。
    if (v.contains(keys::layer::kImagePlacement)) {
        const json& ip = v[keys::layer::kImagePlacement];
        layer.hasImagePlacement = true;
        layer.imageFit = imageFitFromToken(jStr(at(ip, keys::layer::kFit), defs::kImageFitCrop));
        layer.boxWidth = jInt(at(ip, keys::layer::kBoxWidth), 0);
        layer.boxHeight = jInt(at(ip, keys::layer::kBoxHeight), 0);
        layer.imageAnchor = genAnchorFromToken(jStr(at(ip, keys::layer::kImageAnchor), defs::kAnchorCenter));
        layer.imagePivot = genAnchorFromToken(jStr(at(ip, keys::layer::kImagePivot), defs::kAnchorCenter));
        layer.imageX = jInt(at(ip, keys::common::kX), 0);
        layer.imageY = jInt(at(ip, keys::common::kY), 0);
        if (ip.contains(keys::layer::kBackdrop)) layer.boxBackdrop = patternParamsFromJson(ip[keys::layer::kBackdrop]);
    }

    if (const json& pre = at(v, keys::common::kPreprocess); pre.is_array()) {
        for (const json& step : pre) {
            if (preprocess::PreprocessConfig cfg; preprocessFromJson(step, cfg)) layer.preprocess.add(cfg);
        }
    }
    return layer;
}

// ---------------------------------------------------------------------------
// 底图 canvas::ImageLayer ↔ SPEC §7「generate」段（同构序列化）。
//   键名仍叫 "generate"（不破外部/CLI 选项名）。底图不外传文件：来源恒为程序图案（recipe）或
//   运行时 --input 提供的位图。纯图案底图（有 recipe、无内层落位）仅写图案键；带落位（把外部输入图 /
//   图案按落位摆入画布）在图案键之外追加 fit/imageAnchor/imagePivot/x/y/backdrop 键——底图落位框尺寸恒＝
//   画布(source)、属冗余派生值，**不落盘**（读取端据 source 回填，见 engineConfigFromJson）——
//   **不写 file 路径**：保配置为纯处理配方、可跨不同 --input 批处理复用（路径一旦入配置会使底图被钉死、
//   批处理结果恒同）。位图恒不入配置。底图钉住最底：transform 恒等、zOrder 固定、不入本段（读取端复位默认）。
// ---------------------------------------------------------------------------
json baseToJson(const canvas::ImageLayer& base) {
    json gen;
    // 图案键（仅当有配方）：无配方（纯落位 / 外部图底）→ 不写顶层图案键。
    if (base.recipe) gen.update(patternParamsToJson(*base.recipe));
    // 底图整体不透明度（合成期 alpha 乘子 0..255）：随段写入 opacity 键，供 GUI 载入回填 baseOpacity、
    //   CLI 在边界施加淡出。
    gen[keys::common::kOpacity] = base.transform.opacity;
    // 底图色彩预处理步（仅色彩合法：gray/invert/split；几何/resize 会改画布尺寸，写出时过滤掉）。
    //   缺省空 → 不写 preprocess 键。
    json pre = json::array();
    for (const preprocess::PreprocessConfig& step : base.preprocess.items())
        if (preprocess::isColorStep(step)) pre.push_back(preprocessToJson(step));
    if (!pre.empty()) gen[keys::common::kPreprocess] = pre;
    // 内层落位（把外部输入图 / 图案按落位摆入画布时启用）：追加 fit/image* + backdrop 图案；不写文件路径。
    //   boxWidth/boxHeight 不写：底图落位框恒＝画布(source)，由读取端据 source 回填，避免与 source 冗余双存。
    if (base.hasImagePlacement) {
        gen[keys::layer::kFit] = imageFitToken(base.imageFit);
        gen[keys::layer::kImageAnchor] = genAnchorToken(base.imageAnchor);
        gen[keys::layer::kImagePivot] = genAnchorToken(base.imagePivot);
        gen[keys::common::kX] = base.imageX;
        gen[keys::common::kY] = base.imageY;
        if (base.boxBackdrop) gen[keys::layer::kBackdrop] = patternParamsToJson(*base.boxBackdrop);
    }
    return gen;
}
canvas::ImageLayer baseFromJson(const json& gen) {
    canvas::ImageLayer base;   // transform 恒等、zOrder=0（底位）：均为 ImageLayer 默认。
    // 底图整体不透明度：读 opacity（缺省 255）→ transform.opacity；上层（GUI Document / CLI config）据此在合成边界施加淡出。
    base.transform.opacity = std::clamp(jInt(at(gen, keys::common::kOpacity), nums::engine::kDefaultOpacity), 0, 255);
    // 底图色彩预处理（可选 preprocess 数组）：仅接受色彩步（gray/invert/split），几何/resize 步忽略
    //   （底图预处理不改画布尺寸 / 坐标基准；还原端遇非法几何步静默丢弃）。
    if (const json& preArr = at(gen, keys::common::kPreprocess); preArr.is_array()) {
        for (const json& step : preArr) {
            if (preprocess::PreprocessConfig cfg; preprocessFromJson(step, cfg) && preprocess::isColorStep(cfg)) base.preprocess.add(cfg);
        }
    }
    // 图案键存在（含图案 "kind"）→ 解析配方；否则 recipe 留空（底图为运行时 --input 位图，仅存落位参数）。
    if (gen.contains(keys::common::kKind)) {
        base.recipe = patternParamsFromJson(gen);
    }
    // 内层落位（存在 fit 或 imageAnchor 键即启用；box 尺寸不落盘，由 engineConfigFromJson 据 source 回填）。
    if (gen.contains(keys::layer::kFit) || gen.contains(keys::layer::kImageAnchor)) {
        base.hasImagePlacement = true;
        base.imageFit = imageFitFromToken(jStr(at(gen, keys::layer::kFit), defs::kImageFitCrop));
        base.imageAnchor = genAnchorFromToken(jStr(at(gen, keys::layer::kImageAnchor), defs::kAnchorCenter));
        base.imagePivot = genAnchorFromToken(jStr(at(gen, keys::layer::kImagePivot), defs::kAnchorCenter));
        base.imageX = jInt(at(gen, keys::common::kX), 0);
        base.imageY = jInt(at(gen, keys::common::kY), 0);
        if (gen.contains(keys::layer::kBackdrop)) base.boxBackdrop = patternParamsFromJson(gen[keys::layer::kBackdrop]);
    }
    return base;
}

// ---------------------------------------------------------------------------
// EngineConfig ↔ SPEC §7 schema JSON（文件内私有；仅由下方 save/load 调用）。
// ---------------------------------------------------------------------------
// 把 EngineConfig 序列化为 SPEC §7 schema 的 JSON 值；annoSet 非空且含标注时额外写 annotations 段。
json engineConfigToJson(const EngineConfig& config, const annotation::AnnotationSet* annoSet) {
    // 顶层落盘段序（ordered_json 保插入序，写入顺序即输出顺序）：
    //   source → generate → layers → preprocess → opacity → annotations → cut → order（含 select）→ export。
    json root;

    // source：原图尺寸。
    root[keys::config::kSource] = json{{keys::common::kWidth, config.source.width}, {keys::common::kHeight, config.source.height}};

    // generate：底图描述（可选段）——缺省省略（表示用外部输入图，非生成底图）。
    //   底图＝同构 canvas::ImageLayer：纯图案底图（仅配方、无落位）序列化为纯 recipe 形态的 generate 段；带落位时追加
    //   fit/box*/image*/backdrop 键（**不含 file 路径**，外部图凭运行时 --input 供给以保批处理复用）。尺寸恒取 source.width/height。
    if (config.base.has_value())
        root[keys::config::kGenerate] = baseToJson(*config.base);

    // assetsDir：资产目录声明（可选键）——非空才写（空＝未声明，加载端走纯约定查找）。
    if (!config.assetsDir.empty())
        root[keys::config::kAssetsDir] = config.assetsDir;

    // layers：多层画布（可选段）——缺省省略（表示单一输入图/生成源，无图层）。直接以数组承载。
    if (config.layers.has_value()) {
        json arr = json::array();
        for (const canvas::ImageLayer& layer : *config.layers) arr.push_back(layerToJson(layer));
        root[keys::config::kLayers] = arr;
    }

    // preprocess：基础图像处理步骤数组。
    json pre = json::array();
    for (const preprocess::PreprocessConfig& step : config.preprocess.items())
        pre.push_back(preprocessToJson(step));
    root[keys::common::kPreprocess] = pre;

    // opacity：画布级整体不透明度（顶层可选键，0..255，255＝不透明）。缺省 255 时仍写出（观感一致、无歧义）。
    root[keys::common::kOpacity] = config.canvasOpacity;

    // annotations：标注列表（可选段）——集合为空或无标注时省略（输出不含 annotations 键）。
    // 段内逐条序列化实现归属 annotation 模块（经 json_common.h 交换 JSON 片段）。
    if (annoSet != nullptr && !annoSet->annotations().empty())
        root[keys::config::kAnnotations] = annotation::annotationsSegmentToJson(*annoSet);

    // cut：层级 / 生成器 / 几何参数 / 极性。
    json cut;
    cut[keys::config::kTier] = tierName(config.cut.tier);
    cut[keys::config::kGenerator] = generatorName(config.cut.generator);
    cut[keys::config::kPolarity] = polarityName(config.cut.polarity);
    if (config.cut.generator == CutGenerator::MultiRect) {
        json rs = json::array();
        for (const RectRegion& r : config.cut.rects) rs.push_back(rectToJson(r));
        cut[keys::config::kParams] = json{{keys::config::kRects, rs}};
    } else if (config.cut.generator == CutGenerator::Grid) {
        cut[keys::config::kParams] = gridToJson(config.cut.grid);
    } else {
        cut[keys::config::kParams] = rectToJson(config.cut.rect);
    }
    root[keys::config::kCut] = cut;

    // order：L3 显式选择集 select（std::vector<int> 直接映射为 JSON 数组；strategy=custom 时数组序即自定义输出序）
    //        + 排序策略 strategy/reverse/snake。
    json order;
    order[keys::config::kSelect] = config.selectedCells;
    order[keys::config::kStrategy] = strategyName(config.order.strategy);
    order[keys::config::kReverse] = config.order.reverse;
    order[keys::config::kSnake] = config.order.snake;
    root[keys::config::kOrder] = order;

    // export：输出模式 / 合并布局 / 格式 / 命名 / 质量 / 元数据。
    json merge;
    merge[keys::config::kLayout] = layoutName(config.exportParams.layout);
    merge[keys::config::kCols] = optIntToJson(config.exportParams.cols);
    merge[keys::config::kRows] = optIntToJson(config.exportParams.rows);
    merge[keys::config::kCellWidth] = optIntToJson(config.exportParams.cellWidth);
    merge[keys::config::kCellHeight] = optIntToJson(config.exportParams.cellHeight);
    merge[keys::config::kPadColor] = colorToHex(config.exportParams.padColor);
    // arrange：重排填充顺序（仅 Rearrange 用；与顶层 order 选择排序正交）。
    json arrange;
    arrange[keys::config::kStrategy] = strategyName(config.exportParams.mergeOrder.strategy);
    arrange[keys::config::kReverse] = config.exportParams.mergeOrder.reverse;
    arrange[keys::config::kSnake] = config.exportParams.mergeOrder.snake;
    merge[keys::config::kArrange] = arrange;
    json exportSeg;
    exportSeg[keys::config::kMode] = exportModeName(config.exportParams.mode);
    exportSeg[keys::config::kMerge] = merge;
    exportSeg[keys::config::kFormat] = formatName(config.exportParams.format);
    exportSeg[keys::config::kNaming] = config.exportParams.naming;
    exportSeg[keys::config::kQuality] = config.exportParams.quality;
    exportSeg[keys::config::kKeepMetadata] = config.exportParams.keepMetadata;
    root[keys::config::kExport] = exportSeg;

    return root;
}

// ---------------------------------------------------------------------------
// §7 配置 schema 严格校验（读侧前置守卫）：仅拦「键显式写出且值非 null」而类型/枚举/颜色非法的
//   present-but-invalid 情形——这类值若静默落默认值，会产出与意图相左却 exit 0 的结果
//   （如 "mode":"sepreate"→merged），故一律报错。缺失 / null 仍走宽容路径填默认（向后兼容不变）；
//   所有数值键一律整数（浮点即报 kErrConfigNotInteger，不静默截断）；例外是「有基准尺寸的几何坐标」
//   （矩形 / 网格 / 合并单元尺寸 / 图层 transform / generate 落位 / resize 目标尺寸），它们另接受
//   百分比串 "50%"（见 checkCoord 与 resolvePercentCoords）；数值越界仅图层 opacity（0..255）由
//   validateLayerSeg 拦截，其余越界数值仍由读侧 clamp。
//   合法别名（format "jpg"）与各枚举默认 token
//   （tier "erase"、generator "rect"…）一律放行——只有真正拼错才报。逐段镜像 engineConfigFromJson
//   的读取分派，保证「校验判据 == 读取判据」不漂移。首处失败即停，reason 携 JSON 路径定位。
// ---------------------------------------------------------------------------
// 叶子取值助手：键缺失或值为 null → 放行（交默认）；present 且类型/取值不符 → 写定位报因、返回 false。
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
//   尺寸 / 图层 transform / generate 落位 / resize 目标尺寸。百分比在解析前由 resolvePercentCoords
//   就地换算为像素，故各段解析器始终只见整数；其余数值键仍走 checkInt。
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

// 底图内层落位键（fit / imageAnchor / imagePivot / x / y）：generate 段与 layer.imagePlacement 段共用。
//   allowPercent：generate 的落位框恒＝画布（基准已知）故接受百分比；layer.imagePlacement 的框由
//   boxWidth/boxHeight 决定、可小于画布，无统一基准 → 只收整数。
bool validatePlacementKeys(const json& o, const std::string& path, const bool allowPercent, std::string& reason) {
    if (!checkEnum(o, keys::layer::kFit, path + "." + keys::layer::kFit,
                   {defs::kImageFitCrop, "stretch", "native"}, reason)) return false;
    if (!checkAnchor(o, keys::layer::kImageAnchor, path + "." + keys::layer::kImageAnchor, reason)) return false;
    if (!checkAnchor(o, keys::layer::kImagePivot, path + "." + keys::layer::kImagePivot, reason)) return false;
    const auto checkXY = [&](const char* k) {
        return allowPercent ? checkCoord(o, k, path + "." + k, reason) : checkInt(o, k, path + "." + k, reason);
    };
    if (!checkXY(keys::common::kX)) return false;
    if (!checkXY(keys::common::kY)) return false;
    if (!checkObject(o, keys::layer::kBackdrop, path + "." + keys::layer::kBackdrop, reason)) return false;
    if (const json& bk = at(o, keys::layer::kBackdrop); !bk.is_null() && bk.is_object()) {
        if (!validatePatternParams(bk, path + "." + keys::layer::kBackdrop, reason)) return false;
    }
    return true;
}

// 底图 generate 段：含 kind → 配方（校验 patternParams）；placement 键与 preprocess（仅色彩步，但仍按类型/取值校验）。
bool validateGenerateSeg(const json& gen, const std::string& path, std::string& reason) {
    if (!gen.is_object()) { reason = IDC_KTRF(msg, kErrConfigNotObject, path); return false; }
    if (gen.contains(keys::common::kKind) && !gen.at(keys::common::kKind).is_null()) {
        if (!validatePatternParams(gen, path, reason)) return false;
    }
    if (!checkInt(gen, keys::common::kOpacity, path + "." + keys::common::kOpacity, reason)) return false;
    if (!validatePlacementKeys(gen, path, true, reason)) return false;
    if (!checkArray(gen, keys::common::kPreprocess, path + "." + keys::common::kPreprocess, reason)) return false;
    if (!validatePreprocessArray(at(gen, keys::common::kPreprocess), path + "." + keys::common::kPreprocess, reason)) return false;
    return true;
}

bool validateLayerSeg(const json& lv, const std::string& path, std::string& reason) {
    if (!lv.is_object()) { reason = IDC_KTRF(msg, kErrConfigNotObject, path); return false; }
    if (!checkObject(lv, keys::config::kSource, path + "." + keys::config::kSource, reason)) return false;
    if (const json& src = at(lv, keys::config::kSource); !src.is_null() && src.is_object()) {
        if (const json& r = at(src, keys::layer::kRecipe); src.contains(keys::layer::kRecipe) && !r.is_null()) {
            if (!r.is_object()) { reason = IDC_KTRF(msg, kErrConfigNotObject, path + "." + keys::config::kSource + "." + keys::layer::kRecipe); return false; }
            if (!validatePatternParams(r, path + "." + keys::config::kSource + "." + keys::layer::kRecipe, reason)) return false;
        } else if (!checkString(src, keys::common::kFile, path + "." + keys::config::kSource + "." + keys::common::kFile, reason)) {
            return false;
        }
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
    if (!checkObject(lv, keys::layer::kImagePlacement, path + "." + keys::layer::kImagePlacement, reason)) return false;
    if (const json& ip = at(lv, keys::layer::kImagePlacement); !ip.is_null() && ip.is_object()) {
        const std::string ipp = path + "." + keys::layer::kImagePlacement;
        if (!validatePlacementKeys(ip, ipp, false, reason)) return false;
        for (const char* k : {keys::layer::kBoxWidth, keys::layer::kBoxHeight}) {
            if (!checkInt(ip, k, ipp + "." + k, reason)) return false;
        }
    }
    if (!checkArray(lv, keys::common::kPreprocess, path + "." + keys::common::kPreprocess, reason)) return false;
    if (!validatePreprocessArray(at(lv, keys::common::kPreprocess), path + "." + keys::common::kPreprocess, reason)) return false;
    return true;
}

bool validateSchema(const json& root, std::string& reason) {
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

// ---------------------------------------------------------------------------
// 百分比坐标的读侧预归一化（"50%" → 像素）：schema 守卫通过后、各段解析器运行前，把白名单几何键上的
//   百分比串就地换成整数像素，令 rectFromJson / layerFromJson / annotation 段解析器始终只见整数。
//   基准＝该字段所在坐标系的尺寸：cut / export.merge / generate / layers.transform / resize 目标尺寸取
//   source（画布）宽高；annotations 取工作图尺寸（source 经 preprocess 几何步推导）。
//   每处换算记一条 PercentHint（JSON Pointer + 比例 + 像素），保存时据此原样回写 "N%"。
// ---------------------------------------------------------------------------
struct BoxSize { int width{0}; int height{0}; };

// 可变子节点：键缺失或值为 null → nullptr（jsonutil::at 返回 const 引用，无法用于就地替换）。
json* mutableAt(json& obj, const char* key) {
    if (!obj.is_object()) return nullptr;
    const auto it = obj.find(key);
    return it == obj.end() || it->is_null() ? nullptr : &*it;
}

// JSON Pointer 片段追加（"/cut" + "/params" → "/cut/params"）：与 engineConfigToJson 的输出结构一一对应。
std::string joinPtr(const std::string& base, const std::string& leaf) { return base + "/" + leaf; }

// 单个坐标叶子：整数 / null / 缺失不动；"N%" → 依 basis 四舍五入为像素并记账。
//   path＝点分路径（报因定位），ptr＝JSON Pointer（保存回写），二者描述同一字段。
bool resolveLeaf(json& node, const int basis, const std::string& path, const std::string& ptr,
                 core::PercentHints& hints, std::string& err) {
    if (!node.is_string()) return true;
    const auto ratio = core::parsePercent(node.get_ref<const std::string&>());
    if (!ratio) { err = IDC_KTRF(msg, kErrConfigNotCoord, path, node.dump()); return false; }
    if (basis <= 0) { err = IDC_KTRF(msg, kErrConfigPercentNoBasis, path); return false; }
    const int px = core::resolvePercent(*ratio, basis);
    hints.push_back(core::PercentHint{ptr, *ratio, static_cast<double>(px)});
    node = px;
    return true;
}

bool resolveCoord(json& obj, const char* key, const int basis, const std::string& path, const std::string& ptr,
                  core::PercentHints& hints, std::string& err) {
    json* node = mutableAt(obj, key);
    return node == nullptr || resolveLeaf(*node, basis, path, ptr, hints, err);
}

// 数组元素（按索引）的百分比换算：points / segments[].coords 用。
bool resolveIndexed(json& arr, const std::size_t idx, const int basis, const std::string& path, const std::string& ptr,
                    core::PercentHints& hints, std::string& err) {
    if (!arr.is_array() || idx >= arr.size()) return true;
    return resolveLeaf(arr[idx], basis, path, ptr, hints, err);
}

// 矩形四键：x 轴取宽、y 轴取高（缺失键跳过，故对参数对象同样适用）。
bool resolveRectCoords(json& o, const BoxSize& box, const std::string& path, const std::string& ptr,
                       core::PercentHints& hints, std::string& err) {
    if (!o.is_object()) return true;
    const auto one = [&](const char* k, const int basis) {
        return resolveCoord(o, k, basis, path + "." + k, joinPtr(ptr, k), hints, err);
    };
    return one(keys::common::kX1, box.width) && one(keys::common::kY1, box.height)
        && one(keys::common::kX2, box.width) && one(keys::common::kY2, box.height);
}

// 网格参数：平铺形态四键（基准点与单元宽取宽轴、高取高轴）；分轴形态按 x / y 子对象逐轴换算
//   （x 轴取宽、y 轴取高）。weights 是相对比例而非坐标，不参与百分比→像素换算。
bool resolveGridCoords(json& o, const BoxSize& box, const std::string& path, const std::string& ptr,
                       core::PercentHints& hints, std::string& err) {
    if (!o.is_object()) return true;
    const auto one = [&](const char* k, const int basis) {
        return resolveCoord(o, k, basis, path + "." + k, joinPtr(ptr, k), hints, err);
    };
    if (json* ax = mutableAt(o, keys::layer::kAxisX); ax != nullptr && ax->is_object()) {
        const std::string ap = path + "." + keys::layer::kAxisX;
        const std::string apt = joinPtr(ptr, keys::layer::kAxisX);
        if (!resolveCoord(*ax, keys::layer::kAxisOrigin, box.width, ap + "." + keys::layer::kAxisOrigin,
                          joinPtr(apt, keys::layer::kAxisOrigin), hints, err)) return false;
        if (!resolveCoord(*ax, keys::layer::kAxisCell, box.width, ap + "." + keys::layer::kAxisCell,
                          joinPtr(apt, keys::layer::kAxisCell), hints, err)) return false;
    }
    if (json* ay = mutableAt(o, keys::layer::kAxisY); ay != nullptr && ay->is_object()) {
        const std::string ap = path + "." + keys::layer::kAxisY;
        const std::string apt = joinPtr(ptr, keys::layer::kAxisY);
        if (!resolveCoord(*ay, keys::layer::kAxisOrigin, box.height, ap + "." + keys::layer::kAxisOrigin,
                          joinPtr(apt, keys::layer::kAxisOrigin), hints, err)) return false;
        if (!resolveCoord(*ay, keys::layer::kAxisCell, box.height, ap + "." + keys::layer::kAxisCell,
                          joinPtr(apt, keys::layer::kAxisCell), hints, err)) return false;
    }
    return one(keys::layer::kOriginX, box.width) && one(keys::layer::kOriginY, box.height)
        && one(keys::config::kCellWidth, box.width) && one(keys::config::kCellHeight, box.height);
}

// 工作图尺寸（annotations 的坐标基准）：source 尺寸经 preprocess 几何步推导——rotate 90/270 交换宽高；
//   resize 正尺寸覆盖（非正＝保持原尺寸，与 preprocess 单步语义一致）；颜色步（gray/split/invert/flip）不改尺寸。
BoxSize workSizeOf(const json& root) {
    const json& src = at(root, keys::config::kSource);
    BoxSize box{jInt(at(src, keys::common::kWidth), 0), jInt(at(src, keys::common::kHeight), 0)};
    const json& pre = at(root, keys::common::kPreprocess);
    if (!pre.is_array()) return box;
    for (const json& step : pre) {
        if (const std::string op = jStr(at(step, keys::pre::kOp), ""); op == "rotate") {
            if (const int deg = (jInt(at(step, keys::pre::kDeg), 0) % 360 + 360) % 360; deg == 90 || deg == 270) std::swap(box.width, box.height);
        } else if (op == "resize") {
            const int w = jInt(at(step, keys::common::kWidth), 0);
            const int h = jInt(at(step, keys::common::kHeight), 0);
            if (w > 0) box.width = w;
            if (h > 0) box.height = h;
        }
    }
    return box;
}

// 标注几何：x/y（文字落点）、x1..y2（锚点图元 / 圆 / 椭圆）、points（直线 / 折线顶点）、
//   segments[].coords（path 控制点，偶索引 x、奇索引 y）。只动这四处已知几何键——样式与 transform
//   快照（matrix / scale / rotation）是相对量，不作坐标换算。
bool resolveAnnotationCoords(json& a, const BoxSize& box, const std::string& path, const std::string& ptr,
                             core::PercentHints& hints, std::string& err) {
    if (!a.is_object()) return true;
    if (!resolveCoord(a, keys::common::kX, box.width, path + "." + keys::common::kX, joinPtr(ptr, keys::common::kX), hints, err)) return false;
    if (!resolveCoord(a, keys::common::kY, box.height, path + "." + keys::common::kY, joinPtr(ptr, keys::common::kY), hints, err)) return false;
    if (!resolveRectCoords(a, box, path, ptr, hints, err)) return false;
    if (json* pts = mutableAt(a, keys::common::kPoints); pts != nullptr && pts->is_array()) {
        for (std::size_t i = 0; i < pts->size(); ++i) {
            json& p = (*pts)[i];
            if (!p.is_array()) continue;
            const std::string ip = std::to_string(i);
            const std::string ppath = path + "." + keys::common::kPoints + "[" + ip + "]";
            const std::string pptr = joinPtr(joinPtr(ptr, keys::common::kPoints), ip);
            if (!resolveIndexed(p, 0, box.width, ppath + "[0]", joinPtr(pptr, "0"), hints, err)) return false;
            if (!resolveIndexed(p, 1, box.height, ppath + "[1]", joinPtr(pptr, "1"), hints, err)) return false;
        }
    }
    if (json* segs = mutableAt(a, keys::anno::kSegments); segs != nullptr && segs->is_array()) {
        for (std::size_t i = 0; i < segs->size(); ++i) {
            json* coords = mutableAt((*segs)[i], keys::anno::kCoords);
            if (coords == nullptr || !coords->is_array()) continue;
            const std::string ip = std::to_string(i);
            const std::string spath = path + "." + keys::anno::kSegments + "[" + ip + "]." + keys::anno::kCoords;
            const std::string sptr = joinPtr(joinPtr(joinPtr(ptr, keys::anno::kSegments), ip), keys::anno::kCoords);
            for (std::size_t j = 0; j < coords->size(); ++j) {
                const std::string jp = std::to_string(j);
                if (!resolveIndexed(*coords, j, j % 2 == 0 ? box.width : box.height,
                                    spath + "[" + jp + "]", joinPtr(sptr, jp), hints, err)) return false;
            }
        }
    }
    return true;
}

// 全档预归一化入口：按 schema 段序逐段换算百分比坐标。annotations 置于 preprocess 之后——
//   其基准需先拿到 resize 归一后的工作图尺寸。
bool resolvePercentCoords(json& root, core::PercentHints& hints, std::string& err) {
    const json& src = at(root, keys::config::kSource);
    const BoxSize canvas{jInt(at(src, keys::common::kWidth), 0), jInt(at(src, keys::common::kHeight), 0)};

    // cut：params 依 generator 分派（rect / multi-rect / grid），基准＝画布。
    if (json* cut = mutableAt(root, keys::config::kCut)) {
        const CutGenerator gen = generatorFromName(jStr(at(*cut, keys::config::kGenerator), defs::kGeneratorRect));
        if (json* params = mutableAt(*cut, keys::config::kParams)) {
            const std::string pp = std::string(keys::config::kCut) + "." + keys::config::kParams;
            const std::string ppt = joinPtr(joinPtr("", keys::config::kCut), keys::config::kParams);
            if (gen == CutGenerator::MultiRect) {
                if (json* rs = mutableAt(*params, keys::config::kRects); rs != nullptr && rs->is_array()) {
                    for (std::size_t i = 0; i < rs->size(); ++i) {
                        if (const std::string n = std::to_string(i); !resolveRectCoords((*rs)[i], canvas, pp + "." + keys::config::kRects + "[" + n + "]",
                                                                                        joinPtr(joinPtr(ppt, keys::config::kRects), n), hints, err)) return false;
                    }
                }
            } else if (gen == CutGenerator::Grid) {
                if (!resolveGridCoords(*params, canvas, pp, ppt, hints, err)) return false;
            } else {
                if (!resolveRectCoords(*params, canvas, pp, ppt, hints, err)) return false;
            }
        }
    }

    // export.merge：单元尺寸可写百分比（相对画布）。
    if (json* ex = mutableAt(root, keys::config::kExport)) {
        if (json* mg = mutableAt(*ex, keys::config::kMerge)) {
            const std::string mp = std::string(keys::config::kExport) + "." + keys::config::kMerge;
            const std::string mpt = joinPtr(joinPtr("", keys::config::kExport), keys::config::kMerge);
            if (!resolveCoord(*mg, keys::config::kCellWidth, canvas.width, mp + "." + keys::config::kCellWidth, joinPtr(mpt, keys::config::kCellWidth), hints, err)) return false;
            if (!resolveCoord(*mg, keys::config::kCellHeight, canvas.height, mp + "." + keys::config::kCellHeight, joinPtr(mpt, keys::config::kCellHeight), hints, err)) return false;
        }
    }

    // generate：底图落位 x/y（落位框恒＝画布）。
    if (json* gen = mutableAt(root, keys::config::kGenerate)) {
        const std::string gpt = joinPtr("", keys::config::kGenerate);
        if (!resolveCoord(*gen, keys::common::kX, canvas.width, std::string(keys::config::kGenerate) + "." + keys::common::kX, joinPtr(gpt, keys::common::kX), hints, err)) return false;
        if (!resolveCoord(*gen, keys::common::kY, canvas.height, std::string(keys::config::kGenerate) + "." + keys::common::kY, joinPtr(gpt, keys::common::kY), hints, err)) return false;
    }

    // layers：transform 的位置与尺寸（画布坐标）。
    if (json* ls = mutableAt(root, keys::config::kLayers); ls != nullptr && ls->is_array()) {
        for (std::size_t i = 0; i < ls->size(); ++i) {
            const std::string n = std::to_string(i);
            json* tf = mutableAt((*ls)[i], keys::common::kTransform);
            if (tf == nullptr) continue;
            const std::string tp = std::string(keys::config::kLayers) + "[" + n + "]." + keys::common::kTransform;
            const std::string tpt = joinPtr(joinPtr(joinPtr("", keys::config::kLayers), n), keys::common::kTransform);
            if (!resolveCoord(*tf, keys::common::kX, canvas.width, tp + "." + keys::common::kX, joinPtr(tpt, keys::common::kX), hints, err)) return false;
            if (!resolveCoord(*tf, keys::common::kY, canvas.height, tp + "." + keys::common::kY, joinPtr(tpt, keys::common::kY), hints, err)) return false;
            if (!resolveCoord(*tf, keys::common::kWidth, canvas.width, tp + "." + keys::common::kWidth, joinPtr(tpt, keys::common::kWidth), hints, err)) return false;
            if (!resolveCoord(*tf, keys::common::kHeight, canvas.height, tp + "." + keys::common::kHeight, joinPtr(tpt, keys::common::kHeight), hints, err)) return false;
        }
    }

    // preprocess：仅 resize 的目标尺寸（基准＝source）。
    if (json* pre = mutableAt(root, keys::common::kPreprocess); pre != nullptr && pre->is_array()) {
        for (std::size_t i = 0; i < pre->size(); ++i) {
            json& step = (*pre)[i];
            if (jStr(at(step, keys::pre::kOp), "") != "resize") continue;
            const std::string n = std::to_string(i);
            const std::string sp = std::string(keys::common::kPreprocess) + "[" + n + "]";
            const std::string spt = joinPtr(joinPtr("", keys::common::kPreprocess), n);
            if (!resolveCoord(step, keys::common::kWidth, canvas.width, sp + "." + keys::common::kWidth, joinPtr(spt, keys::common::kWidth), hints, err)) return false;
            if (!resolveCoord(step, keys::common::kHeight, canvas.height, sp + "." + keys::common::kHeight, joinPtr(spt, keys::common::kHeight), hints, err)) return false;
        }
    }

    // annotations：基准＝工作图尺寸（resize 已在上段归一）。
    if (json* annos = mutableAt(root, keys::config::kAnnotations); annos != nullptr && annos->is_array()) {
        const BoxSize work = workSizeOf(root);
        for (std::size_t i = 0; i < annos->size(); ++i) {
            if (const std::string n = std::to_string(i); !resolveAnnotationCoords((*annos)[i], work, std::string(keys::config::kAnnotations) + "[" + n + "]",
                                                                                  joinPtr(joinPtr("", keys::config::kAnnotations), n), hints, err)) return false;
        }
    }
    return true;
}

// 保存侧回写：把「仍是载入时那个像素值、且基准尺寸未变」的字段换回 "N%"（比例原样保留）；值被编辑过
//   则写像素，路径失效（图层 / 标注被删、结构变化）则跳过，基准变了（换图 / 新建画布改变 source 或
//   预处理几何步）则也不再按旧比例回写——三者都为了绝不写出与当前尺寸不符的百分比。
void applyPercentHints(json& root, const core::PercentHints& hints) {
    if (hints.empty()) return;
    const json& src = at(root, keys::config::kSource);
    const BoxSize canvas{jInt(at(src, keys::common::kWidth), 0), jInt(at(src, keys::common::kHeight), 0)};
    BoxSize work = canvas;
    bool workComputed = false;   // workSizeOf 只在遇首条 annotations 记录时算一次。
    for (const auto&[pointer, ratio, resolved] : hints) {
        const json::json_pointer ptr(pointer);
        if (!root.contains(ptr)) continue;
        json& node = root[ptr];
        if (!node.is_number() || node.get<double>() != resolved) continue;
        const bool anno = pointer.rfind("/annotations/", 0) == 0;   // 标注基准是工作图，其余字段是画布。
        if (anno && !workComputed) { work = workSizeOf(root); workComputed = true; }
        const auto&[width, height] = anno ? work : canvas;
        if (const int px = static_cast<int>(resolved); core::resolvePercent(ratio, width) != px && core::resolvePercent(ratio, height) != px) continue;
        node = core::formatPercent(ratio);
    }
}

// 段级入口的图层百分比换算（transform 四键，基准＝画布宽高）：就地换成像素，不记 PercentHints
//   （命令行传入的 token 不属于配置载入的往返记录，保存时即落像素）。与整档读侧 layers 段同口径：
//   x/width 用宽、y/height 用高；写百分比而基准未知（<= 0）→ 报错，不静默归零。
bool resolveLayerPercentInPlace(json& item, const BoxSize& canvas, const std::string& path, std::string& err) {
    json* tf = mutableAt(item, keys::common::kTransform);
    if (tf == nullptr || !tf->is_object()) return true;
    const std::string tp = path + "." + keys::common::kTransform;
    const auto one = [&](const char* key, const int basis) -> bool {
        json* node = mutableAt(*tf, key);
        if (node == nullptr || !node->is_string()) return true;
        const std::string field = tp + "." + key;
        const auto ratio = core::parsePercent(node->get_ref<const std::string&>());
        if (!ratio) { err = IDC_KTRF(msg, kErrConfigNotCoord, field, node->dump()); return false; }
        if (basis <= 0) { err = IDC_KTRF(msg, kErrLayerPercentNoBasis, field); return false; }
        *node = core::resolvePercent(*ratio, basis);
        return true;
    };
    return one(keys::common::kX, canvas.width) && one(keys::common::kY, canvas.height)
        && one(keys::common::kWidth, canvas.width) && one(keys::common::kHeight, canvas.height);
}

// 从 JSON 值还原 EngineConfig；根非对象返回 ConfigParseFailed，字段缺失/类型不符取默认值。
// annoSet 非空时同步还原标注（含缺段→清空：配置文件为标注真相源）；annoErr 非空时逐条回传非法标注原因。
// percentHints 非空时收集本档以百分比书写的几何字段（供保存时原样回写）；value 为已解析的可变副本，
//   百分比坐标在解析前就地归一化为像素。
std::expected<void, core::CoreError> engineConfigFromJson(json& value, EngineConfig& out, annotation::AnnotationSet* annoSet,
                          std::string* annoErr, core::PercentHints* percentHints) {
    if (!value.is_object()) return std::unexpected(core::CoreError{core::CoreError::Code::ConfigParseFailed, {}});
    // 严格 schema 守卫：present-but-invalid（枚举拼错 / 颜色非法 / 类型不符）一律报错并回传定位报因
    //   （与 CLI value_parser、annotation 段严格校验对齐）；缺失/null 仍走下方宽容路径
    //   （数值越界仅图层 opacity 由 validateLayerSeg 拦截）。
    //   文件级通用失败（语法 / 根非对象 / IO）仍留空 message（两层契约：由上层自产本地化文案），
    //   此处是唯一填充 ConfigParseFailed.message 的来源（仅 Core reader 知晓哪个键/token 错）。
    if (std::string schemaErr; !validateSchema(value, schemaErr))
        return std::unexpected(core::CoreError{core::CoreError::Code::ConfigParseFailed, schemaErr});
    // 百分比坐标预归一化：白名单几何键上的 "N%" 依画布 / 工作图尺寸换算为像素（就地替换），
    //   令下方各段解析器始终只见整数；每处换算记入 percentHints 供保存时原样回写。
    core::PercentHints localHints;   // 调用方不需要回写时兼当记账容器，免分支。
    core::PercentHints& hints = percentHints != nullptr ? *percentHints : localHints;
    hints.clear();
    if (std::string coordErr; !resolvePercentCoords(value, hints, coordErr))
        return std::unexpected(core::CoreError{core::CoreError::Code::ConfigParseFailed, coordErr});
    out = EngineConfig{}; // 先复位为默认，再逐字段覆盖。

    // source。
    const json& src = at(value, keys::config::kSource);
    out.source.width = jInt(at(src, keys::common::kWidth), 0);
    out.source.height = jInt(at(src, keys::common::kHeight), 0);

    // opacity（顶层可选键）：画布级整体不透明度，缺省 255、夹 [0,255]。
    out.canvasOpacity = std::clamp(jInt(at(value, keys::common::kOpacity), nums::engine::kDefaultOpacity), 0, 255);

    // preprocess。
    out.preprocess.clear();
    if (const json& pre = at(value, keys::common::kPreprocess); pre.is_array()) {
        for (const json& step : pre) {
            if (preprocess::PreprocessConfig cfg; preprocessFromJson(step, cfg)) out.preprocess.add(cfg);
        }
    }

    // cut。
    const json& cut = at(value, keys::config::kCut);
    out.cut.tier = tierFromName(jStr(at(cut, keys::config::kTier), defs::kTierErase));
    out.cut.generator = generatorFromName(jStr(at(cut, keys::config::kGenerator), defs::kGeneratorRect));
    out.cut.polarity = polarityFromName(jStr(at(cut, keys::config::kPolarity), defs::kPolarityRemove));
    const json& params = at(cut, keys::config::kParams);
    if (out.cut.generator == CutGenerator::MultiRect) {
        out.cut.rects.clear();
        if (const json& rs = at(params, keys::config::kRects); rs.is_array())
            for (const json& r : rs) out.cut.rects.push_back(rectFromJson(r));
    } else if (out.cut.generator == CutGenerator::Grid) {
        out.cut.grid = gridFromJson(params);
    } else {
        out.cut.rect = rectFromJson(params);
    }

    // order（选择集 select 归于此段内）。
    const json& order = at(value, keys::config::kOrder);
    out.selectedCells.clear();
    if (const json& sel = at(order, keys::config::kSelect); sel.is_array())
        for (const json& s : sel) out.selectedCells.push_back(jInt(s, 0));
    out.order.strategy = strategyFromName(jStr(at(order, keys::config::kStrategy), defs::kStrategyRowMajor));
    out.order.reverse = jBool(at(order, keys::config::kReverse), false);
    out.order.snake = jBool(at(order, keys::config::kSnake), false);

    // export。
    const json& exportSeg = at(value, keys::config::kExport);
    out.exportParams.mode = exportModeFromName(jStr(at(exportSeg, keys::config::kMode), defs::kExportModeMerged));
    out.exportParams.format = formatFromName(jStr(at(exportSeg, keys::config::kFormat), defs::kFormatPng));
    out.exportParams.naming = jStr(at(exportSeg, keys::config::kNaming), defs::kNamingTemplate);
    out.exportParams.quality = jInt(at(exportSeg, keys::config::kQuality), nums::engine::kDefaultQuality);
    out.exportParams.keepMetadata = jBool(at(exportSeg, keys::config::kKeepMetadata), false);
    const json& merge = at(exportSeg, keys::config::kMerge);
    out.exportParams.layout = layoutFromName(jStr(at(merge, keys::config::kLayout), defs::kLayoutCollapse));
    out.exportParams.cols = jsonToOptInt(at(merge, keys::config::kCols));
    out.exportParams.rows = jsonToOptInt(at(merge, keys::config::kRows));
    out.exportParams.cellWidth = jsonToOptInt(at(merge, keys::config::kCellWidth));
    out.exportParams.cellHeight = jsonToOptInt(at(merge, keys::config::kCellHeight));
    out.exportParams.padColor = colorFromHex(jStr(at(merge, keys::config::kPadColor), defs::kPadColorTransparent));
    // arrange：重排填充顺序（缺失取默认 row-major / 无 reverse / 无 snake）。
    const json& arrange = at(merge, keys::config::kArrange);
    out.exportParams.mergeOrder.strategy = strategyFromName(jStr(at(arrange, keys::config::kStrategy), defs::kStrategyRowMajor));
    out.exportParams.mergeOrder.reverse = jBool(at(arrange, keys::config::kReverse), false);
    out.exportParams.mergeOrder.snake = jBool(at(arrange, keys::config::kSnake), false);

    // generate（可选段）：缺段或非对象 → 无底图（std::nullopt）。解析为同构 canvas::ImageLayer。
    if (const json& gen = at(value, keys::config::kGenerate); gen.is_object()) {
        out.base = baseFromJson(gen);   // 位图 / 外部文件不入配置：落位底图源图由上层凭运行时 --input 注入 decodedPixels。
        // 底图落位框尺寸不落盘（§7 设计：base box 恒＝画布）：由 source 回填，令 composeIntoBox 有框可裁、往返逐像素一致。
        if (out.base->hasImagePlacement) { out.base->boxWidth = out.source.width; out.base->boxHeight = out.source.height; }
    }

    // layers（可选段）：缺段 → 无图层（std::nullopt）。直接以数组承载。
    if (const json& ls = at(value, keys::config::kLayers); ls.is_array()) {
        std::vector<canvas::ImageLayer> layers;
        for (const json& lv : ls) layers.push_back(layerFromJson(lv));
        // 注：File 层的 decodedPixels 不经配置还原（位图不入配置）；由上层（CLI/GUI）按相对名从 assets 解码注入。
        out.layers = std::move(layers);
    }

    // assetsDir：资产目录声明（可选键，generate 与 layers 之间）——空串/缺键 → 未声明（纯约定查找）。
    out.assetsDir = jStr(at(value, keys::config::kAssetsDir), "");

    // annotations：传入集合指针才处理（段缺失/非数组 → 清空，与“配置为真相源”语义一致）。
    // 逐条严格校验、任一非法即整档载入失败（不静默丢弃）——重建逻辑归属 annotation 模块
    // （经 json_common.h 交换 JSON 片段，annoErr 回传逐条原因）。
    if (annoSet != nullptr) {
        std::string localErr;                          // annoErr 为空时兼当报因收集器，以便打包进 CoreError.message。
        if (std::string* sink = annoErr != nullptr ? annoErr : &localErr; !annotation::annotationsFromSegmentJson(at(value, keys::config::kAnnotations), *annoSet, sink)) {
            const std::string& reason = annoErr != nullptr ? *annoErr : localErr;
            return std::unexpected(core::CoreError{core::CoreError::Code::AnnotationInvalid, reason});
        }
    }

    return {};
}

} // namespace

// 保存配置到文件（缩进美化）；成功返回空 expected。path 为空或写盘失败（E-8）返回错误。annotations 非空时附带 SPEC §7 标注段。
// percentHints 非空时把仍未被编辑的百分比字段回写为 "N%"（见 applyPercentHints）。
std::expected<void, core::CoreError> saveEngineConfig(const std::string& path, const EngineConfig& config,
                      const annotation::AnnotationSet* annotations, const core::PercentHints* percentHints) {
    if (path.empty()) return std::unexpected(core::CoreError{core::CoreError::Code::PathEmpty, {}});
    json root = engineConfigToJson(config, annotations);
    if (percentHints != nullptr) applyPercentHints(root, *percentHints);
    const std::string text = root.dump(2); // 缩进 2 空格美化。
    std::ofstream ofs(core::pathFromUtf8(path), std::ios::trunc);   // path 按 UTF-8 → 宽字符开，中文 / 非 ASCII 路径可写。
    if (!ofs) return std::unexpected(core::CoreError{core::CoreError::Code::Io, {}}); // E-8：无法创建/写入（目录不存在或被占用）。
    ofs << text;
    ofs.close();
    if (ofs.fail()) return std::unexpected(core::CoreError{core::CoreError::Code::Io, {}});
    return {};
}

// 从文件加载配置；成功返回空 expected（out 经引用回填）。path 为空、读盘失败或 JSON 解析失败返回错误。annotations 非空时同步还原标注（严格校验）。
// annoErr 非空时，若因标注段非法而失败，回传逐条原因（供 CLI 报因；GUI 可传 nullptr）。
// percentHints 非空时收集本档以百分比书写的几何字段（供保存时原样回写）。
std::expected<void, core::CoreError> loadEngineConfig(const std::string& path, EngineConfig& out,
                      annotation::AnnotationSet* annotations, std::string* annoErr, core::PercentHints* percentHints) {
    if (path.empty()) return std::unexpected(core::CoreError{core::CoreError::Code::PathEmpty, {}});
    std::ifstream ifs(core::pathFromUtf8(path), std::ios::binary);   // path 按 UTF-8 → 宽字符开，中文 / 非 ASCII 路径可读。
    if (!ifs) return std::unexpected(core::CoreError{core::CoreError::Code::Io, {}}); // E-8：文件不存在或不可读。
    try {
        json value = json::parse(ifs); // 直接从流解析；语法错误抛 parse_error。百分比归一化需就地改写，故持有可变副本。
        return engineConfigFromJson(value, out, annotations, annoErr, percentHints);
    } catch (const json::exception&) {
        return std::unexpected(core::CoreError{core::CoreError::Code::ConfigParseFailed, {}}); // JSON 语法错误。
    }
}

// 段级解析：把一段「图层 JSON」（§7 layers 元素语法，根为单对象或对象数组）解析为 ImageLayer 列表
// 追加到 out（与 annotation 模块的 parseAnnotationsFromJsonText 同层同形，供 CLI 命令行 --layer/--layers 复用）。
// transform 百分比坐标按 basisWidth/basisHeight（画布宽高）换算为像素；基准未知而写百分比 → 报错。
// 文本为空 / 根非对象非数组 / 解析不出有效图层 → 返回 false 并回传原因；JSON 语法错误直接透传 parse_error 详情。
bool parseImageLayersFromJsonText(const std::string& text, std::vector<canvas::ImageLayer>& out,
                                  std::string& err, const int basisWidth, const int basisHeight) {
    namespace msg = core::texts::msg;
    if (text.empty()) { err = IDC_KTR(msg, kErrLayerTextEmpty); return false; }
    try {
        const json value = json::parse(text);
        const BoxSize canvas{basisWidth, basisHeight};
        std::vector<canvas::ImageLayer> built;
        // 与配置文件的 layers 段同规：逐条先过严格校验（validateLayerSeg），锚点拼错 / 浮点 / 越界
        //   一律报错、报因定位到字段；再按画布基准换算百分比坐标，最后过 layerFromJson 还原。
        const auto parseOne = [&built, &canvas](const json& item0, const std::string& path, std::string& reason) -> bool {
            if (!validateLayerSeg(item0, path, reason)) return false;
            json item = item0;   // 百分比换算需就地改写：持拷贝，不动调用方视图。
            if (!resolveLayerPercentInPlace(item, canvas, path, reason)) return false;
            built.push_back(layerFromJson(item));
            return true;
        };
        if (value.is_array()) {
            for (std::size_t i = 0; i < value.size(); ++i) {
                if (std::string reason; !parseOne(value[i], "[" + std::to_string(i) + "]", reason)) { err = std::move(reason); return false; }
            }
        } else if (value.is_object()) {
            if (std::string reason; !parseOne(value, "[0]", reason)) { err = std::move(reason); return false; }
        } else {
            err = IDC_KTR(msg, kErrLayerRootType);
            return false;
        }
        if (built.empty()) { err = IDC_KTR(msg, kErrLayerNoValidEntry); return false; }
        out.insert(out.end(), built.begin(), built.end());
        return true;
    } catch (const json::exception& e) {
        err = e.what();
        return false;
    }
}

} // namespace idc::engine
