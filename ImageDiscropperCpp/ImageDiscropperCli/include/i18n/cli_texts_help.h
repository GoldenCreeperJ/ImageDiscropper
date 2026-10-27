// ============================================================================
// 文件：include/i18n/cli_texts_help.h（CLI 常量层 · 字符串 · 后半：help）
// 作用：收录 **help 组**（--help 输出的散文层）：
//       · kUse*  —— optionRow 的「用法列」（选项名 + 占位符），前半必须与 texts::opt 逐字一致，属脚本契约**不译**；
//       · kDesc* / kLead* —— 「描述列」与小节 lead（纯面向人，**可译**）；
//       · kText* —— raw string（R"IDCH(...)"）散文块，逐字保留（换行 / 缩进 / 空行都是输出的一部分）。
//       opt / cmd / env / msg 见同目录 cli_texts_base.h；两半经聚合转发头 cli_texts.h 统一 include。
// 架构定位：CLI 顶层 i18n 基建；help 散文长而独立，单列一文件以免 cli_texts 基座过长。
// 目录与对账：help 的英文表住 src/i18n/lang_en_help.cpp（与 msg 分两 TU 装配，installCatalogs 内先后登记）；
//       kUse* 不进表（脚本契约）。tests/test_i18n_catalog.cpp 逐文件对账本层全部可译组。
// ============================================================================
#pragma once


// ===========================================================================
// help：--help 输出散文（可译）：落点见 src/help.cpp。
//   三组同在一个命名空间里，但形态不同：
//     · kUse*  —— optionRow 的「用法列」（选项名 + 占位符）；前半必须与 texts::opt 逐字一致，
//       改名时两组同改，后半占位符不译；
//     · kDesc* / kLead* —— optionRow 的「描述列」与小节 lead（纯面向人，可译）；
//     · kText* —— raw string 散文块，逐字保留（含换行、缩进与空行），措辞与 --help 输出一一对应。
//   同值且语义相同的只留一条，行尾标 ×N（×N 指帮助里的出现次数）。
// ===========================================================================
namespace idc::cli::texts::help {

// 用法列（选项名 + 占位符）。
// 注：下面标「同 opt::」的四条无占位符，帮助里显示的就是解析器正在匹配的那个串；
//     char[] 之间不能初始化，故只能各写一份，**改 option 名必须两处同步**。
inline constexpr char kUseHelpShort[] = "-h, --help";                    // ×1（顶层运行修饰；命令页用 kTextSeeThisHelp——命令后 -h 被拒）。
inline constexpr char kUseVersionShort[] = "-V, --version";
inline constexpr char kUseVerbose[] = "-v, --verbose";                      // 同 opt::kVerbose / kShortVerbose。
inline constexpr char kUseSaveConfig[] = "--save-config <file>";          // ×1（顶层作业组）。
inline constexpr char kUseAssetsDir[] = "--assets-dir[=<dir>]";          // 同 opt::kAssetsDir。
inline constexpr char kUseStrict[] = "--strict";                            // 同 opt::kStrict。
inline constexpr char kUseLang[] = "--lang <code>";                          // 同 opt::kLang 前缀（占位符不译）。
inline constexpr char kUseInput[] = "--input <file>";                    // ×4（extract/erase/grid/config）。
inline constexpr char kUseSize[] = "--size WxH";
inline constexpr char kUseFit[] = "--fit <mode>";                  // 位图→画布适应（--input 与 --size 同给时）。
inline constexpr char kUseRect[] = "--rect x1,y1,x2,y2";                  // ×2。
inline constexpr char kUseHband[] = "--hband y1,y2";                      // ×2。
inline constexpr char kUseVband[] = "--vband x1,x2";                      // ×2。
inline constexpr char kUseGrid[] = "--grid x0,y0,cw,ch";
inline constexpr char kUseGridX[] = "--grid-x x0,cw";
inline constexpr char kUseGridY[] = "--grid-y y0,ch";
inline constexpr char kUseDivide[] = "--divide ColxRow";
inline constexpr char kUseDivideX[] = "--divide-x <n>";
inline constexpr char kUseDivideY[] = "--divide-y <n>";
inline constexpr char kUseWeightsX[] = "--weights-x w,w,...";
inline constexpr char kUseWeightsY[] = "--weights-y w,w,...";
inline constexpr char kUseKeep[] = "--keep r,c";
inline constexpr char kUseRemove[] = "--remove r,c";
inline constexpr char kUseOrder[] = "--order r,c;r,c";
inline constexpr char kUseSort[] = "--sort <strategy>";
inline constexpr char kUseDecorate[] = "--decorate <method>";
inline constexpr char kUseMargin[] = "--margin <policy>";
inline constexpr char kUseOutput[] = "--output <file>";                  // ×4。
inline constexpr char kUseOutputDir[] = "--output-dir <dir>";            // ×3。
inline constexpr char kUseFormat[] = "--format <fmt>";                  // ×4。
inline constexpr char kUseQuality[] = "--quality <n>";                  // ×4。
inline constexpr char kUseNaming[] = "--naming <tpl>";                  // ×2。
inline constexpr char kUseMerge[] = "--merge <mode>";
inline constexpr char kUseCompose[] = "--compose";                            // 同 opt::kCompose。
inline constexpr char kUseCanvas[] = "--canvas ColxRow";
inline constexpr char kUseMergeSort[] = "--merge-sort <strategy>";
inline constexpr char kUseMergeDecorate[] = "--merge-decorate <method>";
inline constexpr char kUsePadColor[] = "--pad-color <hex>";              // ×2。
inline constexpr char kUsePattern[] = "--pattern <kind>";                // ×1（new 页；生成源页指针见 kTextGenParamsPtr）。
inline constexpr char kUseOrientation[] = "--orientation <dir>";          // ×1（new 页）。
inline constexpr char kUseBg[] = "--bg <hex>";                          // ×1（new 页）。
inline constexpr char kUseFg[] = "--fg <hex>";                          // ×1（new 页）。
inline constexpr char kUsePeriod[] = "--period <n>";                    // ×1（new 页）。
inline constexpr char kUseLineWidth[] = "--line-width <n>";              // ×1（new 页）。
inline constexpr char kUseDotRadius[] = "--dot-radius <n>";              // ×1（new 页）。
inline constexpr char kUseSeed[] = "--seed <n>";                        // ×1（new 页）。
inline constexpr char kUseNoise[] = "--noise <mode>";                    // ×1（new 页）。
inline constexpr char kUseLoad[] = "--load <file>";
inline constexpr char kUseTier[] = "--tier <tier>";                // config 字段覆盖族（帮助展示串）。
inline constexpr char kUsePolarity[] = "--polarity <mode>";
inline constexpr char kUseExportMode[] = "--export-mode <mode>";
inline constexpr char kUseExportLayout[] = "--export-layout <lt>";
inline constexpr char kUseAnnotation[] = "--annotation '<json>'";        // ×4（通用标注小节）。
inline constexpr char kUseAnnotations[] = "--annotations <file>";        // ×4。
inline constexpr char kUseLayer[] = "--layer '<json>'";                  // ×4（通用图层小节）。
inline constexpr char kUseLayers[] = "--layers <file>";                  // ×4。

// 描述列与通用小节 lead（纯面向人）。
inline constexpr char kDescHelp[] = "显示帮助";
inline constexpr char kDescVersion[] = "显示版本";
inline constexpr char kTextSeeThisHelp[] = "  查看本帮助：idc --help ";   // ×5（命令页通用小节；命令名由 help.cpp 按页拼接；命令后 -h/--help 被拒）。
inline constexpr char kDescVerbose[] = "输出进度诊断日志到 stderr（默认静默；结果行恒写 stdout）";
inline constexpr char kDescLang[] = "指定帮助与报错文本的语言（zh-CN / en）；未指定时依次取 IDC_LANG、LC_ALL、LANG";
inline constexpr char kDescTopSaveConfig[] = "把当前参数序列化为 JSON 配置文件（dry-run，不执行作业）";
inline constexpr char kDescTopAnnoPtr[] = "[可重复] 命令行标注（字段与语法：idc --help annotations）";
inline constexpr char kDescTopLayerPtr[] = "[可重复] 命令行图层（字段与语法：idc --help layers）";
inline constexpr char kDescTopGenPtr[] = "程序化生成源（适用矩阵：idc --help generate）";
inline constexpr char kDescTopAssetsDir[] = "资产目录三态：不写＝只写 JSON；flag＝与 --save-config 同用时打包进 <配置名>.assets/；=path＝自定义目录（加载插查找链最前、打包输出到该目录，路径须用 '=' 形式）";
inline constexpr char kDescTopStrict[] = "严格模式：把「图层资源缺失降级占位」「尺寸错配静默裁切」与「零产出」三处默认降级统一升级为报错退出（退出码 3/2，供无人值守脚本据非零退出码中断）";
inline constexpr char kDescInput[] = "输入图像路径（与 --size 同给时：画布尺寸由 --size 定、位图居中铺入）";            // ×3。
inline constexpr char kDescRectKeep[] = "矩形区域（保留中心矩形）";
inline constexpr char kDescHbandKeep[] = "水平带（保留 y∈[y1,y2) 的整条横带）";
inline constexpr char kDescVbandKeep[] = "垂直带（保留 x∈[x1,x2) 的整条竖带）";
inline constexpr char kDescRectErase[] = "[可重复] 一次 = 单矩形十字切割；多次 = 多矩形并集剔除";
inline constexpr char kDescHbandErase[] = "删除 y∈[y1,y2) 的整条横带";
inline constexpr char kDescVbandErase[] = "删除 x∈[x1,x2) 的整条竖带";
inline constexpr char kDescOutputRequired[] = "输出图像路径（与 --save-config 至少给一个；dry-run 时可不给）";
inline constexpr char kDescFormatInfer[] = "输出格式 png/jpeg/webp/bmp（默认由 --output 扩展名推断）";   // ×2。
inline constexpr char kDescFormatPngDefault[] = "输出格式 png/jpeg/webp/bmp（默认 png；合并时优先按 --output 扩展名）";
inline constexpr char kDescFormatPlain[] = "输出格式 png/jpeg/webp/bmp";
inline constexpr char kDescQuality[] = "有损质量 0~100（默认 90；仅 jpeg/webp 生效，png/bmp 忽略）";       // ×4。
inline constexpr char kDescNaming[] = "分离导出文件名模板：{name}＝输出文件夹名（取不到回退 region）、{index}＝序号、{index:03d}＝零填充序号、{row}/{col}＝源网格行列号（非网格来源按 0）；默认 {name}_{index:03d}；仅分离导出生效（合并单图不用），须非空且不含路径分隔符";
inline constexpr char kDescMerge[] = "仅 collapse（坍缩）；缺省为分离导出（rearrange 为 grid 专属）";
inline constexpr char kDescOutputMergeErase[] = "合并输出路径（--merge collapse 时用；与 --save-config 至少给一个）";
inline constexpr char kDescOutputMergeCompose[] = "合并输出路径（--compose 时用；与 --save-config 至少给一个）";
inline constexpr char kDescOutputDirErase[] = "分离输出目录（缺省分离导出时用；不存在会自动创建；与 --save-config 至少给一个）";
inline constexpr char kDescOutputDirGrid[] = "分离输出目录（缺省分离导出时用；与 --save-config 至少给一个）";
inline constexpr char kDescOutputConfigMerged[] = "合并输出路径（配置 export.mode=merged 时用）";
inline constexpr char kDescOutputDirConfigSeparate[] = "分离输出目录（配置 export.mode=separate 时用）";
inline constexpr char kDescAnnoInline[] = "[可重复] 内联一条标注对象，或一个标注对象数组";
inline constexpr char kDescAnnoFile[] = "[可重复] 从「根为标注数组」的 JSON 文件读取";
inline constexpr char kDescLayerInline[] = "[可重复] 内联一条图层（v2 JSON 对象 / 数组），或两种等价简写：平铺 {file=a.png,imageOpacity=200,backdrop={\"kind\":\"solid\"}} / 块形 image={...} backdrop={...}";
inline constexpr char kDescLayerFile[] = "[可重复] 从「根为图层数组」的 JSON 文件读取";
inline constexpr char kDescGrid[] = "整幅周期：基准点 + 单元尺寸（基准点可为负，cw/ch 换算后必须为正；各值可写百分比，如 \"50%\"）";
inline constexpr char kDescGridX[] = "横向周期：基准点 x0 + 单元宽 cw（百分比按图宽换算）；纵向未定义时＝整幅 1 份";
inline constexpr char kDescGridY[] = "纵向周期：基准点 y0 + 单元高 ch（百分比按图高换算）；横向未定义时＝整幅 1 份";
inline constexpr char kDescDivide[] = "两轴分割数 ColxRow（如 3x4＝3 竖 × 4 横）：基准点恒 0，尽量均分，末份吸收取整误差";
inline constexpr char kDescDivideX[] = "横向分割数（基准点恒 0，尽量均分）；纵向未定义时＝整幅 1 份";
inline constexpr char kDescDivideY[] = "纵向分割数（基准点恒 0，尽量均分）；横向未定义时＝整幅 1 份";
inline constexpr char kDescWeightsX[] = "横向比例（正数或百分比串，如 1,2,1 或 25%,50%,25%）：只取相对比例，末份吸收取整误差";
inline constexpr char kDescWeightsY[] = "纵向比例（正数或百分比串）：只取相对比例，末份吸收取整误差";
inline constexpr char kDescKeepCell[] = "[可重复] 保留指定单元：一次给一个 r,c，或分号连写 r,c;r,c（行、列从 0 起）";
inline constexpr char kDescRemoveCell[] = "[可重复] 删除指定单元：一次给一个 r,c，或分号连写 r,c;r,c（行、列从 0 起）";
inline constexpr char kDescSort[] = "排序策略：row-major 横优先（逐行编号、行内从左到右，默认）/ column-major 竖优先（逐列编号、列内从上到下）/ custom 自定义（点选顺序即序号，须配 --order）";
inline constexpr char kDescDecorate[] = "排序修饰（--sort custom 下不可用）：none 不修饰（默认）/ reverse 整体逆序 / snake 蛇形（隔行/隔列反向）/ reverse-snake 与 snake-reverse 等价（固定先蛇形后逆序）";
inline constexpr char kDescOrderCustom[] = "自定义序列（--sort custom 时必填）：同时定义被保留单元与其输出顺序（任意排列），与 --keep/--remove/--decorate 互斥——--keep/--remove 只定保留集、顺序由 --sort 决定，本选项两者都定";
inline constexpr char kDescMargin[] = "余量策略（网格超出图像边界产生的残缺单元）：discard 丢弃（仅保留完整单元，默认）/ keep-partial 保留并裁剪到图像内 / pad 补白保留（保持完整单元尺寸，越界部分以 --pad-color 填充）";
inline constexpr char kDescCompose[] = "合并为单图（重排）；缺省为分离导出";
inline constexpr char kDescCanvas[] = "重排画布的单元列 × 行，如 2x2（--compose 时必填）";
inline constexpr char kDescMergeSort[] = "重排填充顺序 row-major（默认）/ column-major（与 --sort 选择序正交；不支持 custom）";
inline constexpr char kDescMergeDecorate[] = "重排填充修饰 none（默认）/ reverse / snake / reverse-snake / snake-reverse";
inline constexpr char kDescPadColorDefault[] = "补白填充色 #RRGGBB / #AARRGGBB（--margin pad 的越界部分与重排合并画布空位的填充，默认透明）";
inline constexpr char kDescPadColorFlatten[] = "有损格式（jpeg/bmp）压平透明像素所用背景色 #RRGGBB/#AARRGGBB（默认透明→黑底，建议透明画布导出 jpeg 时显式指定）";
inline constexpr char kDescFit[] = "位图→画布适应：crop 裁剪铺满 / stretch 拉伸 / native 原生（与 --input 和 --size 同给时生效，缺省 native 居中）";
inline constexpr char kDescFitOverride[] = "位图→画布适应覆盖 crop / stretch / native（--input 与 --size 同给时生效；配置带落位时覆盖其 fit）";
inline constexpr char kDescSizeGen[] = "生成画布尺寸（像素正整数；单边 ≤65535 且 宽×高 ≤64,000,000≈256MB；与 --input 同给时定画布尺寸）";
inline constexpr char kDescSizeGenRequired[] = "[必填] 生成画布尺寸（像素正整数；单边 ≤65535 且 宽×高 ≤64,000,000≈256MB）";
inline constexpr char kDescPatternKinds[] = "solid(默认)/linear-gradient/radial-gradient/checkerboard/stripes/grid-lines/dot-grid/concentric/radial-rays/hue-gradient/noise";   // ×1（new 页）。
inline constexpr char kDescOrientationDirs[] = "horizontal(默认)/vertical/diagonal-down/diagonal-up——线性渐变轴 / 条纹方向(含斜纹) / 同心环形状 / 色相扫描轴";   // ×1（new 页）。
inline constexpr char kDescBg[] = "底色 #RRGGBB/#AARRGGBB（alpha 生效，默认不透明白 #FFFFFFFF）";   // ×1（new 页）。
inline constexpr char kDescFg[] = "前景/图案色 #RRGGBB/#AARRGGBB（默认黑 #FF000000）";   // ×1（new 页）。
inline constexpr char kDescPeriod[] = "周期 / 格子边长 / 环间距 / 色相波长像素（正整数，默认 32）";   // ×1（new 页）。
inline constexpr char kDescLineWidth[] = "网格线 / 同心环（圆环·方环）线宽像素（正整数，默认 1）";   // ×1（new 页）。
inline constexpr char kDescDotRadius[] = "点阵点半径像素（正整数，默认 2）";   // ×1（new 页）。
inline constexpr char kDescSeed[] = "噪声随机种子（非负整数，默认 0；仅 --pattern noise 生效，同种子恒同图）";   // ×1（new 页）。
inline constexpr char kDescNoise[] = "噪声配色模式 full-color(默认)/grayscale/two-color（仅 --pattern noise 生效）";   // ×1（new 页）。
inline constexpr char kDescNewOutput[] = "生成图像的落盘路径（与 --save-config 至少给一个）";
inline constexpr char kDescLoad[] = "[必填] 从 JSON 配置文件读取全部参数";
inline constexpr char kDescTierOverride[] = "层阶覆盖 extract / erase / grid";
inline constexpr char kDescPolarityOverride[] = "极性覆盖 keep / remove";
inline constexpr char kDescCutRectOverride[] = "切割矩形覆盖（整数像素或百分比；与 --hband / --vband 三选一）";
inline constexpr char kDescHbandOverride[] = "横带覆盖 y1,y2（执行时按图宽转全幅带；不可与 --save-config 同用）";
inline constexpr char kDescVbandOverride[] = "竖带覆盖 x1,x2（同上）";
inline constexpr char kDescExportModeOverride[] = "导出模式覆盖 merged / separate";
inline constexpr char kDescExportLayoutOverride[] = "合并布局覆盖 collapse / rearrange";
inline constexpr char kDescExportFormatOverride[] = "输出格式覆盖 png/jpeg/webp/bmp";
inline constexpr char kDescCanvasOverride[] = "重排画布覆盖 列x行（--export-layout rearrange 时用）";
inline constexpr char kDescConfigInput[] = "底图图像路径（与 --size 同给时：画布尺寸由 --size 定、位图按配置落位或居中铺入；与配置 layers 段可组合，layers 叠加于此底图之上；配置含 generate 段时亦可均省略）";
inline constexpr char kLeadGenOverride[] = "命令行生成选项可与 --input 同给（画布尺寸由 --size 定、位图按配置落位或居中铺入）；若既无 --input/--size、配置又含 generate 段，则据配置合成画布。";
inline constexpr char kLeadAnnoAppend[] = "命令行标注追加在 --load 配置自带的 annotations 段之后；加 --replace-annotations 则替换（无命令行标注即清空）。";
inline constexpr char kLeadLayerAppend[] = "命令行图层追加在 --load 配置自带的 layers 段之后（zOrder 未给则自增续尾、叠在最上）；加 --replace-layers 则替换（无命令行图层即移除该段）。";
inline constexpr char kLeadLayerOverlay[] = "命令行图层作为叠加层合成在输入之上（与 --annotation 同构；file 资产按当前工作目录解析）。";
inline constexpr char kLeadAnnoNew[] = "坐标基准＝生成画布尺寸；烧入在图层合成之后（标注恒在最上）。";
inline constexpr char kLeadLayerNew[] = "命令行图层作为叠加层合成在生成画布之上（与 --annotation 同构；file 资产按当前工作目录解析）。";

// 散文块（逐字搬移：行首缩进、空行与末尾换行都是输出的一部分，改动会直接反映到 --help）。

// 顶层帮助（src/help.cpp::printTopHelp）：选项行之前的导语与选项行之后的退出码 / 示例 / 尾巴。
inline constexpr char kTextTopIntro[] = R"IDCH(idc — 图像区域提取 / 反向剔除 / 网格分割工具

用法：
  idc <command> [options]
  idc --version
  idc --help [<subcommand> | <topic>]   命令帮助 / 主题帮助

子命令：
  extract   标准提取：保留矩形 / 水平带 / 垂直带，输出单图
  erase     反向剔除：删除十字带 / 横带 / 竖带 / 多矩形并集，分离或合并导出
  grid      网格分割：按基准点与单元尺寸切网格，选择单元后分离或重排合并
  new       新建画布：按尺寸 / 底色(alpha) / 程序化图案生成一张画布并落盘
  config    从 JSON 配置文件执行一次完整作业（含可选标注 / 多层画布）

)IDCH";
inline constexpr char kTextTopModsHead[] = R"IDCH(
运行修饰：
)IDCH";
inline constexpr char kTextTopJobHead[] = R"IDCH(
作业级通用选项：
)IDCH";
inline constexpr char kTextTopTail[] = R"IDCH(
退出码：
  0 成功 | 1 参数错误 | 2 运行时错误 | 3 输入文件错误 | 4 输出失败 | 5 内部错误

示例：
  # 保留矩形（中心单元）
  idc extract --input photo.jpg --rect 100,100,300,250 --output out.png
  # 十字切割，坍缩合并为单图
  idc erase --input photo.jpg --rect 100,100,300,250 --merge collapse --output out.png
  # 十字切割，分离导出四角到文件夹
  idc erase --input photo.jpg --rect 100,100,300,250 --output-dir ./out/ --format png
  # 网格保留四角并重排合并到 2x2 画布
  idc grid --input photo.jpg --grid 100,100,200,150 --keep 0,0 --keep 0,2 --keep 2,0 --keep 2,2 --compose --canvas 2x2 --output result.png
  # 标注烧入：extract 同时烧入一条红色描边矩形
  idc extract --input photo.jpg --rect 100,100,300,250 --annotation '{"type":"rectangle","x1":10,"y1":10,"x2":80,"y2":50,"color":"#FFFF0000","strokeWidth":3}' --output out.png
  # 新建：生成 1024x768 棋盘格（白底 / 半透明蓝纹）写盘
  idc new --size 1024x768 --pattern checkerboard --bg "#FFFFFFFF" --fg "#800000FF" --period 64 --output board.png
  # 生成源切割：用程序生成的画布直接作为输入（--size 替代 --input）
  idc extract --size 200x200 --pattern radial-gradient --bg "#FF000000" --fg "#FFFFFFFF" --rect 50,50,150,150 --output grad.png
)IDCH";

// 通用小节（src/help.cpp 内的 annotationSection / layerSection / patternMatrix / generateSourceSection）：标注段 / 图层段 / 图案矩阵 / 生成源段。
inline constexpr char kTextAnnoSeeTopic[] = R"IDCH(
命令行标注：--annotation / --annotations（可重复）；字段与语法见 idc --help annotations
)IDCH";
inline constexpr char kTextAnnoHead[] = R"IDCH(
标注（可选，切割前烧入工作图；坐标基准＝预处理产出后的工作图尺寸；几何坐标可写百分比串，如 "50%"，横向按工作图宽、纵向按高换算）：
)IDCH";
inline constexpr char kTextLayerSeeTopic[] = R"IDCH(
命令行图层：--layer / --layers（可重复）；字段与语法见 idc --help layers
)IDCH";
inline constexpr char kTextLayerHead[] = R"IDCH(
图层（可选：作为叠加层展平进引擎，与标注同构；transform 的 x/y/width/height 可写百分比串，基准＝画布尺寸；config 命令追加在 --load 配置的 layers 段之后）：
)IDCH";
inline constexpr char kTextLayerFields[] = R"IDCH(  字段按元素 / 落位 / 层序分派（缺省值见括注；装配序＝图片源处理（适应→元素预处理）→区域定尺寸→图案元素→图片落位合成→区域缩放→节点外观）：
    image 图片元素（可选；缺省＝纯图案层）：
      file：相对资产名（如 "sub/a.png"）——按「自定义目录（--assets-dir）→ <配置名>.assets/ → 配置所在目录」查找；
            缺失降级为空白占位（合成时跳过，不报错）
      fit 源图→图层框适应：crop 裁剪铺满（默认）/ stretch 拉伸 / native 原生
      boxWidth / boxHeight 图层框尺寸（≤0＝无图层框，内层落位退化——源图直接按 transform 落位）
      anchor / pivot 图在框的锚点 / 枢轴（9 向枚举同上，默认 center）；x / y 图在框内的自由偏移（可负，缺省 0）
      preprocess 元素外观·预处理队列（键同节点 preprocess，可含几何步——落位前施于源、无框时区域随预处理后尺寸）；opacity 元素外观·不透明度 0..255（缺省 255）
    backdrop 图案元素（可选；缺省＝透明底）：
      图案键与 --pattern 相同（kind / background / foreground / period / line-width / dot-radius /
        orientation / seed / noise）+ preprocess 元素外观·预处理队列（仅色彩步，几何步忽略）
        + opacity 元素外观·不透明度 0..255（缺省 255）
    transform 节点落位（均可省略取默认）：
      inCanvasAnchor 画布锚点 / layerPivot 图层枢轴——9 向枚举：top-left / top-center / top-right /
        middle-left / center（默认）/ middle-right / bottom-left / bottom-center / bottom-right
      x / y：自由偏移像素（可负，缺省 0；可写百分比串，横向按画布宽、纵向按高换算）
      width / height：目标尺寸（≤0＝源图 / 图案原生尺寸、不缩放；缺省 0；可写百分比串，按画布宽 / 高换算）
      opacity：图层整体不透明度 0..255（缺省 255＝不透明；合成期逐像素施加）
    zOrder 叠放次序：整数、升序叠放（大＝在上；同值按数组先后、后加入者在上）；缺省 0——
      命令行追加时自增续尾（叠在配置自带层之上）
    preprocess 节点外观·预处理数组（先物化再落位；与配置文件的预处理段同规）：
      {"op":"gray"} 灰度；{"op":"split","mask":"111"} 通道分离 / {"op":"invert","mask":"111"} 反色（mask＝三位 RGB 遮码，'1' 启用）
      {"op":"rotate","deg":90} 旋转（任意整数度）；{"op":"flip","axis":"horizontal"|"vertical"} 翻转
      {"op":"resize","width":W,"height":H} 重采样到 W×H
)IDCH";
inline constexpr char kTextLayerTail[] = R"IDCH(
  校验与退出码（与标注同级的强制校验，逐条报因）：
    · 锚点 / fit / 图案键拼错、颜色非法、数值字段非整数也非百分比串、opacity 越 0..255 → 退出码 1，报因定位到字段（如「配置字段 '[0].transform.inCanvasAnchor' 的取值 'bogus' 无法识别」）。
    · zOrder 须为整数（负数合法，视为最底层）；image.file 名须为字符串（空＝无图片元素）；JSON 语法错误、根非对象/数组、数组内无有效图层对象 → 退出码 1。
    · file 名越界（绝对路径 / 含 ..）→ 执行期退出码 3；缺文件默认降级为空白占位并告警，--strict 改为退出码 3。
  单条示例（JSON 形；平铺 / 块形简写见 --layer 选项说明）：
    {"image":{"file":"badge.png"},"transform":{"inCanvasAnchor":"top-right","layerPivot":"top-right"}}
    {"backdrop":{"kind":"stripes","foreground":"#FF000000"},"transform":{"width":320,"height":240}}
    {"image":{"file":"badge.png","fit":"stretch","boxWidth":160,"boxHeight":120,"opacity":200},
     "backdrop":{"kind":"solid","background":"#FFFFFFFF"},"transform":{"x":0,"y":0}}
  导出：--save-config 会把命令行图层写入配置文件的 layers 段（config 命令追加在 --load 自带段之后；其余命令随配置透写）。
)IDCH";
inline constexpr char kTextAnnoFields[] = R"IDCH(  字段按 type 分派几何，样式对所有类型通用：
    几何坐标（x/y、x1..y2、points、segments[].coords）可写整数或百分比串（如 "50%"，>100% 与负值合法；只对坐标生效，fontSize / cornerRadius / strokeWidth / zOrder 仍须整数）：
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
    · type 缺失或未知字符串一律报错（不默认按 line 处理）。
    · 锚点退化即报「锚点退化」：矩形/菱形需 |Δx|>0 且 |Δy|>0；正方形/等腰直角需 max(|Δx|,|Δy|)>0；等腰/等边需半底与高、直角需两直角边非退化；圆需半径 >0、椭圆需两半轴均 >0。
    · 自由图元：line 点数须恰为 2；text 的 fontSize 若给出须 >0；path 须有非空且有效的 segments。
    · 几何字段既非整数也非百分比串即报错（如 "wide%"）；写百分比时基准尺寸必已知（解图后换算），否则报错而非归零。
    · color/fillColor 须为合法 #RRGGBB 或 #AARRGGBB；strokeWidth / zOrder 须为整数（zOrder 为小数/字符串即报错；负数合法）。JSON 语法错误、根非对象/数组同样退出码 1。
  单条示例：
    {"type":"line","points":[[10,10],[200,150]],"color":"#FF0000","strokeWidth":4}
    {"type":"rectangle","x1":10,"y1":10,"x2":120,"y2":80,"fill":true,"fillColor":"#80FF0000","strokeWidth":2}
    {"type":"rhombus","x1":60,"y1":50,"x2":110,"y2":20,"color":"#FF0000"}
    {"type":"circle","x1":70,"y1":70,"x2":100,"y2":70,"color":"#00FF00"}
    {"type":"text","x":20,"y":30,"text":"HELLO","fontSize":18,"color":"#0000FF"}
    {"type":"path","segments":[{"t":"M","coords":[0,0]},{"t":"L","coords":[50,50]},{"t":"Z","coords":[]}],"color":"#00FFFF"}
  多条：重复 --annotation 各传一条，或一次传数组 [{"type":"line",…},{"type":"circle",…}]；也可 --annotations file 从文件批量读入。
  导出：--save-config 会把命令行标注一并写入配置文件的 annotations 段（往返不丢）。
)IDCH";
inline constexpr char kTextPatternMatrix[] = R"IDCH(
图案 × 参数适用矩阵（未列参数对该图案无效，与引擎实际行为一致）：
    solid            整幅＝--bg；忽略 --fg/--orientation/--period/--line-width/--dot-radius
    linear-gradient  --bg --fg 沿 --orientation 线性插值（水平随 x、垂直随 y）；忽略 --period/--line-width/--dot-radius
    radial-gradient  --bg（边缘） --fg（圆心）径向插值；--orientation 无效，忽略 --period/--line-width/--dot-radius
    checkerboard     --fg 方格边长＝--period（--orientation 无效）；忽略 --line-width/--dot-radius
    stripes          --fg 条带宽度＝--period；--orientation 水平=竖条(随 x)/垂直=横条(随 y)/diagonal-down=↘(沿 x−y)/diagonal-up=↗(沿 x+y)；忽略 --line-width/--dot-radius
    grid-lines       --fg 网格线：间距＝--period、线宽＝--line-width；--orientation 无效，忽略 --dot-radius
    dot-grid         --fg 圆点：点距＝--period、半径＝--dot-radius；--orientation 无效，忽略 --line-width
    concentric       --fg 同心环：环间距＝--period、环宽＝--line-width；--orientation 水平=圆环(欧氏)/垂直=方环(切比雪夫回字纹)，忽略 --dot-radius
    radial-rays      --fg 放射扇区：疏密＝--period（值越小越密）；--orientation 无效，忽略 --line-width/--dot-radius
    hue-gradient     全色相彩虹沿 --orientation 扫描（H 0→1、全饱和全明度不透明），--period＝一道彩虹的像素宽；忽略 --bg/--fg/--line-width/--dot-radius
    noise            确定性伪随机噪声：--seed 定种（同 seed+同尺寸恒同图，供逐像素对拍）、--noise 选配色 full-color(默认)/grayscale/two-color；two-color 于 --bg --fg 间随机取色，其余忽略 --bg/--fg/--orientation/--period/--line-width/--dot-radius
  alpha：--bg/--fg 的 #AARRGGBB alpha 参与插值并写盘；PNG/WebP 保留透明，JPEG/BMP 导出时按填充色压平。
  注：图案类型不含图片贴图；图片经配置文件的 layers 段（file 图层）或 generate 段落位声明引入，由 config --load 合成进画布。
)IDCH";
inline constexpr char kTextGenTail[] = R"IDCH(
  示例：
    # 径向渐变画布作为 extract 的生成源，保留中心 100×100
    idc extract --size 200x200 --pattern radial-gradient --bg "#FF000000" --fg "#FFFFFFFF" --rect 50,50,150,150 --output grad.png
    # 棋盘格输入 erase：十字切割后坍缩合并
    idc erase --size 300x200 --pattern checkerboard --fg "#FF000000" --period 50 --rect 100,100,200,150 --merge collapse --output out.png
    # 确定性噪声（同 --seed 恒同图，供逐像素对拍）
    idc extract --size 256x256 --pattern noise --seed 7 --rect 0,0,256,256 --output n.png
  约束与退出码：
    · --input 与 --size 至少给一个；--size 单边 ≤65535 且 宽×高 ≤64,000,000。
    · 参数非法（图案名 / 颜色 / 方向 / 周期等）→ 退出码 1，报因逐条列出。
    · 与 --save-config 同用时仅 source 尺寸与切割 / 标注 / 图层落盘，图案参数不写 generate 段——配方需落盘请用 idc new --save-config。
)IDCH";
inline constexpr char kTextGenTopicHead[] = R"IDCH(
程序化生成源（extract / erase / grid / config 共用；与 new 命令同一套图案参数）：用 --size（可配 --pattern 等）
程序生成一张画布；可与 --input 同给——画布尺寸由 --size 定，位图居中铺入（缺省原生尺寸；--fit 改适应方式；
越界裁、不足露底）：
)IDCH";
inline constexpr char kTextGenSeeTopic[] = R"IDCH(
生成源：--size（+ --pattern 等）可与 --input 同给——画布尺寸由 --size 定，位图居中铺入（缺省原生尺寸；--fit 改适应方式）；完整适用矩阵见 idc --help generate
)IDCH";
inline constexpr char kTextGenParamsPtr[] = R"IDCH(
图案参数（--pattern / --orientation / --bg / --fg / --period / --line-width / --dot-radius / --seed / --noise）和适用矩阵与 new 命令相同：idc --help new
)IDCH";

// 各命令的小节标题（多命令同值，故单独抽出）。
inline constexpr char kTextSecCutThree[] = R"IDCH(
切割线（--rect / --hband / --vband 三选一）：
)IDCH";   // extract / erase。
inline constexpr char kTextSecOutput[] = R"IDCH(
输出：
)IDCH";   // extract / new。
inline constexpr char kTextSecExport[] = R"IDCH(
导出：
)IDCH";   // erase / grid。
inline constexpr char kTextSecCommonBlank[] = R"IDCH(

通用：
)IDCH";   // extract / erase / grid / config。
inline constexpr char kTextSecCommon[] = R"IDCH(
通用：
)IDCH";   // new（无前置空行）。
inline constexpr char kTextCommonOptsPtr[] = R"IDCH(作业级通用选项（--save-config / --assets-dir）：见 idc --help
)IDCH";   // extract / erase / grid / new（config 页在示例与常见错误中已述）。
inline constexpr char kTextSecGrid[] = R"IDCH(
网格：
)IDCH";
inline constexpr char kTextSecSelect[] = R"IDCH(
选择（--keep 与 --remove 二选一）：
)IDCH";
inline constexpr char kTextSecSortDecorate[] = R"IDCH(
排序与修饰：
)IDCH";
inline constexpr char kTextSecMargin[] = R"IDCH(
余量：
)IDCH";
inline constexpr char kTextSecCompose[] = R"IDCH(
合并重排：
)IDCH";
inline constexpr char kTextSecOverride[] = R"IDCH(
字段覆盖（可选，覆盖配置对应字段；--save-config 落盘与执行同样生效）：
)IDCH";
inline constexpr char kTextSecIo[] = R"IDCH(
输入输出：
)IDCH";
inline constexpr char kTextSecSizeBg[] = R"IDCH(
尺寸与底色：
)IDCH";
inline constexpr char kTextSecPattern[] = R"IDCH(
图案：
)IDCH";

// 各命令的导语与尾巴（示例 / 约束 / 常见错误）。
inline constexpr char kTextExtractIntro[] = R"IDCH(idc extract — 标准提取模式

坐标取值：整数像素，或百分比串（如 "50%"，按输入图像宽 / 高换算）。

输入：
)IDCH";
inline constexpr char kTextExtractTail[] = R"IDCH(
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
)IDCH";
inline constexpr char kTextEraseIntro[] = R"IDCH(idc erase — 反向剔除模式（差异化内核）

坐标取值：整数像素，或百分比串（如 "50%"，按输入图像宽 / 高换算）。

输入：
)IDCH";
inline constexpr char kTextEraseTail[] = R"IDCH(
约束：
  - --rect / --hband / --vband 互斥；--merge 与 --output-dir 互斥。
  - --merge collapse 但选择集不可坍缩 → 退出码 2（无重排可用，请调整切割线使
    删除区覆盖整行 / 整列，或改分离导出）。

示例：
  idc erase --input photo.jpg --rect 100,100,300,250 --output-dir ./out/ --format png
  idc erase --input photo.jpg --rect 100,100,300,250 --merge collapse --output out.png
  idc erase --input photo.jpg --rect 100,100,200,150 --rect 400,300,500,400 --merge collapse --output out.png

常见错误：
  退出码 1：缺必填 / 互斥冲突 / 坐标非法或退化（退化选框请改用 --hband / --vband）/ --merge rearrange（不支持重排）
  退出码 2：坍缩不可行（见约束）
  退出码 3：输入图像错误
  退出码 4：输出目录 / 文件写入失败
)IDCH";
inline constexpr char kTextGridIntro[] = R"IDCH(idc grid — 网格分割模式（完备表达层）

坐标取值：整数像素，或百分比串（如 "50%"，按输入图像宽 / 高换算）。

网格定义三选一（可整幅给，也可按轴分别用 -x / -y 版本；每轴只能一种形态）：
  · 周期   --grid / --grid-x / --grid-y：基准点 + 单元尺寸，跨界残缺单元按 --margin 处理；
  · 分割数 --divide / --divide-x / --divide-y：基准点恒 0，尽量均分，末份吸收取整误差；
  · 比例   --weights-x / --weights-y：基准点恒 0，按权重分配，末份吸收取整误差。
  分割数与比例恒恰好铺满、无残缺单元，故不需要 --margin；未定义的轴视为整幅 1 份。

输入：
)IDCH";
inline constexpr char kTextGridTail[] = R"IDCH(
约束：
  - --keep 与 --remove 互斥；--compose 与 --output-dir 互斥。
  - 网格定义：--grid 与 --divide 二选一，且均不可与按轴选项（--grid-x/--grid-y、--divide-x/--divide-y、--weights-x/--weights-y）同用；同一轴只给一种形态。
  - 余量策略 --margin 只对周期轴生效（分割数 / 比例无残缺单元）。
  - --decorate 在 --sort custom 下不可用；--merge-sort 不支持 custom（重排填充仅
    行 / 列优先，自定义块序用 --sort custom）。
  - --compose 时 --canvas 的 cols/rows 必填且须为正。
  - 引擎先施加 snake 再整体 reverse（固定次序），故 reverse-snake 与 snake-reverse 结果相同。

示例：
  idc grid --input photo.jpg --grid 100,100,200,150 --keep 0,0 --keep 0,2 --keep 2,0 --keep 2,2 --compose --canvas 2x2 --output result.png
  idc grid --input photo.jpg --grid 0,0,100,100 --remove 1,1 --output-dir ./cells/
  idc grid --input photo.jpg --grid 0,0,100,100 --sort custom --order 0,0;1,1;2,2 --compose --canvas 3x1 --output diag.png
  # 3 竖 × 4 横等分（末份吸收取整误差），导出左下角单元
  idc grid --input photo.jpg --divide 3x4 --keep 3,0 --output-dir ./cells/
  # 横向 3 等分 + 纵向固定 200px 周期（混合两轴形态）
  idc grid --input photo.jpg --divide-x 3 --grid-y 0,200 --keep 0,0 --output-dir ./cells/
  # 横向按比例 1:2:1 切分
  idc grid --input photo.jpg --weights-x 1,2,1 --keep 0,0 --output-dir ./cells/

常见错误：
  退出码 1：缺必填 / 互斥冲突 / r,c 越界 / 格式非法
  退出码 3：输入图像错误
  退出码 4：输出失败
)IDCH";
inline constexpr char kTextConfigIntro[] = R"IDCH(idc config — 配置文件作业

配置中的几何字段与 --rect 覆盖均可用百分比串（如 "50%"，按画布 source 宽 / 高换算）。

配置：
)IDCH";
inline constexpr char kTextConfigLayers[] = R"IDCH(
多层画布（layers 段，可选）：仅承载叠加图层（不含底图），按 zOrder 叠于底图之上；执行前由引擎
      展平为 source 尺寸的画布（顺序：全透明底 → 底图 → 叠加层；引擎仍只收一张展平画布）。
      底图来自 --input / --size / 配置 generate 段（可组合、非互斥）；皆无时叠加层直接铺于全透明底之上。每层来源三选一：
        · recipe 配方（与 generate 段同构的图案参数，可逐字节复现，不留位图）；
        · file  文件（仅存相对名，相对配置文件目录或 <配置名>.assets/ 解析；解码注入；越界（绝对路径 / ..）拒绝，缺文件降级为占位）；
        · 空占位（缺文件 / 未指定，合成时跳过）。
      每层可带 transform（画布点 inCanvasAnchor + 图层枢轴 layerPivot + 自由 x/y + 目标 width/height）与逐层 preprocess；
      越出画布的像素在写入时自然裁剪（导出即舍弃）。

底图（generate 段，可选）：底图＝与图层同构的一条图层记录，仅角色为钉住最底、不可拖 / 不可改落位。
      除图案字段（kind/background/foreground/…，与 generate 段图案参数同构）外，新建画布所附图片底图可额外携
      fit + imageAnchor/imagePivot + x/y + backdrop（图层框衬底图案）落位键（落位框尺寸恒＝画布、由 source 派生，不单独存），使底图落位随配置往返、可复现
      （配置只存落位、不存文件路径，底图位图恒由运行时 --input 供给，故同一配置可对不同输入批处理复用）。
      --input / 打开图＝无落位、画布尺寸＝图片尺寸的恒等底图，外部用法一致。

预处理（preprocess 数组，可选）：gray/split/invert/rotate/flip/resize 六类步（不透明度不经预处理步，由层合成参数承载）。

层不透明度（transform.opacity，可选）：整层淡出程度（0..255，缺省 255＝完全不透明、0＝完全透明）。
      该参数在导出合成时按不透明度比例作用于整层、不改动像素本身，故灰度图同样生效。
)IDCH";
inline constexpr char kTextConfigTail[] = R"IDCH(
示例：
  idc config --load my-config.json --input photo.jpg --output result.png
  idc extract --input photo.jpg --rect 100,100,300,250 --output out.png --save-config out.json
  # 多层画布配置（仅 layers 叠加层）直接执行：无底图时叠加层铺于全透明底之上
  idc config --load canvas.json --output out.png
  # 底图 + 叠加层组合：--input 提供底图，配置 layers 段叠加其上（展平顺序：全透明底 → 底图 → 叠加层）
  idc config --load canvas.json --input base.jpg --output out.png
  # 把配置另存并携带 File 层资源到 <配置名>.assets/（供随配置分发 / 复现）
  idc config --load canvas.json --save-config bundle.json --assets-dir

常见错误：
  退出码 1：缺 --load / 缺输出 / 配置解析失败；--hband/--vband 与 --save-config 同用；--assets-dir 时 File 层名为绝对路径 / 含 ..（越界）
  退出码 2：画布展平产出空图（source 尺寸非法）/ 引擎业务失败
  退出码 3：配置文件或输入图像不存在 / 不可读；图层资源路径越界；--assets-dir 时 File 层源图缺失
  退出码 4：输出 / 写盘失败；--assets-dir 复制资源或建目录失败
)IDCH";
inline constexpr char kTextNewIntro[] = R"IDCH(idc new — 新建画布（程序化生成画布并落盘）

)IDCH";
inline constexpr char kTextNewTail[] = R"IDCH(
示例：
  idc new --size 800x600 --bg "#FFFFFFFF" --output white.png
  idc new --size 1024x1024 --pattern checkerboard --bg "#FFFFFFFF" --fg "#FF000000" --period 64 --output board.png
  idc new --size 1920x1080 --pattern radial-gradient --bg "#FF000000" --fg "#FFFFFFFF" --output glow.png
  # 斜纹（--orientation 选 diagonal-down ↘ / diagonal-up ↗）与全色相彩虹
  idc new --size 512x512 --pattern stripes --orientation diagonal-down --fg "#FF000000" --period 24 --output diag.png
  idc new --size 512x256 --pattern hue-gradient --period 128 --output rainbow.png
  # 同心方框（--orientation vertical = 方环回字纹）
  idc new --size 512x512 --pattern concentric --orientation vertical --fg "#FF000000" --period 32 --line-width 2 --output rings.png
  # 确定性噪声（同 --seed 恒同图，供逐像素对拍）
  idc new --size 256x256 --pattern noise --seed 1234 --noise full-color --output noise.png
  # 透明底：底色 alpha=00 得全透明画布
  idc new --size 512x512 --bg "#00FFFFFF" --output transparent.png
  # 透明画布导出 JPEG：显式给压平底色，避免静默黑底
  idc new --size 512x512 --bg "#00FFFFFF" --format jpeg --pad-color "#FFFFFFFF" --output flat.jpg
  # 把生成配方存为配置（dry-run；含全幅保留 cut 段，config --load 可直接复现画布）
  idc new --size 200x150 --pattern solid --bg "#FFFF0000" --save-config recipe.json
  # 生成画布同时烧入标注、叠加图层（图层合成在前，标注恒在最上）
  idc new --size 200x150 --bg "#FFFFFFFF" --annotation '{"type":"circle","x1":100,"y1":75,"x2":130,"y2":75,"color":"#FF00FF00","strokeWidth":3}' --layer '{"source":{"recipe":{"kind":"stripes","foreground":"#40000000"}},"transform":{"width":200,"height":150}}' --output done.png

常见错误：
  退出码 1：缺 --size；非 dry-run 时缺 --output；--size 非 WxH 正整数 / 单边超 65535 / 宽×高超 64,000,000；--pattern / --orientation / --bg / --fg / --pad-color / --period / --seed / --noise 等格式非法；--quality 非 0~100 整数；给出 --input（new 不接受）；--annotation / --layer 参数非法；--assets-dir 打包时 File 层名越界
  退出码 3：图层 File 资产路径越界；--strict 时 File 源图缺失；--assets-dir 打包时 File 源图缺失
  退出码 4：输出写入失败；--assets-dir 复制资源或建目录失败
)IDCH";

} // namespace idc::cli::texts::help

