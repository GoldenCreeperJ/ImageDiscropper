// ============================================================================
// 文件：src/help.cpp
// 作用：CLI 帮助文本输出的全部实现——顶层帮助 / 版本 / 子命令分派，五个子命令的帮助
//       文本本体，跨命令复用的通用小节（标注段 / 图案矩阵 / 生成源段），以及各命令选项
//       列宽的编译期推导。对外只经 include/help.h 暴露 printTopHelp / printVersion /
//       printCommandHelp；其余皆为本 TU 内部件。
// 分块依据：帮助是纯文本输出，集中一处便于命令清单 / 术语 / 示例与规格保持一致（SPEC §2.2 术语、
//       帮助要求与版本格式见 Cli README）；命令名统一用实际可执行名 "idc"。文本本体（散文块 /
//       用法列 / 描述列）全在 cli_texts.h::help，本 TU 只决定输出顺序与列宽。
// 列宽（constexpr optWidth）：用法列内容不随语言变（optionRow 的 opt 恒为不可译契约串 hp::kUse*），
//       故其最长长度为编译期常量——从字符串真长推导，零运行期开销，加/减选项时列宽自动跟进。
// i18n 分工：用法列 help::kUse* = 选项名 + 占位符，属**不可译契约**（optionRow 的 opt 形参 const char*）；
//       描述列 / 小节 lead / 散文块走门面 IDC_CTR（返回 std::string，形参 const std::string&）。
// 排版约定：选项行 = 两空格缩进 + 定宽选项列 + 两空格 + 描述；选项按功能分组、组标题独占一行、组间空行；
//       需求标签统一为 [必填] / [可重复]，条件性必填写入描述括注。
// 测试约束：IT-13 断言 --version 形如 "idc <ver> (core <ver>)"；IT-14 断言顶层帮助含全部子命令名
//       （extract / erase / grid / new / config）——排版调整不得破坏这两条。
// 说明：版本号来自编译期宏 IDC_CLI_VERSION / IDC_CORE_VERSION（由 CMake 注入，与构建系统一致）。
// ============================================================================
#include "help.h"
#include "cli_error.h"
#include "exit_code.h"
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
constexpr int kGenSourceOptWidth = optWidth({hp::kUseSize, hp::kUseFit});   // 生成源主题页仅剩 --size / --fit 行（图案参数与矩阵指向 new 页）。
constexpr int kAnnoOptWidth = optWidth({hp::kUseAnnotation, hp::kUseAnnotations});
constexpr int kLayerOptWidth = optWidth({hp::kUseLayer, hp::kUseLayers});

// 顶层与 new：不引用共享小节，列宽即各自选项集合的 max（--save-config <file> = 20）。
constexpr int kTopModsWidth = optWidth({
    hp::kUseHelpShort, hp::kUseVersionShort, hp::kUseVerbose, hp::kUseStrict, hp::kUseLang});
constexpr int kTopJobWidth = optWidth({hp::kUseSaveConfig, hp::kUseAssetsDir, hp::kUseAnnotation, hp::kUseLayer, hp::kUseSize});
constexpr int kNewOptWidth = optWidth({
    hp::kUseSize, hp::kUseBg, hp::kUsePattern, hp::kUseOrientation, hp::kUseFg, hp::kUsePeriod,
    hp::kUseLineWidth, hp::kUseDotRadius, hp::kUseSeed, hp::kUseNoise, hp::kUseOutput,
    hp::kUseFormat, hp::kUseQuality, hp::kUsePadColor});

// 各命令帮助仅列自身选项（标注 / 图层 / 生成源三套通用选项见主题页，topic* 与 pointer*）。
constexpr int kExtractOptWidth = optWidth({
    hp::kUseInput, hp::kUseRect, hp::kUseHband, hp::kUseVband, hp::kUseOutput,
    hp::kUseFormat, hp::kUseQuality});
constexpr int kEraseOptWidth = optWidth({
    hp::kUseInput, hp::kUseRect, hp::kUseHband, hp::kUseVband, hp::kUseMerge, hp::kUseOutput,
    hp::kUseOutputDir, hp::kUseFormat, hp::kUseQuality, hp::kUseNaming});
constexpr int kGridOptWidth = optWidth({
    hp::kUseInput, hp::kUseGrid, hp::kUseGridX, hp::kUseGridY, hp::kUseDivide, hp::kUseDivideX, hp::kUseDivideY,
    hp::kUseWeightsX, hp::kUseWeightsY, hp::kUseKeep, hp::kUseRemove, hp::kUseSort, hp::kUseDecorate,
    hp::kUseOrder, hp::kUseMargin, hp::kUsePadColor, hp::kUseCompose, hp::kUseCanvas,
    hp::kUseMergeSort, hp::kUseMergeDecorate, hp::kUseOutput, hp::kUseOutputDir, hp::kUseFormat,
    hp::kUseQuality, hp::kUseNaming});
constexpr int kConfigOptWidth = optWidth({
    hp::kUseLoad, hp::kUseInput, hp::kUseOutput, hp::kUseOutputDir,
    hp::kUseLayer, hp::kUseLayers, hp::kUseTier, hp::kUsePolarity, hp::kUseRect,
    hp::kUseHband, hp::kUseVband, hp::kUseExportMode, hp::kUseExportLayout,
    hp::kUseFormat, hp::kUseQuality, hp::kUseNaming, hp::kUseCanvas, hp::kUseFit});

// 打印一行选项：两空格缩进 + 定宽选项列 + 两空格 + 描述。
// 描述以 [必填] / [可重复] 标签开头（无标签时直接写描述），所有行的标签在同一列起头，视觉对齐。
// opt 恒为 help::kUse*（不可译），desc 为门面现取出的译文。
void optionRow(const int width, const char* opt, const std::string& desc) {
    std::cout << "  " << std::left << std::setw(width) << opt << "  " << desc << '\n';
}

// ── 跨命令通用小节 ────────────────────────────────────
// 标注主题页（idc help annotations）：自包含的完整字段说明与示例，不引用任何工程文档
//（用户只需读本页即可写出一条合法标注）。
void topicAnnotations() {
    std::cout << IDC_CTR(help, kTextAnnoHead);
    optionRow(kAnnoOptWidth, hp::kUseAnnotation, IDC_CTR(help, kDescAnnoInline));
    optionRow(kAnnoOptWidth, hp::kUseAnnotations, IDC_CTR(help, kDescAnnoFile));
    std::cout << IDC_CTR(help, kTextAnnoFields);
    std::cout << '\n';   // 帮助页统一以空行结尾
}

// 命令帮助内的标注指针行（字段与语法见主题页）；lead 为命令差异提示（如 config 的追加语义）。
void annotationPointer(const std::string& lead = {}) {
    std::cout << IDC_CTR(help, kTextAnnoSeeTopic);
    if (!lead.empty()) std::cout << "  " << lead << '\n';
}

// 图层主题页（idc help layers）：与标注主题页同构的自包含字段说明（§7 layers 元素语法）。
void topicLayers() {
    std::cout << IDC_CTR(help, kTextLayerHead);
    optionRow(kLayerOptWidth, hp::kUseLayer, IDC_CTR(help, kDescLayerInline));
    optionRow(kLayerOptWidth, hp::kUseLayers, IDC_CTR(help, kDescLayerFile));
    std::cout << IDC_CTR(help, kTextLayerFields);
    std::cout << IDC_CTR(help, kTextLayerTail);
    std::cout << '\n';   // 帮助页统一以空行结尾
}

// 命令帮助内的图层指针行；lead 为命令差异提示（config 追加 / 三命令叠加）。
void layerPointer(const std::string& lead = {}) {
    std::cout << IDC_CTR(help, kTextLayerSeeTopic);
    if (!lead.empty()) std::cout << "  " << lead << '\n';
}

// 图案 × 参数适用矩阵（new 页独占，生成源页经指针跳转）：逐图案说明哪些参数真正生效、哪些被忽略——
// 与 Core generate::makePattern 实现严格一致（period 下限 2、lineWidth/dotRadius 下限 1，由 Core 钳位）。
void patternMatrix() {
    std::cout << IDC_CTR(help, kTextPatternMatrix);
}

// 图像生成源选项（extract / erase / grid / config 四层命令通用）：说明 --input 可用 --size 程序生成
// 画布替代（二者互斥、择一），并列出与 new 命令同一套生成选项。lead 为命令差异提示，空串即无。
// 生成源主题页（idc help generate）：--size 的「替代 --input」语义与互斥约束 + 示例 / 约束；
// 图案参数与适用矩阵与 new 命令相同，指针过去（去重，不复制整块矩阵）。
void topicGenerate() {
    std::cout << IDC_CTR(help, kTextGenTopicHead);
    optionRow(kGenSourceOptWidth, hp::kUseSize, IDC_CTR(help, kDescSizeGen));
    optionRow(kGenSourceOptWidth, hp::kUseFit, IDC_CTR(help, kDescFit));
    std::cout << IDC_CTR(help, kTextGenParamsPtr);
    std::cout << IDC_CTR(help, kTextGenTail);
    std::cout << '\n';   // 帮助页统一以空行结尾
}

// 命令帮助内的生成源指针行；lead 为命令差异提示（如 config 的"命令行生成覆盖本次输入来源"）。
void generatePointer(const std::string& lead = {}) {
    std::cout << IDC_CTR(help, kTextGenSeeTopic);
    if (!lead.empty()) std::cout << "  " << lead << '\n';
}

// ── 顶层帮助与版本 ──────────────────────────────────────────────────────────
void printTopHelp() {
    std::cout << IDC_CTR(help, kTextTopIntro);
    std::cout << IDC_CTR(help, kTextTopModsHead);
    optionRow(kTopModsWidth, hp::kUseHelpShort, IDC_CTR(help, kDescHelp));
    optionRow(kTopModsWidth, hp::kUseVersionShort, IDC_CTR(help, kDescVersion));
    optionRow(kTopModsWidth, hp::kUseVerbose, IDC_CTR(help, kDescVerbose));
    optionRow(kTopModsWidth, hp::kUseStrict, IDC_CTR(help, kDescTopStrict));
    optionRow(kTopModsWidth, hp::kUseLang, IDC_CTR(help, kDescLang));
    std::cout << IDC_CTR(help, kTextTopJobHead);
    optionRow(kTopJobWidth, hp::kUseSaveConfig, IDC_CTR(help, kDescTopSaveConfig));
    optionRow(kTopJobWidth, hp::kUseAssetsDir, IDC_CTR(help, kDescTopAssetsDir));
    optionRow(kTopJobWidth, hp::kUseAnnotation, IDC_CTR(help, kDescTopAnnoPtr));
    optionRow(kTopJobWidth, hp::kUseLayer, IDC_CTR(help, kDescTopLayerPtr));
    optionRow(kTopJobWidth, hp::kUseSize, IDC_CTR(help, kDescTopGenPtr));
    std::cout << IDC_CTR(help, kTextTopTail);
    std::cout << '\n';   // 帮助页统一以空行结尾
}

// 版本信息：形如 "idc <version> (core <core-version>)"。
void printVersion() {
    std::cout << "idc " << IDC_CLI_VERSION << " (core " << IDC_CORE_VERSION << ")\n";
}

// 已知子命令名判定（--help 目标分派用）。
bool isKnownCommand(const std::string& cmd) {
    return cmd == cmds::kExtract || cmd == cmds::kErase || cmd == cmds::kGrid ||
           cmd == cmds::kConfig || cmd == cmds::kNew;
}

// 已知主题名判定（--help 目标分派用）。
bool isKnownTopic(const std::string& topic) {
    return topic == cmds::kTopicAnnotations || topic == cmds::kTopicLayers || topic == cmds::kTopicGenerate;
}

// 主题帮助页分派（idc --help <topic>）：annotations / layers / generate；未知主题报参数错误（1）。
int printTopicHelp(const std::string& topic) {
    if (topic == cmds::kTopicAnnotations) { topicAnnotations(); return toInt(ExitCode::Ok); }
    if (topic == cmds::kTopicLayers) { topicLayers(); return toInt(ExitCode::Ok); }
    if (topic == cmds::kTopicGenerate) { topicGenerate(); return toInt(ExitCode::Ok); }
    reportError(makeError(ExitCode::ArgError, IDC_CTR(msg, kErrUnknownHelpTarget) + topic + "'",
                          "", IDC_CTR(msg, kHintKnownHelpTargets)));
    return toInt(ExitCode::ArgError);
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
    annotationPointer(IDC_CTR(help, kLeadAnnoNew));
    layerPointer(IDC_CTR(help, kLeadLayerNew));
    std::cout << IDC_CTR(help, kTextSecCommon);
    std::cout << IDC_CTR(help, kTextCommonOptsPtr);
    std::cout << IDC_CTR(help, kTextSeeThisHelp) << cmds::kNew << '\n';
    std::cout << IDC_CTR(help, kTextNewTail);
    std::cout << '\n';   // 帮助页统一以空行结尾
}

void helpExtract() {
    std::cout << IDC_CTR(help, kTextExtractIntro);
    optionRow(kExtractOptWidth, hp::kUseInput, IDC_CTR(help, kDescInput));
    generatePointer();
    std::cout << IDC_CTR(help, kTextSecCutThree);
    optionRow(kExtractOptWidth, hp::kUseRect, IDC_CTR(help, kDescRectKeep));
    optionRow(kExtractOptWidth, hp::kUseHband, IDC_CTR(help, kDescHbandKeep));
    optionRow(kExtractOptWidth, hp::kUseVband, IDC_CTR(help, kDescVbandKeep));
    std::cout << IDC_CTR(help, kTextSecOutput);
    optionRow(kExtractOptWidth, hp::kUseOutput, IDC_CTR(help, kDescOutputRequired));
    optionRow(kExtractOptWidth, hp::kUseFormat, IDC_CTR(help, kDescFormatInfer));
    optionRow(kExtractOptWidth, hp::kUseQuality, IDC_CTR(help, kDescQuality));
    annotationPointer();
    layerPointer(IDC_CTR(help, kLeadLayerOverlay));
    std::cout << IDC_CTR(help, kTextCommonOptsPtr);
    std::cout << IDC_CTR(help, kTextSecCommonBlank);
    std::cout << IDC_CTR(help, kTextSeeThisHelp) << cmds::kExtract << '\n';
    std::cout << IDC_CTR(help, kTextExtractTail);
    std::cout << '\n';   // 帮助页统一以空行结尾
}

void helpErase() {
    std::cout << IDC_CTR(help, kTextEraseIntro);
    optionRow(kEraseOptWidth, hp::kUseInput, IDC_CTR(help, kDescInput));
    generatePointer();
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
    optionRow(kEraseOptWidth, hp::kUseNaming, IDC_CTR(help, kDescNaming));
    annotationPointer();
    layerPointer(IDC_CTR(help, kLeadLayerOverlay));
    std::cout << IDC_CTR(help, kTextCommonOptsPtr);
    std::cout << IDC_CTR(help, kTextSecCommonBlank);
    std::cout << IDC_CTR(help, kTextSeeThisHelp) << cmds::kErase << '\n';
    std::cout << IDC_CTR(help, kTextEraseTail);
    std::cout << '\n';   // 帮助页统一以空行结尾
}

void helpGrid() {
    std::cout << IDC_CTR(help, kTextGridIntro);
    optionRow(kGridOptWidth, hp::kUseInput, IDC_CTR(help, kDescInput));
    generatePointer();
    std::cout << IDC_CTR(help, kTextSecGrid);
    optionRow(kGridOptWidth, hp::kUseGrid, IDC_CTR(help, kDescGrid));
    optionRow(kGridOptWidth, hp::kUseGridX, IDC_CTR(help, kDescGridX));
    optionRow(kGridOptWidth, hp::kUseGridY, IDC_CTR(help, kDescGridY));
    optionRow(kGridOptWidth, hp::kUseDivide, IDC_CTR(help, kDescDivide));
    optionRow(kGridOptWidth, hp::kUseDivideX, IDC_CTR(help, kDescDivideX));
    optionRow(kGridOptWidth, hp::kUseDivideY, IDC_CTR(help, kDescDivideY));
    optionRow(kGridOptWidth, hp::kUseWeightsX, IDC_CTR(help, kDescWeightsX));
    optionRow(kGridOptWidth, hp::kUseWeightsY, IDC_CTR(help, kDescWeightsY));
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
    optionRow(kGridOptWidth, hp::kUseNaming, IDC_CTR(help, kDescNaming));
    annotationPointer();
    layerPointer(IDC_CTR(help, kLeadLayerOverlay));
    std::cout << IDC_CTR(help, kTextCommonOptsPtr);
    std::cout << IDC_CTR(help, kTextSecCommonBlank);
    std::cout << IDC_CTR(help, kTextSeeThisHelp) << cmds::kGrid << '\n';
    std::cout << IDC_CTR(help, kTextGridTail);
    std::cout << '\n';   // 帮助页统一以空行结尾
}

void helpConfig() {
    std::cout << IDC_CTR(help, kTextConfigIntro);
    optionRow(kConfigOptWidth, hp::kUseLoad, IDC_CTR(help, kDescLoad));
    std::cout << IDC_CTR(help, kTextConfigLayers);
    std::cout << IDC_CTR(help, kTextSecIo);
    optionRow(kConfigOptWidth, hp::kUseInput, IDC_CTR(help, kDescConfigInput));
    generatePointer(IDC_CTR(help, kLeadGenOverride));
    optionRow(kConfigOptWidth, hp::kUseOutput, IDC_CTR(help, kDescOutputConfigMerged));
    optionRow(kConfigOptWidth, hp::kUseOutputDir, IDC_CTR(help, kDescOutputDirConfigSeparate));
    annotationPointer(IDC_CTR(help, kLeadAnnoAppend));
    layerPointer(IDC_CTR(help, kLeadLayerAppend));
    std::cout << IDC_CTR(help, kTextSecOverride);
    optionRow(kConfigOptWidth, hp::kUseTier, IDC_CTR(help, kDescTierOverride));
    optionRow(kConfigOptWidth, hp::kUsePolarity, IDC_CTR(help, kDescPolarityOverride));
    optionRow(kConfigOptWidth, hp::kUseRect, IDC_CTR(help, kDescCutRectOverride));
    optionRow(kConfigOptWidth, hp::kUseHband, IDC_CTR(help, kDescHbandOverride));
    optionRow(kConfigOptWidth, hp::kUseVband, IDC_CTR(help, kDescVbandOverride));
    optionRow(kConfigOptWidth, hp::kUseExportMode, IDC_CTR(help, kDescExportModeOverride));
    optionRow(kConfigOptWidth, hp::kUseExportLayout, IDC_CTR(help, kDescExportLayoutOverride));
    optionRow(kConfigOptWidth, hp::kUseFormat, IDC_CTR(help, kDescExportFormatOverride));
    optionRow(kConfigOptWidth, hp::kUseQuality, IDC_CTR(help, kDescQuality));
    optionRow(kConfigOptWidth, hp::kUseNaming, IDC_CTR(help, kDescNaming));
    optionRow(kConfigOptWidth, hp::kUseCanvas, IDC_CTR(help, kDescCanvasOverride));
    optionRow(kConfigOptWidth, hp::kUseFit, IDC_CTR(help, kDescFitOverride));
    std::cout << IDC_CTR(help, kTextSecCommonBlank);
    std::cout << IDC_CTR(help, kTextSeeThisHelp) << cmds::kConfig << '\n';
    std::cout << IDC_CTR(help, kTextConfigTail);
    std::cout << '\n';   // 帮助页统一以空行结尾
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
