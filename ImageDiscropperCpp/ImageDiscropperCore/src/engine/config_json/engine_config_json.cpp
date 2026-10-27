// ============================================================================
// 文件：src/engine/config_json/engine_config_json.cpp
// 作用：engine_config_json.h 的实现——EngineConfig 与 SPEC §7 schema JSON 的双向映射与文件存取。
// 分块依据：本文件只承载「配置字段 ↔ JSON 键」的序列化本体（枚举串、颜色十六进制、矩形/网格
//       子对象、预处理步骤数组）；读侧严格 schema 守卫独立为同目录 schema_validate.{h,cpp}，
//       百分比坐标的读侧归一化与保存侧回写独立为同目录 percent_coords.{h,cpp}——三者皆只经
//       本文件调用，故 nlohmann/json 依赖不泄漏到公共头（PRIVATE）。JSON 语法委托第三方库
//       nlohmann/json，本文件不手写解析器。
//       annotations 段（标注列表）的 JSON 实现归属 annotation 模块：本文件组装 / 解析整档时对
//       src/json_common.h 声明的 annotation::annotationsSegmentToJson / annotationsFromSegmentJson
//       发起跨模块调用（公共 save/load 签名仍以可选 AnnotationSet* 传递，向后兼容不变）。
// 说明：读取顺序＝先过严格 schema 守卫（validateSchema：present-but-invalid——枚举拼错 / 颜色非法 /
//       类型不符——一律拒绝并回传 CoreError.message 定位报因）→ 再百分比归一化（resolvePercentCoords）
//       → 最后逐段宽容还原：缺失 / null 仍取默认值、越界仍 clamp（向后兼容不变，绝不抛异常给调用方）。
//       写出侧严格对齐 SPEC §7 示例的键名与嵌套结构，保存时按 percentHints 原样回写百分比；JSON 采用
//       nlohmann::ordered_json（保插入序），序列化按写入顺序输出。
// ============================================================================
#include "engine/engine_config_json.h"

#include "preprocess/preprocess_config.h"
#include "annotation/annotation_set.h"
#include "core/path_utf8.h" // pathFromUtf8（取代 C++23 已移除的 u8path）

#include "../../json_common.h" // 标注段交换函数声明（实现在 annotation 模块）
#include "engine_config_json_internal.h" // 枚举串/颜色/各段 toJson/fromJson 助手区（detail 命名空间）
#include "schema_validate.h"  // validateSchema / validateLayerSeg（读侧严格校验，同目录私有实现）
#include "percent_coords.h"   // resolvePercentCoords / applyPercentHints / resolveLayerPercentInPlace（同目录私有实现）
#include "i18n/core_i18n.h"  // IDC_KTR：段级解析报因取译文（缺译文回退常量层）

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
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
// std::vector<canvas::ImageLayer> ↔ SPEC §7 layers 段（可选，直接以数组承载）。元素分块（SPEC §2.2a）：
//   每层 = 图层框节点 { image?, backdrop?, transform, zOrder, preprocess }。
//   image（图片元素，可选）：file / fit / box* / anchor / pivot / x / y / preprocess / opacity；
//   backdrop（图案元素，可选）：图案键族 + preprocess / opacity。
//   decodedPixels 不入配置；未识别键静默忽略。
// ---------------------------------------------------------------------------
// 声明式队列 → JSON 数组（图层各元素外观/节点外观共用）。
json pipelineToJson(const preprocess::PreprocessPipeline& pipeline) {
    json arr = json::array();
    for (const preprocess::PreprocessConfig& step : pipeline.items())
        arr.push_back(preprocessToJson(step));
    return arr;
}

// 声明式队列 → JSON 数组（仅色彩步）：图案元素外观用——图案按区域尺寸生成后才施外观，几何步会改
//   尺寸（图案元素外观不改节点区域），写出时过滤、还原端遇几何步静默丢弃（与底图整体外观同口径）。
json pipelineToJsonColorOnly(const preprocess::PreprocessPipeline& pipeline) {
    json arr = json::array();
    for (const preprocess::PreprocessConfig& step : pipeline.items())
        if (preprocess::isColorStep(step)) arr.push_back(preprocessToJson(step));
    return arr;
}

json layerToJson(const canvas::ImageLayer& layer) {
    json o;
    // 图片元素（可选：有图片元素才写）。
    if (hasImageElement(layer)) {
        json im;
        if (!layer.fileName.empty()) im[keys::common::kFile] = layer.fileName;   // 仅存相对名；像素不入配置。
        if (layer.hasImagePlacement) {
            im[keys::layer::kFit] = imageFitToken(layer.imageFit);
            im[keys::layer::kBoxWidth] = layer.boxWidth;
            im[keys::layer::kBoxHeight] = layer.boxHeight;
            im[keys::layer::kAnchor] = genAnchorToken(layer.imageAnchor);
            im[keys::layer::kPivot] = genAnchorToken(layer.imagePivot);
            im[keys::common::kX] = layer.imageX;
            im[keys::common::kY] = layer.imageY;
        }
        if (!layer.imagePreprocess.empty()) im[keys::common::kPreprocess] = pipelineToJson(layer.imagePreprocess);
        im[keys::common::kOpacity] = layer.imageOpacity;   // 元素外观·不透明度。
        o[keys::layer::kImage] = im;
    }
    // 图案元素（可选：空 = 透明底，不写）。元素外观仅色彩步（几何步写出时过滤）。
    if (layer.backdrop) {
        json bd = patternParamsToJson(*layer.backdrop);
        if (!layer.backdropPreprocess.empty()) {
            if (const json pre = pipelineToJsonColorOnly(layer.backdropPreprocess); !pre.empty()) bd[keys::common::kPreprocess] = pre;
        }
        bd[keys::common::kOpacity] = layer.backdropOpacity;   // 元素外观·不透明度。
        o[keys::layer::kBackdrop] = bd;
    }

    json t;
    t[keys::layer::kInCanvasAnchor] = genAnchorToken(layer.transform.inCanvasAnchor);
    t[keys::layer::kLayerPivot] = genAnchorToken(layer.transform.layerPivot);
    t[keys::common::kX] = layer.transform.x;
    t[keys::common::kY] = layer.transform.y;
    t[keys::common::kWidth] = layer.transform.width;
    t[keys::common::kHeight] = layer.transform.height;
    t[keys::common::kOpacity] = layer.transform.opacity;   // 节点外观·不透明度（合成期 alpha 乘子 0..255）。
    o[keys::common::kTransform] = t;

    o[keys::common::kZOrder] = layer.zOrder;

    json pre = json::array();
    for (const preprocess::PreprocessConfig& step : layer.preprocess.items())
        pre.push_back(preprocessToJson(step));
    o[keys::common::kPreprocess] = pre;
    return o;
}

// 从 JSON 数组还原声明式队列（未知/非法步跳过，与顶层 preprocess 同口径）。
preprocess::PreprocessPipeline pipelineFromJson(const json& v) {
    preprocess::PreprocessPipeline out;
    if (v.is_array()) {
        for (const json& step : v) {
            if (preprocess::PreprocessConfig cfg; preprocessFromJson(step, cfg)) out.add(cfg);
        }
    }
    return out;
}

// 从 JSON 数组还原声明式队列（仅色彩步）：图案元素外观用——还原端遇几何步静默丢弃。
preprocess::PreprocessPipeline pipelineFromJsonColorOnly(const json& v) {
    preprocess::PreprocessPipeline out;
    if (v.is_array()) {
        for (const json& step : v) {
            if (preprocess::PreprocessConfig cfg; preprocessFromJson(step, cfg) && preprocess::isColorStep(cfg))
                out.add(cfg);
        }
    }
    return out;
}

canvas::ImageLayer layerFromJson(const json& v) {
    canvas::ImageLayer layer;
    // 图片元素（可选）。
    if (const json& im = at(v, keys::layer::kImage); im.is_object()) {
        layer.fileName = jStr(at(im, keys::common::kFile), "");
        if (im.contains(keys::layer::kFit)) {   // 有 fit 即内层落位启用。
            layer.hasImagePlacement = true;
            layer.imageFit = imageFitFromToken(jStr(at(im, keys::layer::kFit), defs::kImageFitCrop));
            layer.boxWidth = jInt(at(im, keys::layer::kBoxWidth), 0);
            layer.boxHeight = jInt(at(im, keys::layer::kBoxHeight), 0);
            layer.imageAnchor = genAnchorFromToken(jStr(at(im, keys::layer::kAnchor), defs::kAnchorCenter));
            layer.imagePivot = genAnchorFromToken(jStr(at(im, keys::layer::kPivot), defs::kAnchorCenter));
            layer.imageX = jInt(at(im, keys::common::kX), 0);
            layer.imageY = jInt(at(im, keys::common::kY), 0);
        }
        layer.imagePreprocess = pipelineFromJson(at(im, keys::common::kPreprocess));
        layer.imageOpacity = std::clamp(jInt(at(im, keys::common::kOpacity), nums::engine::kDefaultOpacity), 0, 255);
    }
    // 图案元素（可选）。元素外观仅色彩步（还原端遇几何步静默丢弃）。
    if (const json& bd = at(v, keys::layer::kBackdrop); bd.is_object()) {
        layer.backdrop = patternParamsFromJson(bd);   // 只读图案键族，忽略 preprocess/opacity 之外的键。
        layer.backdropPreprocess = pipelineFromJsonColorOnly(at(bd, keys::common::kPreprocess));
        layer.backdropOpacity = std::clamp(jInt(at(bd, keys::common::kOpacity), nums::engine::kDefaultOpacity), 0, 255);
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

    layer.preprocess = pipelineFromJson(at(v, keys::common::kPreprocess));
    return layer;
}

// ---------------------------------------------------------------------------
// 底图 canvas::ImageLayer ↔ SPEC §7「generate」段（元素分块，与 layers 同构）。
//   键名仍叫 "generate"（不破外部/CLI 选项名）。底图不外传文件：图片元素恒无 file（位图由运行时
//   --input 供给，配置可跨输入批处理复用）；box 尺寸恒＝画布(source)、不落盘（读取端据 source 回填）。
//   底图钉住最底：transform 恒等、zOrder 固定、不入本段（读取端复位默认）。
// ---------------------------------------------------------------------------
json baseToJson(const canvas::ImageLayer& base) {
    json gen;
    // 图案元素（可选：空 = 透明底，不写）。元素外观仅色彩步（几何步写出时过滤）。
    if (base.backdrop) {
        json bd = patternParamsToJson(*base.backdrop);
        if (!base.backdropPreprocess.empty()) {
            if (const json pre = pipelineToJsonColorOnly(base.backdropPreprocess); !pre.empty()) bd[keys::common::kPreprocess] = pre;
        }
        bd[keys::common::kOpacity] = base.backdropOpacity;
        gen[keys::layer::kBackdrop] = bd;
    }
    // 图片元素（可选：落位声明底；不写 file；box 不落盘）。
    if (hasImageElement(base)) {
        json im;
        if (base.hasImagePlacement) {
            im[keys::layer::kFit] = imageFitToken(base.imageFit);
            im[keys::layer::kAnchor] = genAnchorToken(base.imageAnchor);
            im[keys::layer::kPivot] = genAnchorToken(base.imagePivot);
            im[keys::common::kX] = base.imageX;
            im[keys::common::kY] = base.imageY;
        }
        if (!base.imagePreprocess.empty()) im[keys::common::kPreprocess] = pipelineToJson(base.imagePreprocess);
        im[keys::common::kOpacity] = base.imageOpacity;
        gen[keys::layer::kImage] = im;
    }
    // 底图整体不透明度（节点外观·不透明度 0..255）：随段写入 opacity 键，供 GUI 载入回填 baseOpacity、
    //   CLI 在边界施加淡出。
    gen[keys::common::kOpacity] = base.transform.opacity;
    // 底图色彩预处理步（节点外观；仅色彩合法：gray/invert/split——几何/resize 会改画布尺寸，写出时过滤掉）。
    //   缺省空 → 不写 preprocess 键。
    json pre = json::array();
    for (const preprocess::PreprocessConfig& step : base.preprocess.items())
        if (preprocess::isColorStep(step)) pre.push_back(preprocessToJson(step));
    if (!pre.empty()) gen[keys::common::kPreprocess] = pre;
    return gen;
}
canvas::ImageLayer baseFromJson(const json& gen) {
    canvas::ImageLayer base;   // transform 恒等、zOrder=0（底位）：均为 ImageLayer 默认。
    // 底图整体不透明度：读 opacity（缺省 255）→ transform.opacity；上层（GUI Document / CLI config）据此在合成边界施加淡出。
    base.transform.opacity = std::clamp(jInt(at(gen, keys::common::kOpacity), nums::engine::kDefaultOpacity), 0, 255);
    // 底图色彩预处理（节点外观；可选 preprocess 数组）：仅接受色彩步（gray/invert/split），几何/resize 步忽略
    //   （底图预处理不改画布尺寸 / 坐标基准；还原端遇非法几何步静默丢弃）。
    if (const json& preArr = at(gen, keys::common::kPreprocess); preArr.is_array()) {
        for (const json& step : preArr) {
            if (preprocess::PreprocessConfig cfg; preprocessFromJson(step, cfg) && preprocess::isColorStep(cfg)) base.preprocess.add(cfg);
        }
    }
    // 图案元素（可选）。元素外观仅色彩步（还原端遇几何步静默丢弃）。
    if (const json& bd = at(gen, keys::layer::kBackdrop); bd.is_object()) {
        base.backdrop = patternParamsFromJson(bd);
        base.backdropPreprocess = pipelineFromJsonColorOnly(at(bd, keys::common::kPreprocess));
        base.backdropOpacity = std::clamp(jInt(at(bd, keys::common::kOpacity), nums::engine::kDefaultOpacity), 0, 255);
    }
    // 图片元素（可选：落位声明底；box 尺寸不落盘，由 engineConfigFromJson 据 source 回填）。
    if (const json& im = at(gen, keys::layer::kImage); im.is_object()) {
        if (im.contains(keys::layer::kFit)) {
            base.hasImagePlacement = true;
            base.imageFit = imageFitFromToken(jStr(at(im, keys::layer::kFit), defs::kImageFitCrop));
            base.imageAnchor = genAnchorFromToken(jStr(at(im, keys::layer::kAnchor), defs::kAnchorCenter));
            base.imagePivot = genAnchorFromToken(jStr(at(im, keys::layer::kPivot), defs::kAnchorCenter));
            base.imageX = jInt(at(im, keys::common::kX), 0);
            base.imageY = jInt(at(im, keys::common::kY), 0);
        }
        base.imagePreprocess = pipelineFromJson(at(im, keys::common::kPreprocess));
        base.imageOpacity = std::clamp(jInt(at(im, keys::common::kOpacity), nums::engine::kDefaultOpacity), 0, 255);
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
        // 底图落位框尺寸不落盘（§7 设计：base box 恒＝画布）：由 source 回填，令 materializeLayer 有框可裁、往返逐像素一致。
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
