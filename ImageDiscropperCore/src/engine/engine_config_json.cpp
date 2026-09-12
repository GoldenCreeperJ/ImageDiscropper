// ============================================================================
// 文件：src/engine/engine_config_json.cpp
// 作用：engine_config_json.h 的实现——EngineConfig 与 §9 schema JSON 的双向映射与文件存取。
// 分块依据：集中承载“配置字段 ↔ JSON 键”的语义映射（枚举串、颜色十六进制、矩形/网格
//       子对象、预处理步骤数组）；JSON 语法委托第三方库 nlohmann/json，本文件不手写解析器。
//       转换函数（engineConfigToJson / engineConfigFromJson）为文件内私有实现，仅由
//       saveEngineConfig / loadEngineConfig 调用，故 nlohmann/json 依赖不泄漏到公共头（PRIVATE）。
// 说明：读取一律宽容（缺字段/类型不符取默认值，绝不抛异常给调用方），写出严格对齐 §9
//       示例的键名与嵌套结构；nlohmann::json 以 std::map 存对象，序列化按键升序输出。
// ============================================================================
#include "engine/engine_config_json.h"

#include "preprocess/preprocess_config.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <fstream>
#include <optional>
#include <string>

namespace idc::engine {
namespace {

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// 枚举 ↔ 短横线小写串（§9：如 "row-major" / "multi-rect"）。
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
// 颜色 ↔ "#AARRGGBB"（§9 示例 "#00000000" 即全透明）。
// ---------------------------------------------------------------------------
std::string colorToHex(const core::Color& c) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X%02X", c.a, c.r, c.g, c.b);
    return std::string(buf);
}

// 解析两位十六进制；越界或非法返回 fallback。
int hex2(const std::string& s, const std::size_t pos, const int fallback) {
    if (pos + 2 > s.size()) return fallback;
    int v = 0;
    for (int i = 0; i < 2; ++i) {
        const char c = s[pos + static_cast<std::size_t>(i)];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= (c - '0');
        else if (c >= 'a' && c <= 'f') v |= (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (c - 'A' + 10);
        else return fallback;
    }
    return v;
}

core::Color colorFromHex(const std::string& str) {
    std::string h = str;
    if (!h.empty() && h[0] == '#') h.erase(0, 1);
    if (h.size() >= 8) { // AARRGGBB
        return core::Color(static_cast<std::uint8_t>(hex2(h, 2, 0)),
                           static_cast<std::uint8_t>(hex2(h, 4, 0)),
                           static_cast<std::uint8_t>(hex2(h, 6, 0)),
                           static_cast<std::uint8_t>(hex2(h, 0, 255)));
    }
    if (h.size() >= 6) { // RRGGBB（不透明）
        return core::Color(static_cast<std::uint8_t>(hex2(h, 0, 0)),
                           static_cast<std::uint8_t>(hex2(h, 2, 0)),
                           static_cast<std::uint8_t>(hex2(h, 4, 0)),
                           255);
    }
    return core::kTransparent;
}

// ---------------------------------------------------------------------------
// 宽容读取助手：nlohmann 的 const operator[] 对缺失键会断言，故统一经 at() 取子元素，
//       再按类型安全取值；缺失或类型不符时返回给定默认值（与原手写实现的语义一致）。
// ---------------------------------------------------------------------------
// 安全取对象子元素：obj 非对象或键缺失时返回静态 null 单例（避免断言/悬垂）。
const json& at(const json& obj, const char* key) {
    static const json kNull = nullptr;
    if (!obj.is_object()) return kNull;
    const auto it = obj.find(key);
    return (it == obj.end()) ? kNull : *it;
}

int jInt(const json& v, const int def) {
    if (v.is_number_integer()) return v.get<int>();                    // 整数直接取。
    if (v.is_number_float()) return static_cast<int>(v.get<double>()); // 浮点截断。
    return def;                                                        // 其余（含 null/缺失）取默认。
}

std::string jStr(const json& v, const char* def) {
    return v.is_string() ? v.get<std::string>() : std::string(def);
}

bool jBool(const json& v, const bool def) {
    return v.is_boolean() ? v.get<bool>() : def;
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

// 网格参数 ↔ JSON：仅「基准点 + 单元尺寸 + 余量策略」（终稿 §4.4.4）；
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
// 预处理步骤 ↔ {op, ...params}（§9 preprocess 数组）。
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
    if (op == "flip") { FlipOp s; s.horizontal = (jStr(at(v, "axis"), "horizontal") == "horizontal"); out = s; return true; }
    return false;
}

// ---------------------------------------------------------------------------
// EngineConfig ↔ §9 schema JSON（文件内私有；仅由下方 save/load 调用）。
// ---------------------------------------------------------------------------
// 把 EngineConfig 序列化为 §9 schema 的 JSON 值。
json engineConfigToJson(const EngineConfig& config) {
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
    merge["layout"] = layoutName(config.emit.layout);
    merge["cols"] = optIntToJson(config.emit.cols);
    merge["rows"] = optIntToJson(config.emit.rows);
    merge["cellWidth"] = optIntToJson(config.emit.cellWidth);
    merge["cellHeight"] = optIntToJson(config.emit.cellHeight);
    merge["padColor"] = colorToHex(config.emit.padColor);
    json emit;
    emit["mode"] = emitModeName(config.emit.mode);
    emit["merge"] = merge;
    emit["format"] = formatName(config.emit.format);
    emit["naming"] = config.emit.naming;
    emit["quality"] = config.emit.quality;
    emit["keepMetadata"] = config.emit.keepMetadata;
    root["emit"] = emit;

    return root;
}

// 从 JSON 值还原 EngineConfig；根非对象返回 false，字段缺失/类型不符取默认值。
bool engineConfigFromJson(const json& value, EngineConfig& out) {
    if (!value.is_object()) return false;
    out = EngineConfig{}; // 先复位为默认，再逐字段覆盖。

    // source。
    const json& src = at(value, "source");
    out.source.width = jInt(at(src, "width"), 0);
    out.source.height = jInt(at(src, "height"), 0);

    // preprocess。
    out.preprocess.clear();
    const json& pre = at(value, "preprocess");
    if (pre.is_array()) {
        for (const json& step : pre) {
            preprocess::PreprocessConfig cfg;
            if (preprocessFromJson(step, cfg)) out.preprocess.add(cfg);
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
        const json& rs = at(params, "rects");
        if (rs.is_array())
            for (const json& r : rs) out.cut.rects.push_back(rectFromJson(r));
    } else if (out.cut.generator == CutGenerator::GRID) {
        out.cut.grid = gridFromJson(params);
    } else {
        out.cut.rect = rectFromJson(params);
    }

    // select。
    out.selectedCells.clear();
    const json& sel = at(value, "select");
    if (sel.is_array())
        for (const json& s : sel) out.selectedCells.push_back(jInt(s, 0));

    // order。
    const json& order = at(value, "order");
    out.order.strategy = strategyFromName(jStr(at(order, "strategy"), "row-major"));
    out.order.reverse = jBool(at(order, "reverse"), false);
    out.order.snake = jBool(at(order, "snake"), false);

    // emit。
    const json& emit = at(value, "emit");
    out.emit.mode = emitModeFromName(jStr(at(emit, "mode"), "merged"));
    out.emit.format = formatFromName(jStr(at(emit, "format"), "png"));
    out.emit.naming = jStr(at(emit, "naming"), "{name}_{index:03d}");
    out.emit.quality = jInt(at(emit, "quality"), 90);
    out.emit.keepMetadata = jBool(at(emit, "keepMetadata"), false);
    const json& merge = at(emit, "merge");
    out.emit.layout = layoutFromName(jStr(at(merge, "layout"), "collapse"));
    out.emit.cols = jsonToOptInt(at(merge, "cols"));
    out.emit.rows = jsonToOptInt(at(merge, "rows"));
    out.emit.cellWidth = jsonToOptInt(at(merge, "cellWidth"));
    out.emit.cellHeight = jsonToOptInt(at(merge, "cellHeight"));
    // 兼容 §9 示例的 "cellSize"：{width,height} / {w,h} / [w,h] 三种写法。
    if (!out.emit.cellWidth && !out.emit.cellHeight) {
        const json& cs = at(merge, "cellSize");
        if (cs.is_array() && cs.size() >= 2) {
            out.emit.cellWidth = jInt(cs[0], 0);
            out.emit.cellHeight = jInt(cs[1], 0);
        } else if (cs.is_object()) {
            const int w = cs.contains("width") ? jInt(at(cs, "width"), 0) : jInt(at(cs, "w"), 0);
            const int h = cs.contains("height") ? jInt(at(cs, "height"), 0) : jInt(at(cs, "h"), 0);
            out.emit.cellWidth = w;
            out.emit.cellHeight = h;
        }
    }
    out.emit.padColor = colorFromHex(jStr(at(merge, "padColor"), "#00000000"));

    return true;
}

} // namespace

// 保存配置到文件（缩进美化）；path 为空或写盘失败（E-8）返回 false。
bool saveEngineConfig(const std::string& path, const EngineConfig& config) {
    if (path.empty()) return false;
    const std::string text = engineConfigToJson(config).dump(2); // 缩进 2 空格美化。
    std::ofstream ofs(path, std::ios::trunc);
    if (!ofs) return false; // E-8：无法创建/写入（目录不存在或被占用）。
    ofs << text;
    ofs.close();
    return !ofs.fail();
}

// 从文件加载配置；path 为空、读盘失败或 JSON 解析失败返回 false。
bool loadEngineConfig(const std::string& path, EngineConfig& out) {
    if (path.empty()) return false;
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return false; // E-8：文件不存在或不可读。
    try {
        const json value = json::parse(ifs); // 直接从流解析；语法错误抛 parse_error。
        return engineConfigFromJson(value, out);
    } catch (const json::exception&) {
        return false; // JSON 语法错误（对齐原“解析失败返回 false”契约）。
    }
}

} // namespace idc::engine
