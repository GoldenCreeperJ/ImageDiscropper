# ImageDiscropperGui — Qt 6 图形前端

本目录是 ImageDiscropper 的 **GUI 层**（可执行目标 `idc_gui`），基于 **Qt 6.x + Widgets + QGraphicsView**。
GUI 是 Core（`image_discropper_core` 静态库）的**纯消费者**：只采集交互、把交互翻译为 Core 参数、
调用 Core、渲染 Core 的结果；**不实现任何切割 / 几何 / 排序 / 极性 / 导出逻辑**。

> 命名空间统一为 `idc::gui`。本文件是 GUI 的总览与决策记录：最终用户读「用户操作速览」，维护者读
> 「关键设计决策」与「扩展指引」。**各模块的实现细节以 `include/<模块>/README.md`（声明边界）与
> `src/<模块>/README.md`（实现说明）为准**，本文件只保留架构边界，不重复逐文件细节。
> 功能规格（唯一行为基线）见 [`../SPEC.md`](../../SPEC.md)，贡献约定见根 [`CONTRIBUTING.md`](../../CONTRIBUTING.md)。

## 职责与目录

采用与 Core / CLI 一致的 **include/src 分离**：头文件与其声明边界 `README.md` 在 `include/<模块>/`，
实现与其实现说明 `README.md` 在 `src/<模块>/`。构建包含根为 `include`，故源码内以 `"canvas/…"`、`"model/…"` 形式互相引用。

| 模块       | 头文件               | 实现 + README                         | 职责                                                                                                                                                                      |
|----------|-------------------|-------------------------------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `i18n`   | `include/i18n/`   | [src/i18n/](src/i18n/README.md)     | i18n 门面（`IDC_GTR` / `IDC_GT` / `qs`）、热切换重放基建（`retranslate.h`）、字符串常量层、英文译文表                                                                                              |
| `util`   | `include/util/`   | [src/util/](src/util/README.md)     | `core::Image` ↔ Qt 图像/绘图类型适配、降采样预览、标注矢量绘制与导出预览渲染（视图关注点）                                                                                                                 |
| `model`  | `include/model/`  | [src/model/](src/model/README.md)   | 会话状态单一真相源 `Document`、唯一触碰切割引擎的 `EngineBridge`、唯一驱动 Core 标注的 `AnnotationBridge`                                                                                          |
| `canvas` | `include/canvas/` | [src/canvas/](src/canvas/README.md) | QGraphicsView/Scene 画布：图层化渲染底图/遮罩/切割线/选区/标注矢量叠加 + 交互（输入适配器分层：桌面 / 触控）                                                                                                   |
| `panels` | `include/panels/` | [src/panels/](src/panels/README.md) | 左侧模式/极性/标注工具/图层面板，右侧图片编辑/预处理/参数/导出/标注属性面板；均只发意图信号或读写 `Document`，不含业务逻辑                                                                                                  |
| `app`    | `include/app/`    | [src/app/](src/app/README.md)       | 主窗口装配壳 `MainWindow` + 八个控制器（预览/预处理/标注/历史/画布交互/会话/导出/配置）+ 键表分派 `KeyRouter` + 状态栏组件 + 装配建造者 `MainWindowUi`、程序入口                                                           |
| `mobile` | `include/mobile/` | [src/mobile/](src/mobile/README.md) | 移动端形态（与 app 并列）：`MobileShell` 继承 `MainWindow` 换装骨架（画布为主 + 底部工具条/抽屉）+ 无键盘补偿控件；复用既有面板/画布/模型，零复刻编排逻辑；桌面作为独立产物 `idc_gui_mobile` 与 `idc_gui` 并列输出（CMake 开关 `IDC_GUI_MOBILE`） |

**依赖方向**：`app → panels/canvas/model → util → Core`（`mobile` 继承 `app`，同向）。`model` 是唯一触碰 Core 引擎/标注的层，
面板与画布只读写 `Document`/桥，不各自持有真相（降低耦合）。

> **唯一例外（已登记，勿扩大）**：`canvas → app` 存在一条**单向只读**箭头——渲染层在**绘制期 / 构造期**直接读 `app` 的两个偏好单例
> [`Palette`](include/app/palette.h)（六项语义色）与 [`CanvasPrefs`](include/app/canvas_prefs.h)（吸附/手柄/余量等交互偏好），
> 以省去每帧注色管道（见 `palette.h` 头注"渲染图元绘制期直接取色"）。持有该箭头的**恒为** 5 个文件：`canvas/canvas_view.cpp`
> （Palette + CanvasPrefs：重设背景刷 / 联动缩放步进与滚动余量 / 回设吸附）、`canvas/items/selection_rect_item.cpp`
> （Palette 绘制期取选区 / 切割线色 + CanvasPrefs 取 `snapEnabled`）、`canvas/items/annotation_item.cpp`（Palette 绘制期取标注色）、
> `canvas/items/grid_layer.cpp` 与 `canvas/items/mask_layer.cpp`（后两者仅 Palette，绘制期取网格 / 遮罩色）。其余图元
> （如 `cell_picker_item`、`image_layer_item`）**不**直连偏好单例，色值经 `CanvasScene`/构造 `setXxx` 参数下传。此为刻意取舍、非疏漏：**禁止**沿此方向
> 新增任何反向依赖（尤其不得让 `canvas` 反向 include `app` 的装配/窗口头，也不得让偏好单例回引画布）；新的取色/取偏好需求一律经
> 图元构造/`setXxx` 参数下传或 `changed()` 信号联动，不得直接抓 `app` 对象。

## 依赖与数据流（一条主线）

GUI 只在 `model` 层三处触碰 Core：`EngineBridge`（切割 / 预处理 / 生成 / 导出 / 配置存取）、`AnnotationBridge`
（标注会话，驱动 Core `AnnotationEditor`）、`ConfigCodec`（§7 JSON 往返）。面板 / 画布 / 控制器都不直接调 Core。
具体调用的 Core 接口签名以 Core 各头文件与 [Core README](../ImageDiscropperCore/README.md) 为准，本层不复述。

```
用户交互（画布拖拽 / 面板输入 / 快捷键）
        │
        ▼
   Document（状态：模式 / 极性 / 选区 / 导出参数）── 组装 EngineConfig
        │  changed() / imageChanged() / layersChanged()
        ▼
   EngineBridge（切割引擎唯一入口）── runPreviewGeometry / runEngine / generateCutLines / exportImage
        │  EngineResult{ok,error,kept,composition,collapsible} / CutLineSet
        ▼
   CanvasScene（渲染）── 底图 + 红/绿遮罩 + 橙色选区框（含贯穿切割线延伸、可直接拖边）
        │
        ▼
   MainWindow（状态栏 / 面板提示回显）
```

`MainWindow` 只做「装配 + 编排 + 转发」，编排按域下沉到八个控制器（连接住在属主旁）；装配顺序、各控制器职责与信号接线见
[src/app/README.md](src/app/README.md)。

## 用户操作速览

### 启动

- 直接运行 `idc_gui`；或带图启动：`idc_gui path/to/image.png`（首个位置参数为图像路径时自动打开）。
- 首次进入默认 **L1 标准提取模式**。
- **Windows 运行提示**：若启动报缺 `qwindowsd.dll` 等 Qt 平台插件，需把 vcpkg 安装的 Qt
  `bin` 目录（含 `platforms/`）加入 `PATH`，或用 `windeployqt` 部署插件目录。

### 界面概览

主窗口采用「菜单栏 + 工具栏 + 左中右三栏 + 状态栏」的经典图像工具布局（`QSplitter` 水平三分）：

```
┌───────────────────────────────────────────────────────────────┐
│ 菜单栏：文件 / 编辑 / 模式 / 预处理 / 图片 / 标注 / 视图 / 帮助    │
├───────────────────────────────────────────────────────────────┤
│ 工具栏：新建 打开 导出 加载配置 保存配置 | 撤销 重做 | 放大 缩小 适应 | 重置全部│
├──────────┬────────────────────────────────┬───────────────────┤
│ 左侧面板  │         中央画布                │   右侧面板         │
│ (约330px)│      (QGraphicsView)           │  (约375px, 折叠式) │
│ ·模式切换 │                                │  ·图片（图层编辑）  │
│ ·极性开关 │   底图 + 遮罩 + 切割线 +        │  ·预处理           │
│ ·标注工具 │   选区 + 网格 + 标注 + 编号      │  ·标注属性         │
│ ·图层面板 │                                │  ·参数             │
│          │                                │  ·导出             │
├──────────┴────────────────────────────────┴───────────────────┤
│ 状态栏：缩放 | 坐标 | 像素RGB | 提示信息（模式名并入提示线）        │
└───────────────────────────────────────────────────────────────┘
```

- **左侧面板**（可滚动，三段竖排）：`LeftPanel`（模式切换 L1/L2/L3 + 极性开关 保留/删除）、`ModeToolPanel`（标注工具）、`LayerPanel`（图层显隐 + 图片图层行）。
- **中央画布**：可缩放、可平移，占据最大空间。叠加图层的 z 序见「图层 z 序」。
- **右侧面板**（`AccordionPanel` 折叠式，5 节可同开 + 整栏滚动，按代码装配序）：**图片页**（`LayerEditPanel` 图层编辑）、**预处理页**（`PreprocessPanel` 声明式队列编辑器）、**标注页**（`AnnotationPropPanel` 标注属性）、**参数页**（`ParamPanel`，随模式变化）、**导出页**（`ExportPanel`）。移动端抽屉为 `QStackedWidget` 页区 + 横向可滑按钮条选择式（节名同源，见 `label::kSec*`）。
- **状态栏**（`StatusBar` 四段，左→右）：缩放倍数、光标坐标、光标处像素 RGB、提示/错误信息（当前模式名并入提示线、不占独立段；错误为非模态，限时显示，不打断操作）。

三栏响应式规则、各面板控件构成见 [src/panels/README.md](src/panels/README.md) 与 [src/app/README.md](src/app/README.md)。

### 画布配色与交互偏好（速览）

画布六项核心语义色（保留/删除遮罩、选区/切割线、标注、网格线、画布背景）及其填充 alpha、以及手柄/命中基准、边缘滚动余量、
缩放步进与区间、新建默认尺寸等交互数值，均为**纯呈现偏好**，在 **`视图 → 外观/交互…`**（移动端 `更多 → 外观/交互…`，同一
`AppearanceDialog`）集中配置，经 `Palette` / `CanvasPrefs` 单例持有、`QSettings` **应用级**持久化（跨会话记住，**不进 §7 配置、
不进撤销栈**），改动即时生效并联动重绘；「恢复默认」回退配色与交互两单例（不重置语言）。**唯独选区吸附不在对话框**，而是
`视图 → 选区吸附`（移动端 `更多 → 选区吸附`）一项可勾选运行时开关（避免同一开关双 UI 同步）。

- 出厂默认色值、绘制期取色口径见 [src/canvas/README.md](src/canvas/README.md) 与代码注释（`palette.h` / `mask_layer` / `grid_layer` / `selection_rect_item` / `annotation_item`）；
- 手柄/余量/缩放的具体数值语义与联动链见 [src/canvas/README.md](src/canvas/README.md)「交互」段与 `canvas_prefs.h`；SPEC §8.2 / §8.3 为行为基线。
- 对话框为 `QTabWidget` 分三页：**配色**（六色 + 三 alpha + 深浅色主题）、**其他**（交互数值 + 应用语言 + 默认输出文件夹〔仅桌面〕）、**快捷键**（仅桌面 `shortcutsEnabled`，移动端关此页）——驱动 `Palette` / `CanvasPrefs` / `UiPrefs` / `KeyBindings` 四单例；深浅色主题（应用外壳明/暗/跟随系统）与画布六色 `Palette` **正交**。构成与持久化组（`palette/`、`canvas/`、`ui/`、`shortcuts/`）见 [src/app/README.md](src/app/README.md)。

### 会话记忆与杂项偏好（速览）

跨会话应用级记忆 / 低频交互开关经 `UiPrefs` 单例持有、`QSettings` `ui/` 组**应用级**持久化（与配色 `palette/`、画布交互 `canvas/`
分家，三组互不耦合；不进 §7、不进撤销栈；与 CLI 无感——`ui/language` 只供 GUI 决议界面语言，CLI 走自己的 `--lang` / 环境变量链，
互不读写）。`UiPrefs` 非 QObject、无即时联动消费者（最近文件菜单经 `aboutToShow` 懒重建读取、导出确认在 `onExport` 调用点即时读取）：

| 项           | 桌面入口 / 移动入口                                                 | 行为                                                                                                                                                              |
|-------------|-------------------------------------------------------------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 最近打开文件 MRU  | `文件 → 最近打开`（末项清除）/ 移动无（本地固定目录不入 MRU）                        | 载入成功后本地可再开路径自动入列（有序、上限 8、重复前移；`content://` 不入）                                                                                                                  |
| 上次打开 / 导出目录 | 相应文件对话框起始目录                                                 | 选定后回写，下次从此起步（与 `Document` 本次会话输出目录正交）                                                                                                                           |
| 导出重排前二次确认   | `文件 → 导出设置 → 重排前二次确认` / `更多 → 重排前二次确认`（均可勾选，默认开）            | 开：遇合并重排参数风险弹模态确认；关：跳过直接导出（内联红字警告恒显示，不受此开关影响）                                                                                                                    |
| 界面语言        | `视图 → 外观/交互…` 「其他」页 / `更多 → 外观/交互…` 「其他」页（同一对话框，语言控件无移动端门控） | **即时热切换**：选定即切全局语言 + 原地重放各窗文案（`MainWindow::retranslateUi`，不销毁重建窗口）；`auto` 取系统 locale 前缀映射，未知写法回退源语言 zh-CN                                                       |
| 深浅色主题       | `视图 → 外观/交互…` 配色页 / `更多 → 外观/交互…` 配色页（同一对话框）                | 下拉选 跟随系统/浅色/深色，写 `ui/themeMode`，选中即 `theme::apply`（强制 Fusion + 灌 `QPalette` + 递归 re-polish 全部 chrome）即时生效；与画布六色 `Palette` 正交，跟随系统经 OS `colorSchemeChanged` 自动重放 |
| 默认输出文件夹     | `视图 → 外观/交互…` 「其他」页（仅桌面）/ 移动无                               | 手输或「浏览…」选定即写 `ui/defaultExportDir`，作导出对话框起始目录种子（有效目录时优先，否则回退上次使用目录；与上次目录 / MRU 正交）                                                                              |

控件字段名与持久化细节见 [src/app/README.md](src/app/README.md)。

### 三种模式操作示例

| 模式     | 名称   | 定位        | 适用场景                            |
|--------|------|-----------|---------------------------------|
| **L1** | 标准提取 | 默认，降低上手门槛 | 「我只想要这一块」——矩形/横带/竖带提取           |
| **L2** | 反向剔除 | 产品核心特色    | 「我要挖掉这一块，留下其余」——十字带剔除、多矩形并集剔除   |
| **L3** | 网格分割 | 高级编排      | 「把图切成网格，挑选单元并按序重拼」——图集/九宫格/切片重排 |

**L1 标准提取示例**（输入：一张 1920×1080 的照片）：

1. 文件 → 打开图像（`Ctrl+O`）。
2. 左侧模式选 **标准提取 (L1)**（或按 `1`）。
3. 右侧参数页选形状：**矩形**（或横带/竖带），可直接填 `x1,y1,x2,y2`，或在画布上拖拽框选。
4. 拖动选区四角手柄调整大小；方向键微调（±1px，`Shift`+方向键 ±10px）；靠近图像边缘/中心会自动吸附。
5. 画布上绿色区即保留内容。
6. 右侧导出页选「分离导出」+ 目标目录 + 格式（PNG），点导出（`Ctrl+S`）得到裁剪后的图。

**L2 反向剔除示例**（输入：一张需要挖掉中部主体、保留四角的图）：

1. 打开图像，左侧模式选 **反向剔除 (L2)**（或按 `2`）。
2. 右侧参数页选子功能：**十字**（或横线/竖线/多矩形）。
3. 在画布拖拽出要剔除的矩形——橙色切割线**贯穿全图**，被删的十字带显示红色遮罩，保留的四角显示绿色遮罩。
4. 用 **极性开关**（`K`/`R`）在 keep/remove 间切换，遮罩颜色与位置即时更新。
5. 需剔除多个区域时选「多矩形」，逐个框选；删除区域为所有十字带的**并集**。
6. 若想把当前矩形改成网格编辑，点参数页的「**转为网格模式编辑…**」。
7. 导出页选「合并坍缩」（剩余单元紧贴拼接）或「分离导出」，导出即得剔除后的结果。

**L3 网格分割示例**（输入：一张图集，希望按网格切片、挑选部分单元并重排）：

1. 打开图像，左侧模式选 **网格分割 (L3)**（或按 `3`）。
2. 右侧参数页为横、纵两轴各选一个「**定义方式**」并填对应参数——**周期**（基准点 `(x0, y0)` + 单元尺寸 `cw/ch`，或在画布上直接**拖网格线层的两个基准手柄**：基准点手柄平移相位、单元尺寸手柄改 cw/ch，释放时一次性写回）/ **分割数**（份数，基准点恒 0、尽量均分、末份吸收取整误差）/ **比例**（权重，`1,2,1` 与 `25%,50%,25%` 等价，只取相对比例）；两轴可各用各的（如横向 3 等分 + 纵向周期 150px）。有周期轴时才需选余量策略（丢弃/保留部分/填充），两轴皆非周期时该项置灰（恰好铺满、无残缺单元）；非周期轴的手柄分量不参与拖动，两轴皆非周期时手柄整体隐藏；网格线自动铺满全图，只读栏显示派生的行×列规模。
3. 用**选择集**：单击选单元、画布框选、或「全选/反选/清空」。
4. 设**排序**：行优先(row-major) / 列优先(column-major) / 自定义(custom)，可叠加 reverse（倒序）、snake（蛇形）；自定义序下可在画布拖拽单元调序，每个单元左上角显示序号角标。
5. 导出页选「合并重排」，设置列/行（或点「按格数自动」依保留块数开方）、单元宽/高、填充顺序（填充色行恒可见，Pad 补白与有损压平共用）；参数不足时面板给出红字警告，导出前二次确认。
6. 导出页点导出，得到按序重排拼合的新画布图像。

### 标注工具

标注是**独立矢量图层**，叠加在底图之上、**不改底图像素**；只有导出时勾选「烧录」才会合成进像素。

**添加标注**：在左侧 **标注工具面板**（`ModeToolPanel`）选工具后在画布绘制——两点形状（矩形 / 正方形 / 圆 / 椭圆 / 各类三角形）按下拖拽实时预览、松手提交；
多线段 / 画笔连续单击追加顶点 / 按住拖动绘制自由路径、按 **Esc** 或切换工具收笔；文字单击落点弹输入框填写。完整工具清单与绘制方式见
[src/panels/README.md](src/panels/README.md)「mode_tool_panel」。

> **无独立「圆角矩形」工具**：转角圆角是**标注属性页的「圆角半径」控件**（0＝直角），作用于多边形顶点（直线 / 圆 / 椭圆 / 文字无转角，控件自动置灰）。

**编辑标注属性**：选中标注后右侧 **标注页** 设置边框色 / 填充色（与边框色独立）/ 粗细 / 是否填充 / 圆角半径 / 文字 / 字号，
以及 **OBB 变换**（带符号缩放 X/Y、旋转角度，改动即生效、无「应用」按钮）；填充形状会同时按边框色描边，与画布预览一致。
画布上选中标注出现 **8 个缩放手柄 + 1 个旋转手柄**（定向包围盒 OBB），拖拽即做缩放/拉伸/旋转/翻转（拖手柄越过对边＝翻转）。

**叠放次序**：标注以矢量图层列表叠放，每条 `Annotation` 携带显式 `zOrder`（恒等于列表下标，越大越靠上），命中检测与烧录都以此为准。
新标注默认叠在最上层；**上移一层 / 下移一层 / 置于顶层 / 置于底层**由标注属性页的「叠放次序」区调整（仅选中标注时可用、不入菜单、无快捷键），
委托 Core `moveAnnotation` 就地重排并规范化 `zOrder`，改序即时反映到画布叠放、命中优先与（勾选烧录时的）导出预览，并随 §7 `annotations` 段往返。

### 图层与烧录

左侧 **图层面板** 提供显示叠加开关（按画布 z 序；七项均**仅切换画布预览可见性与交互性、不影响导出**——「图片图层」为栈级整体开关，
关时所有图片层退出预览叠加、落位框一并隐藏，导出是否并入叠加层由导出面板「烧录图片图层」开关独立决定）。7 项开关（底图 / 图片图层 / 标注 / 遮罩 / 网格线与切割线（同一开关：L3 为灰色网格线、L1/L2 为贯穿切割线，两模式互斥）/ 选取边框 / 单元编号）
的具体语义与交互门控（「隐藏图层＝不可交互」）见 [src/panels/README.md](src/panels/README.md)「layer_panel」与
[src/canvas/README.md](src/canvas/README.md)。**图片图层/标注图层隐藏即整域禁用**：对应画布模式（图片拾取 / 标注选取·绘制）、右侧属性页与整域菜单（图片 / 标注）一并置灰，当前模式被禁时回落框选。

**烧录开关**位于右侧 **导出页**（「导出时烧录标注」）：勾选后导出时把标注按矢量合成进像素，标注**随像素一起被切割**落位到各输出块。

### 预处理（预处理页：声明式队列编辑器）

预处理在**切割之前**执行，是一串**有序步骤（队列）**而非即时动作。右侧 **预处理页**（`PreprocessPanel`）编排队列：轻操作分
「几何操作」「色彩操作」两个独立列表呈现（底层 `steps` 恒为「全部几何步在前 + 全部色彩步在后」，提交时按此合并），各自带类型选择 +
参数区与步骤列表，可独立「添加 / 更新所选 / 删除 / 上移 / 下移」；顶部「画布尺寸」为独立常驻区（不入队列）；底部「重置预处理」清空整条队列。
**原图始终保留**，工作图 = 队列对原图的重放结果；重放以 `base_`（最后一个 resize 及其之前产物）为缓存，编辑 resize 之后的轻操作只
在 `base_` 上重放轻后缀（免重复重采样），故 resize 永远以原图分辨率为基准、不累积误差。步骤类型（旋转 / 翻转 / 按比例缩放 / 目标尺寸 / 黑白 /
反色 / 色道分离 / 重置）与控件构成详见 [src/panels/README.md](src/panels/README.md)「preprocess_panel」与
[src/app/README.md](src/app/README.md)「预处理」。执行层融合（`applyExec`）与逐像素恒等口径见 Core。

> **几何签名尺寸门**：队列改动后用 `PreprocessPipeline::geometryOutput(w,h)` **纯元数据推演**输出尺寸与净旋转象限（不碰像素）；
> 仅当**尺寸或净旋转象限变化**时才清除失效框选（坐标基于旧几何）；标注、图片图层与色彩预处理**恒保留**。大图缩放/重采样期间弹「处理中」进度对话框（不可取消）。
>
> **已知取舍**：预处理队列**不纳入 Ctrl+Z 全局撤销**（全局撤销只覆盖切割参数态），如需回退请删改队列步骤或「重置预处理」。

### 导出参数说明

右侧 **导出页** 提供三种输出模式：

| 模式       | 说明                                    | 目标          |
|----------|---------------------------------------|-------------|
| **分离导出** | 每个保留块单独输出为一个文件                        | 目标目录 + 命名模板 |
| **合并坍缩** | 删除整行/整列后剩余单元紧贴拼接为一张图（坍缩不可行时该项禁用并给出原因） | 目标文件        |
| **合并重排** | 按显式序列把保留块填入新画布（**仅 L3 可用**）           | 目标文件        |

其他参数：格式（PNG / JPEG / WebP / BMP）、质量（有损格式）、命名模板（仅分离导出）、导出前预览缩略图（实时展示输出效果，所见即所得）、
导出时烧录标注；**合并公共**：填充色（合并画布底色 + Pad 余量补白 + 有损格式压平透明像素的背景色；三模式恒显示）；
**重排专属**：列/行（0＝自动推导）、单元宽/高（0＝用保留块原尺寸）、填充顺序（行/列优先 + 蛇形 + 倒序）、
「按格数自动」按钮，`cols×rows < 保留块数` 或单元尺寸 < 网格单元尺寸时给内联红字警告 + 导出前弹窗二次确认（该确认可由「文件 → 导出设置 → 重排前二次确认」关闭）。
控件构成与自动行列/警告判定见 [src/panels/README.md](src/panels/README.md)「export_panel」。

### 新建画布（空白 / 程序生成）

无需外部文件也能从零建一张画布：**文件 → 新建画布…（`Ctrl+N`）**（工具栏「新建」、移动端底部条「新建」同源），弹出 `NewCanvasDialog` 采集
尺寸 / 底色 / 图案（程序生成：纯色 / 渐变 / 棋盘格 / 条纹 / 网格线 / 点阵 / 同心环 / 放射线 / 色相渐变 / 噪声，随类型联动可用参数）/
可选附加图片（裁剪 / 拉伸 / 原生三适应 + 落位 9 锚点 / 枢轴 / 偏移）/ 前景色 / 实时预览（最长边 ≤ 256 代理，确定时才产全尺寸图）。
**像素生成全在 Core**：对话框采参后经 `EngineBridge::generatePattern` 委托 Core `generate::makePattern`，GUI 不复现任何图案算法。
产出画布走与「打开图像」同一收尾链路，故新建图与载入图后续操作（切割/预处理/标注/导出）完全一致。**生成图须视为「不可重读来源」**
（`canReloadSource_ = false`，不对应磁盘上任何原图文件）。对话框采参子件见 [src/app/forms/README.md](src/app/forms/README.md)，控件细节见 [src/app/README.md](src/app/README.md)。

### 多层画布与图层编辑（SPEC §8.2）

> **术语基线见 [SPEC §2.2](../../SPEC.md)（权威定义）**：**画布**（输出边界 / 工作图尺寸）＝ **底图**（基座＝ input 生态位，
> 同构 `canvas::ImageLayer`、钉最底、不入 `layers`）+ 其上叠放的**图层**数组；一张图片按落点分为**底图图片**（入底图、走 §7 `generate` 段）
> 与**图层图片**（入图层栈、走 `layers` 段、位图靠 assets 引用）。

**多图层是「画布层」而非「引擎层」概念**——承重不变：`runEngine(const core::Image&, config)` 永远只收**一张展平的画布图 + 空 preprocess**，
Grid-Selection-Export / export / preview / CLI 主链零改动。图层数据模型 `canvas::ImageLayer` 与合成 `composeCanvas`（依底图 base→叠加层按
`zOrder` 升序逐层物化 → 施 `transform.opacity` alpha 乘子 → 落位 → source-over 写入画布尺寸缓冲、越界自然裁剪）均在 **Core**；
每层的 `PreprocessSession`（GUI 声明态包装 + `base_` 缓存编排）归 **GUI**。

图层编辑（`LayerEditPanel`）、底图基座与三元预处理目标（图片图层 / 底图 / 画布）、逐层物化缓存、落位框坐标跨系映射
（`mapRectGeometry` 正映 / `mapDeltaToInput` 逆映）、叠加图层整体显隐、画布尺寸变化只清框选、越界与滚动边界等实现细节，
分别见 [src/panels/README.md](src/panels/README.md)「layer_edit_panel」、[src/canvas/README.md](src/canvas/README.md)「交互」、
[src/app/README.md](src/app/README.md)「预处理」与 [src/model/README.md](src/model/README.md)。**不透明度属合成概念、不属像素预处理**
（逐层 `transform.opacity`，Core 像素预处理管线不含 OpacityOp 步）。

### 配置文件加载 / 保存

- **文件 → 保存配置…**：把当前作业参数（模式、切割、选择集、排序、导出参数，**以及完整的预处理队列 steps**）写为 SPEC §7 schema 的 JSON 文件。
- **文件 → 加载配置…**：读取 JSON 反向还原到界面（含 `applyLoadedQueue` 队列回灌重放）；**加载本身可一步撤销**（入同一撤销栈）。
- 配置**不含图像像素**，仅保存参数态与声明式预处理队列。**例外（多层画布，SPEC §7）**：图层 `ImageLayer` 以**相对文件名**引用图源（位图不入配置），
  可经 CLI `--bundle-assets` 复制进伴随 `<配置名>.assets/` 目录；缺文件降级为占位（宽容读取）。

### 快捷键列表

| 快捷键                    | 功能                                                |
|------------------------|---------------------------------------------------|
| `Ctrl+N`               | 新建画布（空白/程序生成）                                     |
| `Ctrl+O`               | 打开图像                                              |
| `Ctrl+S`               | 导出                                                |
| `Ctrl+Z` / `Ctrl+Y`    | 全局撤销 / 重做（**上下文路由**：标注工具激活或有选中标注时→标注撤销，否则→文档参数撤销） |
| `Ctrl +` / `Ctrl -`    | 放大 / 缩小                                           |
| `Ctrl+0`               | 适应窗口                                              |
| `1` / `2` / `3`        | 切换 L1 / L2 / L3 模式                                |
| `K` / `R`              | 切换极性（保留 / 删除）                                     |
| `Esc`                  | 清除选区（绘制态下＝收笔 / 取消预览）                              |
| `Delete` / `Backspace` | 删除选中标注                                            |
| 方向键                    | 微调选区 ±1px                                         |
| `Shift` + 方向键          | 微调选区 ±10px                                        |
| 滚轮                     | 缩放画布                                              |
| 空格 + 拖拽 / 中键拖拽         | 平移画布                                              |
| 右键                     | 画布右键菜单：清除切割线 / 重置视图 / 切换预览遮罩（绘制态下＝收笔）             |

> 单键快捷键（`1/2/3/K/R/Delete`/方向键）在焦点位于坐标/文本输入框时被自动忽略，避免打字误触。全部快捷键（组合键 + 单键共 15 条）
> 收进 `app/keybindings.{h,cpp}` 命令注册表（单一真相源），桌面可在「视图 → 外观/交互…」的「快捷键」Tab 逐条重绑（改后即生效、跨会话持久化）。

## 布局与交互要点

### 图层 z 序（`include/canvas/items/z_order.h`，单一定义点）

叠放次序：**底图 < 图片图层落位框 < 标注 < 标注绘制预览 < 遮罩 < 网格 < 选区 < 单元编号 < 网格基准手柄**（遮罩为**单一层** `kMask`：
一个图元内先铺红色删除底、再叠绿色保留块，绿覆盖处即保留、透出红处即删除，GUI 不做区域布尔——见决策 #1）。

| z 值 | 常量                   | 图层                                                                      |
|-----|----------------------|-------------------------------------------------------------------------|
| 0   | `kBase`              | 底图（预处理后）                                                                |
| 10  | `kImageLayer`        | 图片图层落位框（多层画布）：标示各图片图层可拾取区域                                              |
| 20  | `kAnnotation`        | 标注图层（矢量叠加）                                                              |
| 30  | `kAnnotationPreview` | 标注绘制预览（橡皮筋）：恒位于已提交标注之上、遮罩之下（避免与后提交标注同 z 被盖住）                            |
| 40  | `kMask`              | 保留(绿)/删除(红)遮罩（单一图层：先铺红色删除底、再叠绿色保留块）                                     |
| 50  | `kGrid`              | 网格线（L3 / L2 多矩形诱导）                                                      |
| 60  | `kSelection`         | 选区边框（橙）+ 手柄 + 贯穿切割线延伸（同一橙色图元）                                           |
| 70  | `kCellNumber`        | 单元格编号角标（L3 自定义序；`CellPickerItem` 的角标子图层，子 z = kCellNumber − kSelection） |
| 80  | `kGridHandle`        | 网格基准手柄（L3 网格线层内：拖基准点 `(x0,y0)` / 单元尺寸 `(cw,ch)`；置顶以先接按下）                |

> 场景坐标 = 原图像素坐标（见决策 #2）。各图元的可见/可拖门控、L3 单元点选、切割线即选区边等实现见 [src/canvas/README.md](src/canvas/README.md)。

### 交互事件流

```
鼠标/键盘 (CanvasView)
   │  拖拽框选 rubberSelect / 微调 nudgeSelection / 光标 cursorScenePos / 缩放 zoomChanged
   │  L3 网格基准拖拽释放 gridParamsEdited（拖拽中只预览、不写 Document）
   ▼
MainWindow 编排槽 → CanvasCoordinator 写回 Document（L1/L2 setRect、MULTI_RECT addRect、L3 命中单元 addCells、L3 网格基准 setGridParams）
   ▼
Document.changed ──► onDocChanged ──► PreviewController.refreshPreview()
   │                    buildEngineConfig + EngineBridge.runPreviewGeometry → CanvasScene 刷新 + 输出预览
   ▼
scheduleHistoryCapture（500ms 防抖）──► DocHistory 压入 EngineConfig 快照
```

选区即切割线（决策 #6）、拖拽期防抖、边缘/中心吸附（网格线吸附未接入，见「已知限制」）、L3 单元交互、绘制态门控等实现细节见
[src/canvas/README.md](src/canvas/README.md)。

### 预处理与标注数据流

- **工作图恒为「轻操作队列(resize(original))」**：resize 不入队列（常驻独立尺寸区、恒最先从原图重采样）；`steps_`（与 `resize_`/`base_`
  一同内聚于 `model::PreprocessSession`，由 `Document` 组合持有）仅承载几何/色彩轻步。面板只发意图信号，控制器维持「几何块在前 + 色彩块在后」不变式。
- **`commitPipeline` 公共收尾**：先比较几何签名 `geomSig`（尺寸或净旋转象限变 → 先清失效框选；标注/图层/色彩预处理恒保留），
  再复用 `base_` 缓存重放轻后缀（仅 resize 前缀变化才重烤 `base_`，唯一重采样点）；忙碌框仅在有 resize 需重采样时弹。实际像素变换经
  `EngineBridge` 调 Core，**面板不碰 Core、不持真相**。
- **标注随框选隐柄**：空白处起框发 `marqueeStarted` → `AnnotationCoordinator` 若当前有选中则 `clearSelection()`，使标注 OBB 手柄随框选即时消失。
- **标注（`AnnotationCoordinator` 编排）**：每个 Core `Annotation` 对应一个 `AnnotationItem`，用 `Shape::worldPath()`→`QPainterPath`
  矢量叠加绘制（z=`kAnnotation`）；OBB 非破坏性变换（决策 #9）与烧录仅在导出时发生（决策 #8）；移动标注经 Core `translateWorld` 保留具体类型（不退化为 PATH）。

完整控制器接线、信号表与提交链见 [src/app/README.md](src/app/README.md) 与 [src/model/README.md](src/model/README.md)。

## 关键设计决策

1. **遮罩渲染法（不做几何布尔）**：`EngineResult.kept` 只给出保留块。画布先铺一层覆盖全图的红色半透明"删除底"，再在其上按 `kept` 各片段叠加绿色半透明"保留块"；绿覆盖处即保留、透出红处即删除。这样 GUI 无需自行计算"删除集"。
2. **场景坐标 = 原图像素坐标**：底图用降采样 pixmap 经 `QTransform::fromScale` 铺到原图尺寸，于是遮罩 / 切割线 / 选区都能直接按原图坐标叠加，避免到处换算。
3. **几何在全分辨率算、只在预览图上画**：`runEngine` 只做区域数学（split/compose 不搬像素），故可对全分辨率工作图跑预览、按缩放因子映射到预览 pixmap；导出时对全分辨率图跑，保证无损（NFR-2）。
4. **降采样预览复用 `core::Image::resized`**：`makePreview(img, maxDim)` 内部调 Core 的 `resized`（NEAREST）生成预览副本，无需新增 Core 接口。
5. **单一状态源 + 双桥接**：`Document` 是唯一真相源，切割/预处理经 `EngineBridge`、标注域经 `AnnotationBridge`（各自唯一入口）；面板 / 画布只读写 `Document`，不各自持有真相，降低耦合。
6. **切割线即选区边（唯一橙色图元、可直接拖边）**：单矩形诱导的四条切割线恰落在选区矩形四边上，是选区边的"贯穿全图延伸"。故无独立切割线图元——由唯一的橙色 `SelectionRectItem` 一并绘制选区框与被标记边的贯穿延伸线（同一支橙色画笔），并支持**直接抓取任一条边沿法向拖动**（拖动某边＝移动对应切割线）。哪几条边延伸由 `CanvasScene` 依 Core `generateCutLines` 结果下发（`setCutEdges`），GUI 不自算几何；因线与选区同一图元、同一位置来源，不存在“蓝/橙双线并存、手柄错位”的双重表示。
7. **合并重排仅 L3、自动 cols/rows、双警告**：导出面板的「合并重排」项**仅 L3 启用**（非 L3 置灰，离开 L3 自动复位为坍缩）；cols/rows 首次由保留块数开方自动填入（cols=ceil(√n)、rows=ceil(n/cols)，可再改）；`cols×rows < 保留块数` 或单元宽/高 < 网格单元尺寸时面板内联**红字警告** + 导出前**弹窗二次确认**；填充顺序（行/列优先 + 蛇形 + 倒序）与 L3 选择排序正交，写回 `Document.mergeOrder_`。
8. **标注矢量叠加、仅导出时烧录**：标注层与底图**分离**——每个 Core `Annotation` 对应一个 `AnnotationItem`（`QGraphicsObject`），用 `Shape::worldPath()` 矢量叠加绘制（含非破坏性变换，z=`kAnnotation`），**不改底图像素**；预处理（含尺寸变化）不清标注。**烧录仅在导出时发生**：`AnnotationBridge::burnIn(base)` 以**当前工作图**为底逐个调 Core `rasterize` 合成再送引擎切割。标注几何/命中/光栅化全在 Core；撤销/重做复用 `AnnotationEditor` 内建条目级会话态。
9. **标注 OBB 非破坏性变换（画布手柄 + 面板绝对值）**：选中标注后画布绘**定向包围盒（OBB）**的 8 缩放手柄 + 1 旋转手柄，拖拽经 Core `obbPreviewTransform`（逐帧矢量预览）/`applyObbTransform`（释放提交）做缩放/拉伸/旋转/翻转——变换以矩阵 `xform_` 累积、**不改子类参数化几何**（类型/文字字形不退化），负缩放即翻转。属性面板变换区显示 Core 累积的**绝对带符号值**，改动即生效、无「应用」按钮（每 spin 按单轴换算增量下发，避免取整误差牵连其余轴）。矩阵运算全在 Core。
10. **全局撤销/重做＝双历史 + 上下文路由**：①文档参数态用 Core `HistoryManager<EngineConfig>` 快照（`capture=buildEngineConfig`、`restore=applyEngineConfig`，与配置加载同构、复用同一对互逆映射）；②标注沿用 Core `AnnotationEditor` 内建条目级会话态。Ctrl+Z/Y 经 `MainWindow::onUndo/onRedo` **上下文路由**（标注工具激活或有选中标注时→标注撤销，否则→文档参数撤销）。文档历史由 `DocHistory` 控制器持有：500ms 防抖合并连续 `changed()`；还原/载入期 `suppress_` 抑制再采集防回环；启用态经 `availabilityChanged` 回灌菜单动作。**预处理不纳入全局撤销**（快照仅参数态、不含像素），靠「重置预处理」回退——此为产品决策。
11. **配置文件加载/保存**：文件菜单「加载配置/保存配置」经 `EngineBridge::saveConfig/loadConfig` 委托 Core `saveEngineConfig/loadEngineConfig`（§7 schema JSON，GUI 不自实现 JSON、不感知 nlohmann）；加载走 `Document::applyEngineConfig` 反向映射并守卫「非 L3 不得重排」「网格单元尺寸为正」不变式，**加载本身入同一撤销栈**（可一步撤销）。**标注随配置往返**：保存附带 `annotations` 段；加载时 Core 先把标注收进**临时层**，待队列重放**之后**再 `replaceAnnotations` 覆盖真实层（顺序纪律：先重放预处理、再还原标注），还原走 Core `restoreAnnotations`、**不纳入 DocHistory 文档撤销栈**（已知限制）。
12. **导出前输出图像预览（只渲染不重算）**：导出面板 `previewLabel_` 缩略图由 `util::composeOutputThumbnail` 按 `refreshPreview` **已算好**的 `EngineResult.composition` 渲染——**不重跑 Core、不落盘**：把缓存的小源图（最长边 ≤ 512）按 `placements` 的 `source→dest` blit 到小画布（合并模式按真实画布等比缩放、所见即所得；分离模式拼成触图），故原图 8000×8000 也只在小图上运算。**渲染逻辑下沉到 `util/`（离屏预览），app 层（`PreviewController`）只编排缓存与门控**：烧录可见时经 `util::bakeAnnotationsInto` 只烘焙一次；**按需渲染**仅在「导出」页为当前选项卡时才渲染、否则置脏待切回补渲染，烧录开关/标注变更不重跑 Core、仅用缓存重渲染。
13. **界面文案走自研 i18n 目录 + 原地热切换（不接 Qt Linguist）**：文案真相源是 `texts::` 常量层（中文 = 源语言与回退值），使用点写 `IDC_GTR(分组, 常量名)`——宏在预处理器里同时拼出键 `分组::常量名` 并取到同名中文常量作回退值，**两处同源**，手写键串不可能拼错。不走 `tr()`/`lupdate`/`.ts`/`.qm`（lupdate 抽不到常量层的值，`.qm` 又是 Qt6 内部二进制格式、零 Qt 的 Core/CLI 读不了）；英文表以 **C++ 表文件编译期内嵌**（`src/i18n/lang_en.cpp` + `lang_en_detail.cpp`），与 CLI / Core 共用 Core 的同一份进程级语言状态，且不把 qttools 拖进依赖图。`texts::ident` 与 §7 JSON token **永不进表**（不可译契约）；源语言 zh-CN **不建表**（全部回退），故中文下行为与未接 i18n 时逐字节相同。**语言热切换原地生效**：外观对话框内选定即调 `setupFromPreference` 切全局语言 + `MainWindow::retranslateUi` 逐窗原地重放（各常驻类自持 `Retranslator` + `bind*` 闭包，经 `AppearanceDialog::languageApplied` 触发），**不销毁重建窗口**（`Document` / `DocHistory` 随窗口存活、UI 与业务态无损）；桌面与移动同一机制。机制与扩展见 [include/i18n/README.md](include/i18n/README.md)。
14. **网格基准参数可在画布上拖（做在网格线层、释放才写回）**：L3 两个基准手柄（`GridHandleItem`：基准点 `(x0,y0)` / 单元尺寸 `(x0+cw,y0+ch)`）由网格线层持有，随「网格线与切割线」开关显隐；拖动中只发 `previewed`，场景依草稿参数交 Core `Grid::build` 重铺线（**不写 `Document`**、不触发 Core 全量预览重算），释放发一次 `committed` → `Document::setGridParams`（保留「几何变即清空选择集」既有语义）。**按轴定义后（SPEC §3.4.3）手柄只服务周期轴**：非周期轴的分量按 0 计且不参与拖动，两轴皆非周期时手柄整体隐藏（`setPitchAxes` 门控）；周期轴写回仍收回平铺四键 `originX/cellWidth`（`Document::setGridAxis` 归一时复位 `axisX/axisY`），使旧配置语义逐字节不变。吸附只取整数像素（四个参数本就是整数量，不做边缘/中线磁吸），越界钳制在图内（基准点 `[0,W-cw]×[0,H-ch]`、单元宽高 `[1,W-x0]×[1,H-y0]`，两手柄均不出图；参数面板的基准点上限（`W-cw`/`H-ch`）与单元尺寸上限（`W`/`H`）同此口径）；z 取 `kGridHandle`（高于 `kSelection`），否则按下会被 L3 单元点选图元先吃掉。

## 扩展指引

### 新增一个右侧面板

1. 在 `include/panels/` 加头文件（`<模块>_panel.h`）、`src/panels/` 加实现，类继承 `QWidget`、`namespace idc::gui`。
2. 面板**只发意图信号 / 只读写 `Document`**，不含业务逻辑；构造用布局管理器，控件加中文 tooltip；面对人的文案经 `retranslate.h` 的 `bind*` 助手（而非裸 `setText`）以便热切换重放。
3. 在 `MainWindowUi::buildCentral` 中实例化并加入右侧容器；面板意图信号连到对应控制器（预处理意图→`PreprocessController`、标注/图层意图→`AnnotationCoordinator`、选项卡切换→`PreviewController`），画布交互意图经 `CanvasCoordinator`。
4. 若面板状态需持久化，扩展 `Document::buildEngineConfig/applyEngineConfig`（保持与配置加载互逆）。
5. 在 `include/panels/README.md` 与 `src/panels/README.md` 补声明边界/实现说明（工程规范见 CONTRIBUTING.md）。

### 新增一个画布交互

1. 在 `CanvasView`（视图级手势）或对应图元（`SelectionRectItem`/`CellPickerItem`/`AnnotationItem`）里处理事件，发语义信号（如 `nudgeSelection`），**不在图元内直接改 Document**。
2. 在 `CanvasCoordinator::connectSignals()` 连接信号到编排槽，槽内写回 `Document` → 触发 `changed` → `refreshPreview`。
3. 新增图层图元时在 `z_order.h` 追加 z 常量（保持单一定义点），并在 `CanvasScene` 提供 `update*/clear*` 接口。
4. 拖拽类交互注意：拖拽期跳过 `syncPanels` 与对正在拖图元的回设（避免抖动），释放时同步一次。

### 新增一种标注形状

> ⚠️ 标注几何/命中/光栅化全在 Core，**须先扩展 Core `ShapeType` 与 `ShapeFactory`**（改 Core 前先征得同意）。
> Core 支持后：在 `AnnotationBridge::AnnoTool` 加枚举、`mode_tool_panel.cpp` 的 `kTools[]` 加一行、
> `util::paintAnnotation`/`AnnotationItem` 走 `worldPath()` 自动获得渲染，无需在 GUI 补几何。

### 新增 / 修改一条界面文案

1. 在 `include/i18n/gui_texts.h`（转发至 `gui_texts_base.h`〔menu / label〕/ `gui_texts_detail.h`〔tooltip / status / msg〕）的对应组加一条 `inline constexpr char kXxx[] = "中文原文";`——**中文即源语言**，它同时是未译语言的回退值。
2. 使用点写 `IDC_GTR(分组, kXxx)`（返回 `QString`）；需先存后转的清单表位写 `IDC_GT(...)`（返回 `TextRef`，交 `bind*` 助手设初值 + 登记重放闭包）。**绝不手写键串**。
3. 不可译的东西不往 `texts::` 放：QSettings 键 / MIME / objectName / 快捷键 / §7 JSON token 一律进 `texts::ident`（恒不查表）。
4. 要给英文：menu / label 在 `src/i18n/lang_en.cpp`、tooltip / status / msg 在 `src/i18n/lang_en_detail.cpp` 同一分组块里补一行 `{"分组::kXxx", "English"}`（与常量层同序、分块中文注释）；带 `%1..%9` 的条目**编号是契约**，`(&X)` 助记符字母、`…`/`×`/`°`/`—`/`§7`/`\n` 逐字沿用。
5. 不补译也不影响使用（自动回退中文），但 `unit_tests` 里的 `test_i18n_catalog` 会兜住写错键名、重复登记、契约组越界与跨层撞键（均为 fail），并打印各组覆盖率。改完跑一次全量 `ctest` 即可。

### 调用 Core 的纪律

- 任何切割/几何/排序/极性/导出/JSON/编码逻辑**一律调 Core**，GUI 不重复实现。
- 发现 Core 缺接口：不在 GUI 补，改为报告 + `// TODO(core): 需要 Core 提供 xxx` 标注。
- L1/L2/L3 必须走同一引擎路径，模式切换只切参数面板与预览呈现。

## 已知限制

| 项                        | 现状                                | 根因 / 说明                                                                                                           |
|--------------------------|-----------------------------------|-------------------------------------------------------------------------------------------------------------------|
| 预处理全局撤销                  | 不纳入 `Ctrl+Z`                      | `DocHistory` 仅快照 `EngineConfig` 参数态、不含像素；靠「重置预处理」回退（产品取舍，决策 #10）                                                  |
| 标注配置还原的撤销                | 不纳入文档撤销栈                          | `replaceAnnotations` 走 Core `restoreAnnotations`（不推撤销快照）；标注域自身可 `Ctrl+Z` 逐层回退，但「加载配置」这一步不可整体撤销（决策 #11）            |
| 网格线吸附                    | 未接入                               | `selection_rect_item` 吸附目标仅图像边缘 + 中心（NFR-7 要求三选，尚缺网格线）                                                            |
| 插值方式下拉                   | 未提供                               | `engine_bridge` resize 固定双线性；Core 支持 `ResampleMode{NEAREST,BILINEAR}`，预处理面板未暴露选择                                  |
| 队列编辑不可撤销                 | 不纳入 `Ctrl+Z`                      | 删/改/移步骤后文档撤销栈找不回；队列模型天然支持单步撤销，属未实现的机会项                                                                            |
| 大图内存驻留                   | `original_`+`base_`+`working_` 三份 | 三份常驻图：原图 `original_`、resize 前缀缓存 `base_`、重放工作图 `working_`；预览从全分辨率 `working_` 降采样；「`working_` 不常驻 / 小图重放」的进一步优化未实现 |
| `label` / `tooltip` 英文译文 | 未逐条补齐（缺译自动回退中文）                   | 已译使用期现取的三组（`menu` / `status` / `msg`）；缺译不报错也不留空。进度由 `tests/test_i18n_catalog.cpp` 的覆盖率报表跟踪（只打印不 fail）             |
| Qt 自带控件文字（标准文件对话框按钮等）    | 恒为 Qt 默认语言                        | 需 Qt 官方 `qtbase_zh_CN.qm`（与本工程的自研目录无关，也不受 `ui/language` 控制），暂不集成                                                  |
| 8000×8000 ≥30fps         | 未正式验证                             | 预览已用降采样副本满足设计目标，但性能验收尚未落地实测（NFR-3）                                                                                |

## 构建

前置：CMake ≥ 3.28 + vcpkg（设置环境变量 `VCPKG_ROOT` 指向 vcpkg 根目录）。
依赖经 [`vcpkg.json`](../vcpkg.json) 清单自动安装（`qtbase` 首次构建较慢），推荐用 Presets：

```bash
cmake --preset windows-debug   # 以仓库根为源目录（Ninja + MSVC，Debug）
cmake --build build/windows-debug --target idc_gui
```

手动方式：`cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake` 后 `cmake --build build --target idc_gui`（清单模式自动生效）。
Linux / macOS 用 `linux-debug` / `macos-debug` 预设（Linux 上 qtbase 构建需系统 X11/EGL 等开发库，清单见 CI 工作流）。

- 目标编排（双可执行）：`idc_gui_common`（OBJECT 库，除程序入口外全部共享源）→ 桌面 `idc_gui`（纯桌面）与 `idc_gui_mobile`（移动形态，受 `IDC_GUI_MOBILE` 控制）；Android/iOS 只出单目标 `idc_gui`。`main.cpp` 随各 `qt_add_executable` 各编一份、不入 common。详见 [`CMakeLists.txt`](CMakeLists.txt)。
- 版本号经编译期宏注入：`IDC_GUI_VERSION` 与 `IDC_CORE_VERSION`（Core 经 PARENT_SCOPE 回传，与 CLI 同一来源）。
- 仅链接 `Qt6::Widgets Qt6::Gui Qt6::Core` + `image_discropper_core`；vcpkg qtbase 附带的 `DB2/PPS/OCI/Libb2` 等可选驱动目标本 GUI 不需要、故不链接。
- Core 的第三方依赖（stb / WebP / nlohmann_json）在 Core 侧以 PRIVATE 接入，GUI 无需感知。

## 许可

本模块随仓库整体采用 **GPL-3.0**（见根 [`LICENSE`](../../LICENSE)）。
