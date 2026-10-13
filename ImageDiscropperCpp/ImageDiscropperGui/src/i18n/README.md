# src/i18n

**目录作用**：`include/i18n` 下 GUI 门面头的实现与 GUI 英文译文表。语言决议收「需要 Qt locale 知识」的那段，
译文表按「menu/label」与「tooltip/status/msg」分两个 TU，各自一张 `constexpr` 表、经累加装配到 Core 的同一份
进程目录。

**分块依据**：门面实现（`gui_i18n.cpp`）与译文数据（`lang_en*.cpp`）分置；译文因 menu+label 与 tooltip+status+msg
体量相当且形态各异（后半含多行 `\n` 长正文），拆两半便于逐行与常量层对账（`installLanguage` 对同一语言**累加**，
两半落到同一目录）。

| 文件                   | 职责                                                                                                                                                                                                               |
|----------------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `gui_i18n.cpp`       | GUI 门面的装配实现：`setupFromPreference()`（`auto`/空 → `QLocale::system()` 的 BCP-47 名，其余直接用偏好值，交 `core::i18n::setLanguage` 归一，认不出自动回退源语言——GUI 侧不报错）+ `resolveForPreference()`（同决议链但不改状态，供外观对话框比对「已存偏好 vs 当前生效语言」）        |
| `lang_en.cpp`        | **GUI 英文译文表 · 前半**：`texts::menu`、`texts::label` 两组条目（与 `gui_texts_base.h` 同序）；并定义 `installCatalogs()`——先调 `core::i18n::installCoreCatalogs()` 装 Core 报因表，再装本层 menu/label 表，最后前向声明调用 `installDetailCatalog()` 装后半 |
| `lang_en_detail.cpp` | **GUI 英文译文表 · 后半**：`texts::tooltip`、`texts::status`、`texts::msg` 三组条目 + `installDetailCatalog()` 定义（`kEntriesEnDetail`）                                                                                          |

> **装配时机**：`app/main.cpp` 在 `QApplication` 构造后、顶层窗口构造前调 `installCatalogs()`，随后由
> `gui_i18n.cpp` 决议语言；装配一次，其后只读。热切换时于模态静止点再走一次语言决议 + `retranslateUi` 重放。

> **表住本层而非 Core**：GUI 的菜单 / 标签译文不该进纯逻辑静态库（CLI 与 demo 都不需要它，进了就是无谓膨胀）；
> 只有 Core 自身报因的表住 Core，由 `installCoreCatalogs()` 供三层共享。

**加一门语言**：新增 `lang_xx.cpp` + `lang_xx_detail.cpp`（照本对文件形状写两半表）+ 在 `installCatalogs()` /
`installDetailCatalog()` 各加一行 + 在 `core_i18n.h` 补语言码常量。重编即可，无构建期工具、无资源文件。

**改完怎么自检**：

1. **键逐字同名**：两张表的键必须分别与 `gui_texts_base.h`（menu/label）/ `gui_texts_detail.h`
   （tooltip/status/msg）的定义逐字同名；键写错不报错、只是永不命中（界面静默留中文）。
2. **助记符 / 占位符逐字保留**：`&`、`…`、`\n`、`%N` 编号在译文中不变；格式名 / §7 token / 语言名不译。
3. **构建 + 单测**：`cmake --build` 后跑 `ctest`；`test_i18n_catalog` 对账两半表与常量层（改名 / 删条目后必跑）。
