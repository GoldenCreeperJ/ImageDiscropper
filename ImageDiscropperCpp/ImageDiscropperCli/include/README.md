# include

**目录作用**：CLI 层的公共头文件集合。对下游（可执行 `idc` 与测试 `cli_tests`）暴露 CLI 的接口；
其中部分头直接引用 Core 的概念类型（`RectRegion` / `GridParams` / `ExportFormat` / `Color`…），
故 CMake 对 `image_discropper_core` 采用 PUBLIC 链接，使 Core 的 include 目录向下游传播。

**分块依据**：按职责单一拆分——「语法归类 / 值语义 / 错误 / 退出码 / 帮助 / 执行桥 / 命令声明」各自独立，
避免上帝头文件。所有头均为纯声明（`command_support.h` 为体量极小的 inline 助手）。

| 头文件                 | 职责                                                                                                           |
|---------------------|--------------------------------------------------------------------------------------------------------------|
| `commands.h`        | 各子命令入口 `cmdExtract/cmdErase/cmdGrid/cmdConfig` 与顶层 `cliMain` 声明（顶层编排实现见 `src/cli_app.cpp`）                   |
| `arg_parser.h`      | `ArgParser`（token 语法归类）+ `GlobalOptions` + `parseGlobalOptions`                                              |
| `value_parser.h`    | 「字符串 → Core 强类型」解析与格式校验（坐标 / 带 / 网格 / r,c / 画布 / 颜色 / 枚举 / 模板）                                               |
| `command_support.h` | 跨命令复用的极薄 inline 助手：`fail` / `argError` / `makeJobOptions` / `applyFormatNaming`                              |
| `cli_error.h`       | 错误结构 + `makeError`（三段式格式）+ `reportError`（写 stderr）                                                           |
| `exit_code.h`       | 退出码枚举 `ExitCode` + `toInt`                                                                                   |
| `job.h`             | CLI↔Core 执行桥：`JobOutput` / `JobOptions` / `loadInputImage` / `executeJob` / `saveConfigToFile` / `finishJob` |
| `help.h`            | 帮助 / 版本文本：`printTopHelp` / `printCommandHelp` / `printVersion`                                               |

**依赖方向**：CLI 头单向依赖 Core 头（`core/*`、`engine/*`），Core 不反向依赖 CLI。第三方库不经这些头暴露。
