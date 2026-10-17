# src/i18n

**目录作用**：`include/i18n` 下头文件的实现集合——i18n 内核的进程级语言状态与查表实现，
加上 Core 自己的英文译文表。内核与译文表**分开安置**：一个只做查表 / 语言决议（不含任何文案），
一个是纯条目表并导出装配入口。

**分块依据**：Core 是三层共用的静态库、零 Qt，故语言状态的落地与 Core 报因的译文都属本层职责；
按「实现 vs 数据」拆两文件。

| 文件              | 职责                                                                                                                                                                                                                                                                                                            |
|-----------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `core_i18n.cpp` | `core_i18n.h` 的实现：匿名命名空间内一个 Meyers 单例 `State`（可用语言清单 + 各语言 `std::map<string_view,string_view,less<>>` 目录 + 当前语言指针）。`installLanguage` 同语言**累加**（一层的表可拆多 TU、多层各自装配同一语言落到同一目录）、`resolveLanguage` 语言码归一（大小写不敏感 + 基础子标签匹配，`zh*` 归源语言）、`translate` 永不空永不崩的回退、`formatPattern` 的 `%N` 按编号替换、`availableLanguages` 动态汇出 |
| `lang_en.cpp`   | **Core 英文译文表**：`texts::msg` 的引擎失败报因、标注校验报因与图案显示名（`kEntriesCoreEn`），并定义 `installCoreCatalogs()` 把它登记给内核；源语言 zh-CN **不建表**，全部回退常量层原文                                                                                                                                                                            |

> **为何用 `std::map<...,std::less<>>` 而非 `unordered_map`**：`unordered_map` 无透明查找
> （`find(string_view)` 会先构造临时 `std::string`），千余条目下「每次查表一次堆分配」比 O(log n) 的短串
> 比较贵得多；`std::less<>` 透明比较器让 `find` 直接吃 `string_view`，零分配。键与值都是静态字面量的 view，
> 不持所有权。

> **谁装配本表**：GUI 的 `installCatalogs()` 与 CLI 的 `installCatalogs()` 各调用 `installCoreCatalogs()`
> 一次（GUI 弹窗与 CLI stderr 显示的是同一批 Core 报因，各存一份迟早漂移；`installLanguage` 累加故两次调用
> 落到同一目录，不冲突也不重复）。

**加一门语言**：新增 `src/i18n/lang_xx.cpp`（照 `lang_en.cpp` 形状写表）+ 在 `installCoreCatalogs()` 加一行 + 在
`core_i18n.h` 补语言码常量。重编即可，无构建期工具、无资源文件。

**改完怎么自检**：

1. **键逐字同名**：`lang_en.cpp` 的每条键必须与 `include/i18n/core_texts.h` 里 `msg` 组的定义逐字同名；
   键写错不报错、只是永不命中（静默留中文）。
2. **占位符编号不变**：句式里 `%1` / `%2` 逐字保留且编号不变（`formatPattern` 按编号取，语序可自由调整）。
3. **构建 + 单测**：Core 不依赖 Qt，`cmake --build` 后跑 `ctest`；`tests/test_i18n_catalog.cpp` 对账本表与
   常量层（改名 / 删条目后必跑）。
