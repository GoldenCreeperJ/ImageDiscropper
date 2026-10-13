// ============================================================================
// 文件：src/help.cpp
// 作用：CLI 帮助文本输出的全部实现——顶层帮助 / 版本 / 子命令分派，五个子命令的帮助
//       文本本体，跨命令复用的通用小节（标注段 / 图案矩阵 / 生成源段），以及各命令选项
//       列宽的编译期推导。对外只经 include/help.h 暴露 printTopHelp / printVersion /
//       printCommandHelp；其余皆为本 TU 内部件。（原 src/help/ 目录与 help_common.* 已合并至此。）
// 分块依据：帮助是纯文本输出，集中一处便于命令清单 / 术语 / 示例与规格保持一致（SPEC §2.2 术语、
//       帮助要求与版本格式见 Cli README）；命令名统一用实际可执行名 "idc"。文本本体（散文块 /
//       用法列 / 描述列）全在 cli_texts.h::help，本 TU 只决定输出顺序与列宽。
// 列宽（constexpr optWidth）：用法列内容不随语言变（optionRow 的 opt 恒为不可译契约串 hp::kUse*），
//       故其最长长度为编译期常量——从字符串真长推导，零运行期开销，既根除旧手数的 off-by-one
//       （曾把 --annotation '<json>' 数成 22，实为 21），加/减选项时列宽亦自动跟进。
// i18n 分工：用法列 help::kUse* = 选项名 + 占位符，属**不可译契约**（optionRow 的 opt 形参 const char*）；
//       描述列 / 小节 lead / 散文块走门面 IDC_CTR（返回 std::string，形参 const std::string&）。
// 排版约定：选项行 = 两空格缩进 + 定宽选项列 + 两空格 + 描述；选项按功能分组、组标题独占一行、组间空行；
//       需求标签统一为 [必填] / [可重复]，条件性必填写入描述括注。
// 测试约束：IT-13 断言 --version 形如 "idc <ver> (core <ver>)"；IT-14 断言顶层帮助含全部子命令名
//       （extract / erase / grid / config）——排版调整不得破坏这两条。
// 说明：版本号来自编译期宏 IDC_CLI_VERSION / IDC_CORE_VERSION（由 CMake 注入，与构建系统一致）。
// ============================================================================
#include "help.h"
#include "i18n/cli_i18n.h"
#include "i18n/cli_texts.h"

#include <algorithm>
#include <initializer_list>
#include <iomanip>
#include <iostream>
#include <string>

// 编译期版本宏（正常由 CMake 注入；缺失时用占位，避免编译失败）。
#ifndef IDC_CLI_VERSION
#define IDC_CLI_VERSION "0.0.0"
#endif
#ifndef IDC_CORE_VERSION
#define IDC_CORE_VERSION "0.0.0"
#endif

namespace idc::cli {

// 子命令名简写（printCommandHelp 的形参叫 cmd，故别名取 cmds）；
//       用法列简写 hp（本层只剩 help::kUse*，选项名 + 占位符，不可译）。
namespace cmds = texts::cmd;
namespace hp = texts::help;

// ── 选项列宽：编译期按各命令实际打印的用法列串长取 max ──────────────────────
// 逐条列出该命令 optionRow 消费的 hp::kUse*（含其调用的共享小节选项），max 即列宽；
//   增删选项务必同步本清单——须与各小节体内实际打印的用法列一一对应。
constexpr int optWidth(const std::initializer_list<const char*> opts) {
    int w = 0;
    for (const char* o : opts) {
        int n = 0;
        while (o[n] != '\0') { ++n; }
        if (n > w) { w = n; }
    }
    return w;
}

// 共享小节各自的最长用法列（供所有调用该小节的命令并入，免每命令重列整串生成/标注选项）。
//   对应下方 generateSourceSection / annotationSection 的 optionRow 选项集。
constexpr int kGenSourceOptWidth = optWidth({
    hp::kUseSize, hp::kUsePattern, hp::kUseOrientation, hp::kUseBg, hp::kUseFg,
    hp::kUsePeriod, hp::kUseLineWidth, hp::kUseDotRadius, hp::kUseSeed, hp::kUseNoise});
constexpr int kAnnoOptWidth = optWidth({hp::kUseAnnotation, hp::kUseAnnotations});

// 顶层与 new：不引用共享小节，列宽即各自选项集合的 max（--save-config <file> = 20）。
constexpr int kTopOptWidth = optWidth({
    hp::kUseHelpShort, hp::kUseVersionShort, hp::kUseVerbose, hp::kUseQuiet,
    hp::kUseLang, hp::kUseConfigFile, hp::kUseSaveConfig, hp::kUseBundleAssets});
constexpr int kNewOptWidth = optWidth({
    hp::kUseSize, hp::kUseBg, hp::kUsePattern, hp::kUseOrientation, hp::kUseFg, hp::kUsePeriod,
    hp::kUseLineWidth, hp::kUseDotRadius, hp::kUseSeed, hp::kUseNoise, hp::kUseOutput,
    hp::kUseFormat, hp::kUseQuality, hp::kUsePadColor, hp::kUseSaveConfig, hp::kUseHelpShort});

// extract / erase / grid / config：自身选项 ∪ 生成源段 ∪ 标注段（三者的最长项皆落在 --annotation
//   '<json>' = 21，grid 因 --merge-decorate <method> = 25 反而更宽）。
constexpr int kExtractOptWidth = std::max({optWidth({
    hp::kUseInput, hp::kUseRect, hp::kUseHband, hp::kUseVband, hp::kUseOutput,
    hp::kUseFormat, hp::kUseQuality, hp::kUseHelpShort}), kGenSourceOptWidth, kAnnoOptWidth});
constexpr int kEraseOptWidth = std::max({optWidth({
    hp::kUseInput, hp::kUseRect, hp::kUseHband, hp::kUseVband, hp::kUseMerge, hp::kUseOutput,
    hp::kUseOutputDir, hp::kUseFormat, hp::kUseQuality, hp::kUseNaming, hp::kUseHelpShort}),
    kGenSourceOptWidth, kAnnoOptWidth});
constexpr int kGridOptWidth = std::max({optWidth({
    hp::kUseInput, hp::kUseGrid, hp::kUseKeep, hp::kUseRemove, hp::kUseSort, hp::kUseDecorate,
    hp::kUseOrder, hp::kUseMargin, hp::kUsePadColor, hp::kUseCompose, hp::kUseCanvas,
    hp::kUseMergeSort, hp::kUseMergeDecorate, hp::kUseOutput, hp::kUseOutputDir, hp::kUseFormat,
    hp::kUseQuality, hp::kUseNaming, hp::kUseHelpShort}), kGenSourceOptWidth, kAnnoOptWidth});
constexpr int kConfigOptWidth = std::max({optWidth({
    hp::kUseLoad, hp::kUseSaveConfig, hp::kUseBundleAssets, hp::kUseInput, hp::kUseOutput,
    hp::kUseOutputDir, hp::kUseHelpShort}), kGenSourceOptWidth, kAnnoOptWidth});

// 打印一行选项：两空格缩进 + 定宽选项列 + 两空格 + 描述。
// 描述以 [必填] / [可重复] 标签开头（无标签时直接写描述），所有行的标签在同一列起头，视觉对齐。
// opt 恒为 help::kUse*（不可译），desc 为门面现取出的译文。
void optionRow(const int width, const char* opt, const std::string& desc) {
    std::cout << "  " << std::left << std::setw(width) << opt << "  " << desc << '\n';
}

// ── 跨命令通用小节（原 help_common.cpp）─────────────────────────────────────
// 标注段（extract / erase / grid / config 四层命令通用）：自包含的完整字段说明与示例，
// 不引用任何工程文档（用户只需读 --help 即可写出一条合法标注）。lead 为命令差异提示，省略即无。
void annotationSection(const int width, const std::string& lead = {}) {
    std::cout << IDC_CTR(help, kTextAnnoHead);
    if (!lead.empty()) std::cout << "  " << lead << '\n';
    optionRow(width, hp::kUseAnnotation, IDC_CTR(help, kDescAnnoInline));
    optionRow(width, hp::kUseAnnotations, IDC_CTR(help, kDescAnnoFile));
    std::cout << IDC_CTR(help, kTextAnnoFields);
}

// 图案 × 参数适用矩阵（new 与生成源共用）：逐图案说明哪些参数真正生效、哪些被忽略——
// 与 Core generate::makePattern 实现严格一致（period 下限 2、lineWidth/dotRadius 下限 1，由 Core 钳位）。
void patternMatrix() {
    std::cout << IDC_CTR(help, kTextPatternMatrix);
}

// 图像生成源选项（extract / erase / grid / config 四层命令通用）：说明 --input 可用 --size 程序生成
// 画布替代（二者互斥、择一），并列出与 new 命令同一套生成选项。lead 为命令差异提示，空串即无。
void generateSourceSection(const int width, const std::string& lead = {}) {
    std::cout << IDC_CTR(help, kTextGenHead);
    if (!lead.empty()) std::cout << "  " << lead << '\n';
    optionRow(width, hp::kUseSize, IDC_CTR(help, kDescSizeGen));
    optionRow(width, hp::kUsePattern, IDC_CTR(help, kDescPatternKinds));
    optionRow(width, hp::kUseOrientation, IDC_CTR(help, kDescOrientationDirs));
    optionRow(width, hp::kUseBg, IDC_CTR(help, kDescBg));
    optionRow(width, hp::kUseFg, IDC_CTR(help, kDescFg));
    optionRow(width, hp::kUsePeriod, IDC_CTR(help, kDescPeriod));
    optionRow(width, hp::kUseLineWidth, IDC_CTR(help, kDescLineWidth));
    optionRow(width, hp::kUseDotRadius, IDC_CTR(help, kDescDotRadius));
    optionRow(width, hp::kUseSeed, IDC_CTR(help, kDescSeed));
    optionRow(width, hp::kUseNoise, IDC_CTR(help, kDescNoise));
    patternMatrix();
}

// ── 顶层帮助与版本 ──────────────────────────────────────────────────────────
void printTopHelp() {
    std::cout << IDC_CTR(help, kTextTopIntro);
    optionRow(kTopOptWidth, hp::kUseHelpShort, IDC_CTR(help, kDescHelp));
    optionRow(kTopOptWidth, hp::kUseVersionShort, IDC_CTR(help, kDescVersion));
    optionRow(kTopOptWidth, hp::kUseVerbose, IDC_CTR(help, kDescVerbose));
    optionRow(kTopOptWidth, hp::kUseQuiet, IDC_CTR(help, kDescQuiet));
    optionRow(kTopOptWidth, hp::kUseLang, IDC_CTR(help, kDescLang));
    optionRow(kTopOptWidth, hp::kUseConfigFile, IDC_CTR(help, kDescTopConfig));
    optionRow(kTopOptWidth, hp::kUseSaveConfig, IDC_CTR(help, kDescTopSaveConfig));
    optionRow(kTopOptWidth, hp::kUseBundleAssets, IDC_CTR(help, kDescTopBundleAssets));
    std::cout << IDC_CTR(help, kTextTopTail);
}

// 版本信息：形如 "idc <version> (core <core-version>)"。
void printVersion() {
    std::cout << "idc " << IDC_CLI_VERSION << " (core " << IDC_CORE_VERSION << ")\n";
}

// ── 各子命令帮助文本（语义 / 全部选项 / 示例 / 常见错误）──────────────────────
// 每个函数只决定本命令的输出顺序：文本本体（散文块 / 用法列 / 描述列）均在 cli_texts.h::help，
//   选项列宽取上方编译期算好的 k<命令>OptWidth 常量。

void helpNew() {
    std::cout << IDC_CTR(help, kTextNewIntro);
    std::cout << IDC_CTR(help, kTextSecSizeBg);
    optionRow(kNewOptWidth, hp::kUseSize, IDC_CTR(help, kDescSizeGenRequired));
    optionRow(kNewOptWidth, hp::kUseBg, IDC_CTR(help, kDescBg));
    std::cout << IDC_CTR(help, kTextSecPattern);
    optionRow(kNewOptWidth, hp::kUsePattern, IDC_CTR(help, kDescPatternKinds));
    optionRow(kNewOptWidth, hp::kUseOrientation, IDC_CTR(help, kDescOrientationDirs));
    optionRow(kNewOptWidth, hp::kUseFg, IDC_CTR(help, kDescFg));
    optionRow(kNewOptWidth, hp::kUsePeriod, IDC_CTR(help, kDescPeriod));
    optionRow(kNewOptWidth, hp::kUseLineWidth, IDC_CTR(help, kDescLineWidth));
    optionRow(kNewOptWidth, hp::kUseDotRadius, IDC_CTR(help, kDescDotRadius));
    optionRow(kNewOptWidth, hp::kUseSeed, IDC_CTR(help, kDescSeed));
    optionRow(kNewOptWidth, hp::kUseNoise, IDC_CTR(help, kDescNoise));
    patternMatrix();
    std::cout << IDC_CTR(help, kTextSecOutput);
    optionRow(kNewOptWidth, hp::kUseOutput, IDC_CTR(help, kDescNewOutput));
    optionRow(kNewOptWidth, hp::kUseFormat, IDC_CTR(help, kDescFormatInfer));
    optionRow(kNewOptWidth, hp::kUseQuality, IDC_CTR(help, kDescQuality));
    optionRow(kNewOptWidth, hp::kUsePadColor, IDC_CTR(help, kDescPadColorFlatten));
    std::cout << IDC_CTR(help, kTextSecCommon);
    optionRow(kNewOptWidth, hp::kUseSaveConfig, IDC_CTR(help, kDescNewSaveConfig));
    optionRow(kNewOptWidth, hp::kUseHelpShort, IDC_CTR(help, kDescHelpThis));
    std::cout << IDC_CTR(help, kTextNewTail);
}

void helpExtract() {
    std::cout << IDC_CTR(help, kTextExtractIntro);
    optionRow(kExtractOptWidth, hp::kUseInput, IDC_CTR(help, kDescInput));
    generateSourceSection(kExtractOptWidth);
    std::cout << IDC_CTR(help, kTextSecCutThree);
    optionRow(kExtractOptWidth, hp::kUseRect, IDC_CTR(help, kDescRectKeep));
    optionRow(kExtractOptWidth, hp::kUseHband, IDC_CTR(help, kDescHbandKeep));
    optionRow(kExtractOptWidth, hp::kUseVband, IDC_CTR(help, kDescVbandKeep));
    std::cout << IDC_CTR(help, kTextSecOutput);
    optionRow(kExtractOptWidth, hp::kUseOutput, IDC_CTR(help, kDescOutputRequired));
    optionRow(kExtractOptWidth, hp::kUseFormat, IDC_CTR(help, kDescFormatInfer));
    optionRow(kExtractOptWidth, hp::kUseQuality, IDC_CTR(help, kDescQuality));
    annotationSection(kExtractOptWidth);
    std::cout << IDC_CTR(help, kTextSecCommonBlank);
    optionRow(kExtractOptWidth, hp::kUseHelpShort, IDC_CTR(help, kDescHelpThis));
    std::cout << IDC_CTR(help, kTextExtractTail);
}

void helpErase() {
    std::cout << IDC_CTR(help, kTextEraseIntro);
    optionRow(kEraseOptWidth, hp::kUseInput, IDC_CTR(help, kDescInput));
    generateSourceSection(kEraseOptWidth);
    std::cout << IDC_CTR(help, kTextSecCutThree);
    optionRow(kEraseOptWidth, hp::kUseRect, IDC_CTR(help, kDescRectErase));
    optionRow(kEraseOptWidth, hp::kUseHband, IDC_CTR(help, kDescHbandErase));
    optionRow(kEraseOptWidth, hp::kUseVband, IDC_CTR(help, kDescVbandErase));
    std::cout << IDC_CTR(help, kTextSecExport);
    optionRow(kEraseOptWidth, hp::kUseMerge, IDC_CTR(help, kDescMerge));
    optionRow(kEraseOptWidth, hp::kUseOutput, IDC_CTR(help, kDescOutputMergeErase));
    optionRow(kEraseOptWidth, hp::kUseOutputDir, IDC_CTR(help, kDescOutputDirErase));
    optionRow(kEraseOptWidth, hp::kUseFormat, IDC_CTR(help, kDescFormatPngDefault));
    optionRow(kEraseOptWidth, hp::kUseQuality, IDC_CTR(help, kDescQuality));
    optionRow(kEraseOptWidth, hp::kUseNaming, IDC_CTR(help, kDescNamingDefault));
    annotationSection(kEraseOptWidth);
    std::cout << IDC_CTR(help, kTextSecCommonBlank);
    optionRow(kEraseOptWidth, hp::kUseHelpShort, IDC_CTR(help, kDescHelpThis));
    std::cout << IDC_CTR(help, kTextEraseTail);
}

void helpGrid() {
    std::cout << IDC_CTR(help, kTextGridIntro);
    optionRow(kGridOptWidth, hp::kUseInput, IDC_CTR(help, kDescInput));
    generateSourceSection(kGridOptWidth);
    std::cout << IDC_CTR(help, kTextSecGrid);
    optionRow(kGridOptWidth, hp::kUseGrid, IDC_CTR(help, kDescGrid));
    std::cout << IDC_CTR(help, kTextSecSelect);
    optionRow(kGridOptWidth, hp::kUseKeep, IDC_CTR(help, kDescKeepCell));
    optionRow(kGridOptWidth, hp::kUseRemove, IDC_CTR(help, kDescRemoveCell));
    std::cout << IDC_CTR(help, kTextSecSortDecorate);
    optionRow(kGridOptWidth, hp::kUseSort, IDC_CTR(help, kDescSort));
    optionRow(kGridOptWidth, hp::kUseDecorate, IDC_CTR(help, kDescDecorate));
    optionRow(kGridOptWidth, hp::kUseOrder, IDC_CTR(help, kDescOrderCustom));
    std::cout << IDC_CTR(help, kTextSecMargin);
    optionRow(kGridOptWidth, hp::kUseMargin, IDC_CTR(help, kDescMargin));
    optionRow(kGridOptWidth, hp::kUsePadColor, IDC_CTR(help, kDescPadColorDefault));
    std::cout << IDC_CTR(help, kTextSecCompose);
    optionRow(kGridOptWidth, hp::kUseCompose, IDC_CTR(help, kDescCompose));
    optionRow(kGridOptWidth, hp::kUseCanvas, IDC_CTR(help, kDescCanvas));
    optionRow(kGridOptWidth, hp::kUseMergeSort, IDC_CTR(help, kDescMergeSort));
    optionRow(kGridOptWidth, hp::kUseMergeDecorate, IDC_CTR(help, kDescMergeDecorate));
    std::cout << IDC_CTR(help, kTextSecExport);
    optionRow(kGridOptWidth, hp::kUseOutput, IDC_CTR(help, kDescOutputMergeCompose));
    optionRow(kGridOptWidth, hp::kUseOutputDir, IDC_CTR(help, kDescOutputDirGrid));
    optionRow(kGridOptWidth, hp::kUseFormat, IDC_CTR(help, kDescFormatPlain));
    optionRow(kGridOptWidth, hp::kUseQuality, IDC_CTR(help, kDescQuality));
    optionRow(kGridOptWidth, hp::kUseNaming, IDC_CTR(help, kDescNamingPlain));
    annotationSection(kGridOptWidth);
    std::cout << IDC_CTR(help, kTextSecCommonBlank);
    optionRow(kGridOptWidth, hp::kUseHelpShort, IDC_CTR(help, kDescHelpThis));
    std::cout << IDC_CTR(help, kTextGridTail);
}

void helpConfig() {
    std::cout << IDC_CTR(help, kTextConfigIntro);
    optionRow(kConfigOptWidth, hp::kUseLoad, IDC_CTR(help, kDescLoad));
    optionRow(kConfigOptWidth, hp::kUseSaveConfig, IDC_CTR(help, kDescConfigSaveAs));
    optionRow(kConfigOptWidth, hp::kUseBundleAssets, IDC_CTR(help, kDescConfigBundle));
    std::cout << IDC_CTR(help, kTextConfigLayers);
    std::cout << IDC_CTR(help, kTextSecIo);
    optionRow(kConfigOptWidth, hp::kUseInput, IDC_CTR(help, kDescConfigInput));
    generateSourceSection(kConfigOptWidth, IDC_CTR(help, kLeadGenOverride));
    optionRow(kConfigOptWidth, hp::kUseOutput, IDC_CTR(help, kDescOutputConfigMerged));
    optionRow(kConfigOptWidth, hp::kUseOutputDir, IDC_CTR(help, kDescOutputDirConfigSeparate));
    annotationSection(kConfigOptWidth, IDC_CTR(help, kLeadAnnoAppend));
    std::cout << IDC_CTR(help, kTextSecCommonBlank);
    optionRow(kConfigOptWidth, hp::kUseHelpShort, IDC_CTR(help, kDescHelpThis));
    std::cout << IDC_CTR(help, kTextConfigTail);
}

// ── 分派：按子命令名选择帮助输出；未知命令回退为顶层帮助 ──────────────────────
void printCommandHelp(const std::string& cmd) {
    if (cmd == cmds::kExtract) { helpExtract(); return; }
    if (cmd == cmds::kErase) { helpErase(); return; }
    if (cmd == cmds::kGrid) { helpGrid(); return; }
    if (cmd == cmds::kConfig) { helpConfig(); return; }
    if (cmd == cmds::kNew) { helpNew(); return; }
    printTopHelp();
}

} // namespace idc::cli
