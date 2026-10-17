# include/util — 图像适配与预览工具对外声明

**目录作用**：GUI **工具层**的公共头，声明 `core::Image` ↔ Qt 图像类型适配、`geometry::Path`/`core::Color` ↔ Qt 绘图类型适配、大图降采样预览工具，以及**无状态渲染工具**（标注矢量绘制、导出输出预览缩略图）。
本目录只放**类声明边界**；实现与详尽说明见 [src/util/](../../src/util/README.md)。

**分块依据**：均为 GUI 侧的**视图关注点**工具，不含任何切割 / 几何 / 引擎逻辑。降采样**复用 Core
`core::Image::resized`**，不自造缩放。按「图像适配 / 几何·颜色适配 / 预览生成 / 标注绘制 / 输出预览渲染」关注点各成一文件。
其中渲染类工具（`annotation_qt_painter` / `output_preview_renderer`）只消费 Core 结果（`Annotation` / `Composition`）+ Qt 绘图，使上层只做编排。

| 文件                          | 声明                                                                 | 职责                                                                                                                                                                                                                                                                                                                           |
|-----------------------------|--------------------------------------------------------------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `image_qt_adapter.h`        | `toQImage` / `toPixmap`                                            | `core::Image` → `QImage` / `QPixmap`（按 `format()` 深拷贝像素，Core 缓冲生命周期独立于 Qt，避免悬垂引用）                                                                                                                                                                                                                                            |
| `annotation_qt_painter.h`   | `paintAnnotation`                                                  | 把单条 `annotation::Annotation` 矢量绘到 `QPainter`（描边/填充；文字绘字形）的**公共渲染函数**，供画布 `AnnotationItem::paint` 与 `output_preview_renderer` 的标注烘焙共用（消除重复）                                                                                                                                                                                   |
| `output_preview_renderer.h` | `bakeAnnotationsInto` / `composeOutputThumbnail`                   | 导出面板「输出图像预览」的**离屏渲染器**：把标注烘焙进源图副本、按 `engine::Composition` 把源图 blit 成降采样缩略图（不落盘、不跑 Core）                                                                                                                                                                                                                                      |
| `path_qt_adapter.h`         | `toQPainterPath` / `toQColor` / `toCoreColor`                      | `geometry::Path` → `QPainterPath`（逐段映射 moveTo/lineTo/quadTo/cubicTo/close，保留贝塞尔精度）、`core::Color` ↔ `QColor`（供标注矢量渲染与属性面板取色）                                                                                                                                                                                                  |
| `gui_nums.h`                | —（纯常量）                                                             | **常量层 · 数字**：布局尺寸与间距、控件触控下限、动画 / 节流时长、缩放步进、阈值、参数范围上下限、绘制工具与标注的出厂默认属性，按来源模块分组（`nums::canvas` / `panels` / `dialog` / `mobile` / `app` / `model`），header-only                                                                                                                                                                  |
| `i18n/`（子目录）                | —（本目录外）                                                            | **常量层 · 字符串** `gui_texts.h`（薄聚合转发头，含拆分的 `gui_texts_base.h`〔ident 契约 + menu/label〕/ `gui_texts_detail.h`〔tooltip/status/msg〕）、**i18n 门面** `gui_i18n.h`（`qs`/`TextRef`/`IDC_GTR`/`IDC_GT`）与**热切换重放基建** `retranslate.h` 见 [`include/i18n/`](../i18n/README.md)；门面实现与英文译文表在 [`src/i18n/`](../../src/i18n/README.md)，逐文件职责见该两目录说明 |
| `preview_scaler.h`          | `PreviewImage` / `makePreview`                                     | 大图降采样预览：最长边超阈值时调 Core `core::Image::resized(Nearest)`，回传预览副本 + 预览→原图放大系数                                                                                                                                                                                                                                                     |
| `scrollable_dialog.h`       | `wrapScrollable` / `limitDialogToScreen` / `showScrollableMessage` | **小屏 / 移动端对话框通用件**（归 common，桌面大屏等效无约束、行为不变）：`wrapScrollable`（主体装纵向可滑 `QScrollArea` + 换装触控滚动，复用 `mobile/touch_scroll`）、`limitDialogToScreen`（按屏幕可用几何钳对话框最大宽 / 高 + 首显示收尺寸 + 旋转重钳不重建）、`showScrollableMessage`（使用说明 / 关于长文本可滚动呈现）；根治小屏 / 横屏 / 长文本时「显示不全 / 无法关闭」                                                                  |
| `theme.h`                   | `theme::Mode` / `apply`                                            | 全局应用外壳控件**深浅色主题**纯函数集（`idc::gui::theme`、无单例、mode 存 UiPrefs `ui/themeMode`）：`Mode{System/Light/Dark}` ↔ 串 `fromString`/`toString`、`resolveDark`（System 查平台配色、Unknown 回退 Window 明度）、`buildPalette`、`apply`（强制 Fusion + 灌 QPalette + 递归 re-polish）；与 Palette 画布六色正交、画布色不受主题影响                                                   |

> 命名空间统一 `idc::gui`。场景坐标统一为原图像素坐标：底图 pixmap 用 `scaleX/scaleY` 变换铺回原图尺寸（见 canvas/）。

## 常量层（`gui_nums.h` · 本目录 / `gui_texts.h` · [`i18n/`](../i18n/README.md)）

**为何存在**：呈现参数散在各面板时调一处看不全影响；文案不集中则无法接翻译。两文件按「数字 / 字符串」分文件、
按来源模块分 `namespace`，并把**不可译标识与可译文案显式分组隔开**。

**提取判据**（与 Core / CLI 常量层一致，见 [Core include/core/README.md](../../../ImageDiscropperCore/include/core/README.md)「常量层」）：
重复出现、人工可调的尺寸 / 时长 / 阈值 / 范围上下限 / 默认值、一切 Settings 键与文案均提取；数学与换算常数、
图层 z 序（已集中到 `include/canvas/items/z_order.h`）、Qt 枚举、只出现一次且自明的临时值、`tests/` 期望值不提取。

**口径警告**：数字名必须带口径后缀 —— `…ScreenPx`（屏幕像素，与缩放无关）与 `…ScenePx`（场景 / 原图像素，
随缩放线性放大）**不可混用**；历史缺陷正是把屏幕口径的量去夹场景口径的边界。

**类型**：条目一律 `inline constexpr`；字符串存 `char[]`（UTF-8 字节串，MSVC 已 `/utf-8`）。不用 `inline const QString`
全局对象（避开静态初始化顺序与启动开销）。

**使用点写法**：可译组写 `IDC_GTR(menu, kMenuFileOpen)`（清单表位写 `IDC_GT(...)`），**ident 组保持**
`QString::fromUtf8(texts::ident::kXxx)` 不动。未接门面的可译组引用会在 `test_i18n_catalog` 里表现为孤儿键。

**i18n 机制**：自研目录，**键 = `分组::常量名`**，常量层中文即源语言与回退值；英文表编译期内嵌
在 `src/i18n/lang_en.cpp`（menu/label）与 `src/i18n/lang_en_detail.cpp`（tooltip/status/msg）。**不引 Qt Linguist / `tr()` / `.ts` / `.qm`**：lupdate 抽不到常量层，其 `.qm` 又是
Qt6 内部二进制格式（零 Qt 的 Core / CLI 读不了）；三层共用一张进程表胜过三套工具链各存一份。`ident` 与
§7 token **永不进表**；缺译文自动回退源语言，故译文可逐批改。语言在外观对话框内**即时热切换**（`setupFromPreference` 切全局语言 + `retranslateUi` 原地重放，不重启）。
加一门语言 = 新增一份 `lang_xx.cpp` + 在 `installCatalogs()` 登记一行 + `core_i18n.h` 补一个语言码。

**`gui_nums.h` 零 Qt 依赖**（承重约束，勿破）：`include/model/annotation_editor.h` 会被 Core 的 `unit_tests` 目标
直接编译，而该目标不挂 Qt include 路径 ⇒ `gui_nums.h` 只允许 `#include "core/core_nums.h"`，**不得引任何 Qt 头**；
条目类型只用 `int` / `double` / `bool` / `long long`（Qt 侧使用点自行隐式转 `qreal` / `qint64`，与现状一致）。
上游真相源（`generate::kMaxDimension`、`core::nums::{engine,annotation,generate}`）**只转引、不复制数值**；
同值不同义允许各立一名（按语义命名优先于去重）。

**改完怎么自检**（与 Core / CLI 常量层同一套路）：

1. **零回落**：生产代码内 `QStringLiteral("<中文>` 与 `fromUtf8("<中文>` 两条 grep 应恒 0 命中；仍就地的裸串只允许
   豁免项（`" px"` / `" %"` / `" ×"` 等单位后缀、`setStyleSheet` 样式串、格式名与 R/G/B 通道记号）。
2. **零孤儿**：`gui_texts.h` / `gui_nums.h` 每条都需能在 `src/` 内找到引用；找不到即该文案/参数已删，条目连注释一起删。
3. **同值不重复**：同值同角色只留一条并在行尾列全落点；同字不同角色各立一条并互相点名对家（否则改一处菜单就漏改另一处）。
4. **构建 + 测试**：桌面与移动两个目标都要过（`idc_gui` 与 `idc_gui_mobile`，仅改文案时也需双跑，因为两个目标共用本层），
   再跑 `ctest`；MSVC 已开 `/utf-8`，若文案出现乱码先查编译选项而非源文件。
5. **译文表对账**：改名 / 删条目后跑 `unit_tests`，`test_i18n_catalog` 会比对常量层与 `lang_en.cpp` 的键集，
   孤儿键 / 重复键 / 跨 GUI-CLI-Core 的同名键 / 契约组（ident）越界均直接 fail，并打印各组覆盖率。
