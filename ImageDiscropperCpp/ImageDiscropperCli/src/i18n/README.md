# src/i18n

**目录作用**：`include/i18n` 下 CLI 门面头的实现与 CLI 英文译文表。语言决议收「CLI 才有的来源优先级」，
译文表按「msg / help」分两个 TU，各自一张 `constexpr` 表、经累加装配到 Core 的同一份进程目录。

**分块依据**：门面实现（`cli_i18n.cpp`，含环境变量链）与译文数据（`lang_en*.cpp`）分置；译文因 msg 与 help
形态迥异（help 是多行长散文）且体量相当，拆两半便于逐行与常量层对账（`installLanguage` 对同一语言**累加**，
两半落到同一目录）。

| 文件                 | 职责                                                                                                                                                                                                        |
|--------------------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `cli_i18n.cpp`     | CLI 门面的语言决议实现：`applyLanguage()`（`--lang` 显式给了认不出 → `false` 由 `cli_app` 报因退码 1；环境变量 `IDC_LANG` > `LC_ALL` > `LANG` 认不出 → 静默回退源语言）+ `availableLanguageCodes()`（报因枚举可用码）。MSVC 下经 `_dupenv_s` 读环境变量并即时 `free` |
| `lang_en.cpp`      | **CLI 英文译文表 · msg 半**：`texts::msg` 的报错 / 提示 / 明细 / 日志条目；并定义 `installCatalogs()`——先调 `core::i18n::installCoreCatalogs()` 装 Core 报因表，再装本层 msg 表，最后前向声明调用 `installHelpCatalog()` 装 help 半                    |
| `lang_en_help.cpp` | **CLI 英文译文表 · help 半**：`texts::help` 的 `kDesc*` / `kLead*` / `kText*`（散文块整段一键）译文 + `installHelpCatalog()` 定义；`kUse*`（用法列）属脚本契约永不进表                                                                        |

> **必须在 `applyLanguage()` 之前装配**：`installCatalogs()` 在 `cliMain` 顶部走一次，表未登记时任何码都解析为
> 「未安装」。按调用生效：`cli_tests` 进程内多次调 `cliMain`，语言在每次调用开头按同一优先级重决议，互不污染。

> **为何 help 拆两 TU**：help 译文体量与 msg 相当且形态各异（长散文），独立成文件便于核对；跨 TU 装表靠
> `installCatalogs()` 内先声明后调用 `installHelpCatalog()`（同语言累加，无需头文件暴露）。

**加一门语言**：新增 `lang_xx.cpp` + `lang_xx_help.cpp`（照本对文件形状写两半表）+ 在 `installCatalogs()` /
`installHelpCatalog()` 各加一行 + 在 `core_i18n.h` 补语言码常量。重编即可，无构建期工具、无资源文件。

**改完怎么自检**：

1. **键逐字同名**：两张表的键必须分别与 `cli_texts_base.h::msg` / `cli_texts_help.h::help` 的定义逐字同名。
2. **散文块逐字节对拍**：`kText*` 译文的换行 / 缩进 / 示例与末尾换行都是 `--help` 输出的一部分，改后跑五个子命令
   与基线对拍。
3. **构建 + 单测**：CLI 不依赖 Qt，`cmake --build` 后跑 `ctest`（参数解析与集成用例）；`test_i18n_catalog` 对账
   两半表与常量层（改名 / 删条目后必跑）。
