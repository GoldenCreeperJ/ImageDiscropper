# include/i18n

**目录作用**：GUI 层的 i18n 基建头集合——`QString` 侧门面、热切换重放基建，加上 GUI 全部字符串常量层。
语言状态与查表实现住在 Core（三层共用一份进程表），本目录负责「键的拼法 + `QString` 适配 + 重放绑定」。
行为总览见顶层 [`../../README.md`](../../README.md)「三层共用的 i18n」。

**分块依据**：按「门面 / 重放基建 / 常量层」三类拆分。常量层因单文件过长（六分组 · 千余行），按
「可译 / 契约 + 分组」拆两半，`gui_texts.h` 转为薄聚合转发头，消费方引用点逐字不变（仅目录前缀由 `util/`
改为 `i18n/`）。

| 头文件                  | 职责                                                                                                                                                                                                                                                                                                      |
|----------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `gui_i18n.h`         | **i18n 门面**：`IDC_GTR(分组, 常量名)` → `QString`（就地取值）+ `IDC_GT(...)` → `TextRef`（清单表位：先存后转）+ `qs()`；`installCatalogs()` 装配本层表、`setupFromPreference()` 按偏好决议并切换语言（启动初值 + 热切换）、`resolveForPreference()` 解析「应为」语言码（不改状态，供对话框比对）                                                                                 |
| `retranslate.h`      | **热切换重放基建**：`Retranslator`（闭包登记表，`add` / `run`）+ 逐控件族绑定助手（`bindText` / `bindButton` / `bindAction` / `bindStatusTip` / `bindTitle` / `bindToolTip` / `bindMenuTitle` / `bindPlaceholder` / `bindSpecialValue` / `addItemsI18n` / `bindItemTexts` / `bindFormRow` / `bindTabText`），命名空间 `idc::gui::i18n` |
| `gui_texts.h`        | **薄聚合转发头**：仅 `#include` 下面两个半文件，是 GUI 全部字符串常量的统一入口                                                                                                                                                                                                                                                      |
| `gui_texts_base.h`   | **常量层 · 前半**：不可译契约组 `texts::ident`（QSettings 键 / MIME / objectName / 快捷键 / 模板 token 等跨版本契约串）+ 可译组 `texts::menu`（菜单 / 上下文菜单项）、`texts::label`（控件文字：标签 / 按钮 / 分组框 / 复选框 / 对话框标题与过滤器）                                                                                                                       |
| `gui_texts_detail.h` | **常量层 · 后半**：可译组 `texts::tooltip`（各面板 / 画布控件悬停说明）、`texts::status`（状态栏回报与实时回显）、`texts::msg`（弹窗正文与失败报因）                                                                                                                                                                                                   |

> **两条宏同源**：`IDC_GTR`（就地取 `QString`）与 `IDC_GT`（清单表位存 `TextRef`）共用同一套分组名与常量名，
> 键一律由常量名在预处理器里拼出（`#group "::" #name`），故对账测试只认 `分组::常量名` 一种键形态。下拉条目 /
> 工具按钮一类「先装进 constexpr 数组、循环体里才 `addItem`」的文案用 `IDC_GT`（零 Qt、字面类型、`constexpr`
> 不破），循环体调 `qs(ref)`。

> **`ident` 组永不进表**：QSettings 键名、objectName、`kLangAuto` 等是跨版本兼容契约，翻译即破坏用户已有偏好
> 与文件关联，一律保持 `QString::fromUtf8(texts::ident::kXxx)` 原样。可译组是 `menu` / `label` / `tooltip` /
> `status` / `msg` 五个。

> **热切换设计**：语言在运行期于**静止点**（模态外观对话框关闭后）切换，常驻窗口的构造期文案不会自行重译，
> 故绑定助手「先按当前语言设初值，再登记一条同逻辑重放闭包」；切语言后先设全局语言、再 `run()` 逐条按新语言原地
> 换文，**不销毁重建窗口**（`Document` / `DocHistory` 随窗口存活、UI 与业务态无损）。每个常驻类自持一份
> `Retranslator`，顶层按依赖序聚合调用（见 `MainWindow::retranslateUi`）。**成员声明顺序纪律**：宿主的
> `retranslator_` 必须声明在其所有被闭包捕获的控件成员**之后**（置于成员末尾），靠析构逆序保证「闭包销毁时其捕获
> 的控件仍存活」。

**改完怎么自检**（与 Core / CLI 同一套路）：

1. **零孤儿**：`gui_texts_base.h` / `gui_texts_detail.h` 每条可译 `kXxx` 都要能在 `src/` 内找到引用；找不到即
   该文案已删，条目连注释一起删。
2. **助记符与占位符逐字保留**：`&` 助记符、`…` 省略号、`%N` 编号在译文中字母 / 编号不变（语序可自由调整）。
3. **契约组不越界**：`ident` 组永不进语言表。
4. **新添常驻类接重放**：给窗口 / 面板新增可译文案时，改用 `retranslate.h` 的 `bind*` 助手而非裸 `setText`，
   否则该处文案在热切换后停留旧语言。
5. **译文表对账**：`test_i18n_catalog` 逐文件比对两半常量层与 [`src/i18n/README.md`](../src/i18n/README.md) 的
   两张英文表（孤儿键 / 重复键 / 跨层撞键 / 契约组越界均 fail，覆盖率只打印）。
