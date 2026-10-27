// ============================================================================
// 文件：src/layer_args.cpp
// 作用：layer_args.h 的实现——读取 --layer / --layers 两类命令行图层选项，
//       经 Core 的 parseImageLayersFromJsonText 解析为 ImageLayer 列表并累积。
// 分块依据：CLI 不解析 JSON（委托 Core），本文件只做「取选项值 + 读文件 + 调 Core + 汇总错误」，
//       无合成 / 落位 / 引擎语义（CONTRIBUTING.md「分层纪律」）。文件读入用标准库 ifstream（与 job 读图同层依赖）。
// 常量层：选项名取 texts::opt（不可译）、报因文案经门面 IDC_CTR(msg, ...) 取 texts::msg。
// ============================================================================
#include "layer_args.h"

#include <fstream>
#include <ranges>
#include <sstream>
#include <vector>

#include "canvas/layer.h"      // canvas::ImageLayer（Core 合成 / 落位数据模型）
#include "engine/engine_config_json.h" // engine::parseImageLayersFromJsonText（图层段级解析，委托 Core）
#include "command_support.h"   // readFileText（整文件读入，与 annotation_args 共用）
#include "i18n/cli_texts.h"
#include "i18n/cli_i18n.h"

namespace idc::cli {

// 选项名简写（不可译契约；报因文案走 IDC_CTR）。
namespace opts = texts::opt;

namespace {

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

bool isNumberOrBool(const std::string& v) {
    if (v == "true" || v == "false") return true;
    bool digits = !v.empty();
    for (const char c : v)
        if (c != '-' && c != '+' && !(c >= '0' && c <= '9')) { digits = false; break; }
    return digits;
}

// 顶层对切分：逗号或空白为界（括号 []/{} 与引号内不切）——块形以空白分隔（image={...} backdrop={...}）、
//   平铺以逗号分隔（{file=a,imageOpacity=200}）；非引号字符串值内勿含空白（含则加引号写标准 JSON）。括号不平衡 → false。
bool splitPairs(const std::string& body, std::vector<std::pair<std::string, std::string>>& out) {
    int depth = 0;
    bool inStr = false;
    bool esc = false;
    std::size_t start = 0;
    bool inToken = false;
    for (std::size_t i = 0; i < body.size(); ++i) {
        const char c = body[i];
        if (esc) { esc = false; inToken = true; continue; }
        if (c == '\\') { esc = true; inToken = true; continue; }
        if (c == '"') { inStr = !inStr; inToken = true; continue; }
        if (inStr) { inToken = true; continue; }
        if (c == '{' || c == '[') { ++depth; inToken = true; continue; }
        if (c == '}' || c == ']') { --depth; inToken = true; continue; }
        if (depth < 0) return false;
        if ((c == ',' || c == ' ' || c == '\t' || c == '\r' || c == '\n') && depth == 0) {
            if (inToken) {
                out.emplace_back(trim(body.substr(start, i - start)), std::string{});
                inToken = false;
            }
            start = i + 1;
            continue;
        }
        inToken = true;
    }
    if (depth != 0) return false;
    if (inToken) {
        if (const std::string last = trim(body.substr(start)); !last.empty()) out.emplace_back(last, std::string{});
    }
    return !out.empty();
}

// 顶层 '=' 切分（括号/引号保护）。
bool splitKV(const std::string& pair, std::string& k, std::string& v) {
    int depth = 0;
    bool inStr = false;
    bool esc = false;
    for (std::size_t i = 0; i < pair.size(); ++i) {
        const char c = pair[i];
        if (esc) { esc = false; continue; }
        if (c == '\\') { esc = true; continue; }
        if (c == '"') { inStr = !inStr; continue; }
        if (inStr) continue;
        if (c == '{' || c == '[') ++depth;
        else if (c == '}' || c == ']') --depth;
        if (depth < 0) return false;
        if (c == '=' && depth == 0) {
            k = trim(pair.substr(0, i));
            v = trim(pair.substr(i + 1));
            return !k.empty();
        }
    }
    return false;
}

// 值 → JSON 片段：对象/数组/数值/布尔原样；其余按字符串加引号（内部引号转义）。
std::string valueToJson(const std::string& v) {
    if (v.empty()) return {};
    if (v.front() == '{' || v.front() == '[' || isNumberOrBool(v)) return v;
    std::string out = "\"";
    for (const char c : v) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    out += '"';
    return out;
}

// 把 --layer 内联值归一为 §7 v2 JSON 对象文本（平铺 / 块形两种语法等价）。
//   已是标准 JSON（含引号）→ 原样；平铺 {file=icon.png,imageOpacity=200,...}（外层大括号可省）/
//   块形 image={...} backdrop={...}（可带外层大括号）→ 拼 §7 v2 对象。
//   平铺键映射：file/imageFit/imageOpacity/imagePreprocess/imageAnchor/imagePivot/imageX/imageY/
//   boxWidth/boxHeight → image.*；backdropPreprocess/backdropOpacity → backdrop.*；
//   image/backdrop/transform/preprocess/zOrder → 顶层透传。嵌套对象内仍用标准 JSON 写法。
bool normalizeLayerArg(const std::string& text, std::string& out, std::string& err) {
    std::string body = trim(text);
    if (body.empty()) return false;
    // 判定：标准 JSON 数组（[ 开头）或对象（{" 开头）→ 原样；其余按平铺/块形（去外层大括号后按对解析，
    //   嵌套值可含引号——JSON 对象与平铺的区分只看出现在 { 之后的第一个字符是否为 '"'）。
    if (body.front() == '[') { out = body; return true; }
    if (body.front() == '{') {
        if (const std::size_t after = body.find_first_not_of(" \t\r\n", 1); after != std::string::npos && body[after] == '"') { out = body; return true; }
        if (body.back() != '}') { err = IDC_CTR(msg, kErrLayerFlatSyntax) + text; return false; }   // 缺尾括号：报语法错，不截尾静默改义。
        body = trim(body.substr(1, body.size() - 2));
        if (body.empty()) return false;
    }

    std::vector<std::pair<std::string, std::string>> pairs;
    if (!splitPairs(body, pairs)) { err = IDC_CTR(msg, kErrLayerFlatSyntax) + text; return false; }

    std::vector<std::string> imgKeys;
    std::vector<std::string> bdKeys;
    std::vector<std::string> topKeys;
    for (auto &pairText: pairs | std::views::keys) {
        std::string k;
        std::string v;
        if (!splitKV(pairText, k, v) || v.empty()) { err = IDC_CTR(msg, kErrLayerFlatSyntax) + text; return false; }
        const std::string jv = valueToJson(v);
        if (k == "image" || k == "backdrop" || k == "transform") { topKeys.push_back("\"" + k + "\":" + v); continue; }
        if (k == "preprocess") { topKeys.push_back("\"preprocess\":" + jv); continue; }
        if (k == "zOrder") { topKeys.push_back("\"zOrder\":" + jv); continue; }
        if (k == "file") { imgKeys.push_back("\"file\":" + jv); continue; }
        if (k == "imageFit") { imgKeys.push_back("\"fit\":" + jv); continue; }
        if (k == "imageOpacity") { imgKeys.push_back("\"opacity\":" + jv); continue; }
        if (k == "imagePreprocess") { imgKeys.push_back("\"preprocess\":" + jv); continue; }
        if (k == "imageAnchor") { imgKeys.push_back("\"anchor\":" + jv); continue; }
        if (k == "imagePivot") { imgKeys.push_back("\"pivot\":" + jv); continue; }
        if (k == "imageX") { imgKeys.push_back("\"x\":" + jv); continue; }
        if (k == "imageY") { imgKeys.push_back("\"y\":" + jv); continue; }
        if (k == "boxWidth") { imgKeys.push_back("\"boxWidth\":" + jv); continue; }
        if (k == "boxHeight") { imgKeys.push_back("\"boxHeight\":" + jv); continue; }
        if (k == "backdropPreprocess") { bdKeys.push_back("\"preprocess\":" + jv); continue; }
        if (k == "backdropOpacity") { bdKeys.push_back("\"opacity\":" + jv); continue; }
        err = IDC_CTR(msg, kErrLayerFlatSyntax) + text;   // 未知键。
        return false;
    }

    auto join = [](const std::vector<std::string>& items) {
        std::string s;
        for (std::size_t i = 0; i < items.size(); ++i) { if (i) s += ','; s += items[i]; }
        return s;
    };
    // 合并：块形 image={...}/backdrop={...} 与平铺 image.*/backdrop.* 键并入同一元素对象（避免重复键后者覆盖前者）。
    const auto mergeInto = [](const std::string& objVal, const std::vector<std::string>& extra) {
        std::string inner = trim(objVal);
        if (inner.size() >= 2 && inner.front() == '{' && inner.back() == '}') inner = trim(inner.substr(1, inner.size() - 2));
        for (const std::string& e : extra) { if (!inner.empty()) inner += ','; inner += e; }
        return "{" + inner + "}";
    };
    bool hasImageObj = false;
    bool hasBdObj = false;
    std::string imageObj;
    std::string bdObj;
    std::vector<std::string> restTop;
    for (const std::string& t : topKeys) {
        if (t.rfind("\"image\":", 0) == 0) { hasImageObj = true; imageObj = t.substr(8); continue; }
        if (t.rfind("\"backdrop\":", 0) == 0) { hasBdObj = true; bdObj = t.substr(11); continue; }
        restTop.push_back(t);
    }
    std::string obj;
    const auto append = [&](const std::string& part) { if (!obj.empty()) obj += ','; obj += part; };
    if (hasImageObj || !imgKeys.empty())
        append("\"image\":" + (hasImageObj ? mergeInto(imageObj, imgKeys) : "{" + join(imgKeys) + "}"));
    if (hasBdObj || !bdKeys.empty())
        append("\"backdrop\":" + (hasBdObj ? mergeInto(bdObj, bdKeys) : "{" + join(bdKeys) + "}"));
    for (const std::string& t : restTop) append(t);
    if (obj.empty()) { err = IDC_CTR(msg, kErrLayerFlatSyntax) + text; return false; }
    out = "{" + obj + "}";
    return true;
}

} // namespace

bool hasLayerOptions(const ArgParser& args) {
    return args.has(opts::kLayer) || args.has(opts::kLayers);
}

bool collectLayers(const ArgParser& args, std::vector<canvas::ImageLayer>& out, std::string& err,
                   const int basisWidth, const int basisHeight) {
    // 1) 内联：--layer 可重复，逐条归一（平铺/块形 → v2 JSON 对象）后解析追加（保持出现顺序）。
    for (const std::string& item : args.getAll(opts::kLayer)) {
        std::string json;
        if (std::string normalizeErr; !normalizeLayerArg(item, json, normalizeErr)) {
            err = normalizeErr;
            return false;
        }
        if (!engine::parseImageLayersFromJsonText(json, out, err, basisWidth, basisHeight))
            return false; // err 由 Core 给出（语法错误 / 根类型非法 / 无有效条目 / 百分比无基准）。
    }

    // 2) 文件：--layers 指向「根为图层数组（或单对象）」的 JSON 文件；可重复出现亦逐个累积。
    for (const std::string& path : args.getAll(opts::kLayers)) {
        std::string text;
        if (!readFileText(path, text)) {
            err = IDC_CTR(msg, kErrLayerFileUnreadable) + path;
            return false;
        }
        if (!engine::parseImageLayersFromJsonText(text, out, err, basisWidth, basisHeight))
            return false;
    }
    return true;
}

// 追加层 zOrder 定序：未给（§7 缺省 0）→ 按「既有最大 zOrder + 1」起自增续尾；显式非 0 尊重原值。
// existing 传配置自带层（无则空表，追加层从 0 起自增）。
void finalizeAppendedLayers(const std::vector<canvas::ImageLayer>& existing,
                            std::vector<canvas::ImageLayer>& appended) {
    int maxZ = -1;
    for (const canvas::ImageLayer& l : existing) maxZ = std::max(maxZ, l.zOrder);
    for (std::size_t i = 0; i < appended.size(); ++i) {
        if (canvas::ImageLayer& l = appended[i]; l.zOrder == 0) l.zOrder = maxZ + 1 + static_cast<int>(i);
        else maxZ = std::max(maxZ, l.zOrder);
    }
}

} // namespace idc::cli
