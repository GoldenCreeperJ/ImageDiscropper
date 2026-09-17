// ============================================================================
// 文件：tests/test_value_parser.cpp
// 作用：单元测试 value_parser 的全部「字符串 → Core 强类型」解析与格式校验
//       （guideline §9.1：值解析必须覆盖成功与失败两路）。只验证纯转换行为，
//       不涉及语法归类（见 test_arg_parser.cpp）与命令编排（见 test_integration.cpp）。
// 分块依据：一测试关注点一文件（严禁上帝文件）；复用共享 CHECK 宏（cli_test_harness.h）。
// 说明：每个解析函数至少覆盖一条成功路径与一条失败路径（返回 false 且写 err），
//       以校验 §5.3 第 5 步「参数格式校验在调用 Core 之前完成」的输入契约。
// ============================================================================
#include "cli_test_harness.h"

#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "value_parser.h"
#include "core/color.h"
#include "engine/composition.h" // ExportFormat / MergeLayout
#include "engine/grid.h"        // RemainderPolicy
#include "engine/region.h"      // RectRegion
#include "engine/sequence.h"    // SortStrategy

// value_parser 全函数单元测试段。
void testValueParser() {
    using namespace idc::cli;
    std::string err;

    // ---- parseRect："x1,y1,x2,y2"（左闭右开）；成功写 RectRegion，失败于字段数 / 非整数。----
    {
        idc::engine::RectRegion r;
        CHECK(parseRect("50,40,150,120", r, err));
        CHECK(r.left == 50 && r.top == 40 && r.right == 150 && r.bottom == 120);
        CHECK(r.width() == 100 && r.height() == 80);
        CHECK(!parseRect("1,2,3", r, err) && !err.empty());       // 字段不足
        CHECK(!parseRect("1,2,3,4,5", r, err));                    // 字段过多
        CHECK(!parseRect("a,b,c,d", r, err));                      // 非整数
    }

    // ---- parseBand："a,b"（起始, 结束）。----
    {
        int a = 0, b = 0;
        CHECK(parseBand("40,120", a, b, err));
        CHECK(a == 40 && b == 120);
        CHECK(!parseBand("40", a, b, err) && !err.empty());        // 字段不足
        CHECK(!parseBand("40,x", a, b, err));                      // 非整数
    }

    // ---- parseGridGeo："x0,y0,cw,ch"；单元尺寸必须为正（终稿 §4.4.4）。----
    {
        int x0 = 0, y0 = 0, cw = 0, ch = 0;
        CHECK(parseGridGeo("0,0,100,75", x0, y0, cw, ch, err));
        CHECK(x0 == 0 && y0 == 0 && cw == 100 && ch == 75);
        CHECK(parseGridGeo("-5,-7,10,20", x0, y0, cw, ch, err));   // 基准点可为负
        CHECK(x0 == -5 && y0 == -7 && cw == 10 && ch == 20);
        CHECK(!parseGridGeo("0,0,0,75", x0, y0, cw, ch, err) && !err.empty()); // cw<=0
        CHECK(!parseGridGeo("0,0,100,0", x0, y0, cw, ch, err));    // ch<=0
        CHECK(!parseGridGeo("0,0,100", x0, y0, cw, ch, err));      // 字段不足
    }

    // ---- parseCell："r,c"（行, 列，从 0 起，不可为负）。----
    {
        int r = 0, c = 0;
        CHECK(parseCell("1,2", r, c, err));
        CHECK(r == 1 && c == 2);
        CHECK(parseCell("0,0", r, c, err) && r == 0 && c == 0);
        CHECK(!parseCell("-1,0", r, c, err) && !err.empty());      // 负行
        CHECK(!parseCell("0,-2", r, c, err));                      // 负列
        CHECK(!parseCell("1", r, c, err));                         // 字段不足
    }

    // ---- parseCanvas："ColxRow"（重排画布列 × 行）；大小写不敏感，须为正。----
    {
        int cols = 0, rows = 0;
        CHECK(parseCanvas("2x1", cols, rows, err));
        CHECK(cols == 2 && rows == 1);
        CHECK(parseCanvas("8X6", cols, rows, err) && cols == 8 && rows == 6); // 大写 X
        CHECK(!parseCanvas("2", cols, rows, err) && !err.empty()); // 缺分隔符
        CHECK(!parseCanvas("0x2", cols, rows, err));               // 列<=0
        CHECK(!parseCanvas("2x0", cols, rows, err));               // 行<=0
    }

    // ---- parseOrderCells："r,c;r,c"（分号分隔，保序，容忍尾随分号）。----
    {
        std::vector<std::pair<int, int>> cells;
        CHECK(parseOrderCells("1,1;0,0", cells, err));
        CHECK(cells.size() == 2);
        CHECK(cells[0].first == 1 && cells[0].second == 1);        // 保序
        CHECK(cells[1].first == 0 && cells[1].second == 0);
        CHECK(parseOrderCells("0,0;1,1;", cells, err) && cells.size() == 2); // 尾随分号
        CHECK(!parseOrderCells("", cells, err) && !err.empty());   // 空序列
        CHECK(!parseOrderCells("0,0;a,b", cells, err));            // 含非整数
    }

    // ---- parseColor：#RGB / #RRGGBB / #AARRGGBB（不带 alpha → a=255）。----
    {
        idc::core::Color col;
        CHECK(parseColor("#F00", col, err));                       // #RGB 每位扩展
        CHECK(col.r == 255 && col.g == 0 && col.b == 0 && col.a == 255);
        CHECK(parseColor("#00FF00", col, err));                    // #RRGGBB
        CHECK(col.r == 0 && col.g == 255 && col.b == 0 && col.a == 255);
        CHECK(parseColor("#80FF0000", col, err));                  // #AARRGGBB
        CHECK(col.a == 128 && col.r == 255 && col.g == 0 && col.b == 0);
        CHECK(parseColor("0000FF", col, err) && col.b == 255);     // 省略 '#'
        CHECK(!parseColor("#12345", col, err) && !err.empty());    // 非法长度
        CHECK(!parseColor("#GGGGGG", col, err));                   // 非法十六进制
    }

    // ---- parseFormat：png / jpeg(jpg) / webp / bmp。----
    {
        idc::engine::ExportFormat f;
        CHECK(parseFormat("png", f, err) && f == idc::engine::ExportFormat::PNG);
        CHECK(parseFormat("JPEG", f, err) && f == idc::engine::ExportFormat::JPEG); // 大写
        CHECK(parseFormat("jpg", f, err) && f == idc::engine::ExportFormat::JPEG);
        CHECK(parseFormat("webp", f, err) && f == idc::engine::ExportFormat::WEBP);
        CHECK(parseFormat("bmp", f, err) && f == idc::engine::ExportFormat::BMP);
        CHECK(!parseFormat("tiff", f, err) && !err.empty());       // 不支持
    }

    // ---- parseSort：row-major / column-major / custom。----
    {
        idc::engine::SortStrategy s;
        CHECK(parseSort("row-major", s, err) && s == idc::engine::SortStrategy::ROW_MAJOR);
        CHECK(parseSort("column-major", s, err) && s == idc::engine::SortStrategy::COLUMN_MAJOR);
        CHECK(parseSort("custom", s, err) && s == idc::engine::SortStrategy::CUSTOM);
        CHECK(!parseSort("diagonal", s, err) && !err.empty());     // 不支持
    }

    // ---- parseMargin：discard / keep-partial / pad。----
    {
        idc::engine::RemainderPolicy m;
        CHECK(parseMargin("discard", m, err) && m == idc::engine::RemainderPolicy::DISCARD);
        CHECK(parseMargin("keep-partial", m, err) && m == idc::engine::RemainderPolicy::KEEP_PARTIAL);
        CHECK(parseMargin("pad", m, err) && m == idc::engine::RemainderPolicy::PAD);
        CHECK(!parseMargin("shrink", m, err) && !err.empty());     // 不支持
    }

    // ---- parseMerge：collapse / rearrange。----
    {
        idc::engine::MergeLayout m;
        CHECK(parseMerge("collapse", m, err) && m == idc::engine::MergeLayout::COLLAPSE);
        CHECK(parseMerge("rearrange", m, err) && m == idc::engine::MergeLayout::REARRANGE);
        CHECK(!parseMerge("stack", m, err) && !err.empty());       // 不支持
    }

    // ---- parseDecorate：none / reverse / snake / reverse-snake / snake-reverse。----
    {
        bool rev = false, snk = false;
        CHECK(parseDecorate("none", rev, snk, err) && !rev && !snk);
        CHECK(parseDecorate("reverse", rev, snk, err) && rev && !snk);
        CHECK(parseDecorate("snake", rev, snk, err) && !rev && snk);
        // Core 先 snake 再整体 reverse，两种写法结果相同（见 value_parser.h 说明）。
        CHECK(parseDecorate("reverse-snake", rev, snk, err) && rev && snk);
        CHECK(parseDecorate("snake-reverse", rev, snk, err) && rev && snk);
        CHECK(!parseDecorate("spiral", rev, snk, err) && !err.empty()); // 不支持
    }

    // ---- checkNaming：非空且不含路径分隔符（防越界写盘）。----
    // 覆盖 guideline §9.1 明列的两类模板：{name}_{index:03d} 与 {name}_r{row}c{col}
    //（{row}/{col} 占位符的实际替换由 Core applyNaming 完成，此处仅校验模板合法性）。
    {
        CHECK(checkNaming("{name}_{index:03d}", err));
        CHECK(checkNaming("{name}_r{row}c{col}", err));            // §9.1 行/列命名模板
        CHECK(checkNaming("cell_{index}", err));
        CHECK(!checkNaming("", err) && !err.empty());              // 空模板
        CHECK(!checkNaming("a/b_{index}", err));                   // 含 '/'
        CHECK(!checkNaming("a\\b_{index}", err));                  // 含 '\\'
    }

    std::cout << "[cli-value-parser] OK\n";
}
