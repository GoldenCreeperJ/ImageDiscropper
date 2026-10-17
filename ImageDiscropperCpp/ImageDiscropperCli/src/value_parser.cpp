// ============================================================================
// 文件：src/value_parser.cpp
// 作用：实现 value_parser.h——把命令行字符串解析为 Core 强类型，并做格式校验。
// 分块依据：纯「字符串 → 类型」转换，无副作用、不触碰引擎；失败一律返回 false 并写 err，
//       由命令层组装为退出码 1（第 5 步：参数格式校验在调用 Core 之前完成）。
// 说明：整数解析用 strtol 全量消费校验（拒绝 "12abc" / 空串 / 溢出）；颜色十六进制解析
//       与 Core 配置约定（#RRGGBB / #AARRGGBB）一致。
// 常量层：报因文案经门面 IDC_CTR(msg, ...) 取 texts::msg、质量范围取 nums::parser；
//       各枚举的 token 值（png / row-major / discard / collapse / none …）与枚举成对出现在映射表里，
//       属「数据即定义」，**不提**。
// ============================================================================
#include "value_parser.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>

#include "cli_nums.h"
#include "i18n/cli_texts.h"
#include "i18n/cli_i18n.h"

namespace idc::cli {

namespace {

// 转小写（ASCII 足够，用于枚举 / 扩展名比较）。
std::string toLower(std::string s) {
    std::ranges::transform(s, s.begin(),
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
    if (p.size() != 4) { err = IDC_CTR(msg, kErrRectFourInts); return false; }
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    if (!toInt(p[0], x1) || !toInt(p[1], y1) || !toInt(p[2], x2) || !toInt(p[3], y2)) {
        err = IDC_CTR(msg, kErrRectNotInt) + s + "'";
        return false;
    }
    out = engine::RectRegion(x1, y1, x2, y2);
    return true;
}

bool parseBand(const std::string& s, int& a, int& b, std::string& err) {
    const std::vector<std::string> p = split(s, ',');
    if (p.size() != 2) { err = IDC_CTR(msg, kErrBandTwoInts); return false; }
    if (!toInt(p[0], a) || !toInt(p[1], b)) { err = IDC_CTR(msg, kErrBandNotInt) + s + "'"; return false; }
    return true;
}

bool parseGridGeo(const std::string& s, int& x0, int& y0, int& cw, int& ch, std::string& err) {
    const std::vector<std::string> p = split(s, ',');
    if (p.size() != 4) { err = IDC_CTR(msg, kErrGridFourInts); return false; }
    if (!toInt(p[0], x0) || !toInt(p[1], y0) || !toInt(p[2], cw) || !toInt(p[3], ch)) {
        err = IDC_CTR(msg, kErrGridNotInt) + s + "'";
        return false;
    }
    // 单元尺寸必须为正——否则无法产生贯穿全图的周期切割线（SPEC §3.4.3）。
    if (cw <= 0 || ch <= 0) { err = IDC_CTR(msg, kErrCellSizePositive); return false; }
    return true;
}

bool parseCell(const std::string& s, int& r, int& c, std::string& err) {
    const std::vector<std::string> p = split(s, ',');
    if (p.size() != 2) { err = IDC_CTR(msg, kErrCellTwoInts); return false; }
    if (!toInt(p[0], r) || !toInt(p[1], c)) { err = IDC_CTR(msg, kErrCellNotInt) + s + "'"; return false; }
    if (r < 0 || c < 0) { err = IDC_CTR(msg, kErrCellNegative) + s + "'"; return false; }
    return true;
}

bool parseCanvas(const std::string& s, int& cols, int& rows, std::string& err) {
    // 画布 "ColxRow"：以 'x'/'X' 分隔列数与行数（重排画布的单元列 × 行，非像素尺寸）。
    const std::string low = toLower(s);
    const std::size_t x = low.find('x');
    if (x == std::string::npos) { err = IDC_CTR(msg, kErrCanvasForm); return false; }
    if (!toInt(s.substr(0, x), cols) || !toInt(s.substr(x + 1), rows)) {
        err = IDC_CTR(msg, kErrCanvasNotInt) + s + "'";
        return false;
    }
    if (cols <= 0 || rows <= 0) { err = IDC_CTR(msg, kErrCanvasPositive); return false; }
    return true;
}

bool parseOrderCells(const std::string& s, std::vector<std::pair<int, int>>& out, std::string& err) {
    out.clear();
    for (const std::vector<std::string> items = split(s, ';'); const std::string& item : items) {
        if (item.empty()) continue; // 容忍尾随分号。
        int r = 0, c = 0;
        if (!parseCell(item, r, c, err)) return false;
        out.emplace_back(r, c);
    }
    if (out.empty()) { err = IDC_CTR(msg, kErrOrderEmpty); return false; }
    return true;
}

bool parseColor(const std::string& s, core::Color& out, std::string& err) {
    std::string h = s;
    if (!h.empty() && h.front() == '#') h.erase(0, 1);
    // #RGB：每位十六进制扩展为一个字节（0xF → 0xFF）。
    if (h.size() == 3) {
        const int r = hexNibble(h[0]), g = hexNibble(h[1]), b = hexNibble(h[2]);
        if (r < 0 || g < 0 || b < 0) { err = IDC_CTR(msg, kErrColorBadHex) + s + "'"; return false; }
        out = core::Color(static_cast<std::uint8_t>(r * 17),
                               static_cast<std::uint8_t>(g * 17),
                               static_cast<std::uint8_t>(b * 17), 255);
        return true;
    }
    if (h.size() == 6) { // #RRGGBB（不透明）。
        std::uint8_t r = 0, g = 0, b = 0;
        if (!hexByte(h, 0, r) || !hexByte(h, 2, g) || !hexByte(h, 4, b)) {
            err = IDC_CTR(msg, kErrColorBadHex) + s + "'"; return false;
        }
        out = core::Color(r, g, b, 255);
        return true;
    }
    if (h.size() == 8) { // #AARRGGBB（与 Core 配置约定一致）。
        std::uint8_t a = 0, r = 0, g = 0, b = 0;
        if (!hexByte(h, 0, a) || !hexByte(h, 2, r) || !hexByte(h, 4, g) || !hexByte(h, 6, b)) {
            err = IDC_CTR(msg, kErrColorBadHex) + s + "'"; return false;
        }
        out = core::Color(r, g, b, a);
        return true;
    }
    err = IDC_CTR(msg, kErrColorForm) + s + "'";
    return false;
}

bool parseFormat(const std::string& s, engine::ExportFormat& out, std::string& err) {
    const std::string f = toLower(s);
    if (f == "png") { out = engine::ExportFormat::Png; return true; }
    if (f == "jpeg" || f == "jpg") { out = engine::ExportFormat::Jpeg; return true; }
    if (f == "webp") { out = engine::ExportFormat::Webp; return true; }
    if (f == "bmp") { out = engine::ExportFormat::Bmp; return true; }
    err = IDC_CTR(msg, kErrUnknownFormat) + s + IDC_CTR(msg, kErrUnknownFormatTail);
    return false;
}

bool parseQuality(const std::string& s, int& out, std::string& err) {
    int v = 0;
    if (!toInt(s, v)) { err = IDC_CTR(msg, kErrQualityNotInt) + s + "'"; return false; }
    if (v < nums::parser::kQualityMin || v > nums::parser::kQualityMax) {
        err = IDC_CTR(msg, kErrQualityRange) + s + "'";
        return false;
    }
    out = v;
    return true;
}

bool parseSort(const std::string& s, engine::SortStrategy& out, std::string& err) {
    const std::string v = toLower(s);
    if (v == "row-major") { out = engine::SortStrategy::RowMajor; return true; }
    if (v == "column-major") { out = engine::SortStrategy::ColumnMajor; return true; }
    if (v == "custom") { out = engine::SortStrategy::Custom; return true; }
    err = IDC_CTR(msg, kErrUnknownSort) + s + IDC_CTR(msg, kErrUnknownSortTail);
    return false;
}

bool parseMargin(const std::string& s, engine::RemainderPolicy& out, std::string& err) {
    const std::string v = toLower(s);
    if (v == "discard") { out = engine::RemainderPolicy::Discard; return true; }
    if (v == "keep-partial") { out = engine::RemainderPolicy::KeepPartial; return true; }
    if (v == "pad") { out = engine::RemainderPolicy::Pad; return true; }
    err = IDC_CTR(msg, kErrUnknownMargin) + s + IDC_CTR(msg, kErrUnknownMarginTail);
    return false;
}

bool parseMerge(const std::string& s, engine::MergeLayout& out, std::string& err) {
    const std::string v = toLower(s);
    if (v == "collapse") { out = engine::MergeLayout::Collapse; return true; }
    if (v == "rearrange") { out = engine::MergeLayout::Rearrange; return true; }
    err = IDC_CTR(msg, kErrUnknownMerge) + s + IDC_CTR(msg, kErrUnknownMergeTail);
    return false;
}

bool parseDecorate(const std::string& s, bool& reverse, bool& snake, std::string& err) {
    const std::string v = toLower(s);
    if (v == "none") { reverse = false; snake = false; return true; }
    if (v == "reverse") { reverse = true; snake = false; return true; }
    if (v == "snake") { reverse = false; snake = true; return true; }
    // Core 先施加 snake 再整体 reverse（固定次序），故两种写法结果相同（见头文件说明）。
    if (v == "reverse-snake" || v == "snake-reverse") { reverse = true; snake = true; return true; }
    err = IDC_CTR(msg, kErrUnknownDecorate) + s + IDC_CTR(msg, kErrUnknownDecorateTail);
    return false;
}

bool checkNaming(const std::string& s, std::string& err) {
    if (s.empty()) { err = IDC_CTR(msg, kErrNamingEmpty); return false; }
    // 拒绝路径分隔符——命名模板只用于生成文件名，不应引入子目录（防止越界写盘）。
    if (s.contains('/') || s.contains('\\')) {
        err = IDC_CTR(msg, kErrNamingSeparator) + s + "'";
        return false;
    }
    return true;
}

} // namespace idc::cli
