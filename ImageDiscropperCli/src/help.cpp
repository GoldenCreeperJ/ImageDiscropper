// ============================================================================
// 文件：src/help.cpp
// 作用：实现 help.h——顶层帮助、子命令帮助与版本信息文本（guideline §6）。
// 分块依据：纯文本输出，集中一处以便命令清单 / 术语 / 示例与终稿保持一致（§2.3 术语、
//       §6.1 帮助要求、§6.2 版本格式）；命令名统一用实际可执行名 "idc"（guideline §4：
//       把占位的 image-tool 替换为实际命令名）。
// 说明：版本号来自编译期宏 IDC_CLI_VERSION / IDC_CORE_VERSION（由 CMake 注入，与构建系统一致）。
// ============================================================================
#include "cli/help.h"

#include <iostream>

// 编译期版本宏（正常由 CMake 注入；缺失时用占位，避免编译失败）。
#ifndef IDC_CLI_VERSION
#define IDC_CLI_VERSION "0.0.0"
#endif
#ifndef IDC_CORE_VERSION
#define IDC_CORE_VERSION "0.0.0"
#endif

namespace idc::cli {

// 顶层帮助：工具定位 + 三层模式 + 子命令 + 全局选项 + 退出码 + 各层示例（§6.1）。
void printTopHelp() {
    std::cout << R"HELP(idc — 图像区域提取 / 反向剔除 / 网格分割工具（命令行）

沿贯穿全图的切割线把图像切开、选择保留哪些单元、再按坍缩或重排重新拼合。
三层模式共用同一 Grid-Selection-Emit 引擎（原图 → 切割线 → 诱导网格 → 选择集 → 极性 → 排布导出），
模式仅为参数预设：
  L1 标准提取 (extract)：极性恒 keep，保留选框 / 带，输出单图。
  L2 反向剔除 (erase)  ：极性恒 remove，抽掉中缝两侧对接（十字 / 横线 / 竖线 / 多矩形并集）。
  L3 网格分割 (grid)   ：基准点 + 单元尺寸周期铺满全图，自由点选 + 排序 + 重排。

用法：
  idc <command> [options]
  idc --help | --version

子命令：
  extract   L1 标准提取：保留矩形 / 水平带 / 垂直带，输出单图
  erase     L2 反向剔除：删除十字带 / 横带 / 竖带 / 多矩形并集，分离或合并导出
  grid      L3 网格分割：按基准点与单元尺寸切网格，选择单元后分离或重排合并
  config    从 JSON 配置文件执行作业（对应终稿 §9 结构）

全局选项：
  -h, --help             显示帮助
  -v, --version          显示版本
      --verbose          输出详细日志到 stderr
      --quiet            静默模式，仅输出错误
      --config <file>    从 JSON 配置文件读取全部参数（此时忽略命令的几何参数）
      --save-config <f>  把当前参数序列化为 JSON 配置文件（dry-run，不执行作业）

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

选项：
  --input <file>         [必填] 输入图像路径
  --rect x1,y1,x2,y2     [三选一] 矩形区域（保留中心矩形）
  --hband y1,y2          [三选一] 水平带（保留 y∈[y1,y2) 的整条横带）
  --vband x1,x2          [三选一] 垂直带（保留 x∈[x1,x2) 的整条竖带）
  --output <file>        [必填] 输出图像路径（格式默认由扩展名推断）
  --format <fmt>         输出格式 png/jpeg/webp/bmp（默认由 --output 扩展名推断）
  -h, --help             显示本帮助

示例：
  idc extract --input photo.jpg --rect 100,100,300,250 --output out.png
  idc extract --input photo.jpg --hband 100,250 --output band.png
  idc extract --input photo.jpg --vband 100,300 --output band.png

常见错误：
  退出码 1：缺少 --input / --output；--rect/--hband/--vband 未三选一或同时给出；坐标非法或退化（x1>=x2 / y1>=y2）
  退出码 3：输入图像不存在或无法读取    退出码 4：输出写入失败
)HELP";
}

void helpErase() {
    std::cout << R"HELP(idc erase — L2 反向剔除模式（终稿 §4.3，差异化内核）

语义：极性恒为 remove，"按线删除"——抽掉中缝、两侧对接，尺寸变小（W−Δx × H−Δy）。
      单矩形 → 十字切割（保留四角）；--rect 多次 → 多矩形并集剔除；横线 / 竖线 → 删整条带。
      缺省分离导出（各保留块一张图）；--merge 合并为单图（坍缩或重排）。

选项：
  --input <file>         [必填] 输入图像路径
  --rect x1,y1,x2,y2     [三选一，可重复] 一次 = 单矩形十字切割；多次 = 多矩形并集剔除
  --hband y1,y2          [三选一] 删除 y∈[y1,y2) 的整条横带
  --vband x1,x2          [三选一] 删除 x∈[x1,x2) 的整条竖带
  --merge <mode>         collapse（坍缩）或 rearrange（重排）；缺省为分离导出
  --output <file>        [合并时必填] 合并输出路径
  --output-dir <dir>     [分离时必填] 分离输出目录（不存在会自动创建）
  --format <fmt>         输出格式 png/jpeg/webp/bmp（默认 png；合并时优先按 --output 扩展名）
  --naming <tpl>         分离命名模板，默认 {name}_{index:03d}
  -h, --help             显示本帮助

约束：
  --rect / --hband / --vband 三者互斥；--merge 与 --output-dir 不得同时出现。
  显式 --merge collapse 但选择集不可坍缩时 → 退出码 2（提示改用 rearrange）。

示例：
  idc erase --input photo.jpg --rect 100,100,300,250 --output-dir ./out/ --format png
  idc erase --input photo.jpg --rect 100,100,300,250 --merge collapse --output out.png
  idc erase --input photo.jpg --rect 100,100,200,150 --rect 400,300,500,400 --merge collapse --output out.png

常见错误：
  退出码 1：缺必填 / 互斥冲突 / 坐标非法    退出码 2：坍缩不可行（改用 --merge rearrange）
  退出码 3：输入图像错误    退出码 4：输出目录 / 文件写入失败
)HELP";
}

void helpGrid() {
    std::cout << R"HELP(idc grid — L3 网格分割模式（终稿 §4.4，完备表达层）

语义：以基准点 (x0,y0) 为相位锚、单元尺寸 (cw,ch) 为周期，生成贯穿全图的切割线并诱导网格；
      行数 / 列数由图像边界自动推导（不作为参数）。--keep / --remove 逐单元选择，可排序后
      分离导出或重排合并到 ColxRow 画布。

选项：
  --input <file>         [必填] 输入图像路径
  --grid x0,y0,cw,ch     [必填] 网格定义（基准点 + 单元尺寸；cw,ch 必须为正）
  --keep r,c             [与 --remove 二选一，可重复] 保留指定单元
  --remove r,c           [与 --keep 二选一，可重复] 删除指定单元
  --sort <strategy>      row-major（默认）/ column-major / custom
  --decorate <method>    none（默认）/ reverse / snake / reverse-snake / snake-reverse（custom 下不可用）
  --order r,c;r,c        自定义序列，--sort custom 时必填（同时定义被保留单元与其顺序）
  --margin <policy>      余量策略 discard（默认）/ keep-partial / pad
  --pad-color <hex>      填充色 #RRGGBB / #AARRGGBB，默认透明
  --compose              合并为单图（重排）；缺省为分离导出
  --canvas ColxRow       [--compose 时必填] 重排画布的单元列 × 行（如 2x2）
  --output <file>        [--compose 时必填] 合并输出路径
  --output-dir <dir>     [分离时必填] 分离输出目录
  --format <fmt>         输出格式 png/jpeg/webp/bmp
  --naming <tpl>         分离命名模板
  -h, --help             显示本帮助

约束：
  --keep 与 --remove 互斥；--compose 与 --output-dir 互斥；--decorate 在 --sort custom 下不可用。
  说明：Core 先施加 snake 再整体 reverse（固定次序），故 reverse-snake 与 snake-reverse 结果相同。

示例：
  idc grid --input photo.jpg --grid 100,100,200,150 --keep 0,0 --keep 0,2 --keep 2,0 --keep 2,2 --compose --canvas 2x2 --output result.png
  idc grid --input photo.jpg --grid 0,0,100,100 --remove 1,1 --output-dir ./cells/
  idc grid --input photo.jpg --grid 0,0,100,100 --sort custom --order 0,0;1,1;2,2 --compose --canvas 3x1 --output diag.png

常见错误：
  退出码 1：缺必填 / 互斥冲突 / r,c 越界 / 格式非法    退出码 3：输入图像错误    退出码 4：输出失败
)HELP";
}

void helpConfig() {
    std::cout << R"HELP(idc config — 配置文件作业（终稿 §9 / FR-L3.8）

语义：从 JSON 配置文件读取一次完整作业的全部参数（source/preprocess/cut/order/emit/select），
      复用 Core 的 JSON 模块解析（CLI 不自实现解析）。配置文件不含图像路径，故执行时仍须 --input。

选项：
  --load <file>          [必填] 从 JSON 配置文件读取全部参数
  --input <file>         [执行时必填] 输入图像路径
  --output <file>        合并输出路径（配置 emit.mode=merged 时用）
  --output-dir <dir>     分离输出目录（配置 emit.mode=separate 时用）
  --save-config <file>   把加载的配置另存为 JSON（转换 / 校验；dry-run，不执行作业）
  -h, --help             显示本帮助

示例：
  idc config --load my-config.json --input photo.jpg --output result.png
  idc extract --input photo.jpg --rect 100,100,300,250 --output out.png --save-config out.json

常见错误：
  退出码 1：缺 --load / 缺输出 / 配置解析失败    退出码 3：配置文件或输入图像不存在 / 不可读
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
