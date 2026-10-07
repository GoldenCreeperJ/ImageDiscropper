// ============================================================================
// 文件：src/help.cpp
// 作用：实现 help.h——顶层帮助、子命令帮助与版本信息文本。
// 分块依据：纯文本输出，集中一处以便命令清单 / 术语 / 示例与规格保持一致（SPEC §2.2 术语、
//       帮助要求与版本格式见 Cli README）；命令名统一用实际可执行名 "idc"。
// 排版约定：
//   · 选项行经 optionRow() 以固定列宽打印——列宽按各命令最长选项常量维护，杜绝对齐漂移；
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
constexpr int kTopOptWidth = 20;     // --save-config <file>
constexpr int kExtractOptWidth = 22; // --annotation '<json>'
constexpr int kEraseOptWidth = 22;   // --annotation '<json>'
constexpr int kGridOptWidth = 25;    // --merge-decorate <method>
constexpr int kConfigOptWidth = 22;  // --annotation '<json>'
constexpr int kNewOptWidth = 20;     // --orientation <dir>

// 打印一行选项：两空格缩进 + 定宽选项列 + 两空格 + 描述。
// 描述以 [必填] / [可重复] 标签开头（无标签时直接写描述），
// 因此所有行的标签在同一列起头，视觉对齐。
void optionRow(const int width, const char* opt, const char* desc) {
    std::cout << "  " << std::left << std::setw(width) << opt << "  " << desc << '\n';
}

} // namespace

// 顶层帮助：工具定位 + 三层模式 + 子命令 + 全局选项 + 退出码 + 各层示例。
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
  new       新建画布：按尺寸 / 底色(alpha) / 程序化图案生成一张画布并落盘
  config    从 JSON 配置文件执行一次完整作业（含可选标注 / 多层画布）

全局选项：
)HELP";
    optionRow(kTopOptWidth, "-h, --help", "显示帮助");
    optionRow(kTopOptWidth, "-v, --version", "显示版本");
    optionRow(kTopOptWidth, "--verbose", "输出详细日志到 stderr");
    optionRow(kTopOptWidth, "--quiet", "静默模式，仅输出错误");
    optionRow(kTopOptWidth, "--config <file>", "从 JSON 配置文件读取全部参数（此时忽略命令的几何参数）");
    optionRow(kTopOptWidth, "--save-config <file>", "把当前参数序列化为 JSON 配置文件（dry-run，不执行作业）");
    optionRow(kTopOptWidth, "--bundle-assets", "与 --save-config 同用时，把配置内 File 图层源图按相对名复制进 <配置名>.assets/（需显式开启，非默认）");
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
  # 标注烧入：extract 同时烧入一条红色描边矩形
  idc extract --input photo.jpg --rect 100,100,300,250 --annotation '{"type":"rectangle","x1":10,"y1":10,"x2":80,"y2":50,"color":"#FFFF0000","strokeWidth":3}' --output out.png
  # 新建：生成 1024x768 棋盘格（白底 / 半透明蓝纹）写盘
  idc new --size 1024x768 --pattern checkerboard --bg "#FFFFFFFF" --fg "#800000FF" --period 64 --output board.png
  # 生成源切割：用程序生成的画布直接作为输入（--size 替代 --input）
  idc extract --size 200x200 --pattern radial-gradient --bg "#FF000000" --fg "#FFFFFFFF" --rect 50,50,150,150 --output grad.png

查看某子命令的完整选项：idc <command> --help
)HELP";
}

// 版本信息：形如 "idc <version> (core <core-version>)"。
void printVersion() {
    std::cout << "idc " << IDC_CLI_VERSION << " (core " << IDC_CORE_VERSION << ")\n";
}

// 各子命令帮助文本（语义 / 全部选项 / 示例 / 常见错误）。
namespace {

// 标注段（extract / erase / grid / config 四层命令通用）：自包含的完整字段说明与示例，
// 不引用任何工程文档（用户只需读 --help 即可写出一条合法标注）。lead 为命令差异提示，可为 nullptr。
void annotationSection(const int width, const char* lead) {
    std::cout << R"HELP(
标注（可选，切割前烧入工作图；坐标基准＝预处理产出后的工作图尺寸）：
)HELP";
    if (lead != nullptr) std::cout << "  " << lead << '\n';
    optionRow(width, "--annotation '<json>'", "[可重复] 内联一条标注对象，或一个标注对象数组");
    optionRow(width, "--annotations <file>", "[可重复] 从「根为标注数组」的 JSON 文件读取");
    std::cout << R"HELP(  字段按 type 分派几何，样式对所有类型通用：
    · line 直线：points 恰 2 顶点 [[x1,y1],[x2,y2]]
    · polyline 折线：points ≥2 顶点（开放线段）
    · 约束图元——只写双锚点 x1,y1,x2,y2（几何由引擎依锚点构造、恒合法，不接受手写顶点）；均可选 cornerRadius（圆角像素）：
        rectangle 矩形：(x1,y1) 与 (x2,y2) 为对角两角
        square 正方形：(x1,y1) 为一顶点，边长 = max(|Δx|,|Δy|)，朝 (x2,y2) 所在象限生长
        rhombus 菱形：(x1,y1) 为中心，(|Δx|,|Δy|) 为两半对角线
        isos-triangle 等腰 / equ-triangle 等边：(x1,y1) 为底边中点，(|Δx|,|Δy|) 定半底与高（等边按主方向取边长）
        rect-triangle 直角 / equ-rect-triangle 等腰直角：(x1,y1) 为直角顶点，(|Δx|,|Δy|) 定两直角边（等腰直角取等长 max）
    · ellipse 椭圆 / circle 圆——同样只写双锚点 x1,y1,x2,y2（几何由引擎依锚点构造，不接受包围盒宽高）：
        circle 圆：(x1,y1) 为圆心，半径 = |(x1,y1)→(x2,y2)|
        ellipse 椭圆：(x1,y1) 为圆心，(|Δx|,|Δy|) 为两半轴（均须 >0）
    · path 自由路径：segments=[{"t":类型,"coords":数值数组},…]（须非空且至少一条有效段）
        t=M/L → coords=[x,y]；t=Q → coords=[控制点x,控制点y,x,y]；t=C → coords=[c1x,c1y,c2x,c2y,x,y]；t=Z → coords=[]
    · text 文字：x, y（起点）, text（内容）, fontSize（像素字号，若给出须 >0）
  样式字段（均可省略取默认）：
    color 描边色、fillColor 填充色——均支持 #RRGGBB（不透明）或 #AARRGGBB（AA 为 alpha：00 全透~FF 全不透）
    strokeWidth 线宽像素（默认 2）；fill 是否填充 true/false（默认 false）
    zOrder 叠放次序（整数，0＝最底、越大越靠上；可省略，缺省回退为该元素在数组中的下标）——顶层后画、命中优先，--save-config 随 annotations 段往返；非整数（如小数）判非法，负数合法（视为最底层排序、载入后归一化为非负下标）
    transform（GUI 的缩放/旋转快照）命令行一般省略，几何字段始终写局部坐标
  校验与退出码（强制校验、逐条报因）：
    · 任一条标注非法即整段 --annotation/--annotations 失败（退出码 1，stderr 逐条列出「第 N 个元素：原因」），不静默丢弃。
    · type 缺失或未知字符串一律报错（不再默认按 line 处理）。
    · 锚点退化即报「锚点退化」：矩形/菱形需 |Δx|>0 且 |Δy|>0；正方形/等腰直角需 max(|Δx|,|Δy|)>0；等腰/等边需半底与高、直角需两直角边非退化；圆需半径 >0、椭圆需两半轴均 >0。
    · 自由图元：line 点数须恰为 2；text 的 fontSize 若给出须 >0；path 须有非空且有效的 segments。
    · color/fillColor 须为合法 #RRGGBB 或 #AARRGGBB；strokeWidth / zOrder 须为整数（zOrder 为小数/字符串即报错；负数合法）。JSON 语法错误、根非对象/数组同样退出码 1。
  单条示例：
    {"type":"line","points":[[10,10],[200,150]],"color":"#FF0000","strokeWidth":4}
    {"type":"rectangle","x1":10,"y1":10,"x2":120,"y2":80,"fill":true,"fillColor":"#80FF0000","strokeWidth":2}
    {"type":"rhombus","x1":60,"y1":50,"x2":110,"y2":20,"color":"#FF0000"}
    {"type":"circle","x1":70,"y1":70,"x2":100,"y2":70,"color":"#00FF00"}
    {"type":"text","x":20,"y":30,"text":"HELLO","fontSize":18,"color":"#0000FF"}
    {"type":"path","segments":[{"t":"M","coords":[0,0]},{"t":"L","coords":[50,50]},{"t":"Z","coords":[]}],"color":"#00FFFF"}
  多条：重复 --annotation 各传一条，或一次传数组 [{"type":"line",…},{"type":"circle",…}]；也可 --annotations file 从文件批量读入。
  导出：--save-config 会把命令行标注一并写入配置文件的 annotations 段（往返不丢）。)HELP";
}

// 图案 × 参数适用矩阵（new 与生成源共用）：逐图案说明哪些参数真正生效、哪些被忽略——
// 与 Core generate::makePattern 实现严格一致（period 下限 2、lineWidth/dotRadius 下限 1，由 Core 钳位）。
void patternMatrix() {
    std::cout << R"HELP(
图案 × 参数适用矩阵（未列参数对该图案无效，与引擎实际行为一致）：
    solid            整幅＝--bg；忽略 --fg/--orientation/--period/--line-width/--dot-radius
    linear-gradient  --bg↔--fg 沿 --orientation 线性插值（水平随 x、垂直随 y）；忽略 --period/--line-width/--dot-radius
    radial-gradient  --bg（边缘）↔--fg（圆心）径向插值；--orientation 无效，忽略 --period/--line-width/--dot-radius
    checkerboard     --fg 方格边长＝--period（--orientation 无效）；忽略 --line-width/--dot-radius
    stripes          --fg 条带宽度＝--period；--orientation 水平=竖条(随 x)/垂直=横条(随 y)/diagonal-down=↘(沿 x−y)/diagonal-up=↗(沿 x+y)；忽略 --line-width/--dot-radius
    grid-lines       --fg 网格线：间距＝--period、线宽＝--line-width；--orientation 无效，忽略 --dot-radius
    dot-grid         --fg 圆点：点距＝--period、半径＝--dot-radius；--orientation 无效，忽略 --line-width
    concentric       --fg 同心环：环间距＝--period、环宽＝--line-width；--orientation 水平=圆环(欧氏)/垂直=方环(切比雪夫回字纹)，忽略 --dot-radius
    radial-rays      --fg 放射扇区：疏密＝--period（值越小越密）；--orientation 无效，忽略 --line-width/--dot-radius
    hue-gradient     全色相彩虹沿 --orientation 扫描（H 0→1、全饱和全明度不透明），--period＝一道彩虹的像素宽；忽略 --bg/--fg/--line-width/--dot-radius
    noise            确定性伪随机噪声：--seed 定种（同 seed+同尺寸恒同图，供逐像素对拍）、--noise 选配色 full-color(默认)/grayscale/two-color；two-color 于 --bg↔--fg 间随机取色，其余忽略 --bg/--fg/--orientation/--period/--line-width/--dot-radius
  alpha：--bg/--fg 的 #AARRGGBB alpha 参与插值并写盘；PNG/WebP 保留透明，JPEG/BMP 导出时按填充色压平。
  注：旧「贴图图案 image」已从图案类型中彻底移除（--pattern image / --pattern-image / --image-anchor / --image-scale 不再受理）；
      图片作为「附加图层」只能经配置文件的 layers 段（source.kind="file"）传入，由 config --load 合成进画布。
)HELP";
}

// 图像生成源选项（extract / erase / grid / config 四层命令通用）：说明 --input 可用 --size 程序生成
// 画布替代（二者互斥、择一），并列出与 new 命令同一套生成选项。lead 为命令差异提示，可为 nullptr。
void generateSourceSection(const int width, const char* lead) {
    std::cout << R"HELP(
生成源（可选）：不给 --input 时，用 --size（可配 --pattern 等）程序生成一张画布作为输入；--input 与 --size 互斥、择一。
)HELP";
    if (lead != nullptr) std::cout << "  " << lead << '\n';
    optionRow(width, "--size WxH", "生成画布尺寸（像素正整数；单边 ≤65535 且 宽×高 ≤64,000,000≈256MB；替代 --input）");
    optionRow(width, "--pattern <kind>", "solid(默认)/linear-gradient/radial-gradient/checkerboard/stripes/grid-lines/dot-grid/concentric/radial-rays/hue-gradient/noise");
    optionRow(width, "--orientation <dir>", "horizontal(默认)/vertical/diagonal-down/diagonal-up——线性渐变轴 / 条纹方向(含斜纹) / 同心环形状 / 色相扫描轴");
    optionRow(width, "--bg <hex>", "底色 #RRGGBB/#AARRGGBB（alpha 生效，默认不透明白 #FFFFFFFF）");
    optionRow(width, "--fg <hex>", "前景/图案色 #RRGGBB/#AARRGGBB（默认黑 #FF000000）");
    optionRow(width, "--period <n>", "周期 / 格子边长 / 环间距 / 色相波长像素（正整数，默认 32）");
    optionRow(width, "--line-width <n>", "网格线 / 同心环（圆环·方环）线宽像素（正整数，默认 1）");
    optionRow(width, "--dot-radius <n>", "点阵点半径像素（正整数，默认 2）");
    optionRow(width, "--seed <n>", "噪声随机种子（非负整数，默认 0；仅 --pattern noise 生效，同种子恒同图）");
    optionRow(width, "--noise <mode>", "噪声配色模式 full-color(默认)/grayscale/two-color（仅 --pattern noise 生效）");
    patternMatrix();
}

void helpNew() {
    std::cout << R"HELP(idc new — 新建画布（程序化生成画布并落盘）

语义：按尺寸 + 底色（含 alpha）+ 图案类型与参数，由图案工厂生成一张全新 RGBA 画布，
      写到 --output。与 GUI「新建画布」对话框等价；不涉及切割（要切割生成图请改用 extract/erase/grid 的 --size 生成源）。

尺寸与底色：
)HELP";
    optionRow(kNewOptWidth, "--size WxH", "[必填] 生成画布尺寸（像素正整数；单边 ≤65535 且 宽×高 ≤64,000,000≈256MB）");
    optionRow(kNewOptWidth, "--bg <hex>", "底色 #RRGGBB/#AARRGGBB（alpha 生效，默认不透明白 #FFFFFFFF）");
    std::cout << R"HELP(
图案：
)HELP";
    optionRow(kNewOptWidth, "--pattern <kind>", "solid(默认)/linear-gradient/radial-gradient/checkerboard/stripes/grid-lines/dot-grid/concentric/radial-rays/hue-gradient/noise");
    optionRow(kNewOptWidth, "--orientation <dir>", "horizontal(默认)/vertical/diagonal-down/diagonal-up——线性渐变轴 / 条纹方向(含斜纹) / 同心环形状 / 色相扫描轴");
    optionRow(kNewOptWidth, "--fg <hex>", "前景/图案色 #RRGGBB/#AARRGGBB（默认黑 #FF000000）");
    optionRow(kNewOptWidth, "--period <n>", "周期 / 格子边长 / 环间距 / 色相波长像素（正整数，默认 32）");
    optionRow(kNewOptWidth, "--line-width <n>", "网格线 / 同心环（圆环·方环）线宽像素（正整数，默认 1）");
    optionRow(kNewOptWidth, "--dot-radius <n>", "点阵点半径像素（正整数，默认 2）");
    optionRow(kNewOptWidth, "--seed <n>", "噪声随机种子（非负整数，默认 0；仅 --pattern noise 生效，同种子恒同图）");
    optionRow(kNewOptWidth, "--noise <mode>", "噪声配色模式 full-color(默认)/grayscale/two-color（仅 --pattern noise 生效）");
    patternMatrix();
    std::cout << R"HELP(
输出：
)HELP";
    optionRow(kNewOptWidth, "--output <file>", "生成图像的落盘路径（--save-config dry-run 时可省略）");
    optionRow(kNewOptWidth, "--format <fmt>", "输出格式 png/jpeg/webp/bmp（默认由 --output 扩展名推断）");
    optionRow(kNewOptWidth, "--quality <n>", "有损质量 0~100（默认 90；仅 jpeg/webp 生效，png/bmp 忽略）");
    optionRow(kNewOptWidth, "--pad-color <hex>", "有损格式（jpeg/bmp）压平透明像素所用背景色 #RRGGBB/#AARRGGBB（默认透明→黑底，建议透明画布导出 jpeg 时显式指定）");
    std::cout << R"HELP(
通用：
)HELP";
    optionRow(kNewOptWidth, "--save-config <file>", "dry-run：不落像素，把本次生成配方（source 尺寸 + generate 段 + emit）序列化进配置，供 config --load 复现画布（此路径不要求 --output）");
    optionRow(kNewOptWidth, "-h, --help", "显示本帮助");
    std::cout << R"HELP(
示例：
  idc new --size 800x600 --bg "#FFFFFFFF" --output white.png
  idc new --size 1024x1024 --pattern checkerboard --bg "#FFFFFFFF" --fg "#FF000000" --period 64 --output board.png
  idc new --size 1920x1080 --pattern radial-gradient --bg "#FF000000" --fg "#FFFFFFFF" --output glow.png
  # 斜纹（已合进条纹：--orientation 选 diagonal-down ↘ / diagonal-up ↗）与全色相彩虹
  idc new --size 512x512 --pattern stripes --orientation diagonal-down --fg "#FF000000" --period 24 --output diag.png
  idc new --size 512x256 --pattern hue-gradient --period 128 --output rainbow.png
  # 同心方框（已合进同心环：--orientation vertical = 方环回字纹）
  idc new --size 512x512 --pattern concentric --orientation vertical --fg "#FF000000" --period 32 --line-width 2 --output rings.png
  # 确定性噪声（同 --seed 恒同图，供逐像素对拍）
  idc new --size 256x256 --pattern noise --seed 1234 --noise full-color --output noise.png
  # 透明底：底色 alpha=00 得全透明画布
  idc new --size 512x512 --bg "#00FFFFFF" --output transparent.png
  # 透明画布导出 JPEG：显式给压平底色，避免静默黑底
  idc new --size 512x512 --bg "#00FFFFFF" --format jpeg --pad-color "#FFFFFFFF" --output flat.jpg
  # 把生成配方存为配置（dry-run），供 config --load 据 generate 段复现画布
  idc new --size 200x150 --pattern solid --bg "#FFFF0000" --save-config recipe.json

常见错误：
  退出码 1：缺 --size；非 dry-run 时缺 --output；--size 非 WxH 正整数 / 单边超 65535 / 宽×高超 64,000,000；--pattern / --orientation / --bg / --fg / --pad-color / --period / --seed / --noise 等格式非法；--quality 非 0~100 整数；给出 --input（new 不接受）
  退出码 4：输出写入失败
)HELP";
}

void helpExtract() {
    std::cout << R"HELP(idc extract — L1 标准提取模式

语义：极性恒为 keep，输出恒为单图。矩形保留中心单元；横线 / 竖线保留整条带。
      切割线贯穿全图（公理），保留区经坍缩拼接为单图。

输入：
)HELP";
    optionRow(kExtractOptWidth, "--input <file>", "输入图像路径（与 --size 二选一）");
    generateSourceSection(kExtractOptWidth, nullptr);
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
    optionRow(kExtractOptWidth, "--quality <n>", "有损质量 0~100（默认 90；仅 jpeg/webp 生效，png/bmp 忽略）");
    annotationSection(kExtractOptWidth, nullptr);
    std::cout << R"HELP(

通用：
)HELP";
    optionRow(kExtractOptWidth, "-h, --help", "显示本帮助");
    std::cout << R"HELP(
示例：
  idc extract --input photo.jpg --rect 100,100,300,250 --output out.png
  idc extract --input photo.jpg --hband 100,250 --output band.png
  idc extract --input photo.jpg --vband 100,300 --output band.png
  # 保留矩形同时烧入一条矩形标注
  idc extract --input photo.jpg --rect 100,100,300,250 --annotation '{"type":"rectangle","x1":50,"y1":50,"x2":120,"y2":120,"fill":true,"fillColor":"#FFFF0000"}' --output out.png

常见错误：
  退出码 1：缺少输入图像（--input 或 --size）/ 缺少 --output；三选一未满足（缺切割线或同时给出多个）；坐标非法或退化（x1>=x2 / y1>=y2，退化选框请改用 --hband / --vband）
  退出码 3：输入图像不存在或无法读取
  退出码 4：输出写入失败
)HELP";
}

void helpErase() {
    std::cout << R"HELP(idc erase — L2 反向剔除模式（差异化内核）

语义：极性恒为 remove，"按线删除"——抽掉中缝、两侧对接，尺寸变小（W−Δx × H−Δy）。
      单矩形 → 十字切割（保留四角）；--rect 多次 → 多矩形并集剔除；
      横线 / 竖线 → 删整条带。缺省分离导出（各保留块一张图）；--merge collapse 合并为单图。

输入：
)HELP";
    optionRow(kEraseOptWidth, "--input <file>", "输入图像路径（与 --size 二选一）");
    generateSourceSection(kEraseOptWidth, nullptr);
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
    optionRow(kEraseOptWidth, "--quality <n>", "有损质量 0~100（默认 90；仅 jpeg/webp 生效，png/bmp 忽略）");
    optionRow(kEraseOptWidth, "--naming <tpl>", "分离命名模板，默认 {name}_{index:03d}");
    annotationSection(kEraseOptWidth, nullptr);
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
  退出码 1：缺必填 / 互斥冲突 / 坐标非法或退化（退化选框请改用 --hband / --vband）/ --merge rearrange（L2 不支持重排）
  退出码 2：坍缩不可行（见约束）
  退出码 3：输入图像错误
  退出码 4：输出目录 / 文件写入失败
)HELP";
}

void helpGrid() {
    std::cout << R"HELP(idc grid — L3 网格分割模式（完备表达层）

语义：以基准点 (x0,y0) 为相位锚、单元尺寸 (cw,ch) 为周期，生成贯穿全图的切割线并诱导
      网格；行数 / 列数由图像边界自动推导（不作为参数）。--keep / --remove 逐单元选择，
      可排序后分离导出或重排合并到 ColxRow 画布。

输入：
)HELP";
    optionRow(kGridOptWidth, "--input <file>", "输入图像路径（与 --size 二选一）");
    generateSourceSection(kGridOptWidth, nullptr);
    std::cout << R"HELP(
网格：
)HELP";
    optionRow(kGridOptWidth, "--grid x0,y0,cw,ch", "[必填] 基准点 + 单元尺寸（基准点可为负，cw/ch 必须为正）");
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
    optionRow(kGridOptWidth, "--quality <n>", "有损质量 0~100（默认 90；仅 jpeg/webp 生效，png/bmp 忽略）");
    optionRow(kGridOptWidth, "--naming <tpl>", "分离命名模板");
    annotationSection(kGridOptWidth, nullptr);
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
  - 引擎先施加 snake 再整体 reverse（固定次序），故 reverse-snake 与 snake-reverse 结果相同。

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
    std::cout << R"HELP(idc config — 配置文件作业

语义：从 JSON 配置文件读取一次完整作业的全部参数（source/generate/layers/preprocess/cut/order/emit/select），
      复用统一的 JSON 解析（命令行端不自行重复实现）。配置文件一般不含图像路径，故执行时通常须 --input 或 --size 提供**底图**；
      底图（画布基座，input 生态位）来源多选一：--input 读文件、--size 命令行生成、或配置 generate 段生成；
      若三者皆无而配置含 layers 段，则叠加层直接铺于全透明底之上，仍可执行。

配置：
)HELP";
    optionRow(kConfigOptWidth, "--load <file>", "[必填] 从 JSON 配置文件读取全部参数");
    optionRow(kConfigOptWidth, "--save-config <file>", "把加载的配置另存为 JSON（转换 / 校验；dry-run，不执行作业）");
    optionRow(kConfigOptWidth, "--bundle-assets", "与 --save-config 同用：把 layers 内 File 层源图按相对名复制进 <配置名>.assets/（越界拒绝、缺源报错）");
    std::cout << R"HELP(
多层画布（layers 段，可选）：仅承载**叠加图层**（不含底图），按 zOrder 叠于底图之上；执行前由引擎
      展平为 source 尺寸的画布（顺序：全透明底 → 底图 → 叠加层；引擎仍只收一张展平画布）。
      底图来自 --input / --size / 配置 generate 段（可组合、非互斥）；皆无时叠加层直接铺于全透明底之上。每层来源三选一：
        · recipe 配方（与 generate 段同构的图案参数，可逐字节复现，不留位图）；
        · file  文件（仅存相对名，相对配置文件目录或 <配置名>.assets/ 解析；解码注入；越界（绝对路径 / ..）拒绝，缺文件降级为占位）；
        · 空占位（缺文件 / 未指定，合成时跳过）。
      每层可带 transform（画布点 inCanvasAnchor + 图层枢轴 layerPivot + 自由 x/y + 目标 width/height）与逐层 preprocess；
      越出画布的像素在写入时自然裁剪（导出即舍弃）。

底图（generate 段，可选）：底图＝与图层同构的一条图层记录，仅角色为钉住最底、不可拖 / 不可改落位。
      除图案字段（kind/background/foreground/… 与旧 generate 逐字段等价）外，新建画布所附图片底图可额外携
      fit + boxWidth/boxHeight + imageAnchor/imagePivot + x/y + backdrop（图层框衬底图案）落位键，使底图落位随配置往返、可复现
      （**配置只存落位、不存文件路径**，底图位图恒由运行时 --input 供给，故同一配置可对不同输入批处理复用）。
      --input / 打开图＝无落位、画布尺寸＝图片尺寸的恒等底图，外部用法不变。

预处理（preprocess 数组，可选）：gray/split/invert/rotate/flip/resize 六类步（不透明度不再是预处理步，改由层合成参数承载）。

层不透明度（transform.opacity，可选）：整层淡出程度（0..255，缺省 255＝完全不透明、0＝完全透明）。
      该参数在导出合成时按不透明度比例作用于整层、不改动像素本身，故灰度图同样生效；旧配置里写在 preprocess 的 opacity 步骤会在载入时自动转成该层的 transform.opacity。
)HELP";
    std::cout << R"HELP(
输入输出：
)HELP";
    optionRow(kConfigOptWidth, "--input <file>", "底图图像路径（与 --size 二选一；与配置 layers 段可组合，layers 叠加于此底图之上；配置含 generate 段时亦可均省略）");
    generateSourceSection(kConfigOptWidth, "命令行生成选项会覆盖本次执行的输入来源；若既无 --input/--size、配置又含 generate 段，则据配置合成画布。");
    optionRow(kConfigOptWidth, "--output <file>", "合并输出路径（配置 emit.mode=merged 时用）");
    optionRow(kConfigOptWidth, "--output-dir <dir>", "分离输出目录（配置 emit.mode=separate 时用）");
    annotationSection(kConfigOptWidth, "命令行标注会追加在 --load 配置自带的 annotations 段之后。");
    std::cout << R"HELP(

通用：
)HELP";
    optionRow(kConfigOptWidth, "-h, --help", "显示本帮助");
    std::cout << R"HELP(
示例：
  idc config --load my-config.json --input photo.jpg --output result.png
  idc extract --input photo.jpg --rect 100,100,300,250 --output out.png --save-config out.json
  # 多层画布配置（仅 layers 叠加层）直接执行：无底图时叠加层铺于全透明底之上
  idc config --load canvas.json --output out.png
  # 底图 + 叠加层组合：--input 提供底图，配置 layers 段叠加其上（展平顺序：全透明底 → 底图 → 叠加层）
  idc config --load canvas.json --input base.jpg --output out.png
  # 把配置另存并携带 File 层资源到 <配置名>.assets/（供随配置分发 / 复现）
  idc config --load canvas.json --save-config bundle.json --bundle-assets

常见错误：
  退出码 1：缺 --load / 缺输出 / 配置解析失败；--input 与 --size 同时给出；--bundle-assets 时 File 层名为绝对路径 / 含 ..（越界）
  退出码 2：画布展平产出空图（source 尺寸非法）/ 引擎业务失败
  退出码 3：配置文件或输入图像不存在 / 不可读；图层资源路径越界；--bundle-assets 时 File 层源图缺失
  退出码 4：输出 / 写盘失败；--bundle-assets 复制资源或建目录失败
)HELP";
}

} // namespace

// 子命令帮助分派；未知命令回退为顶层帮助。
void printCommandHelp(const std::string& cmd) {
    if (cmd == "extract") { helpExtract(); return; }
    if (cmd == "erase") { helpErase(); return; }
    if (cmd == "grid") { helpGrid(); return; }
    if (cmd == "config") { helpConfig(); return; }
    if (cmd == "new") { helpNew(); return; }
    printTopHelp();
}

} // namespace idc::cli
