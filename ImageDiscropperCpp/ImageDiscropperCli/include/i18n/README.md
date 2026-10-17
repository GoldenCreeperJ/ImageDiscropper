# include/i18n

**目录作用**：CLI 层的 i18n 门面与字符串常量层集合。语言状态与查表实现住在 Core（三层共用一份进程表），
本目录只负责「键的拼法 + CLI 特有的语言来源优先级」与 CLI 全部字符串常量。行为总览见顶层
[`../../README.md`](../../README.md)「三层共用的 i18n」。

**分块依据**：门面（`cli_i18n.h`）与常量层（`cli_texts*.h`）分置；常量层按「可译 / 契约 + 分组」
拆为 `cli_texts_base.h` + `cli_texts_help.h` 两半，`cli_texts.h` 作薄聚合转发头，消费方统一
`#include "i18n/cli_texts.h"`。

| 头文件                | 职责                                                                                                                                                                                                                                                  |
|--------------------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `cli_i18n.h`       | **i18n 门面**：`IDC_CTR(分组, 常量名)` → `std::string`（宏拼键 `分组::常量名`、同名中文常量作回退值）+ `IDC_CTRF(...)`（取译文后按编号填 `%N`）+ `installCatalogs()` 装配本层表 + `applyLanguage()` 决议语言（优先级 `--lang` > `IDC_LANG` > `LC_ALL` > `LANG` > 源语言）+ `availableLanguageCodes()` 供报因枚举 |
| `cli_texts.h`      | **薄聚合转发头**：仅 `#include` 下面两个半文件，是 CLI 全部字符串常量的统一入口                                                                                                                                                                                                  |
| `cli_texts_base.h` | **常量层 · 前半**：不可译契约组 `texts::opt`（长选项名）/ `texts::cmd`（子命令名）/ `texts::env`（环境变量名）+ 可译组 `texts::msg`（报错 / 提示 / 明细行 / 进度日志）                                                                                                                             |
| `cli_texts_help.h` | **常量层 · 后半**：`texts::help`（`--help` 散文层）——`kUse*`（用法列＝选项名 + 占位符，**脚本契约不译**）/ `kDesc*`（描述列）/ `kLead*`（小节引导语）/ `kText*`（`R"IDCH(...)"` 多行散文块，整段算一个键）                                                                                                  |

> **哪些进语言目录**：CLI 可译分组只有 `msg` 与 `help` 两个，它们经 `IDC_CTR` 进表；`opt` / `cmd` / `env`
> 与 `help::kUse*`（选项名 / 命令名 / 环境变量名 / 用法列前半）是**脚本契约**，一律保持裸常量原样、永不进表。

> **`cli_texts.h` 定位**：薄聚合转发头只缩短单文件、便于按「契约 / 可译 + 分组」核对，不改变对外契约——所有
> 消费方仍 `#include "i18n/cli_texts.h"` 并以 `texts::opt::kXxx` / `texts::help::kTextXxx` 等形态引用。

**常量层提取判据**（与 Core / GUI 一致，详见
[Core include/i18n/README.md](../../../ImageDiscropperCore/include/i18n/README.md)）：重复出现、人工可调的
默认值与边界、一切选项 / 命令标识串均提取；数学与换算常数、成对映射表内的 token 值（`--pattern` 的 `grid-lines`
等、`--sort` 的 `row-major` 等——映射表即唯一定义点）、帮助文本里的格式碎片（`"idc "`、`" → "`）不提取。
`--help` 散文块用 raw string **逐字搬移**，行首缩进、空行与末尾换行都是输出的一部分，一个散文块整段算一个键。

**改完怎么自检**（与 Core / GUI 同一套路）：

1. **逐字节比对 `--help`**：`texts::help` 内任何 `kText*` / `kUse*` / `kDesc*` 改动都会直接反映到输出，需跑完
   顶层 + 五个子命令 + `--version` 与基线逐字节对拍（改 `kUse*` 亦经编译期 max 联动列宽）。
2. **两组同改**：选项改名时 `texts::opt` 与 `texts::help::kUse*` 必须同改（同值不同角色，故不合并）。
3. **契约组不越界**：`opt` / `cmd` / `env` 与 `help::kUse*` 永不进语言表。
4. **译文表对账**：`test_i18n_catalog` 逐文件比对 `cli_texts_base.h` / `cli_texts_help.h` 与
   [`src/i18n/README.md`](../../src/i18n/README.md) 的两张英文表（孤儿键 / 重复键 / 跨层撞键 / 契约组越界均 fail）。