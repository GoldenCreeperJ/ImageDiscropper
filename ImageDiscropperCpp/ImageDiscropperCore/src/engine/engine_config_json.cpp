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
// 说明：读取一律宽容（缺字段/类型不符取默认值，绝不抛异常给调用方），写出严格对齐 SPEC §7
//       示例的键名与嵌套结构；nlohmann::json 以 std::map 存对象，序列化按键升序输出。
// ============================================================================
#include "engine/engine_config_json.h"

#include "preprocess/preprocess_config.h"
#include "annotation/annotation_set.h"
#include "core/path_utf8.h" // pathFromUtf8（取代 C++23 已移除的 u8path）

#include "../json_common.h" // idc::jsonutil 助手 + 标注段交换函数声明（实现在 annotation 模块）
#include "engine_config_json_internal.h" // 枚举串/颜色/各段 toJson/fromJson 助手区（detail 命名空间）

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
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
//   source：{ recipe: {...PatternParams...} } | { file: "相对名" } | 缺省（空占位）。decodedPixels 不入配置。
//   transform：{ inCanvasAnchor, layerPivot, x, y, width, height }。
// ---------------------------------------------------------------------------
const char* layerSourceKindToken(const canvas::LayerSourceKind k) {
    switch (k) {
        case canvas::LayerSourceKind::Recipe: return "recipe";
        case canvas::LayerSourceKind::File: return "file";
        case canvas::LayerSourceKind::Empty: break;
    }
    return "empty";
}

canvas::LayerSourceKind layerSourceKindFromStr(const std::string& s) {
    if (s == "recipe") return canvas::LayerSourceKind::Recipe;
    if (s == "file") return canvas::LayerSourceKind::File;
    return canvas::LayerSourceKind::Empty;
}

json layerToJson(const canvas::ImageLayer& layer) {
    json o;
    json source;
    source[keys::common::kKind] = layerSourceKindToken(layer.sourceKind);
    if (layer.sourceKind == canvas::LayerSourceKind::Recipe && layer.recipe)
        source[keys::layer::kRecipe] = patternParamsToJson(*layer.recipe);
    else if (layer.sourceKind == canvas::LayerSourceKind::File)
        source[keys::common::kFile] = layer.fileName;   // 仅存相对名；像素不入配置（缺失时上层降级为占位）。
    o[keys::config::kSource] = source;

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
    layer.sourceKind = layerSourceKindFromStr(jStr(at(source, keys::common::kKind), defs::kLayerSourceEmpty));
    if (layer.sourceKind == canvas::LayerSourceKind::Recipe) {
        if (const json& r = at(source, keys::layer::kRecipe); r.is_object()) layer.recipe = patternParamsFromJson(r);
        else layer.sourceKind = canvas::LayerSourceKind::Empty;   // 声明 recipe 却无 recipe 体→降级空占位
    } else if (layer.sourceKind == canvas::LayerSourceKind::File) {
        layer.fileName = jStr(at(source, keys::common::kFile), "");
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
    canvas::ImageLayer base;   // transform 恒等、zOrder=0（底位）、sourceKind 默认：均为 ImageLayer 默认。
    // 底图整体不透明度：读 opacity（缺省 255）→ transform.opacity；上层（GUI Document / CLI config）据此在合成边界施加淡出。
    base.transform.opacity = std::clamp(jInt(at(gen, keys::common::kOpacity), nums::engine::kDefaultOpacity), 0, 255);
    // 底图色彩预处理（可选 preprocess 数组）：仅接受色彩步（gray/invert/split），几何/resize 步忽略
    //   （底图预处理不改画布尺寸 / 坐标基准；还原端遇非法几何步静默丢弃）。
    if (const json& preArr = at(gen, keys::common::kPreprocess); preArr.is_array()) {
        for (const json& step : preArr) {
            if (preprocess::PreprocessConfig cfg; preprocessFromJson(step, cfg) && preprocess::isColorStep(cfg)) base.preprocess.add(cfg);
        }
    }
    // 图案键存在（含 "kind"）→ 解析配方并置 Recipe；否则 recipe 留空（底图为运行时 --input 位图，仅存落位参数）。
    if (gen.contains(keys::common::kKind)) {
        base.sourceKind = canvas::LayerSourceKind::Recipe;
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
    json root;

    // source：原图尺寸。
    root[keys::config::kSource] = json{{keys::common::kWidth, config.source.width}, {keys::common::kHeight, config.source.height}};

    // opacity：画布级整体不透明度（顶层可选键，0..255，255＝不透明）。缺省 255 时仍写出（观感一致、无歧义）。
    root[keys::common::kOpacity] = config.canvasOpacity;

    // preprocess：基础图像处理步骤数组。
    json pre = json::array();
    for (const preprocess::PreprocessConfig& step : config.preprocess.items())
        pre.push_back(preprocessToJson(step));
    root[keys::common::kPreprocess] = pre;

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

    // select：L3 显式选择集（单元序号）。std::vector<int> 直接映射为 JSON 数组。
    root[keys::config::kSelect] = config.selectedCells;

    // order：排序策略 + reverse/snake；sequence 预留（自定义序由上层 Sequence 承载）。
    json order;
    order[keys::config::kStrategy] = strategyName(config.order.strategy);
    order[keys::config::kReverse] = config.order.reverse;
    order[keys::config::kSnake] = config.order.snake;
    order[keys::config::kSequence] = nullptr;
    root[keys::config::kOrder] = order;

    // emit：输出模式 / 合并布局 / 格式 / 命名 / 质量 / 元数据。
    json merge;
    merge[keys::config::kLayout] = layoutName(config.emitParams.layout);
    merge[keys::config::kCols] = optIntToJson(config.emitParams.cols);
    merge[keys::config::kRows] = optIntToJson(config.emitParams.rows);
    merge[keys::config::kCellWidth] = optIntToJson(config.emitParams.cellWidth);
    merge[keys::config::kCellHeight] = optIntToJson(config.emitParams.cellHeight);
    merge[keys::config::kPadColor] = colorToHex(config.emitParams.padColor);
    // arrange：重排填充顺序（仅 Rearrange 用；与顶层 order 选择排序正交）。
    json arrange;
    arrange[keys::config::kStrategy] = strategyName(config.emitParams.mergeOrder.strategy);
    arrange[keys::config::kReverse] = config.emitParams.mergeOrder.reverse;
    arrange[keys::config::kSnake] = config.emitParams.mergeOrder.snake;
    merge[keys::config::kArrange] = arrange;
    json emit;
    emit[keys::config::kMode] = emitModeName(config.emitParams.mode);
    emit[keys::config::kMerge] = merge;
    emit[keys::config::kFormat] = formatName(config.emitParams.format);
    emit[keys::config::kNaming] = config.emitParams.naming;
    emit[keys::config::kQuality] = config.emitParams.quality;
    emit[keys::config::kKeepMetadata] = config.emitParams.keepMetadata;
    root[keys::config::kEmit] = emit;

    // generate：底图描述（可选段）——缺省省略（表示用外部输入图，非生成底图）。
    //   底图＝同构 canvas::ImageLayer：纯图案底图（仅配方、无落位）序列化为纯 recipe 形态的 generate 段；带落位时追加
    //   fit/box*/image*/backdrop 键（**不含 file 路径**，外部图凭运行时 --input 供给以保批处理复用）。尺寸恒取 source.width/height。
    if (config.base.has_value())
        root[keys::config::kGenerate] = baseToJson(*config.base);

    // layers：多层画布（可选段）——缺省省略（表示单一输入图/生成源，无图层）。直接以数组承载。
    if (config.layers.has_value()) {
        json arr = json::array();
        for (const canvas::ImageLayer& layer : *config.layers) arr.push_back(layerToJson(layer));
        root[keys::config::kLayers] = arr;
    }

    // annotations：标注列表（可选段）——集合为空或无标注时省略（输出不含 annotations 键）。
    // 段内逐条序列化实现归属 annotation 模块（经 json_common.h 交换 JSON 片段）。
    if (annoSet != nullptr && !annoSet->annotations().empty())
        root[keys::config::kAnnotations] = annotation::annotationsSegmentToJson(*annoSet);

    return root;
}

// 从 JSON 值还原 EngineConfig；根非对象返回 ConfigParseFailed，字段缺失/类型不符取默认值。
// annoSet 非空时同步还原标注（含缺段→清空：配置文件为标注真相源）；annoErr 非空时逐条回传非法标注原因。
std::expected<void, core::CoreError> engineConfigFromJson(const json& value, EngineConfig& out, annotation::AnnotationSet* annoSet,
                          std::string* annoErr) {
    if (!value.is_object()) return std::unexpected(core::CoreError{core::CoreError::Code::ConfigParseFailed, {}});
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

    // select。
    out.selectedCells.clear();
    if (const json& sel = at(value, keys::config::kSelect); sel.is_array())
        for (const json& s : sel) out.selectedCells.push_back(jInt(s, 0));

    // order。
    const json& order = at(value, keys::config::kOrder);
    out.order.strategy = strategyFromName(jStr(at(order, keys::config::kStrategy), defs::kStrategyRowMajor));
    out.order.reverse = jBool(at(order, keys::config::kReverse), false);
    out.order.snake = jBool(at(order, keys::config::kSnake), false);

    // emit。
    const json& emit = at(value, keys::config::kEmit);
    out.emitParams.mode = emitModeFromName(jStr(at(emit, keys::config::kMode), defs::kEmitModeMerged));
    out.emitParams.format = formatFromName(jStr(at(emit, keys::config::kFormat), defs::kFormatPng));
    out.emitParams.naming = jStr(at(emit, keys::config::kNaming), defs::kNamingTemplate);
    out.emitParams.quality = jInt(at(emit, keys::config::kQuality), nums::engine::kDefaultQuality);
    out.emitParams.keepMetadata = jBool(at(emit, keys::config::kKeepMetadata), false);
    const json& merge = at(emit, keys::config::kMerge);
    out.emitParams.layout = layoutFromName(jStr(at(merge, keys::config::kLayout), defs::kLayoutCollapse));
    out.emitParams.cols = jsonToOptInt(at(merge, keys::config::kCols));
    out.emitParams.rows = jsonToOptInt(at(merge, keys::config::kRows));
    out.emitParams.cellWidth = jsonToOptInt(at(merge, keys::config::kCellWidth));
    out.emitParams.cellHeight = jsonToOptInt(at(merge, keys::config::kCellHeight));
    out.emitParams.padColor = colorFromHex(jStr(at(merge, keys::config::kPadColor), defs::kPadColorTransparent));
    // arrange：重排填充顺序（缺失取默认 row-major / 无 reverse / 无 snake）。
    const json& arrange = at(merge, keys::config::kArrange);
    out.emitParams.mergeOrder.strategy = strategyFromName(jStr(at(arrange, keys::config::kStrategy), defs::kStrategyRowMajor));
    out.emitParams.mergeOrder.reverse = jBool(at(arrange, keys::config::kReverse), false);
    out.emitParams.mergeOrder.snake = jBool(at(arrange, keys::config::kSnake), false);

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
std::expected<void, core::CoreError> saveEngineConfig(const std::string& path, const EngineConfig& config,
                      const annotation::AnnotationSet* annotations) {
    if (path.empty()) return std::unexpected(core::CoreError{core::CoreError::Code::PathEmpty, {}});
    const std::string text = engineConfigToJson(config, annotations).dump(2); // 缩进 2 空格美化。
    std::ofstream ofs(core::pathFromUtf8(path), std::ios::trunc);   // path 按 UTF-8 → 宽字符开，中文 / 非 ASCII 路径可写。
    if (!ofs) return std::unexpected(core::CoreError{core::CoreError::Code::Io, {}}); // E-8：无法创建/写入（目录不存在或被占用）。
    ofs << text;
    ofs.close();
    if (ofs.fail()) return std::unexpected(core::CoreError{core::CoreError::Code::Io, {}});
    return {};
}

// 从文件加载配置；成功返回空 expected（out 经引用回填）。path 为空、读盘失败或 JSON 解析失败返回错误。annotations 非空时同步还原标注（严格校验）。
// annoErr 非空时，若因标注段非法而失败，回传逐条原因（供 CLI 报因；GUI 可传 nullptr）。
std::expected<void, core::CoreError> loadEngineConfig(const std::string& path, EngineConfig& out,
                      annotation::AnnotationSet* annotations, std::string* annoErr) {
    if (path.empty()) return std::unexpected(core::CoreError{core::CoreError::Code::PathEmpty, {}});
    std::ifstream ifs(core::pathFromUtf8(path), std::ios::binary);   // path 按 UTF-8 → 宽字符开，中文 / 非 ASCII 路径可读。
    if (!ifs) return std::unexpected(core::CoreError{core::CoreError::Code::Io, {}}); // E-8：文件不存在或不可读。
    try {
        const json value = json::parse(ifs); // 直接从流解析；语法错误抛 parse_error。
        return engineConfigFromJson(value, out, annotations, annoErr);
    } catch (const json::exception&) {
        return std::unexpected(core::CoreError{core::CoreError::Code::ConfigParseFailed, {}}); // JSON 语法错误。
    }
}

} // namespace idc::engine
