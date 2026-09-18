// ============================================================================
// 文件：src/help.cpp
// 作用：实现 help.h——顶层帮助、子命令帮助与版本信息文本（guideline §6）。
// 分块依据：纯文本输出，集中一处以便命令清单 / 术语 / 示例与终稿保持一致（§2.3 术语、
//       §6.1 帮助要求、§6.2 版本格式）；命令名统一用实际可执行名 "idc"（guideline §4：
//       把占位的 image-tool 替换为实际命令名）。
// 排版约定（替换早期手工数空格的对齐方式）：
//   · 选项行经 optionRow() 以固定列宽打印——列宽按各命令最长选项常量维护，杜绝对齐
//     漂移（早期 config 块曾出现 --output 与 --save-config 列宽不一致）；
//   · 选项按功能分组（输入 / 切割 / 导出……），组标题独占一行、组间空行；
//   · 需求标签统一为 [必填] / [可重复] 两种；条件性必填（如「--compose 时必填」）写入
//     描述括注，不再占用标签位；
//   · 约束与常见错误分行列出，避免超长单行在终端折行后参差不齐。
// 测试约束：IT-13 断言 --version 形如 "idc <ver> (core <ver>)"；IT-14 断言顶层帮助
//   含全部子命令名（extract / erase / grid / config）——排版调整不得破坏这两条。
// 说明：版本号来自编译期宏 IDC_CLI_VERSION / IDC_CORE_VERSION（由 CMake 注入，与构建系统一致）。
// ============================================================================
#include "help.h"

#include <iomanip>   // std::setw / std::left：选项列定宽对齐
#include <iostream>

// 编译期版本宏（正常由 CMake 注入；缺失时用占位，避免编译失败）。
#ifndef IDC_CLI_VERSION
#define IDC_CLI_VERSION "0.0.0"
#endif
#ifndef IDC_CORE_VERSION
#define IDC_CORE_VERSION "0.0.0"
#endif

namespace idc::cli {

namespace {

// 各命令的选项列宽（= 该命令最长选项串的长度，注释标注基准项）。
constexpr int kTopOptWidth = 18;     // --save-config <file>
constexpr int kExtractOptWidth = 18; // --rect x1,y1,x2,y2
constexpr int kEraseOptWidth = 18;   // --rect x1,y1,x2,y2
constexpr int kGridOptWidth = 24;    // --merge-decorate <method>
constexpr int kConfigOptWidth = 18;  // --save-config <file>

// 打印一行选项：两空格缩进 + 定宽选项列 + 两空格 + 描述。
// 描述以 [必填] / [可重复] 标签开头（无标签时直接写描述），
// 因此所有行的标签在同一列起头，视觉对齐。
void optionRow(const int width, const char* opt, const char* desc) {
    std::cout << "  " << std::left << std::setw(width) << opt << "  " << desc << '\n';
}

} // namespace

// 顶层帮助：工具定位 + 三层模式 + 子命令 + 全局选项 + 退出码 + 各层示例（§6.1）。
void printTopHelp() {
    std::cout << R"HELP(idc — 图像区域提取 / 反向剔除 / 网格分割工具（命令行）

三层模式共用同一 Grid-Selection-Emit 引擎
（原图 → 切割线 → 诱导网格 → 选择集 → 极性 → 排布导出），模式仅为参数预设：

用法：
  idc <command> [options]
  idc --help | --version

子命令：
  extract   L1 标准提取：保留矩形 / 水平带 / 垂直带，输出单图
  erase     L2 反向剔除：删除十字带 / 横带 / 竖带 / 多矩形并集，分离或合并导出
  grid      L3 网格分割：按基准点与单元尺寸切网格，选择单元后分离或重排合并
  config    从 JSON 配置文件执行作业（对应终稿 §9 结构）

全局选项：
)HELP";
    optionRow(kTopOptWidth, "-h, --help", "显示帮助");
    optionRow(kTopOptWidth, "-v, --version", "显示版本");
    optionRow(kTopOptWidth, "--verbose", "输出详细日志到 stderr");
    optionRow(kTopOptWidth, "--quiet", "静默模式，仅输出错误");
    optionRow(kTopOptWidth, "--config <file>", "从 JSON 配置文件读取全部参数（此时忽略命令的几何参数）");
    optionRow(kTopOptWidth, "--save-config <file>", "把当前参数序列化为 JSON 配置文件（dry-run，不执行作业）");
    std::cout << R"HELP(
退出码：
  0 成功 | 1 参数错误 | 2 运行时错误 | 3 输入文件错误 | 4 输出失败 | 5 内部错误

示例：
  # L1 保留矩形（中心单元）
  idc extract --input photo.jpg --rect 100,100,300,250 --output out.png
  # L2 十字切割，坍缩合并为单图
  idc erase --input photo.jpg --rect 100,100,300,250 --merge collapse --output out.png
  # L2 十字切割，分离导出四角到文件夹
  idc erase --input photo.jpg --rect 100,100,300,250 --output-dir ./out/ --format png
  # L3 网格保留四角并重排合并到 2x2 画布
  idc grid --input photo.jpg --grid 100,100,200,150 --keep 0,0 --keep 0,2 --keep 2,0 --keep 2,2 --compose --canvas 2x2 --output result.png

查看某子命令的完整选项： idc <command> --help
)HELP";
}

// 版本信息：形如 "idc <version> (core <core-version>)"（§6.2）。
void printVersion() {
    std::cout << "idc " << IDC_CLI_VERSION << " (core " << IDC_CORE_VERSION << ")\n";
}

// 各子命令帮助文本（§6.1：语义 / 全部选项 / 示例 / 常见错误）。
namespace {

void helpExtract() {
    std::cout << R"HELP(idc extract — L1 标准提取模式（终稿 §4.2）

语义：极性恒为 keep，输出恒为单图。矩形保留中心单元；横线 / 竖线保留整条带。
      切割线贯穿全图（公理），保留区经坍缩拼接为单图。

输入：
)HELP";
    optionRow(kExtractOptWidth, "--input <file>", "[必填] 输入图像路径");
    std::cout << R"HELP(
切割线（--rect / --hband / --vband 三选一）：
)HELP";
    optionRow(kExtractOptWidth, "--rect x1,y1,x2,y2", "矩形区域（保留中心矩形）");
    optionRow(kExtractOptWidth, "--hband y1,y2", "水平带（保留 y∈[y1,y2) 的整条横带）");
    optionRow(kExtractOptWidth, "--vband x1,x2", "垂直带（保留 x∈[x1,x2) 的整条竖带）");
    std::cout << R"HELP(
输出：
)HELP";
    optionRow(kExtractOptWidth, "--output <file>", "[必填] 输出图像路径");
    optionRow(kExtractOptWidth, "--format <fmt>", "输出格式 png/jpeg/webp/bmp（默认由 --output 扩展名推断）");
    std::cout << R"HELP(
通用：
)HELP";
    optionRow(kExtractOptWidth, "-h, --help", "显示本帮助");
    std::cout << R"HELP(
示例：
  idc extract --input photo.jpg --rect 100,100,300,250 --output out.png
  idc extract --input photo.jpg --hband 100,250 --output band.png
  idc extract --input photo.jpg --vband 100,300 --output band.png

常见错误：
  退出码 1：缺少 --input / --output；三选一未满足（缺切割线或同时给出多个）；坐标非法或退化（x1>=x2 / y1>=y2）
  退出码 3：输入图像不存在或无法读取
  退出码 4：输出写入失败
)HELP";
}

void helpErase() {
    std::cout << R"HELP(idc erase — L2 反向剔除模式（终稿 §4.3，差异化内核）

语义：极性恒为 remove，"按线删除"——抽掉中缝、两侧对接，尺寸变小（W−Δx × H−Δy）。
      单矩形 → 十字切割（保留四角）；--rect 多次 → 多矩形并集剔除；
      横线 / 竖线 → 删整条带。缺省分离导出（各保留块一张图）；--merge collapse 合并为单图。

输入：
)HELP";
    optionRow(kEraseOptWidth, "--input <file>", "[必填] 输入图像路径");
    std::cout << R"HELP(
切割线（--rect / --hband / --vband 三选一）：
)HELP";
    optionRow(kEraseOptWidth, "--rect x1,y1,x2,y2", "[可重复] 一次 = 单矩形十字切割；多次 = 多矩形并集剔除");
    optionRow(kEraseOptWidth, "--hband y1,y2", "删除 y∈[y1,y2) 的整条横带");
    optionRow(kEraseOptWidth, "--vband x1,x2", "删除 x∈[x1,x2) 的整条竖带");
    std::cout << R"HELP(
导出：
)HELP";
    optionRow(kEraseOptWidth, "--merge <mode>", "仅 collapse（坍缩）；缺省为分离导出（rearrange 为 L3 grid 专属）");
    optionRow(kEraseOptWidth, "--output <file>", "合并输出路径（--merge collapse 时必填）");
    optionRow(kEraseOptWidth, "--output-dir <dir>", "分离输出目录（缺省分离导出时必填；不存在会自动创建）");
    optionRow(kEraseOptWidth, "--format <fmt>", "输出格式 png/jpeg/webp/bmp（默认 png；合并时优先按 --output 扩展名）");
    optionRow(kEraseOptWidth, "--naming <tpl>", "分离命名模板，默认 {name}_{index:03d}");
    std::cout << R"HELP(
通用：
)HELP";
    optionRow(kEraseOptWidth, "-h, --help", "显示本帮助");
    std::cout << R"HELP(
约束：
  - --rect / --hband / --vband 互斥；--merge 与 --output-dir 互斥。
  - --merge collapse 但选择集不可坍缩 → 退出码 2（L2 无重排可用，请调整切割线使
    删除区覆盖整行 / 整列，或改分离导出）。

示例：
  idc erase --input photo.jpg --rect 100,100,300,250 --output-dir ./out/ --format png
  idc erase --input photo.jpg --rect 100,100,300,250 --merge collapse --output out.png
  idc erase --input photo.jpg --rect 100,100,200,150 --rect 400,300,500,400 --merge collapse --output out.png

常见错误：
  退出码 1：缺必填 / 互斥冲突 / 坐标非法 / --merge rearrange（L2 不支持重排）
  退出码 2：坍缩不可行（见约束）
  退出码 3：输入图像错误
  退出码 4：输出目录 / 文件写入失败
)HELP";
}

void helpGrid() {
    std::cout << R"HELP(idc grid — L3 网格分割模式（终稿 §4.4，完备表达层）

语义：以基准点 (x0,y0) 为相位锚、单元尺寸 (cw,ch) 为周期，生成贯穿全图的切割线并诱导
      网格；行数 / 列数由图像边界自动推导（不作为参数）。--keep / --remove 逐单元选择，
      可排序后分离导出或重排合并到 ColxRow 画布。

输入：
)HELP";
    optionRow(kGridOptWidth, "--input <file>", "[必填] 输入图像路径");
    std::cout << R"HELP(
网格：
)HELP";
    optionRow(kGridOptWidth, "--grid x0,y0,cw,ch", "[必填] 基准点 + 单元尺寸（cw,ch 必须为正）");
    std::cout << R"HELP(
选择（--keep 与 --remove 二选一）：
)HELP";
    optionRow(kGridOptWidth, "--keep r,c", "[可重复] 保留指定单元");
    optionRow(kGridOptWidth, "--remove r,c", "[可重复] 删除指定单元");
    std::cout << R"HELP(
排序与修饰：
)HELP";
    optionRow(kGridOptWidth, "--sort <strategy>", "row-major（默认）/ column-major / custom");
    optionRow(kGridOptWidth, "--decorate <method>", "none（默认）/ reverse / snake / reverse-snake / snake-reverse（--sort custom 下不可用）");
    optionRow(kGridOptWidth, "--order r,c;r,c", "自定义序列（--sort custom 时必填，同时定义被保留单元与其顺序）");
    std::cout << R"HELP(
余量：
)HELP";
    optionRow(kGridOptWidth, "--margin <policy>", "discard（默认）/ keep-partial / pad");
    optionRow(kGridOptWidth, "--pad-color <hex>", "#RRGGBB / #AARRGGBB，默认透明");
    std::cout << R"HELP(
合并重排：
)HELP";
    optionRow(kGridOptWidth, "--compose", "合并为单图（重排）；缺省为分离导出");
    optionRow(kGridOptWidth, "--canvas ColxRow", "重排画布的单元列 × 行，如 2x2（--compose 时必填）");
    optionRow(kGridOptWidth, "--merge-sort <strategy>", "重排填充顺序 row-major（默认）/ column-major（与 --sort 选择序正交；不支持 custom）");
    optionRow(kGridOptWidth, "--merge-decorate <method>", "重排填充修饰 none（默认）/ reverse / snake / reverse-snake / snake-reverse");
    std::cout << R"HELP(
导出：
)HELP";
    optionRow(kGridOptWidth, "--output <file>", "合并输出路径（--compose 时必填）");
    optionRow(kGridOptWidth, "--output-dir <dir>", "分离输出目录（缺省分离导出时必填）");
    optionRow(kGridOptWidth, "--format <fmt>", "输出格式 png/jpeg/webp/bmp");
    optionRow(kGridOptWidth, "--naming <tpl>", "分离命名模板");
    std::cout << R"HELP(
通用：
)HELP";
    optionRow(kGridOptWidth, "-h, --help", "显示本帮助");
    std::cout << R"HELP(
约束：
  - --keep 与 --remove 互斥；--compose 与 --output-dir 互斥。
  - --decorate 在 --sort custom 下不可用；--merge-sort 不支持 custom（重排填充仅
    行 / 列优先，自定义块序用 --sort custom）。
  - --compose 时 --canvas 的 cols/rows 必填且须为正。
  - Core 先施加 snake 再整体 reverse（固定次序），故 reverse-snake 与 snake-reverse 结果相同。

示例：
  idc grid --input photo.jpg --grid 100,100,200,150 --keep 0,0 --keep 0,2 --keep 2,0 --keep 2,2 --compose --canvas 2x2 --output result.png
  idc grid --input photo.jpg --grid 0,0,100,100 --remove 1,1 --output-dir ./cells/
  idc grid --input photo.jpg --grid 0,0,100,100 --sort custom --order 0,0;1,1;2,2 --compose --canvas 3x1 --output diag.png

常见错误：
  退出码 1：缺必填 / 互斥冲突 / r,c 越界 / 格式非法
  退出码 3：输入图像错误
  退出码 4：输出失败
)HELP";
}

void helpConfig() {
    std::cout << R"HELP(idc config — 配置文件作业（终稿 §9 / FR-L3.8）

语义：从 JSON 配置文件读取一次完整作业的全部参数（source/preprocess/cut/order/emit/select），
      复用 Core 的 JSON 模块解析（CLI 不自实现解析）。配置文件不含图像路径，故执行时仍须 --input。

配置：
)HELP";
    optionRow(kConfigOptWidth, "--load <file>", "[必填] 从 JSON 配置文件读取全部参数");
    optionRow(kConfigOptWidth, "--save-config <file>", "把加载的配置另存为 JSON（转换 / 校验；dry-run，不执行作业）");
    std::cout << R"HELP(
输入输出：
)HELP";
    optionRow(kConfigOptWidth, "--input <file>", "[必填] 输入图像路径");
    optionRow(kConfigOptWidth, "--output <file>", "合并输出路径（配置 emit.mode=merged 时用）");
    optionRow(kConfigOptWidth, "--output-dir <dir>", "分离输出目录（配置 emit.mode=separate 时用）");
    std::cout << R"HELP(
通用：
)HELP";
    optionRow(kConfigOptWidth, "-h, --help", "显示本帮助");
    std::cout << R"HELP(
示例：
  idc config --load my-config.json --input photo.jpg --output result.png
  idc extract --input photo.jpg --rect 100,100,300,250 --output out.png --save-config out.json

常见错误：
  退出码 1：缺 --load / 缺输出 / 配置解析失败
  退出码 3：配置文件或输入图像不存在 / 不可读
)HELP";
}

} // namespace

// 子命令帮助分派；未知命令回退为顶层帮助。
void printCommandHelp(const std::string& cmd) {
    if (cmd == "extract") { helpExtract(); return; }
    if (cmd == "erase") { helpErase(); return; }
    if (cmd == "grid") { helpGrid(); return; }
    if (cmd == "config") { helpConfig(); return; }
    printTopHelp();
}

} // namespace idc::cli
