// ============================================================================
// 文件：src/value_parser.cpp
// 作用：实现 value_parser.h——把命令行字符串解析为 Core 强类型，并做格式校验。
// 分块依据：纯「字符串 → 类型」转换，无副作用、不触碰引擎；失败一律返回 false 并写 err，
//       由命令层组装为退出码 1（§5.3 第 5 步：参数格式校验在调用 Core 之前完成）。
// 说明：整数解析用 strtol 全量消费校验（拒绝 "12abc" / 空串 / 溢出）；颜色十六进制解析
//       与 Core 配置约定（#RRGGBB / #AARRGGBB）一致。
// ============================================================================
#include "value_parser.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>

namespace idc::cli {
namespace {

// 转小写（ASCII 足够，用于枚举 / 扩展名比较）。
std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// 严格整数解析：整串必须是一个十进制整数（允许前导 '+'/'-'），否则失败。
bool toInt(const std::string& s, int& out) {
    if (s.empty()) return false;
    errno = 0;
    char* end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 10);
    if (errno == ERANGE || end == s.c_str() || *end != '\0') return false;
    out = static_cast<int>(v);
    return true;
}

// 按分隔符切分（保留空段，便于校验字段个数）。
std::vector<std::string> split(const std::string& s, const char delim) {
    std::vector<std::string> out;
    std::string cur;
    for (const char c : s) {
        if (c == delim) { out.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    out.push_back(cur);
    return out;
}

// 解析单个十六进制字符为 0~15；非法返回 -1。
int hexNibble(const char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// 解析两位十六进制字节；越界或非法返回 false。
bool hexByte(const std::string& s, const std::size_t pos, std::uint8_t& out) {
    if (pos + 2 > s.size()) return false;
    const int hi = hexNibble(s[pos]);
    const int lo = hexNibble(s[pos + 1]);
    if (hi < 0 || lo < 0) return false;
    out = static_cast<std::uint8_t>(hi << 4 | lo);
    return true;
}

} // namespace

bool parseRect(const std::string& s, engine::RectRegion& out, std::string& err) {
    const std::vector<std::string> p = split(s, ',');
    if (p.size() != 4) { err = "矩形须为 x1,y1,x2,y2 四个整数"; return false; }
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    if (!toInt(p[0], x1) || !toInt(p[1], y1) || !toInt(p[2], x2) || !toInt(p[3], y2)) {
        err = "矩形坐标含非整数值：'" + s + "'";
        return false;
    }
    out = engine::RectRegion(x1, y1, x2, y2);
    return true;
}

bool parseBand(const std::string& s, int& a, int& b, std::string& err) {
    const std::vector<std::string> p = split(s, ',');
    if (p.size() != 2) { err = "带须为 a,b 两个整数（起始, 结束）"; return false; }
    if (!toInt(p[0], a) || !toInt(p[1], b)) { err = "带坐标含非整数值：'" + s + "'"; return false; }
    return true;
}

bool parseGridGeo(const std::string& s, int& x0, int& y0, int& cw, int& ch, std::string& err) {
    const std::vector<std::string> p = split(s, ',');
    if (p.size() != 4) { err = "网格须为 x0,y0,cw,ch 四个整数（基准点 + 单元尺寸）"; return false; }
    if (!toInt(p[0], x0) || !toInt(p[1], y0) || !toInt(p[2], cw) || !toInt(p[3], ch)) {
        err = "网格参数含非整数值：'" + s + "'";
        return false;
    }
    // 单元尺寸必须为正——否则无法产生贯穿全图的周期切割线（终稿 §4.4.4）。
    if (cw <= 0 || ch <= 0) { err = "单元尺寸 cw,ch 必须为正整数"; return false; }
    return true;
}

bool parseCell(const std::string& s, int& r, int& c, std::string& err) {
    const std::vector<std::string> p = split(s, ',');
    if (p.size() != 2) { err = "单元须为 r,c 两个整数（行, 列，从 0 起）"; return false; }
    if (!toInt(p[0], r) || !toInt(p[1], c)) { err = "单元坐标含非整数值：'" + s + "'"; return false; }
    if (r < 0 || c < 0) { err = "单元行列号不可为负：'" + s + "'"; return false; }
    return true;
}

bool parseCanvas(const std::string& s, int& cols, int& rows, std::string& err) {
    // 画布 "ColxRow"：以 'x'/'X' 分隔列数与行数（重排画布的单元列 × 行，非像素尺寸）。
    const std::string low = toLower(s);
    const std::size_t x = low.find('x');
    if (x == std::string::npos) { err = "画布须为 ColxRow 形式（如 8x6）"; return false; }
    if (!toInt(s.substr(0, x), cols) || !toInt(s.substr(x + 1), rows)) {
        err = "画布 ColxRow 含非整数值：'" + s + "'";
        return false;
    }
    if (cols <= 0 || rows <= 0) { err = "画布列数 / 行数必须为正"; return false; }
    return true;
}

bool parseOrderCells(const std::string& s, std::vector<std::pair<int, int>>& out, std::string& err) {
    out.clear();
    const std::vector<std::string> items = split(s, ';');
    for (const std::string& item : items) {
        if (item.empty()) continue; // 容忍尾随分号。
        int r = 0, c = 0;
        if (!parseCell(item, r, c, err)) return false;
        out.emplace_back(r, c);
    }
    if (out.empty()) { err = "自定义序列 --order 为空（须形如 r,c;r,c）"; return false; }
    return true;
}

bool parseColor(const std::string& s, core::Color& out, std::string& err) {
    std::string h = s;
    if (!h.empty() && h.front() == '#') h.erase(0, 1);
    // #RGB：每位十六进制扩展为一个字节（0xF → 0xFF）。
    if (h.size() == 3) {
        const int r = hexNibble(h[0]), g = hexNibble(h[1]), b = hexNibble(h[2]);
        if (r < 0 || g < 0 || b < 0) { err = "颜色含非法十六进制字符：'" + s + "'"; return false; }
        out = core::Color(static_cast<std::uint8_t>(r * 17),
                               static_cast<std::uint8_t>(g * 17),
                               static_cast<std::uint8_t>(b * 17), 255);
        return true;
    }
    if (h.size() == 6) { // #RRGGBB（不透明）。
        std::uint8_t r = 0, g = 0, b = 0;
        if (!hexByte(h, 0, r) || !hexByte(h, 2, g) || !hexByte(h, 4, b)) {
            err = "颜色含非法十六进制字符：'" + s + "'"; return false;
        }
        out = core::Color(r, g, b, 255);
        return true;
    }
    if (h.size() == 8) { // #AARRGGBB（与 Core 配置约定一致）。
        std::uint8_t a = 0, r = 0, g = 0, b = 0;
        if (!hexByte(h, 0, a) || !hexByte(h, 2, r) || !hexByte(h, 4, g) || !hexByte(h, 6, b)) {
            err = "颜色含非法十六进制字符：'" + s + "'"; return false;
        }
        out = core::Color(r, g, b, a);
        return true;
    }
    err = "颜色须为 #RGB / #RRGGBB / #AARRGGBB：'" + s + "'";
    return false;
}

bool parseFormat(const std::string& s, engine::ExportFormat& out, std::string& err) {
    const std::string f = toLower(s);
    if (f == "png") { out = engine::ExportFormat::PNG; return true; }
    if (f == "jpeg" || f == "jpg") { out = engine::ExportFormat::JPEG; return true; }
    if (f == "webp") { out = engine::ExportFormat::WEBP; return true; }
    if (f == "bmp") { out = engine::ExportFormat::BMP; return true; }
    err = "不支持的格式：'" + s + "'（可选 png/jpeg/webp/bmp）";
    return false;
}

bool parseSort(const std::string& s, engine::SortStrategy& out, std::string& err) {
    const std::string v = toLower(s);
    if (v == "row-major") { out = engine::SortStrategy::ROW_MAJOR; return true; }
    if (v == "column-major") { out = engine::SortStrategy::COLUMN_MAJOR; return true; }
    if (v == "custom") { out = engine::SortStrategy::CUSTOM; return true; }
    err = "不支持的排序策略：'" + s + "'（可选 row-major/column-major/custom）";
    return false;
}

bool parseMargin(const std::string& s, engine::RemainderPolicy& out, std::string& err) {
    const std::string v = toLower(s);
    if (v == "discard") { out = engine::RemainderPolicy::DISCARD; return true; }
    if (v == "keep-partial") { out = engine::RemainderPolicy::KEEP_PARTIAL; return true; }
    if (v == "pad") { out = engine::RemainderPolicy::PAD; return true; }
    err = "不支持的余量策略：'" + s + "'（可选 discard/keep-partial/pad）";
    return false;
}

bool parseMerge(const std::string& s, engine::MergeLayout& out, std::string& err) {
    const std::string v = toLower(s);
    if (v == "collapse") { out = engine::MergeLayout::COLLAPSE; return true; }
    if (v == "rearrange") { out = engine::MergeLayout::REARRANGE; return true; }
    err = "不支持的合并方式：'" + s + "'（可选 collapse/rearrange）";
    return false;
}

bool parseDecorate(const std::string& s, bool& reverse, bool& snake, std::string& err) {
    const std::string v = toLower(s);
    if (v == "none") { reverse = false; snake = false; return true; }
    if (v == "reverse") { reverse = true; snake = false; return true; }
    if (v == "snake") { reverse = false; snake = true; return true; }
    // Core 先施加 snake 再整体 reverse（固定次序），故两种写法结果相同（见头文件说明）。
    if (v == "reverse-snake" || v == "snake-reverse") { reverse = true; snake = true; return true; }
    err = "不支持的排序修饰：'" + s + "'（可选 none/reverse/snake/reverse-snake/snake-reverse）";
    return false;
}

bool checkNaming(const std::string& s, std::string& err) {
    if (s.empty()) { err = "命名模板不可为空"; return false; }
    // 拒绝路径分隔符——命名模板只用于生成文件名，不应引入子目录（防止越界写盘）。
    if (s.find('/') != std::string::npos || s.find('\\') != std::string::npos) {
        err = "命名模板不可包含路径分隔符 '/' 或 '\\'：'" + s + "'";
        return false;
    }
    return true;
}

} // namespace idc::cli
