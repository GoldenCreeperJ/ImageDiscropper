// ============================================================================
// 文件：include/value_parser.h
// 作用：把命令行「字符串值」解析为 Core 的强类型（坐标 / 带 / 网格 / 单元 / 画布 /
//       颜色 / 枚举 / 命名模板）。这是 CLI「解析参数」职责中「值语义」的一半，
//       与 arg_parser.h 的「语法归类」互补。
// 分块依据：
//   - 本文件只负责「字符串 → 类型」的纯转换与格式校验，成功返回 true 并写 out，
//     失败返回 false 并写 err（供命令层组装为退出码 1 的参数错误）。
//   - 不含任何切割 / 几何 / 排序逻辑（A-0.1）：例如 r,c → 线性序号需要网格列数，
//     那一步在命令层用 Core 的 Grid 完成，不在此实现。
//   - 直接复用 Core 的类型（RectRegion / GridParams / ExportFormat / Color…），
//     不自造平行结构（A-0.2 不重复造轮子）。
// 说明：格式严格对齐 guideline §4——坐标 "x1,y1,x2,y2"、带 "a,b"、网格 "x0,y0,cw,ch"、
//       选择 "r,c"、画布 "ColxRow"、颜色 "#RRGGBB" / "#AARRGGBB"、序列 "r,c;r,c"。
// ============================================================================
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "core/color.h"
#include "engine/composition.h" // ExportFormat / MergeLayout
#include "engine/grid.h"        // RemainderPolicy
#include "engine/region.h"      // RectRegion
#include "engine/sequence.h"    // SortStrategy

namespace idc::cli {

// 解析矩形 "x1,y1,x2,y2"（左闭右开）；成功写 out。仅校验「四个整数」，
// x1<x2 / y1<y2 的退化校验由命令层按 §6 处理（以便给出更贴切的提示）。
bool parseRect(const std::string& s, engine::RectRegion& out, std::string& err);

// 解析带 "a,b"（起始, 结束）；成功写 a、b。
bool parseBand(const std::string& s, int& a, int& b, std::string& err);

// 解析网格几何 "x0,y0,cw,ch"（基准点 + 单元尺寸，终稿 §4.4.4）；成功写四个整数。
bool parseGridGeo(const std::string& s, int& x0, int& y0, int& cw, int& ch, std::string& err);

// 解析单元坐标 "r,c"（行, 列，均从 0 起）；成功写 r、c。
bool parseCell(const std::string& s, int& r, int& c, std::string& err);

// 解析画布 "ColxRow"（重排画布的列数 × 行数，如 "8x6"）；成功写 cols、rows。
bool parseCanvas(const std::string& s, int& cols, int& rows, std::string& err);

// 解析自定义序列 "r,c;r,c;..."（分号分隔多个单元坐标）；成功按顺序写入 out。
bool parseOrderCells(const std::string& s, std::vector<std::pair<int, int>>& out, std::string& err);

// 解析颜色 "#RGB" / "#RRGGBB" / "#AARRGGBB"（与 Core 配置的十六进制约定一致）；
// 不带 alpha 时视为不透明（a=255）。成功写 out。
bool parseColor(const std::string& s, core::Color& out, std::string& err);

// 解析导出格式 "png" / "jpeg"("jpg") / "webp" / "bmp"；成功写 out。
bool parseFormat(const std::string& s, engine::ExportFormat& out, std::string& err);

// 解析排序策略 "row-major" / "column-major" / "custom"；成功写 out。
bool parseSort(const std::string& s, engine::SortStrategy& out, std::string& err);

// 解析余量策略 "discard" / "keep-partial" / "pad"；成功写 out。
bool parseMargin(const std::string& s, engine::RemainderPolicy& out, std::string& err);

// 解析合并布局 "collapse" / "rearrange"；成功写 out。
bool parseMerge(const std::string& s, engine::MergeLayout& out, std::string& err);

// 解析排序修饰 "none" / "reverse" / "snake" / "reverse-snake" / "snake-reverse"；
// 映射为 Core SequenceParams 的两个布尔（reverse / snake）。
// 注：Core 的 Sequence::build 先施加 snake、再整体 reverse（固定次序），故
//     "reverse-snake" 与 "snake-reverse" 得到同一 (reverse=true, snake=true) 结果。
bool parseDecorate(const std::string& s, bool& reverse, bool& snake, std::string& err);

// 校验分离导出命名模板（如 "{name}_{index:03d}"）：仅要求非空且不含非法字符；
// 实际的占位符替换由 Core 的 exportImage 完成（A-0.1 不在 CLI 重实现）。
// 说明：Core 的 applyNaming 支持 {name}/{index}/{index:03d}/{row}/{col}（§5.1）。
bool checkNaming(const std::string& s, std::string& err);

} // namespace idc::cli
