// ============================================================================
// 文件：include/i18n/cli_texts_base.h（CLI 常量层 · 字符串 · 前半：opt / cmd / env / msg）
// 作用：收录**不可译契约组** opt（长 / 短选项名）、cmd（子命令名）、env（语言环境变量名）
//       与**可译组** msg（面向人的错误 / 校验报因 / 进度日志）。help 组（--help 散文）见同目录
//       cli_texts_help.h；两半经聚合转发头 cli_texts.h 统一 include，消费方以 texts::opt::kXxx 等形态引用。
// 架构定位：CLI 顶层 i18n 基建（与 cli_i18n.h / cli_nums.h 同族，统一住 include/i18n）。
// 命名 / 类型 / 提取判据：k + 语义驼峰；inline constexpr char[]，UTF-8 无 BOM；
//       键名一律提取，成对映射表内值串不提取。
// 目录与对账：msg 的英文表住 src/i18n/lang_en.cpp；opt / cmd / env 不可译、永不进表
//       （tests/test_i18n_catalog.cpp 断言拦下孤儿键 / 重复键 / 跨层撞键 / 契约组越界并打印覆盖率）。
// ============================================================================
#pragma once

namespace idc::cli::texts {

// ---------------------------------------------------------------------------
// 长选项名与短选项名（不可译）：共 45 条，按功能分组；命名去前划线（--output-dir → kOutputDir）。
// 使用点：arg_parser.cpp 的 flag 表与全局扫描、commands/*.cpp 的取值、command_support.h 的公共导出
//         选项、help*.cpp 的示例文本；同值者不得再写字面量，否则改名时会漏改一处。
// ---------------------------------------------------------------------------
namespace opt {

// 全局（任意位置可出现，见 arg_parser.cpp::parseGlobalOptions）。
inline constexpr char kHelp[] = "--help";                    // 显示帮助（与 kShortHelp 同义）。
inline constexpr char kShortHelp[] = "-h";                   // --help 短写。
inline constexpr char kVersion[] = "--version";              // 显示版本（优先于一切）。
inline constexpr char kShortVersion[] = "-v";                // --version 短写。
inline constexpr char kVerbose[] = "--verbose";              // 详细进度到 stderr。与 help::kUseVerbose 同字。
inline constexpr char kQuiet[] = "--quiet";                  // 抑制正常结果输出。与 help::kUseQuiet 同字。
inline constexpr char kConfig[] = "--config";                // 从 JSON 配置读取全部参数（路由到 config 命令）。
inline constexpr char kSaveConfig[] = "--save-config";       // 把当前参数序列化为 JSON（dry-run）。
inline constexpr char kBundleAssets[] = "--bundle-assets";   // 与 --save-config 同用时打包 File 层源图。与 help::kUseBundleAssets 同字。
inline constexpr char kLang[] = "--lang";                    // 全局：指定帮助 / 报错文本的语言（取值为 texts::env 链或可用语言码）。

// 输入来源（二选一：读文件 / 程序生成）。
inline constexpr char kInput[] = "--input";                  // 待处理图像路径（全部命令共用，出现最频繁）。
inline constexpr char kSize[] = "--size";                    // 生成画布尺寸 WxH，替代 --input。

// 几何（切割与选择）。
inline constexpr char kRect[] = "--rect";                    // 矩形（L1 单个 / L2 可重复做并集）。
inline constexpr char kHband[] = "--hband";                  // 水平带 y1,y2。
inline constexpr char kVband[] = "--vband";                  // 垂直带 x1,x2。
inline constexpr char kGrid[] = "--grid";                    // 网格基准点 + 单元尺寸 x0,y0,cw,ch。
inline constexpr char kKeep[] = "--keep";                    // 保留单元（可重复）。
inline constexpr char kRemove[] = "--remove";                // 剔除单元（可重复）。
inline constexpr char kOrder[] = "--order";                  // 自定义序列 r,c;r,c。
inline constexpr char kSort[] = "--sort";                    // 选择排序策略。
inline constexpr char kDecorate[] = "--decorate";            // 选择排序修饰。
inline constexpr char kMargin[] = "--margin";                // 余量策略。

// 导出目标与格式。
inline constexpr char kOutput[] = "--output";                // 合并单图路径（出现次数仅次于 --input）。
inline constexpr char kOutputDir[] = "--output-dir";         // 分离导出文件夹。
inline constexpr char kFormat[] = "--format";                // 输出格式（或扩展名不明时的回退）。
inline constexpr char kQuality[] = "--quality";              // 有损质量。
inline constexpr char kNaming[] = "--naming";                // 分离命名模板。
inline constexpr char kPadColor[] = "--pad-color";           // 余量 / 压平填充色。
inline constexpr char kMerge[] = "--merge";                  // L2 合并方式（仅 collapse）。
inline constexpr char kCompose[] = "--compose";              // L3 重排合并开关（flag）。与 help::kUseCompose 同字。
inline constexpr char kCanvas[] = "--canvas";                // 重排画布 ColxRow。
inline constexpr char kMergeSort[] = "--merge-sort";         // 重排填充顺序（与 --sort 正交）。
inline constexpr char kMergeDecorate[] = "--merge-decorate"; // 重排填充修饰。

// 图案生成（new 与「生成源」共用同一套）。
inline constexpr char kPattern[] = "--pattern";              // 图案类型。
inline constexpr char kOrientation[] = "--orientation";      // 方向轴。
inline constexpr char kBg[] = "--bg";                        // 底色。
inline constexpr char kFg[] = "--fg";                        // 前景 / 图案色。
inline constexpr char kPeriod[] = "--period";                // 周期像素。
inline constexpr char kLineWidth[] = "--line-width";         // 线宽像素。
inline constexpr char kDotRadius[] = "--dot-radius";         // 点半径像素。
inline constexpr char kSeed[] = "--seed";                    // 噪声种子。
inline constexpr char kNoise[] = "--noise";                  // 噪声配色模式。

// 配置与标注。
inline constexpr char kLoad[] = "--load";                    // config 命令的配置路径（cli_app 全局 --config 路由时前置注入）。
inline constexpr char kAnnotation[] = "--annotation";        // 内联标注 JSON（可重复）。
inline constexpr char kAnnotations[] = "--annotations";      // 标注 JSON 文件（可重复）。

} // namespace opt

// ---------------------------------------------------------------------------
// 子命令名（不可译）：cli_app.cpp 分派、config 帮助回退、IT-14 断言顶层帮助含全部命令名。
// ---------------------------------------------------------------------------
namespace cmd {

inline constexpr char kExtract[] = "extract";   // L1 标准提取。
inline constexpr char kErase[] = "erase";       // L2 反向剔除。
inline constexpr char kGrid[] = "grid";         // L3 网格分割。与 GUI palette 键名 gui_texts::ident::kPaletteGrid 同字纯属巧合，不是同一个值。
inline constexpr char kConfig[] = "config";     // 配置文件作业（全局 --config 亦路由到此）。
inline constexpr char kNew[] = "new";           // 新建画布。

} // namespace cmd

// ---------------------------------------------------------------------------
// env：读取的环境变量名（**不可译**，同 opt / cmd：它们是与外壳 / libc 的契约串）。
// 使用点：cli_i18n.cpp 的语言优先级链（IDC_LANG > LC_ALL > LANG）。
//   · IDC_LANG  本工具专用变量（覆盖桌面 / 终端语言，供“终端是英文但想要中文输出”一类场景）；
//   · LC_ALL / LANG  POSIX locale 惯例变量（值可为 "C" / "en_US.UTF-8" 等非纯语言标签，认不出则回退）。
// ---------------------------------------------------------------------------
namespace env {

inline constexpr char kIdcLang[] = "IDC_LANG";
inline constexpr char kLcAll[]   = "LC_ALL";
inline constexpr char kLang[]    = "LANG";

} // namespace env

// ===========================================================================
// msg：面向人的错误（brief / detail / hint）与进度日志（可译）。
//   与 Core 的 core_texts.h::msg 同构：按来源模块分小节，行尾注明落点；
//   重复出现且语义相同的只留一条（如「路径：」前缀与几条长 hint）。
//   以「'」收尾的片段只存前段，后接的原值与尾字符保持就地拼接；括注用的全角「（）」
//   属源语言标点，提取为 kLogParenOpen / kLogParenClose（可译）；含量词 / 序号的句子一律收为整句 + %N，见 IDC_CTRF。
// ===========================================================================
namespace msg {

// CLI 顶层分派（src/cli_app.cpp）。
inline constexpr char kErrUnknownCmd[] = "未知命令：'";   // 后接命令原值与尾引号。
inline constexpr char kHintKnownCmds[] = "可用命令：extract / erase / grid / config / new；用 idc --help 查看用法";
inline constexpr char kErrInternalUnexpected[] = "内部错误（未预期异常）";
inline constexpr char kErrInternalUnknown[] = "内部错误（未知异常）";
inline constexpr char kErrBadLang[] = "无法识别的语言代码：'";   // 后接 --lang 原值与尾引号。
inline constexpr char kHintLangCodes[] = "可用语言代码：";       // 后接 i18n::availableLanguageCodes() 的枚举结果。
inline constexpr char kHintReportBug[] = "请上报 bug（附完整命令行）";   // 两条兜底共用。

// 错误输出版式与日志前缀（src/cli_error.cpp、src/job.cpp）。
// 注：“idc” 为可执行名（不可译），与描述同处一行；将来接翻译时只译 label 部分，前缀保持原样。
inline constexpr char kErrorHead[] = "idc: error: ";     // 错误首行（stderr）。
inline constexpr char kDetailLabel[] = "  详情: ";       // 详情行标签（非空才输出）。
inline constexpr char kHintLabel[] = "  提示: ";         // 提示行标签（非空才输出）。
inline constexpr char kLogPrefix[] = "idc: ";           // verbose / stdout 进度行前缀（job.cpp 多处）。

// 详情行的字段前缀（src/job.cpp、src/commands/config.cpp）。
inline constexpr char kDetPath[] = "路径：";                                   // ×3。
inline constexpr char kDetTarget[] = "目标：";                                 // ×2。
inline constexpr char kDetMissing[] = "未找到：";
inline constexpr char kDetLayerFileName[] = "文件层 fileName：";              // ×2。
inline constexpr char kDetSourceSize[] = "source 尺寸（%1）非法或超出总像素上限";   // %1＝宽x高；三处复用（IDC_CTRF）。
inline constexpr char kDetNoLayers[] = "config 无 layers 段";
inline constexpr char kDetNoGenerate[] = "config 无 generate/base 段";
inline constexpr char kDetBaseNeedsInput[] = "配置 generate 段含 fit/box*/imageAnchor 等落位键，但未提供 --input 源图";
inline constexpr char kDetPreprocessEmpty[] = "预处理流水线产出空图像";
inline constexpr char kDetNotCollapsible[] = "引擎返回：保留集不构成整行或整列的补集";

// 读图与落盘（src/job.cpp）。
inline constexpr char kErrNoInputPath[] = "缺少输入图像路径";
inline constexpr char kHintUseInputFile[] = "请用 --input <file> 指定输入图像";   // job 内部守卫专用（不带 --size 分支）。
inline constexpr char kErrReadInput[] = "无法读取输入图像";
inline constexpr char kHintInputReadable[] = "请确认文件存在、可读，且为受支持的图像格式（PNG/JPEG/BMP/WebP 等）";  // ×2。
inline constexpr char kErrEmptyImage[] = "无法落盘：图像为空";
inline constexpr char kHintCheckGenArgs[] = "生成参数可能非法，请检查 --size / --pattern 等";
inline constexpr char kErrExportFailed[] = "导出失败";                       // ×2（合并与分离共用 brief）。
inline constexpr char kHintOutputWritable[] = "请确认输出路径的父目录存在、文件未被占用、磁盘未满";         // ×2。
inline constexpr char kHintOutputDirWritable[] = "请确认输出目录可创建且可写（未被同名文件占用、权限足够）";
inline constexpr char kErrSaveConfig[] = "无法保存配置文件";
inline constexpr char kHintSaveConfigPath[] = "请确认路径的父目录存在且可写";

// 多层画布与底图（src/job.cpp）。
inline constexpr char kErrLayerCompose[] = "多层画布合成失败";
inline constexpr char kHintInternalLayersOnly[] = "内部错误：buildLayerCanvas 仅在带 layers 的配置上调用";
inline constexpr char kErrAssetPathEscape[] = "图层资源路径越界";
inline constexpr char kHintAssetRelative[] = "图层资源须为配置目录 / <配置名>.assets 下的相对路径（不得含绝对路径或 .. ）";
inline constexpr char kErrAssetDecode[] = "无法解码图层资源图像";
inline constexpr char kErrLayerCanvasEmpty[] = "多层画布合成产出空图";
inline constexpr char kHintSourceBounds[] = "请检查配置 source.width/height（单边 ≤ 65535，宽×高 ≤ 64000000）";   // ×2。
inline constexpr char kErrBaseCompose[] = "底图合成失败";
inline constexpr char kHintInternalBaseOnly[] = "内部错误：buildBaseCanvas 仅在带底图声明的配置上调用";
inline constexpr char kErrBaseNeedsInput[] = "底图为落位声明，需外部输入图";
inline constexpr char kHintProvideBaseInput[] = "请用 --input <file> 提供要落位的底图（图片路径不入配置，保批处理可复用）";
inline constexpr char kErrBaseGenEmpty[] = "底图生成产出空图";
inline constexpr char kErrBasePlaceEmpty[] = "底图落位合成产出空图";
inline constexpr char kHintBasePlacement[] = "请检查配置 source.width/height 与 generate 段落位参数（fit/box*/imageAnchor）";

// 资源打包（src/job.cpp --bundle-assets）。
inline constexpr char kErrAssetEscapeReject[] = "图层资源路径越界，拒绝打包";
inline constexpr char kHintBundleRelative[] = "--bundle-assets 仅接受相对、不含 .. 的文件名";
inline constexpr char kErrAssetMissingBundle[] = "图层资源缺失，无法打包";
inline constexpr char kHintBundleNeedsSource[] = "--bundle-assets 需 File 层源图存在于配置目录或其 .assets 目录";
inline constexpr char kErrMkdirAsset[] = "创建资源子目录失败";
inline constexpr char kErrCopyAsset[] = "复制图层资源失败";

// 引擎执行（src/job.cpp）。
inline constexpr char kErrPreprocessFailed[] = "预处理失败";
inline constexpr char kHintCheckPreprocess[] = "请检查配置 preprocess 的步骤与参数";
inline constexpr char kErrEngineFailed[] = "引擎执行失败";
inline constexpr char kHintCheckSelection[] = "请检查切割 / 选择参数是否产生了有效的保留区域";
inline constexpr char kErrNotCollapsible[] = "当前选择集不满足坍缩条件";
inline constexpr char kHintUseRearrange[] = "请改用 --merge rearrange，或调整切割线使删除区域覆盖整行/整列";

// 进度日志与正常输出（src/job.cpp：verbose 走 stderr，结果行走 stdout）。
inline constexpr char kLogInputRead[] = "已读取输入图像 ";
inline constexpr char kLogWriteGenerated[] = "生成图像落盘 ";
inline constexpr char kLogAssetMissing[] = "图层资源缺失，降级为占位：";
inline constexpr char kLogLayerComposed[] = "已合成多层画布 %1x%2（%3 层）\n";   // %1/%2＝宽高，%3＝层数（IDC_CTRF）。
inline constexpr char kLogBaseGenerated[] = "已按配置底图段生成画布 ";
inline constexpr char kLogBasePlaced[] = "已按配置底图落位摆入外部输入图 ";
inline constexpr char kLogAssetBundled[] = "已打包图层资源 ";
inline constexpr char kLogBurnIn[] = "已把 %1 条标注烧录进工作图";   // %1＝条数（IDC_CTRF）。
inline constexpr char kLogRunEngine[] = "运行 Grid-Selection-Emit 引擎...";
inline constexpr char kLogExportTo[] = "导出到 ";
inline constexpr char kLogSeparateTag[] = "（分离多图）";
inline constexpr char kLogMergedTag[] = "（合并单图）";
inline constexpr char kLogExportedCount[] = "已导出 %1 张图像到文件夹：%2";   // %1＝张数，%2＝文件夹路径（IDC_CTRF）。
inline constexpr char kLogExportedMerged[] = "已导出合并图像：";
inline constexpr char kLogConfigSaved[] = "已保存配置到：";
// 尺寸括注（job.cpp 多处、commands/new.cpp）：全角括号属源语言标点，故入常量层供译；层数括注（「N 层」）含于 kLogLayerComposed 整句。
inline constexpr char kLogParenOpen[] = "（";   // 括注前段（接宽度 x 高度）。
inline constexpr char kLogParenClose[] = "）\n";   // 括注尾段（含行尾换行）。

// 值格式报因（src/value_parser.cpp）：「'」结尾者后接原值与尾引号，就地拼接。
inline constexpr char kErrRectFourInts[] = "矩形须为 x1,y1,x2,y2 四个整数";
inline constexpr char kErrRectNotInt[] = "矩形坐标含非整数值：'";
inline constexpr char kErrBandTwoInts[] = "带须为 a,b 两个整数（起始, 结束）";
inline constexpr char kErrBandNotInt[] = "带坐标含非整数值：'";
inline constexpr char kErrGridFourInts[] = "网格须为 x0,y0,cw,ch 四个整数（基准点 + 单元尺寸）";
inline constexpr char kErrGridNotInt[] = "网格参数含非整数值：'";
inline constexpr char kErrCellSizePositive[] = "单元尺寸 cw,ch 必须为正整数";
inline constexpr char kErrCellTwoInts[] = "单元须为 r,c 两个整数（行, 列，从 0 起）";
inline constexpr char kErrCellNotInt[] = "单元坐标含非整数值：'";
inline constexpr char kErrCellNegative[] = "单元行列号不可为负：'";
inline constexpr char kErrCanvasForm[] = "画布须为 ColxRow 形式（如 8x6）";
inline constexpr char kErrCanvasNotInt[] = "画布 ColxRow 含非整数值：'";
inline constexpr char kErrCanvasPositive[] = "画布列数 / 行数必须为正";
inline constexpr char kErrOrderEmpty[] = "自定义序列 --order 为空（须形如 r,c;r,c）";
inline constexpr char kErrColorBadHex[] = "颜色含非法十六进制字符：'";   // ×3（#RGB / #RRGGBB / #AARRGGBB 三岔共用）。
inline constexpr char kErrColorForm[] = "颜色须为 #RGB / #RRGGBB / #AARRGGBB：'";
inline constexpr char kErrUnknownFormat[] = "不支持的格式：'";
inline constexpr char kErrUnknownFormatTail[] = "'（可选 png/jpeg/webp/bmp）";
inline constexpr char kErrQualityNotInt[] = "质量参数须为 0~100 的整数：'";   // 文本里的 0~100 与 nums::parser 同义，改动须两边同步。
inline constexpr char kErrQualityRange[] = "质量参数超出范围（0~100）：'";
inline constexpr char kErrUnknownSort[] = "不支持的排序策略：'";
inline constexpr char kErrUnknownSortTail[] = "'（可选 row-major/column-major/custom）";
inline constexpr char kErrUnknownMargin[] = "不支持的余量策略：'";
inline constexpr char kErrUnknownMarginTail[] = "'（可选 discard/keep-partial/pad）";
inline constexpr char kErrUnknownMerge[] = "不支持的合并方式：'";
inline constexpr char kErrUnknownMergeTail[] = "'（可选 collapse/rearrange）";
inline constexpr char kErrUnknownDecorate[] = "不支持的排序修饰：'";
inline constexpr char kErrUnknownDecorateTail[] = "'（可选 none/reverse/snake/reverse-snake/snake-reverse）";
inline constexpr char kErrNamingEmpty[] = "命名模板不可为空";
inline constexpr char kErrNamingSeparator[] = "命名模板不可包含路径分隔符 '/' 或 '\\'：'";

// 生成参数报因（src/generate_args.cpp）。
inline constexpr char kErrSizeForm[] = "尺寸须为 WxH 形式（如 1024x768）：'";
inline constexpr char kErrSizeTwoPos[] = "尺寸 WxH 须为两个正整数：'";
inline constexpr char kErrSizeDimMax[] = "尺寸单边超出上限（最大 ";        // 后接 Core 上限与尾段。
inline constexpr char kErrSizePixelsMax[] = "总像素超出上限（宽×高 ≤ ";     // 后接 Core 上限与尾段。
inline constexpr char kErrLimitTail[] = "）：'";                              // 上述两条公共尾段。
inline constexpr char kErrNoSizeForGen[] = "缺少 --size，无法生成画布";
inline constexpr char kErrPeriodPositive[] = "--period 须为正整数：'";
inline constexpr char kErrLineWidthPositive[] = "--line-width 须为正整数：'";
inline constexpr char kErrDotRadiusPositive[] = "--dot-radius 须为正整数：'";
inline constexpr char kErrSeedNonNegative[] = "--seed 须为非负整数：'";
inline constexpr char kErrUnknownKind[] = "不支持的图案类型：'";
inline constexpr char kErrUnknownKindTail[] = "'（可选 solid/linear-gradient/radial-gradient/checkerboard/stripes/grid-lines/dot-grid/"  // 与下行
                                              "concentric/radial-rays/hue-gradient/noise）";                                     // 拼接为单串。
inline constexpr char kErrUnknownOrientation[] = "不支持的方向：'";
inline constexpr char kErrUnknownOrientationTail[] = "'（可选 horizontal/vertical/diagonal-down/diagonal-up）";
inline constexpr char kErrUnknownNoiseMode[] = "不支持的噪声模式：'";
inline constexpr char kErrUnknownNoiseModeTail[] = "'（可选 full-color/grayscale/two-color）";
inline constexpr char kErrPatternGenFailed[] = "图案生成失败（尺寸或参数非法）";   // ×2（generate_args 的 err 与 new 的 brief）。
inline constexpr char kErrGenArgs[] = "图像生成参数错误";                        // ×2（generate_args 与 new）。
inline constexpr char kHintGenArgs[] = "请检查 --size / --pattern / --orientation / --bg / --fg / --period / --seed / --noise 等选项";  // ×3。
inline constexpr char kDetFactoryEmpty[] = "图案工厂返回空图";
inline constexpr char kLogGenCanvas[] = "已生成画布输入 ";

// 命令行标注（src/annotation_args.cpp）。
inline constexpr char kErrAnnoFileUnreadable[] = "标注文件不存在或不可读：";   // 后接路径。

// 命令层公共报错（src/commands/{extract,erase,grid,config}.cpp：同一文案在多命令重复出现）。
inline constexpr char kErrMissingInput[] = "缺少输入图像";                        // ×3。
inline constexpr char kHintInputOrSize[] = "请用 --input <file> 指定输入图像，或用 --size WxH（可配 --pattern）生成画布作为输入";  // ×3。
inline constexpr char kErrInputAndSizeMix[] = "--input 与 --size 不可同时使用";   // ×3。
inline constexpr char kHintPickInputSource[] = "读取文件用 --input；程序生成画布用 --size（二者择一）";   // ×3。
inline constexpr char kErrGeoThreeOne[] = "--rect / --hband / --vband 必须三选一";   // ×2。
inline constexpr char kErrRectDegenerate[] = "矩形退化：需满足 x1<x2 且 y1<y2";     // ×3。
inline constexpr char kHintRectOrder[] = "请检查 --rect 的坐标顺序；只需一条切割线时请改用 --hband / --vband";
inline constexpr char kHintRectOrderLead[] = "请检查 --rect '";                   // 多矩形版：后接原值与下行。
inline constexpr char kHintRectOrderTail[] = "' 的坐标顺序；只需一条切割线时请改用 --hband / --vband";
inline constexpr char kErrHbandDegenerate[] = "水平带退化：需满足 y1<y2";         // ×2。
inline constexpr char kHintHbandOrder[] = "请检查 --hband 的坐标顺序";             // ×2。
inline constexpr char kErrVbandDegenerate[] = "垂直带退化：需满足 x1<x2";         // ×2。
inline constexpr char kHintVbandOrder[] = "请检查 --vband 的坐标顺序";             // ×2。
inline constexpr char kErrMissingOutput[] = "缺少 --output";                       // ×2（extract 与 new）。
inline constexpr char kErrMissingOutputDir[] = "分离导出缺少 --output-dir";       // ×2（erase 与 grid）。
inline constexpr char kErrMissingMergeOutput[] = "合并导出缺少 --output";
inline constexpr char kHintMergeOutputPath[] = "请用 --output <file> 指定合并输出路径";   // ×2（erase 与 grid）。

// extract 专属。
inline constexpr char kHintExtractOutput[] = "extract 输出单图，请用 --output <file> 指定输出路径";
inline constexpr char kHintGeoExactlyOne[] = "请给出恰好一个几何参数";
inline constexpr char kHintGeoNoMultiple[] = "不可同时给出多个几何参数";   // 与 erase 的 kHintGeoNoMix 措辞不同，各自保留。

// erase 专属。
inline constexpr char kHintGeoOneKindRect[] = "请给出恰好一种几何参数（--rect 可重复以做多矩形并集剔除）";
inline constexpr char kHintGeoNoMix[] = "不可混用多种几何参数";
inline constexpr char kErrMergeWithOutputDir[] = "--merge 与 --output-dir 不得同时出现";
inline constexpr char kHintMergeVsSeparate[] = "合并请用 --output <file>；分离请用 --output-dir <dir>（二者择一）";
inline constexpr char kErrEraseNoRearrange[] = "erase 不支持 --merge rearrange";
inline constexpr char kHintRearrangeL3Only[] = "合并重排仅 grid 命令支持；请用 --merge collapse，或去掉 --merge 分离导出";
inline constexpr char kHintOutputDirAutoCreate[] = "请用 --output-dir <dir> 指定输出文件夹（不存在会自动创建）";

// grid 专属。
inline constexpr char kErrMissingGrid[] = "缺少 --grid";
inline constexpr char kHintGridGeo[] = "请用 --grid x0,y0,cw,ch 指定基准点与单元尺寸";
inline constexpr char kErrKeepRemoveMix[] = "--keep 与 --remove 不得同时出现";
inline constexpr char kHintKeepOrRemove[] = "请只用其一：保留单元用 --keep，剔除单元用 --remove";
inline constexpr char kErrCustomNeedsOrder[] = "--sort custom 需要 --order";
inline constexpr char kHintOrderSyntax[] = "请用 --order r,c;r,c 指定自定义序列";
inline constexpr char kErrCustomNoKeepRemove[] = "--sort custom 下不可再用 --keep / --remove";
inline constexpr char kHintOrderIsSelection[] = "自定义序列 --order 本身即保留集与输出顺序";
inline constexpr char kErrCustomNoDecorate[] = "--sort custom 下不可用 --decorate";
inline constexpr char kHintCustomNoDecorate[] = "自定义序已显式给定，无需排序修饰";
inline constexpr char kErrOrderNeedsCustom[] = "--order 仅在 --sort custom 下有效";
inline constexpr char kHintOrderNeedsCustom[] = "请改用 --sort custom，或用 --keep / --remove 选择单元";
inline constexpr char kErrNoCellChoice[] = "缺少单元选择";
inline constexpr char kHintKeepRemoveCells[] = "请用 --keep r,c 或 --remove r,c 指定单元格（均可重复）";
inline constexpr char kErrComposeWithOutputDir[] = "--compose 与 --output-dir 不得同时出现";
inline constexpr char kHintComposeOutput[] = "合并请用 --output <file>；分离请去掉 --compose 并用 --output-dir <dir>";
inline constexpr char kErrComposeNeedsCanvas[] = "--compose 需要 --canvas";
inline constexpr char kHintCanvasColxRow[] = "请用 --canvas ColxRow 指定重排画布（如 8x6）";
inline constexpr char kErrComposeNeedsOutput[] = "--compose 需要 --output";
inline constexpr char kErrMergeSortNoCustom[] = "--merge-sort 不支持 custom";
inline constexpr char kHintMergeSortNoCustom[] = "重排填充顺序仅 row-major / column-major（自定义块序请用 --sort custom）";
inline constexpr char kHintOutputDirOrCompose[] = "请用 --output-dir <dir> 指定输出文件夹，或加 --compose + --canvas + --output 合并为单图";
inline constexpr char kErrOrderDupCell[] = "--order 含重复单元 (";              // 后接 r,c 与「)」。
inline constexpr char kHintOrderUnique[] = "自定义序列中每个单元应唯一";
inline constexpr char kErrGridNoCells[] = "网格未产生任何单元";
inline constexpr char kHintGridNoCells[] = "请减小 --grid 的单元尺寸，或用 --margin keep-partial/pad 保留跨界的残缺单元";
inline constexpr char kErrCellOutOfRange[] = "单元格 (";                       // 后接 r,c 与下行。
inline constexpr char kErrCellOutOfRangeTail[] = ") 越界";
inline constexpr char kHintGridShape[] = "当前网格为 %1 行 × %2 列（行列号从 0 起）";   // %1＝行数，%2＝列数（IDC_CTRF）。

// new 专属。
inline constexpr char kErrMissingSize[] = "缺少 --size";
inline constexpr char kHintSizeForm[] = "请用 --size WxH 指定生成画布的尺寸（像素，正整数）";
inline constexpr char kErrNewNoInput[] = "new 不接受 --input";
inline constexpr char kHintNewPurpose[] = "new 用于程序生成新图像；读取并切割已有图像请改用 extract / erase / grid";
inline constexpr char kHintNewOutput[] = "new 输出单图，请用 --output <file> 指定生成图像的落盘路径";
inline constexpr char kLogImageGenerated[] = "已生成图像：";

// config 专属。
inline constexpr char kErrMissingLoad[] = "缺少 --load";
inline constexpr char kHintLoadFile[] = "请用 --load <file> 指定配置 JSON 文件";
inline constexpr char kErrLoadNoPath[] = "--load 缺少文件路径";
inline constexpr char kHintLoadPath[] = "请用 --load <file> 指定配置文件路径";
inline constexpr char kErrConfigMissing[] = "配置文件不存在或不可访问";
inline constexpr char kHintLoadReadable[] = "请检查 --load 的路径是否正确、文件是否存在且可读";
inline constexpr char kErrConfigParse[] = "配置文件解析失败";
inline constexpr char kDetParseFailedLead[] = "无法按配置 JSON schema 解析：";   // 后接配置路径。
inline constexpr char kHintConfigSchema[] = "请确认 JSON 语法正确且根为对象，字段含 source / preprocess / cut / select / order / emit";
inline constexpr char kErrAnnoCliParse[] = "命令行标注解析失败";
inline constexpr char kHintAnnoSyntax[] = "标注须为 SPEC §7 annotations 元素语法的 JSON 对象（--annotation）或根为标注数组的文件（--annotations）";
inline constexpr char kErrConfigNeedsInput[] = "执行配置需要输入图像";
inline constexpr char kHintConfigInput[] = "配置文件不含图像路径，请用 --input <file> 指定要切割的图像，或用 --size WxH（可配 --pattern）生成画布；亦可在配置中提供 generate / layers 段由其生成";
inline constexpr char kErrRearrangeNeedsL3[] = "合并重排仅 grid 支持";
inline constexpr char kHintRearrangeTier[] = "配置 emit.merge.layout=rearrange 要求 cut.tier=grid；其他层请改用 collapse 或分离导出";
inline constexpr char kErrRearrangeNeedsDims[] = "合并重排需要 cols 与 rows";
inline constexpr char kHintRearrangeColsRows[] = "请在配置 emit.merge 中同时给出正的 cols 和 rows（重排画布列/行数）";
inline constexpr char kErrConfigNeedsOutKey[] = "执行配置需要 ";           // 后接 --output / --output-dir 选项名。
inline constexpr char kHintConfigSeparate[] = "配置为分离导出，请用 --output-dir <dir> 指定输出文件夹";
inline constexpr char kHintConfigMerged[] = "配置为合并导出，请用 --output <file> 指定输出路径";

} // namespace idc::cli::texts::msg
} // namespace idc::cli::texts
