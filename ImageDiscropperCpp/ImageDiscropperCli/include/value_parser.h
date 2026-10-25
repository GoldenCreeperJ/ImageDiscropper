// ============================================================================
// 文件：include/value_parser.h
// 作用：把命令行「字符串值」解析为 Core 的强类型（坐标 / 带 / 网格 / 单元 / 画布 /
//       颜色 / 枚举 / 命名模板）。这是 CLI「解析参数」职责中「值语义」的一半，
//       与 arg_parser.h 的「语法归类」互补。
// 分块依据：
//   - 本文件只负责「字符串 → 类型」的纯转换与格式校验，成功返回 true 并写 out，
//     失败返回 false 并写 err（供命令层组装为退出码 1 的参数错误）。
//   - 不含任何切割 / 几何 / 排序逻辑（CONTRIBUTING.md「分层纪律」）：例如 r,c → 线性序号需要网格列数，
//     那一步在命令层用 Core 的 Grid 完成，不在此实现。
//   - 直接复用 Core 的类型（RectRegion / GridParams / ExportFormat / Color…），
//     不自造平行结构（CONTRIBUTING.md「分层纪律」 不重复造轮子）。
// 说明：格式严格对齐 SPEC §3——坐标 "x1,y1,x2,y2"（各段可为整数像素或百分比串）、带 "a,b"、
//       网格 "x0,y0,cw,ch"、选择 "r,c"、画布 "ColxRow"、颜色 "#RRGGBB" / "#AARRGGBB"、序列 "r,c;r,c"。
// ============================================================================
#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/color.h"
#include "core/percent.h"      // parsePercent / resolvePercent（坐标段可用 "50%" 书写）
#include "engine/composition.h" // ExportFormat / MergeLayout
#include "engine/engine.h"      // Tier / CutGenerator（config 字段覆盖族解析用）
#include "engine/grid.h"        // RemainderPolicy
#include "engine/region.h"      // RectRegion
#include "engine/selection.h"   // Polarity
#include "engine/sequence.h"    // SortStrategy

namespace idc::cli {

// ---------------------------------------------------------------------------
// 几何坐标：整数像素（"120"）或百分比（"50%"，相对输入图像的宽 / 高）。
// 几何解析早于读图、基准尺寸尚不可知，故拆两步：parse*Coords 只判形态（段数 + 每段为整数或
// 百分比），resolve*Coords 在基准可知处换算成像素并做取值校验。百分比换算口径与 §7 配置同源
// （core/percent.h：四舍五入、保留符号）。
// ---------------------------------------------------------------------------
struct Coord {
    int px{0};                     // 整数写法：像素值；百分比写法不读此字段
    std::optional<double> ratio;   // 有值即百分比书写（0.5 = 50%）
};

struct RectCoords { Coord x1, y1, x2, y2; };   // 矩形四角（左闭右开）
struct BandCoords { Coord a, b; };             // 带两端（起始, 结束）
struct GridCoords { Coord x0, y0, cw, ch; };   // 网格（基准点 + 单元尺寸）
struct AxisPitchCoords { Coord origin, cell; }; // 单轴周期（基准点 + 单元尺寸，只占一个方向）

// 解析矩形 "x1,y1,x2,y2"（左闭右开）；仅校验「四个坐标段」，
// x1<x2 / y1<y2 的退化校验由命令层在换算后按 SPEC §5 边界约定处理（以便给出更贴切的提示）。
bool parseRectCoords(const std::string& s, RectCoords& out, std::string& err);

// 解析带 "a,b"（起始, 结束）。
bool parseBandCoords(const std::string& s, BandCoords& out, std::string& err);

// 解析网格几何 "x0,y0,cw,ch"（基准点 + 单元尺寸，SPEC §3.4.3）。
bool parseGridCoords(const std::string& s, GridCoords& out, std::string& err);

// 解析单轴周期 "origin,cell"（--grid-x / --grid-y）：基准点可负，各段可为整数或百分比。
bool parseAxisPitchCoords(const std::string& s, AxisPitchCoords& out, std::string& err);
// 换算单轴周期为像素（x 轴取宽、y 轴取高）；换算后单元尺寸必须为正。
bool resolveAxisPitchCoords(const AxisPitchCoords& in, int basis, engine::GridAxisSpec& out, std::string& err);

// 解析分割数 "ColxRow"（--divide，接受 x / X / 逗号分隔，如 3x4 或 3,4）；列、行均须为正。
bool parseDivide(const std::string& s, int& cols, int& rows, std::string& err);
// 解析单轴分割数（--divide-x / --divide-y）：正整数。
bool parsePositiveCount(const std::string& s, int& out, std::string& err);

// 解析比例 "w,w,..."（--weights-x / --weights-y）：每个元素为正数或百分比串（如 25%,50%,25%）。
// 百分比统一折成百分数量级（25% → 25），与配置 weights 同口径——只取相对比例，不影响切分结果。
bool parseWeightList(const std::string& s, std::vector<double>& out, std::string& err);

// 换算为像素：整数段原样，百分比段按基准（矩形 / 网格的 x 类用宽、y 类用高；带的两段用同一基准）。
// 某段为百分比而基准 <= 0（尺寸未知）→ 无法换算，返回 false 并写 err。
bool resolveRectCoords(const RectCoords& in, int basisW, int basisH, engine::RectRegion& out, std::string& err);
bool resolveBandCoords(const BandCoords& in, int basis, int& a, int& b, std::string& err);
// 网格换算后校验单元尺寸为正——否则无法产生贯穿全图的周期切割线（SPEC §3.4.3）。
bool resolveGridCoords(const GridCoords& in, int basisW, int basisH, engine::GridParams& out, std::string& err);

// 解析单元坐标 "r,c"（行, 列，均从 0 起）；成功写 r、c。
bool parseCell(const std::string& s, int& r, int& c, std::string& err);

// 解析画布 "ColxRow"（重排画布的列数 × 行数，如 "8x6"）；成功写 cols、rows。
bool parseCanvas(const std::string& s, int& cols, int& rows, std::string& err);

// 解析单元列表 "r,c;r,c;..."（分号分隔多个单元坐标，容忍尾随分号，空列表报错）；成功按顺序写入 out。
// --sort custom 的 --order 与 --keep/--remove 的每个值共用此解析（集合语义下调用方自行累加）。
bool parseCellList(const std::string& s, std::vector<std::pair<int, int>>& out, std::string& err);

// 解析自定义序列 "r,c;r,c;..."（委托 parseCellList，同一形态）。
bool parseOrderCells(const std::string& s, std::vector<std::pair<int, int>>& out, std::string& err);

// 解析颜色 "#RGB" / "#RRGGBB" / "#AARRGGBB"（与 Core 配置的十六进制约定一致）；
// 不带 alpha 时视为不透明（a=255）。成功写 out。
bool parseColor(const std::string& s, core::Color& out, std::string& err);

// 解析导出格式 "png" / "jpeg"("jpg") / "webp" / "bmp"；成功写 out。
bool parseFormat(const std::string& s, engine::ExportFormat& out, std::string& err);

// 解析有损格式质量参数 "0"~"100"（仅 JPEG/WebP 生效，PNG/BMP 忽略）；成功写 out，越界或非整数失败。
bool parseQuality(const std::string& s, int& out, std::string& err);

// 解析排序策略 "row-major" / "column-major" / "custom"；成功写 out。
bool parseSort(const std::string& s, engine::SortStrategy& out, std::string& err);

// 解析余量策略 "discard" / "keep-partial" / "pad"；成功写 out。
bool parseMargin(const std::string& s, engine::RemainderPolicy& out, std::string& err);

// 解析合并布局 "collapse" / "rearrange"；成功写 out。
bool parseMerge(const std::string& s, engine::MergeLayout& out, std::string& err);
// 层阶 / 极性 / 导出模式（config 命令字段覆盖族用；取值与 §7 token 同）。
bool parseTier(const std::string& s, engine::Tier& out, std::string& err);
bool parsePolarity(const std::string& s, engine::Polarity& out, std::string& err);
bool parseExportMode(const std::string& s, engine::ExportMode& out, std::string& err);
// 位图→画布适应方式（--fit）：crop / stretch / native。
bool parseFit(const std::string& s, canvas::ImageFit& out, std::string& err);

// 解析排序修饰 "none" / "reverse" / "snake" / "reverse-snake" / "snake-reverse"；
// 映射为 Core SequenceParams 的两个布尔（reverse / snake）。
// 注：Core 的 Sequence::build 先施加 snake、再整体 reverse（固定次序），故
//     "reverse-snake" 与 "snake-reverse" 得到同一 (reverse=true, snake=true) 结果。
bool parseDecorate(const std::string& s, bool& reverse, bool& snake, std::string& err);

// 校验分离导出命名模板（如 "{name}_{index:03d}"）：仅要求非空且不含非法字符；
// 实际的占位符替换由 Core 的 exportImage 完成（CONTRIBUTING.md「分层纪律」 不在 CLI 重实现）。
// 说明：Core 的 applyNaming 支持 {name}/{index}/{index:03d}/{row}/{col}（SPEC §4.1）。
bool checkNaming(const std::string& s, std::string& err);

} // namespace idc::cli
