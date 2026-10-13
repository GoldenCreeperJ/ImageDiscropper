# include

**目录作用**：CLI 层的公共头文件集合。对下游（可执行 `idc` 与测试 `cli_tests`）暴露 CLI 的接口；
其中部分头直接引用 Core 的概念类型（`RectRegion` / `GridParams` / `ExportFormat` / `Color`…），
故 CMake 对 `image_discropper_core` 采用 PUBLIC 链接，使 Core 的 include 目录向下游传播。

**分块依据**：按职责单一拆分——「语法归类 / 值语义 / 错误 / 退出码 / 帮助 / 执行桥 / 命令声明」各自独立，
避免上帝头文件。所有头均为纯声明（`command_support.h` 为体量极小的 inline 助手）。

| 头文件                 | 职责                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
|---------------------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `commands.h`        | 各子命令入口 `cmdExtract/cmdErase/cmdGrid/cmdConfig/cmdNew` 与顶层 `cliMain` 声明（顶层编排实现见 `src/cli_app.cpp`）                                                                                                                                                                                                                                                                                                                                                                                                                                                                                |
| `arg_parser.h`      | `ArgParser`（token 语法归类）+ `GlobalOptions`（含 `bundleAssets`，对应 `--bundle-assets`；含 `lang`，对应 `--lang`）+ `parseGlobalOptions`                                                                                                                                                                                                                                                                                                                                                                                                                                                       |
| `value_parser.h`    | 「字符串 → Core 强类型」解析与格式校验（坐标 / 带 / 网格 / r,c / 画布 / 颜色 / 枚举 / 模板 / 质量 0~100）                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        |
| `command_support.h` | 跨命令复用的极薄 inline 助手：`fail` / `argError` / `makeJobOptions` / `applyExportOptions`（解析 `--format`/`--quality`/`--naming`）                                                                                                                                                                                                                                                                                                                                                                                                                                                           |
| `cli_error.h`       | 错误结构 + `makeError`（三段式格式）+ `reportError`（写 stderr）                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| `exit_code.h`       | 退出码枚举 `ExitCode` + `toInt`                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       |
| `job.h`             | CLI Core 执行桥：`JobOutput` / `JobOptions` / `loadInputImage` / `writeGeneratedImage`（new 专用落盘）/ `executeJob` / `buildLayerCanvas`（`layers` 段以底图 base 为基、逐层解码叠加 `file` 源图→`canvas::composeCanvas(layers,w,h,base)` 先铺全透明底、依底图 base→叠加层（按 zOrder）展平（画布无级背景），越界拒绝）/ `buildBaseCanvas`（§7 `generate` 段同构底图：Recipe 底按 source 尺寸现算、File 底按相对名解码注入后 `canvas::composeBase` 展平，越界名→退出码 3、尺寸非法→退出码 2）/ `bundleLayerAssets`（`--bundle-assets` 把 `file` 层源图按相对名打包进 `<配置名>.assets/`）/ `saveConfigToFile` / `finishJob`（后几者带可选 `AnnotationSet*` 参：先把标注烧进预处理产出的工作图 / 随 SPEC §7 `annotations` 段往返） |
| `generate_args.h`   | 命令行图像生成装配：`hasGenerateOptions` / `buildGeneratedImage`（`--size`/`--pattern`/`--bg`/… → Core `generate::makePattern`）/ `resolveInputImage`（统一 `--input` 读文件与生成画布两种输入来源；与 `annotation_args.h` 同构的“选项→调 Core”胶水）                                                                                                                                                                                                                                                                                                                                                                    |
| `annotation_args.h` | 命令行标注收集：`hasAnnotationOptions` / `collectAnnotations`（`--annotation` 内联对象 + `--annotations` 根数组文件，可重复、累积追加；解析委托 Core `parseAnnotationsFromJsonText`，CLI 不引 nlohmann/json）                                                                                                                                                                                                                                                                                                                                                                                                        |
| `cli_nums.h`        | **常量层 · 数字**：参数范围上下限 / 出厂默认值 / 帮助文本排版（各命令选项列宽），按来源模块分组（`nums::parser` / `job` / `help`），header-only                                                                                                                                                                                                                                                                                                                                                                                                                                                                              |
| `i18n/`（子目录）        | **常量层 · 字符串** `cli_texts.h`（薄聚合转发头，含拆分的 `cli_texts_base.h`〔opt/cmd/env 契约 + msg 可译〕/ `cli_texts_help.h`〔help 组：kUse* 契约不译 / kDesc* / kLead* / kText* 散文块〕）与 **i18n 门面** `cli_i18n.h`（`IDC_CTR`/`IDC_CTRF` + `installCatalogs()`/`applyLanguage()`/`availableLanguageCodes()`）已迁入 [`include/i18n/`](i18n/README.md)，逐文件职责见该目录说明                                                                                                                                                                                                                                                   |
| `help.h`            | 帮助 / 版本文本：`printTopHelp` / `printCommandHelp` / `printVersion`                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |

**常量层（`cli_nums.h` / `cli_texts.h`）**：集中魔数与硬编码字符串，按「数字 / 字符串」分文件、按来源模块分
`namespace`，且把**不可译契约与可译文案显式分组隔开**（i18n 已落地：`opt` / `cmd` / `env` 与 `help::kUse*`
整组保持裸常量原样，**只有 `msg` / `help` 经 `IDC_CTR` 进语言目录**）。提取判据与 Core /
GUI 常量层一致（见 [Core include/core/README.md](../../ImageDiscropperCore/include/core/README.md)「常量层」）：
重复出现、人工可调的默认值与边界、一切选项 / 命令标识串均提取；数学与换算常数、成对映射表内的 token 值
（`--pattern` 的 `grid-lines` / `noise` 等、`--sort` 的 `row-major` 等——映射表即唯一定义点）、帮助文本里的
格式碎片（`"idc "`、`" → "`、`"..）"`）、循环边界与自明临时值不提取。上游已有的护栏数值（Core
`generate::kMaxDimension` / `kMaxTotalPixels`）不在此复制，只在报因文本里引用其名。

条目类型统一 `inline constexpr`（数字用值类型，字符串一律 `char[]` UTF-8 字节串）；`--help` 的多行散文块用
raw string 逐字搬移，**行首缩进、空行与末尾换行都是输出的一部分**，措辞与提取时一字一致（术语清洗属另案）；
一个散文块整段算**一个键**（条目少而长），多行 `R"IDCH(...)"` 内的 `" → "` 等格式碎片仍不拆。

`texts::help` 内按形态分三组：`kUse*`（optionRow 的用法列＝选项名 + 占位符，前半须与 `texts::opt` 逐字一致，
改名两组同改）、`kDesc*` / `kLead*`（描述列与小节引导语）、`kText*`（散文块 / 小节标题 / 导语 / 尾巴）。
各命令 optionRow 的列宽不再是 `nums::help` 手设常量，而由 `src/help.cpp` 按各命令实际打印的不可译
用法列（`texts::help::kUse*`）串长于编译期取 max（`constexpr optWidth({...})`）——加/减选项自动跟进，无数手漂移。

**引用方式**：使用点以命名空间别名简写（`namespace opts = texts::opt;` / `msgs` / `cmds` / `hp`）。别名一律
**只落在 .cpp 或内部头**（`src/help.cpp`）内，不进公共头（`command_support.h` 写全限定名）——
以免污染库使用者的命名空间；别名须避开与就近形参 / 局部变量同名（`cmd` / `opt` 是常见重名，故用 `cmds` / `opts`）。

**依赖方向**：CLI 头单向依赖 Core 头（`core/*`、`engine/*`），Core 不反向依赖 CLI。第三方库不经这些头暴露。

**改完怎么自检**（与 Core / GUI 常量层同一套路）：

1. **逐字节比对 `--help`**：`texts::help` 内任何 `kText*` / `kUse*` / `kDesc*` 改动（改 `kUse*` 亦会经编译期 max 联动列宽）都会直接
   反映到输出，需跑完顶层 + 五个子命令 + `--version` 与基线逐字节对拍。
2. **两组同改**：选项改名时 `texts::opt` 与 `texts::help::kUse*` 必须同改（同值条目属不同角色，故不合并）。
3. **零回落 + 零孤儿**：`src/` 内不再手写选项名与报错串；未被引用的条目连注释一起删。
4. **构建 + 单测**：CLI 不依赖 Qt，`cmake --build` 后跑 `ctest`（参数解析与集成用例）。
5. **译文表对账**：`unit_tests` 内的 `test_i18n_catalog` 会比对 `cli_texts_base.h` / `cli_texts_help.h` 与
   `src/i18n/lang_en.cpp` / `src/i18n/lang_en_help.cpp` 的键集（孤儿键 /
   重复键 / 跨层撞键 / 契约组越界均 fail）；错键不报错只会永不命中，所以改名 / 删条目后必跑。
