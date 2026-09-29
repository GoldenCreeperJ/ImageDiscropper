// ============================================================================
// 文件：src/engine/engine_config_json.cpp
// 作用：engine_config_json.h 的实现——EngineConfig 与 SPEC §7 schema JSON 的双向映射与文件存取。
// 分块依据：集中承载“配置字段 ↔ JSON 键”的语义映射（枚举串、颜色十六进制、矩形/网格
//       子对象、预处理步骤数组）；JSON 语法委托第三方库 nlohmann/json，本文件不手写解析器。
//       转换函数（engineConfigToJson / engineConfigFromJson）为文件内私有实现，仅由
//       saveEngineConfig / loadEngineConfig 调用，故 nlohmann/json 依赖不泄漏到公共头（PRIVATE）。
//       annotations 段（标注列表）的 JSON 实现归属 annotation 模块：本文件组装 / 解析整档时对
//       src/json_common.h 声明的 annotation::annotationsSegmentToJson / annotationsFromSegmentJson
//       发起跨模块调用（公共 save/load 签名仍以可选 AnnotationLayer* 传递，向后兼容不变）。
// 说明：读取一律宽容（缺字段/类型不符取默认值，绝不抛异常给调用方），写出严格对齐 SPEC §7
//       示例的键名与嵌套结构；nlohmann::json 以 std::map 存对象，序列化按键升序输出。
// ============================================================================
#include "engine/engine_config_json.h"

#include "preprocess/preprocess_config.h"
#include "annotation/annotation_layer.h"

#include "../json_common.h" // idc::jsonutil 助手 + 标注段交换函数声明（实现在 annotation 模块）

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace idc::engine {
namespace {

using json = nlohmann::json;

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
// ---------------------------------------------------------------------------
const char* tierName(const Tier t) {
    switch (t) { case Tier::L1: return "L1"; case Tier::L2: return "L2"; case Tier::L3: return "L3"; }
    return "L2";
}
Tier tierFromName(const std::string& s) {
    if (s == "L1") return Tier::L1;
    if (s == "L3") return Tier::L3;
    return Tier::L2;
}

const char* generatorName(const CutGenerator g) {
    switch (g) {
        case CutGenerator::RECT: return "rect";
        case CutGenerator::HORIZONTAL_LINE: return "horizontal";
        case CutGenerator::VERTICAL_LINE: return "vertical";
        case CutGenerator::MULTI_RECT: return "multi-rect";
        case CutGenerator::GRID: return "grid";
    }
    return "rect";
}
CutGenerator generatorFromName(const std::string& s) {
    if (s == "horizontal") return CutGenerator::HORIZONTAL_LINE;
    if (s == "vertical") return CutGenerator::VERTICAL_LINE;
    if (s == "multi-rect") return CutGenerator::MULTI_RECT;
    if (s == "grid") return CutGenerator::GRID;
    return CutGenerator::RECT;
}

const char* polarityName(const Polarity p) {
    return p == Polarity::KEEP ? "keep" : "remove";
}
Polarity polarityFromName(const std::string& s) {
    return s == "keep" ? Polarity::KEEP : Polarity::REMOVE;
}

const char* strategyName(const SortStrategy s) {
    switch (s) {
        case SortStrategy::ROW_MAJOR: return "row-major";
        case SortStrategy::COLUMN_MAJOR: return "column-major";
        case SortStrategy::CUSTOM: return "custom";
    }
    return "row-major";
}
SortStrategy strategyFromName(const std::string& s) {
    if (s == "column-major") return SortStrategy::COLUMN_MAJOR;
    if (s == "custom") return SortStrategy::CUSTOM;
    return SortStrategy::ROW_MAJOR;
}

const char* emitModeName(const EmitMode m) {
    return m == EmitMode::SEPARATE ? "separate" : "merged";
}
EmitMode emitModeFromName(const std::string& s) {
    return s == "separate" ? EmitMode::SEPARATE : EmitMode::MERGED;
}

const char* layoutName(const MergeLayout l) {
    return l == MergeLayout::COLLAPSE ? "collapse" : "rearrange";
}
MergeLayout layoutFromName(const std::string& s) {
    return s == "rearrange" ? MergeLayout::REARRANGE : MergeLayout::COLLAPSE;
}

const char* formatName(const ExportFormat f) {
    switch (f) {
        case ExportFormat::PNG: return "png";
        case ExportFormat::JPEG: return "jpeg";
        case ExportFormat::WEBP: return "webp";
        case ExportFormat::BMP: return "bmp";
    }
    return "png";
}
ExportFormat formatFromName(const std::string& s) {
    if (s == "jpeg" || s == "jpg") return ExportFormat::JPEG;
    if (s == "webp") return ExportFormat::WEBP;
    if (s == "bmp") return ExportFormat::BMP;
    return ExportFormat::PNG;
}

const char* remainderName(const RemainderPolicy r) {
    switch (r) {
        case RemainderPolicy::DISCARD: return "discard";
        case RemainderPolicy::KEEP_PARTIAL: return "keep-partial";
        case RemainderPolicy::PAD: return "pad";
    }
    return "discard";
}
RemainderPolicy remainderFromName(const std::string& s) {
    if (s == "keep-partial") return RemainderPolicy::KEEP_PARTIAL;
    if (s == "pad") return RemainderPolicy::PAD;
    return RemainderPolicy::DISCARD;
}

// ---------------------------------------------------------------------------
// 矩形 / 网格 / optional<int> 子对象映射。
// ---------------------------------------------------------------------------
json rectToJson(const RectRegion& r) {
    return json{{"x1", r.left}, {"y1", r.top}, {"x2", r.right}, {"y2", r.bottom}};
}
RectRegion rectFromJson(const json& v) {
    return RectRegion(jInt(at(v, "x1"), 0), jInt(at(v, "y1"), 0),
                      jInt(at(v, "x2"), 0), jInt(at(v, "y2"), 0));
}

// 网格参数 ↔ JSON：仅「基准点 + 单元尺寸 + 余量策略」（SPEC §3.4.3）；
// 行列数由图像边界自动推导，不作为配置字段（无 gap / cols / rows）。
json gridToJson(const GridParams& g) {
    return json{
        {"originX", g.originX}, {"originY", g.originY},
        {"cellWidth", g.cellWidth}, {"cellHeight", g.cellHeight},
        {"remainder", remainderName(g.remainder)},
    };
}
GridParams gridFromJson(const json& v) {
    GridParams g;
    g.originX = jInt(at(v, "originX"), 0); g.originY = jInt(at(v, "originY"), 0);
    g.cellWidth = jInt(at(v, "cellWidth"), 1); g.cellHeight = jInt(at(v, "cellHeight"), 1);
    g.remainder = remainderFromName(jStr(at(v, "remainder"), "discard"));
    return g;
}

json optIntToJson(const std::optional<int>& o) {
    return o.has_value() ? json(*o) : json(nullptr);
}
std::optional<int> jsonToOptInt(const json& v) {
    if (v.is_number_integer()) return v.get<int>();
    if (v.is_number_float()) return static_cast<int>(v.get<double>());
    return std::nullopt; // null 或缺失 → 无值。
}

// ---------------------------------------------------------------------------
// 预处理步骤 ↔ {op, ...params}（SPEC §7 preprocess 数组）。
// ---------------------------------------------------------------------------
json preprocessToJson(const preprocess::PreprocessConfig& cfg) {
    using namespace preprocess;
    json o;
    switch (configType(cfg)) {
        case PreprocessType::GRAY:
            o["op"] = "gray"; break;
        case PreprocessType::SPLIT:
            o["op"] = "split"; o["mask"] = std::get<SplitOp>(cfg).mask; break;
        case PreprocessType::INVERT:
            o["op"] = "invert"; o["mask"] = std::get<InvertOp>(cfg).mask; break;
        case PreprocessType::ROTATE:
            o["op"] = "rotate"; o["deg"] = std::get<RotateOp>(cfg).angle; break;
        case PreprocessType::FLIP:
            o["op"] = "flip"; o["axis"] = std::get<FlipOp>(cfg).horizontal ? "horizontal" : "vertical"; break;
        case PreprocessType::RESIZE:
            o["op"] = "resize"; o["width"] = std::get<ResizeOp>(cfg).width; o["height"] = std::get<ResizeOp>(cfg).height; break;
    }
    return o;
}

bool preprocessFromJson(const json& v, preprocess::PreprocessConfig& out) {
    using namespace preprocess;
    const std::string op = jStr(at(v, "op"), "");
    if (op == "gray") { out = GrayOp{}; return true; }
    if (op == "split") { SplitOp s; s.mask = jStr(at(v, "mask"), "111"); out = s; return true; }
    if (op == "invert") { InvertOp s; s.mask = jStr(at(v, "mask"), "111"); out = s; return true; }
    if (op == "rotate") { RotateOp s; s.angle = jInt(at(v, "deg"), 0); out = s; return true; }
    if (op == "flip") { FlipOp s; s.horizontal = jStr(at(v, "axis"), "horizontal") == "horizontal"; out = s; return true; }
    if (op == "resize") { ResizeOp s; s.width = jInt(at(v, "width"), 0); s.height = jInt(at(v, "height"), 0); out = s; return true; }
    return false;
}

// ---------------------------------------------------------------------------
// EngineConfig ↔ SPEC §7 schema JSON（文件内私有；仅由下方 save/load 调用）。
// ---------------------------------------------------------------------------
// 把 EngineConfig 序列化为 SPEC §7 schema 的 JSON 值；annoLayer 非空且含标注时额外写 annotations 段。
json engineConfigToJson(const EngineConfig& config, const annotation::AnnotationLayer* annoLayer) {
    json root;

    // source：原图尺寸。
    root["source"] = json{{"width", config.source.width}, {"height", config.source.height}};

    // preprocess：基础图像处理步骤数组。
    json pre = json::array();
    for (const preprocess::PreprocessConfig& step : config.preprocess.steps())
        pre.push_back(preprocessToJson(step));
    root["preprocess"] = pre;

    // cut：层级 / 生成器 / 几何参数 / 极性。
    json cut;
    cut["tier"] = tierName(config.cut.tier);
    cut["generator"] = generatorName(config.cut.generator);
    cut["polarity"] = polarityName(config.cut.polarity);
    if (config.cut.generator == CutGenerator::MULTI_RECT) {
        json rs = json::array();
        for (const RectRegion& r : config.cut.rects) rs.push_back(rectToJson(r));
        cut["params"] = json{{"rects", rs}};
    } else if (config.cut.generator == CutGenerator::GRID) {
        cut["params"] = gridToJson(config.cut.grid);
    } else {
        cut["params"] = rectToJson(config.cut.rect);
    }
    root["cut"] = cut;

    // select：L3 显式选择集（单元序号）。std::vector<int> 直接映射为 JSON 数组。
    root["select"] = config.selectedCells;

    // order：排序策略 + reverse/snake；sequence 预留（自定义序由上层 Sequence 承载）。
    json order;
    order["strategy"] = strategyName(config.order.strategy);
    order["reverse"] = config.order.reverse;
    order["snake"] = config.order.snake;
    order["sequence"] = nullptr;
    root["order"] = order;

    // emit：输出模式 / 合并布局 / 格式 / 命名 / 质量 / 元数据。
    json merge;
    merge["layout"] = layoutName(config.emitParams.layout);
    merge["cols"] = optIntToJson(config.emitParams.cols);
    merge["rows"] = optIntToJson(config.emitParams.rows);
    merge["cellWidth"] = optIntToJson(config.emitParams.cellWidth);
    merge["cellHeight"] = optIntToJson(config.emitParams.cellHeight);
    merge["padColor"] = colorToHex(config.emitParams.padColor);
    // arrange：重排填充顺序（仅 REARRANGE 用；与顶层 order 选择排序正交）。
    json arrange;
    arrange["strategy"] = strategyName(config.emitParams.mergeOrder.strategy);
    arrange["reverse"] = config.emitParams.mergeOrder.reverse;
    arrange["snake"] = config.emitParams.mergeOrder.snake;
    merge["arrange"] = arrange;
    json emit;
    emit["mode"] = emitModeName(config.emitParams.mode);
    emit["merge"] = merge;
    emit["format"] = formatName(config.emitParams.format);
    emit["naming"] = config.emitParams.naming;
    emit["quality"] = config.emitParams.quality;
    emit["keepMetadata"] = config.emitParams.keepMetadata;
    root["emit"] = emit;

    // annotations：标注列表（可选段）——层为空或无标注时省略，保持旧文件观感不变。
    // 段内逐条序列化实现归属 annotation 模块（经 json_common.h 交换 JSON 片段）。
    if (annoLayer != nullptr && !annoLayer->annotations().empty())
        root["annotations"] = annotation::annotationsSegmentToJson(*annoLayer);

    return root;
}

// 从 JSON 值还原 EngineConfig；根非对象返回 false，字段缺失/类型不符取默认值。
// annoLayer 非空时同步还原标注（含缺段→清空：配置文件为标注真相源）；annoErr 非空时逐条回传非法标注原因。
bool engineConfigFromJson(const json& value, EngineConfig& out, annotation::AnnotationLayer* annoLayer,
                          std::string* annoErr) {
    if (!value.is_object()) return false;
    out = EngineConfig{}; // 先复位为默认，再逐字段覆盖。

    // source。
    const json& src = at(value, "source");
    out.source.width = jInt(at(src, "width"), 0);
    out.source.height = jInt(at(src, "height"), 0);

    // preprocess。
    out.preprocess.clear();
    if (const json& pre = at(value, "preprocess"); pre.is_array()) {
        for (const json& step : pre) {
            if (preprocess::PreprocessConfig cfg; preprocessFromJson(step, cfg)) out.preprocess.add(cfg);
        }
    }

    // cut。
    const json& cut = at(value, "cut");
    out.cut.tier = tierFromName(jStr(at(cut, "tier"), "L2"));
    out.cut.generator = generatorFromName(jStr(at(cut, "generator"), "rect"));
    out.cut.polarity = polarityFromName(jStr(at(cut, "polarity"), "remove"));
    const json& params = at(cut, "params");
    if (out.cut.generator == CutGenerator::MULTI_RECT) {
        out.cut.rects.clear();
        if (const json& rs = at(params, "rects"); rs.is_array())
            for (const json& r : rs) out.cut.rects.push_back(rectFromJson(r));
    } else if (out.cut.generator == CutGenerator::GRID) {
        out.cut.grid = gridFromJson(params);
    } else {
        out.cut.rect = rectFromJson(params);
    }

    // select。
    out.selectedCells.clear();
    if (const json& sel = at(value, "select"); sel.is_array())
        for (const json& s : sel) out.selectedCells.push_back(jInt(s, 0));

    // order。
    const json& order = at(value, "order");
    out.order.strategy = strategyFromName(jStr(at(order, "strategy"), "row-major"));
    out.order.reverse = jBool(at(order, "reverse"), false);
    out.order.snake = jBool(at(order, "snake"), false);

    // emit。
    const json& emit = at(value, "emit");
    out.emitParams.mode = emitModeFromName(jStr(at(emit, "mode"), "merged"));
    out.emitParams.format = formatFromName(jStr(at(emit, "format"), "png"));
    out.emitParams.naming = jStr(at(emit, "naming"), "{name}_{index:03d}");
    out.emitParams.quality = jInt(at(emit, "quality"), 90);
    out.emitParams.keepMetadata = jBool(at(emit, "keepMetadata"), false);
    const json& merge = at(emit, "merge");
    out.emitParams.layout = layoutFromName(jStr(at(merge, "layout"), "collapse"));
    out.emitParams.cols = jsonToOptInt(at(merge, "cols"));
    out.emitParams.rows = jsonToOptInt(at(merge, "rows"));
    out.emitParams.cellWidth = jsonToOptInt(at(merge, "cellWidth"));
    out.emitParams.cellHeight = jsonToOptInt(at(merge, "cellHeight"));
    // 兼容 SPEC §7 示例的 "cellSize"：{width,height} / {w,h} / [w,h] 三种写法。
    if (!out.emitParams.cellWidth && !out.emitParams.cellHeight) {
        if (const json& cs = at(merge, "cellSize"); cs.is_array() && cs.size() >= 2) {
            out.emitParams.cellWidth = jInt(cs[0], 0);
            out.emitParams.cellHeight = jInt(cs[1], 0);
        } else if (cs.is_object()) {
            const int w = cs.contains("width") ? jInt(at(cs, "width"), 0) : jInt(at(cs, "w"), 0);
            const int h = cs.contains("height") ? jInt(at(cs, "height"), 0) : jInt(at(cs, "h"), 0);
            out.emitParams.cellWidth = w;
            out.emitParams.cellHeight = h;
        }
    }
    out.emitParams.padColor = colorFromHex(jStr(at(merge, "padColor"), "#00000000"));
    // arrange：重排填充顺序（缺失取默认 row-major / 无 reverse / 无 snake）。
    const json& arrange = at(merge, "arrange");
    out.emitParams.mergeOrder.strategy = strategyFromName(jStr(at(arrange, "strategy"), "row-major"));
    out.emitParams.mergeOrder.reverse = jBool(at(arrange, "reverse"), false);
    out.emitParams.mergeOrder.snake = jBool(at(arrange, "snake"), false);

    // annotations：传入层指针才处理（段缺失/非数组 → 清空，与“配置为真相源”语义一致）。
    // 逐条严格校验、任一非法即整档载入失败（不静默丢弃）——重建逻辑归属 annotation 模块
    // （经 json_common.h 交换 JSON 片段，annoErr 回传逐条原因）。
    if (annoLayer != nullptr) {
        if (!annotation::annotationsFromSegmentJson(at(value, "annotations"), *annoLayer, annoErr))
            return false;
    }

    return true;
}

} // namespace

// 保存配置到文件（缩进美化）；path 为空或写盘失败（E-8）返回 false。annotations 非空时附带 SPEC §7 标注段。
bool saveEngineConfig(const std::string& path, const EngineConfig& config,
                      const annotation::AnnotationLayer* annotations) {
    if (path.empty()) return false;
    const std::string text = engineConfigToJson(config, annotations).dump(2); // 缩进 2 空格美化。
    std::ofstream ofs(path, std::ios::trunc);
    if (!ofs) return false; // E-8：无法创建/写入（目录不存在或被占用）。
    ofs << text;
    ofs.close();
    return !ofs.fail();
}

// 从文件加载配置；path 为空、读盘失败或 JSON 解析失败返回 false。annotations 非空时同步还原标注（严格校验）。
// annoErr 非空时，若因标注段非法而失败，回传逐条原因（供 CLI 报因；GUI 可传 nullptr）。
bool loadEngineConfig(const std::string& path, EngineConfig& out,
                      annotation::AnnotationLayer* annotations, std::string* annoErr) {
    if (path.empty()) return false;
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return false; // E-8：文件不存在或不可读。
    try {
        const json value = json::parse(ifs); // 直接从流解析；语法错误抛 parse_error。
        return engineConfigFromJson(value, out, annotations, annoErr);
    } catch (const json::exception&) {
        return false; // JSON 语法错误（对齐原“解析失败返回 false”契约）。
    }
}

} // namespace idc::engine
