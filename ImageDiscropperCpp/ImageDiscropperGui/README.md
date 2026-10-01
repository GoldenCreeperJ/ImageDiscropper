# ImageDiscropperGui — Qt 6 图形前端

本目录是 ImageDiscropper 的 **GUI 层**（可执行目标 `idc_gui`），基于 **Qt 6.x + Widgets + QGraphicsView**。
GUI 是 Core（`image_discropper_core` 静态库）的**纯消费者**：只采集交互、把交互翻译为 Core 参数、
调用 Core、渲染 Core 的结果；**不实现任何切割 / 几何 / 排序 / 极性 / 导出逻辑**。

> 命名空间统一为 `idc::gui`。本文件由原独立文档 `USAGE.md`（用户操作手册）与 `DESIGN.md`（设计说明）合并而来：
> 最终用户读「用户操作速览」，维护者读「关键设计决策」「布局与交互要点」与「扩展指引」。
> 功能规格（唯一行为基线）见 [`../SPEC.md`](../../SPEC.md)，贡献约定见根 [`CONTRIBUTING.md`](../../CONTRIBUTING.md)。

## 目录结构与职责

采用与 Core / CLI 一致的 **include/src 分离**：头文件与其**声明边界 `README.md`** 在 `include/<模块>/`，
实现与其**实现说明 `README.md`** 在 `src/<模块>/`（与 Core 的 include/src 双侧 README 约定一致）。构建包含根为 `include`，故源码内以 `"canvas/…"`、`"model/…"` 形式互相引用。

| 模块       | 头文件               | 实现 + README                         | 职责                                                                                                                                                                      |
|----------|-------------------|-------------------------------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `util`   | `include/util/`   | [src/util/](src/util/README.md)     | `core::Image` ↔ Qt 图像适配、`geometry::Path`/`core::Color` ↔ Qt 绘图类型适配、降采样预览（视图关注点）                                                                                         |
| `model`  | `include/model/`  | [src/model/](src/model/README.md)   | 会话状态单一真相源 `Document`、唯一触碰切割引擎的 `EngineBridge`、唯一驱动 Core 标注的 `AnnotationBridge`                                                                                          |
| `canvas` | `include/canvas/` | [src/canvas/](src/canvas/README.md) | QGraphicsView/Scene 画布：图层化渲染底图/遮罩/切割线/选区/标注矢量叠加 + 交互                                                                                                                    |
| `panels` | `include/panels/` | [src/panels/](src/panels/README.md) | 左侧模式/极性面板与标注工具/图层面板、右侧参数/导出/图像/标注属性面板                                                                                                                                   |
| `app`    | `include/app/`    | [src/app/](src/app/README.md)       | 主窗口装配壳 `MainWindow` + 四个控制器（预览/预处理/标注/历史）+ 状态栏组件 + 装配建造者 `MainWindowUi`、程序入口                                                                                            |
| `mobile` | `include/mobile/` | [src/mobile/](src/mobile/README.md) | 移动端形态（与 app 并列）：`MobileShell` 继承 `MainWindow` 换装骨架（画布为主 + 底部工具条/抽屉）+ 无键盘补偿控件；复用既有面板/画布/模型，零复刻编排逻辑；桌面作为独立产物 `idc_gui_mobile` 与 `idc_gui` 并列输出（CMake 开关 `IDC_GUI_MOBILE`） |

**依赖方向**：`app → panels/canvas/model → util → Core`（`mobile` 继承 `app`，同向）。`model` 是唯一触碰 Core 引擎/标注的层，
面板与画布只读写 `Document`/桥，不各自持有真相（降低耦合）。

## 数据流（一条主线）

```
用户交互（画布拖拽 / 面板输入 / 快捷键）
        │
        ▼
   Document（状态：模式 / 极性 / 选区 / 导出参数）── 组装 EngineConfig
        │  changed() / imageChanged()
        ▼
   EngineBridge（切割引擎唯一入口）── runEngine / generateCutLines / exportImage
        │  EngineResult{ok,error,kept,composition,collapsible} / CutLineSet
        ▼
   CanvasScene（渲染）── 底图 + 红/绿遮罩 + 橙色选区框（含贯穿切割线延伸、可直接拖边）
        │
        ▼
   MainWindow（状态栏 / 面板提示回显）
```

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
│ 菜单栏：文件 / 编辑 / 图像 / 标注 / 视图 / 帮助                    │
├───────────────────────────────────────────────────────────────┤
│ 工具栏：打开 导出 | 撤销 重做 | 放大 缩小 适应 | 模式切换(L1/L2/L3) │
├──────────┬────────────────────────────────┬───────────────────┤
│ 左侧面板  │         中央画布                │   右侧面板         │
│ (约340px)│      (QGraphicsView)           │  (约300px, 选项卡) │
│ ·模式切换 │                                │  ·参数页           │
│ ·极性开关 │   底图 + 遮罩 + 切割线 +        │  ·导出页           │
│ ·标注工具 │   选区 + 网格 + 标注 + 编号      │  ·图像页           │
│ ·图层列表 │                                │  ·标注页           │
├──────────┴────────────────────────────────┴───────────────────┤
│ 状态栏：坐标 | 像素RGB | 当前模式 | 保留块数 | 缩放 | 提示信息      │
└───────────────────────────────────────────────────────────────┘
```

- **左侧面板**（可滚动）：模式切换（L1/L2/L3）、极性开关（保留/删除）、标注工具面板、图层面板。
- **中央画布**：可缩放、可平移，占据最大空间。叠加图层的 z 序见「图层 z 序」。
- **右侧面板**（`AccordionPanel` 折叠式，四节可同开 + 整栏滚动）：**参数页**（随模式变化）、**导出页**、**图像页**（预处理）、**标注页**（标注属性）。移动端抽屉仍为 `QTabWidget` 选择式。
- **状态栏**：实时显示光标坐标、光标处像素 RGB、当前模式、保留块数、缩放倍数与提示/错误信息（错误为非模态，限时显示，不打断操作）。

### 画布视觉规范（配色）

| 元素              | 颜色                                                                | 含义                     |
|-----------------|-------------------------------------------------------------------|------------------------|
| 画布背景            | 深灰 `rgb(45,45,45)`                                                | 图像以外的空白区               |
| 保留块遮罩           | 绿色半透明 `rgba(0,200,0,60)`（`mask_layer.cpp` `kKeepColor`）           | 将被保留的区域                |
| 删除块遮罩           | 红色半透明 `rgba(200,0,0,60)`（`kDeleteColor`）                          | 将被删除的区域                |
| 切割线 / 选区边框      | 橙色 `rgb(255,140,0)`，高亮 `rgb(255,90,0)`；填充 `rgba(255,165,0,30~60)` | 贯穿全图的直线；切割线即选区边的延伸     |
| 网格线（L3）         | 灰色 `rgb(150,150,150)`，1px 虚线                                      | Core 诱导网格              |
| 多矩形诱导切割线（L2）    | 橙色 `rgb(255,140,0)` 实线                                            | 多矩形并集网格                |
| 标注高亮 / 手柄       | 蓝色 `rgb(0,160,230)`；旋转手柄白底蓝边圆形（与缩放手柄区分）                           | 选中标注的 OBB 手柄           |
| 单元编号角标（L3 自定义序） | 黑底 `rgba(0,0,0,175)` + 白字，13px bold，圆角 3px                        | 每个已选单元左上角的 1-based 序号  |
| 工具按钮            | 中性底 `#f2f2f2` + 深色字 `#1b1b1b`；选中态蓝 `#3b7ddd` + 白字                 | 深色模式下显式指定文字色，避免白字浅底不可读 |
| 线宽              | 选区 / 切割线 / 标注手柄用 cosmetic pen                                     | 屏幕线宽恒定，不随缩放变粗          |
| 面板间距            | 内边距 8px、控件间距 6px、分组间距 12px                                        | 各面板统一布局                |
| 字体 / 文案         | 界面文字全中文；`QStringLiteral` 包裹字面量                                    | —                      |
| 反馈              | 错误经 `notify()` 写状态栏、限时显示（非模态）；所有交互 100ms 内视觉反馈                    | —                      |

### 预处理（图像页：声明式队列编辑器）

预处理在**切割之前**执行，是一串**有序步骤（队列）**而非「点一下改一次像素」的即时动作。
用户在右侧 **图像页** 编排队列：轻操作**分「几何操作」「色彩操作」两个独立列表**呈现，各自带类型选择 + 参数区与步骤列表，可独立「添加 / 更新所选 / 删除 / 上移 / 下移」；
顶部「画布尺寸」为独立常驻区（不入队列）；底部「重置预处理」清空整条队列。**两列表只是同一底层队列的分区视图**——底层 `steps` 恒为「全部几何步在前 + 全部色彩步在后」，提交时按此顺序合并。
**原图始终保留**，工作图 = 队列对原图的重放结果；重放以 `base_`（最后一个 resize 及其之前的产物）为缓存，编辑 resize 之后的轻操作只在 `base_` 上重放轻后缀（免重复重采样），故 resize 永远以原图分辨率为基准、不累积误差。

> **执行层融合物化（几何 + 色彩统一收敛到执行层）**：队列**存储 / 序列化 / 几何签名仍按用户编辑的原始顺序**，仅在物化 `working_` 时对轻后缀走一次 `PreprocessPipeline::applyExec()`——它先把整条队列压成内部 `execPlan`（**绝不落回词表步**），再交融合核单趟执行：① 连续的旋转/翻转步折为**单次 D4 remap**（`remapD4`，闭式逆采样一趟重排，取代逐步搬运）；② 连续的色彩步（黑白/反色/色道分离）折为**单次位映射** `channelMap`：pre-gray 段任意 Split/Invert 序列合为 `f(v)=(v&K)^J` 一趟完成、黑白幂等、黑白之后的分离为死步丢弃、反色按奇偶累积。`ResizeOp` 是**唯一块界**（双线性取整与色彩/几何均不可交换），块内步数恒 ≤4，故**像素趟数与队列长度解耦**；灰度之后的死步（如误置于黑白后的色道分离）在计划构建时自动吞除，无需录入拦截。融合执行与逐步 `apply` **逐像素恒等**（几何为“灰度亦无损的纯位置置换”、色彩为同取整/掩码口径的纯位运算），且以“载入强解 RGBA、进队为多通道”为前提。GUI 侧以「几何块在前 + 色彩块在后」不变式维持两列表分区（跨类上下移被忽略）。

| 步骤类型  | 参数                      | 说明                    |
|-------|-------------------------|-----------------------|
| 旋转    | -90 / 90 / 180（角度下拉）    | 90/270 会交换宽高          |
| 翻转    | 水平 / 垂直                 | 不改尺寸                  |
| 按比例缩放 | 百分比（1..1000%）           | 依当前输出尺寸换算为绝对 resize 步 |
| 目标尺寸  | 宽 / 高 + 「保持宽高比」（改宽联动算高） | 绝对 resize，从原图重烤       |
| 黑白    | —                       | 转灰度                   |
| 反色    | R/G/B 通道勾选              | 对勾选通道取反（全勾＝整体反色）      |
| 色道分离  | R/G/B 通道勾选（勾选＝保留）       | 仅保留勾选通道               |
| 重置    | —                       | 清空整条队列、工作图还原为原图       |

> **几何签名尺寸门**：队列改动后用 `PreprocessPipeline::geometryOutput(w,h)` **纯元数据推演**输出尺寸与净旋转象限（不碰像素）；
> 仅当**尺寸或净旋转象限变化**时才清除失效选区与标注（坐标基于旧尺寸），同维度改参（如换角度 90→180 但尺寸不变、改灰度阈值）保留选区。
> 大图缩放/重采样可能耗时，期间会弹出「处理中」进度对话框（不可取消），完成后自动关闭。
>
> **已知取舍**：预处理队列**不纳入 Ctrl+Z 全局撤销**（全局撤销只覆盖切割参数态），如需回退请删改队列步骤或「重置预处理」。

### 标注工具

标注是**独立矢量图层**，叠加在底图之上、**不改底图像素**；只有导出时勾选「烧录」才会合成进像素。

**添加标注**：

1. 在左侧 **标注工具面板** 选择工具（见下表）。
2. 在画布上拖拽/单击绘制：
   - **两点形状**（矩形、正方形、圆、椭圆等）：按下拖拽实时预览，松手提交。
   - **多线段 / 画笔**：连续单击追加顶点 / 按住拖动绘制自由路径，按 **Esc** 或切换工具收笔。
   - **文字**：单击落点，弹出输入框填写文本后添加。

| 工具      | 绘制方式                                   |
|---------|----------------------------------------|
| 选择/移动   | 选择并拖动已有标注（`Delete` 删除，`Ctrl+Z/Y` 撤销重做） |
| 矩形      | 两点拖拽                                   |
| 正方形     | 落点为一个顶点，拖拽定边长（取 min(宽,高)）画轴对齐正方形       |
| 菱形      | 起点为中心，拖拽绘制                             |
| 圆形      | 起点为圆心，两点距离为半径                          |
| 椭圆      | 落点为中心，拖拽定两半轴（dx/dy）                    |
| 等腰三角形   | 起点为底边中点，拖拽绘制                           |
| 等边三角形   | 落点为底边中点，拖拽绘制底边水平、顶角朝光标生长（参照等腰）         |
| 直角三角形   | 落点为直角顶点，拖拽沿两轴定两直角边长（dx、dy）             |
| 等腰直角三角形 | 落点为直角顶点，拖拽定直角边长（取 min(宽,高)）            |
| 直线      | 两点拖拽                                   |
| 多线段     | 连续单击追加顶点，Esc 收笔                        |
| 文字      | 单击落点后输入                                |
| 画笔      | 按住拖动绘制自由路径，Esc 收笔                      |

> **无独立「圆角矩形」工具**：转角圆角不再是单独的形状类型，而是**属性面板的「圆角半径」控件**（0＝直角）——
> 作用于矩形 / 正方形 / 菱形 / 各类三角形 / 折线等多边形顶点（直线 / 圆 / 椭圆 / 文字无转角，该控件会自动置灰）。

**编辑标注属性**：选中标注后，右侧 **标注页** 可设置：**边框色、填充色（与边框色独立）、粗细、是否填充、圆角半径、文字内容、字号**，以及 **OBB 变换**（带符号缩放 X/Y、旋转角度，改动即生效、无「应用」按钮）。填充形状会同时按边框色描边，与画布预览一致；新建标注的填充色默认跟随边框色，显式改过后不再联动。
画布上选中标注会出现 **8 个缩放手柄 + 1 个旋转手柄**（定向包围盒 OBB），拖拽即做缩放/拉伸/旋转/翻转（拖手柄越过对边＝翻转）。

**标注菜单**：`标注` 菜单提供：撤销标注 / 重做标注 / 删除选中 / 清除全部（二次确认）/ 标注属性定位（**层序四式已移出菜单**，见下）。
标注以**矢量图层列表**叠放，**每条 `Annotation` 携带显式 `zOrder`（恒等于列表下标，下标越大越靠上）**：新标注默认叠在最上层，`命中检测`（顶层优先）与`烧录`（后者覆盖前者）都以此为准。**叠放次序（上移一层 / 下移一层 / 置于顶层 / 置于底层）改由右侧标注属性页的「叠放次序」区调整**（仅选中标注时可用、不入菜单、无快捷键），委托 `AnnotationEditor::moveAnnotation` 就地重排（不清撤销主栈但清重做栈、选中项跟随移动、增删移后统一规范化 `zOrder`＝下标），改序即时反映到画布叠放、命中优先与（勾选烧录时的）导出预览，并随 SPEC §7 `annotations` 段以 `zOrder` 导入导出。

### 图层与烧录

左侧 **图层面板** 提供 7 个显隐开关（按画布 z 序排列；仅影响画布预览，不影响导出结果）：

| 图层   | 作用                                       |
|------|------------------------------------------|
| 底图   | 显示/隐藏预处理后的图像                             |
| 标注   | 显示/隐藏标注图层（隐藏后标注不可交互）                     |
| 遮罩   | 显示/隐藏保留(绿)/删除(红)预览遮罩                     |
| 网格线  | 显示/隐藏 L3 网格与 L2 多矩形诱导网格线                 |
| 切割线  | 显示/隐藏贯穿全图的橙色切割线                          |
| 选取边框 | 显示/隐藏橙色选区框与手柄（含 L3 单元选择高亮；隐藏后不可编辑，需重新勾选） |
| 单元编号 | 显示/隐藏 L3 自定义排序的单元序号角标                    |

**烧录开关**位于右侧 **导出页**（「导出时烧录标注」）：勾选后，导出时把标注按矢量合成进像素，标注会**随像素一起被切割**落位到各输出块。

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
2. 右侧参数页填网格四元组 **`(x0, y0, cellWidth, cellHeight)`**，选择余量策略（丢弃/保留部分/填充）；网格线自动铺满全图，只读栏显示派生的行×列规模。
3. 用**选择集**：单击选单元、画布框选、或「全选/反选/清空」。
4. 设**排序**：行优先(row-major) / 列优先(column-major) / 自定义(custom)，可叠加 reverse（倒序）、snake（蛇形）；自定义序下可在画布拖拽单元调序，每个单元左上角显示序号角标。
5. 导出页选「合并重排」，设置列/行（或点「按格数自动」依保留块数开方）、单元宽/高、填充色、填充顺序；参数不足时面板给出红字警告，导出前二次确认。
6. 导出页点导出，得到按序重排拼合的新画布图像。

### 导出参数说明

右侧 **导出页** 提供三种输出模式：

| 模式       | 说明                                    | 目标          |
|----------|---------------------------------------|-------------|
| **分离导出** | 每个保留块单独输出为一个文件                        | 目标目录 + 命名模板 |
| **合并坍缩** | 删除整行/整列后剩余单元紧贴拼接为一张图（坍缩不可行时该项禁用并给出原因） | 目标文件        |
| **合并重排** | 按显式序列把保留块填入新画布（**仅 L3 可用**）           | 目标文件        |

其他参数：

- **格式**：PNG / JPEG / WebP / BMP。
- **质量**：有损格式（JPEG/WebP）的压缩质量。
- **命名模板**：仅分离导出，控制输出文件名。
- **导出前预览缩略图**：实时展示输出效果（合并模式按真实画布等比缩放、所见即所得；分离模式拼成缩略触图）。
- **导出时烧录标注**：勾选后标注合成进像素随切割落位。
- **重排专属**：列数/行数（0＝自动推导）、单元宽/高（0＝用保留块原尺寸）、填充色、填充顺序（行/列优先 + 蛇形 + 倒序）、「按格数自动」按钮；当 `cols×rows < 保留块数` 或单元尺寸 < 网格单元尺寸时给出内联红字警告 + 导出前弹窗二次确认。

### 配置文件加载 / 保存

- **文件 → 保存配置…**：把当前作业参数（模式、切割、选择集、排序、导出参数，**以及完整的预处理队列 steps**）写为 SPEC §7 schema 的 JSON 文件（`Document::buildSerializableConfig`：携带队列、source 取原图尺寸）。
- **文件 → 加载配置…**：读取 JSON 反向还原到界面（`PreprocessController::applyLoadedQueue` 把配置里的 preprocess 队列回灌为 Document steps 并重放）；**加载本身可一步撤销**（入同一撤销栈）。
- 配置**不含图像像素**，仅保存参数态与声明式预处理队列（队列可在重新打开同图时完整重现）。

### 快捷键列表

| 快捷键                    | 功能                                                |
|------------------------|---------------------------------------------------|
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

> 单键快捷键（`1/2/3/K/R/Delete`/方向键）在焦点位于坐标/文本输入框时被自动忽略，避免打字误触。

## 关键设计决策

1. **遮罩渲染法（不做几何布尔）**：`EngineResult.kept` 只给出保留块。画布先铺一层覆盖全图的红色半透明
   “删除底”，再在其上按 `kept` 各片段的区域叠加绿色半透明“保留块”；绿色覆盖处即保留、透出红色处即删除。
   这样 GUI 无需自行计算“删除集”。
2. **场景坐标 = 原图像素坐标**：底图用降采样 pixmap 经 `QTransform::fromScale(scaleX, scaleY)` 铺到原图尺寸，
   于是遮罩 / 切割线 / 选区都能直接按原图坐标叠加，避免到处换算。
3. **几何在全分辨率算、只在预览图上画**：`runEngine` 只做区域数学（split/compose 不搬像素），故可对全分辨率
   工作图跑预览，把区域按缩放因子映射到预览 pixmap；导出时对全分辨率图跑，保证无损（NFR-2）。
4. **降采样预览复用 `core::Image::resized`**：`makePreview(img, maxDim)` 内部调用 Core 的 `resized`（NEAREST）生成预览副本，
   无需新增 Core 接口。
5. **单一状态源 + 双桥接**：`Document` 是唯一真相源，切割/预处理经 `EngineBridge`、标注域经 `AnnotationBridge`（各自唯一入口）；面板 / 画布只读写
   `Document`，不各自持有真相，降低耦合。
6. **切割线即选区边（唯一橙色图元、可直接拖边）**：单矩形诱导的四条切割线恰好落在选区矩形的四条边上，是选区边的
   「贯穿全图延伸」。故**不再有任何独立的切割线图元**——由唯一的橙色 `SelectionRectItem` 一并绘制选区框与被标记边的
   贯穿延伸线（同一支橙色画笔），并支持**直接抓取任一条边（含其延伸到图像边界的那段）沿法向拖动**：拖动某边＝移动
   该选区边＝移动对应切割线（四角手柄仍优先做对角缩放、框内拖动整体平移）。哪几条边延伸为切割线由 `CanvasScene`
   依 Core `generateCutLines` 结果下发（`setCutEdges`），GUI 不自算几何；`rectChanged` → 写回
   `Document` → Core 重算 → 即时刷新遮罩（NFR-6）。因为线与选区本是同一图元、同一位置来源，物理上不可能再出现
   旧实现的「蓝/橙双线并存、拖拽手柄错位、切割线达上限与选区分离」等双重表示问题。
7. **合并重排仅 L3、自动 cols/rows、双警告**：导出面板的「合并重排」项**仅 L3 启用**（非 L3 置灰，`Document.setMode` 离开 L3 亦自动复位为坍缩）；
   重排的 cols/rows 首次由**保留块数开方**自动填入（cols=ceil(√n)、rows=ceil(n/cols)，用户可再改）；当 cols×rows < 保留块数、
   或单元宽/高 < 网格单元宽/高时，面板内联**红字警告** + 导出前**弹窗二次确认**；填充顺序（行/列优先 + 蛇形 + 倒序）与 L3 选择排序正交，写回 `Document.mergeOrder_`。
8. **标注矢量叠加、仅导出时烧录**：标注层与底图**分离**——每个 Core `Annotation` 对应一个 `AnnotationItem`（`QGraphicsObject`），
   用 `Shape::worldPath()`→`QPainterPath` 矢量叠加绘制（含非破坏性变换，z=`kAnnotation`），**不改底图像素**；同维度预处理不清标注。
   **烧录仅在导出时发生**：`AnnotationBridge::burnIn(base)` 以**当前工作图**为底逐个调 Core `rasterize` 合成（不复用会话 `AnnotationEditor` 所持底图 `image_`，其可能预处理后陈旧），
   再送引擎切割——标注随像素被切开。标注几何/命中/光栅化全在 Core，GUI 只采集手势/翻译参数/渲染；
   标注撤销/重做复用 `AnnotationEditor` 内建 `revoke()/redo()`（条目级会话态）。
9. **标注 OBB 非破坏性变换（画布手柄 + 面板绝对值）**：选中标注后画布绘**定向包围盒（OBB）** 的 8 缩放手柄 + 1 旋转手柄，
   拖拽经 Core `Shape::obbPreviewTransform`（逐帧矢量预览）/`applyObbTransform`（释放提交）做缩放/拉伸/旋转/翻转——变换以矩阵 `xform_`
   累积、**不改子类参数化几何**（类型/文字字形不退化），渲染/命中走 `worldPath()`；负缩放即翻转（拖手柄越过对边自然产生）。
   属性面板变换区 spin 显示 Core 累积的**绝对带符号值**（`obbScaleX()`/`obbScaleY()`/`obbRotationDeg()`，含负）、**改动即生效无「应用」按钮**
   （每 spin 按单轴换算增量下发，避免取整误差牵连其余轴）。矩阵运算全在 Core，GUI 只采集手柄拖拽、把世界光标经 `worldToLocal` 映回局部算系数。
10. **全局撤销/重做＝双历史 + 上下文路由**：撤销分两条独立历史——①**文档参数态**用 Core `HistoryManager<EngineConfig>`
   快照（`capture=Document::buildEngineConfig`、`restore=Document::applyEngineConfig`，与配置加载同构、复用同一对互逆映射）；②**标注**沿用 Core
   `AnnotationEditor` 内建条目级会话态（`AnnotationBridge::undo/redo`）。编辑菜单/工具栏的 Ctrl+Z/Y 经 `MainWindow::onUndo/onRedo` **上下文路由**：
   标注工具激活（非 SELECT）或存在选中标注时转发到标注撤销，否则走文档参数撤销——两条历史不再各绑 Ctrl+Z/Y（消除原标注菜单与编辑菜单的快捷键冲突）。
   文档历史由 `app/DocHistory` 控制器持有：**500ms 防抖定时器**把拖拽/连点的连续 `changed()` 合并为一条（栈顶恒为「当前态」，
   `undoSize>1` 才可撤销）；还原/载入期以 `suppress_` 抑制再采集防回环；启用态经 `availabilityChanged` 信号回灌菜单动作。
   **预处理不纳入全局撤销**（快照仅参数态、不含像素），靠现有「重置预处理」回退——此为产品决策，与「预处理可撤销」的理想态有意取舍。
11. **配置文件加载/保存**：文件菜单「加载配置/保存配置」经 `EngineBridge::saveConfig/loadConfig` 委托 Core `saveEngineConfig/loadEngineConfig`
   （规格 SPEC §7 schema 的 JSON，GUI 不自实现 JSON、不感知 nlohmann）；加载走 `Document::applyEngineConfig` 反向映射回各状态字段（生成器+tier 还原 L1 形状/L2 子功能，
   并守卫「非 L3 不得重排」「网格单元尺寸为正」不变式），且**加载本身入同一撤销栈**（可一步撤销回载入前）。
   **标注随配置往返（SPEC §7 annotations 段）**：保存时把 `AnnotationBridge::layer()` 一并交给 `saveConfig` 序列化；加载时 Core 先把标注收进**临时层**，
   待 `applyLoadedQueue` 队列重放（含尺寸门清标注）**之后**再 `AnnotationBridge::replaceAnnotations` 整体覆盖真实层
   （顺序纪律：先重放预处理、再还原标注，否则刚载入的标注会被尺寸门误清）；还原走 Core `restoreAnnotations`（不推撤销快照），
   **不纳入 DocHistory 文档撤销栈**（已知限制：撤销一次参数会把标注留在还原后状态）。
12. **导出前输出图像预览（只渲染不重算）**：导出面板新增 `previewLabel_` 缩略图，由 `util::composeOutputThumbnail`（`util/output_preview_renderer`，无状态自由函数）按
   `refreshPreview` **已算好**的 `EngineResult.composition` 渲染——**不再跑 Core、不落盘**：把 `rebuildPreviewPixmap` 缓存的**小源图**
   `exportSrcPixmap_`（最长边 ≤ 512，与画布底图分离）按 `placements` 的 `source→dest` 用 `QPainter::drawPixmap` blit 到最长边 ≤ 192 的小画布。
   合并模式（坍缩/重排）按真实画布等比缩放、所见即所得；分离模式（无统一画布）拼成触图（cols=ceil(√n)）。源区域坐标经 `exportSrcScale*`
   （working→小图放大系数）映射，故即便原图 8000×8000 也只在小图上运算（**像素少、速度快**）。面板只负责展示（`setPreviewPixmap`，仅缩不放）；**渲染逻辑下沉到 `util/`（离屏预览），app 层（`PreviewController`）只编排缓存与门控**。
   **B（烧录可见）**：`exportPreviewSource()` 委托 `util::bakeAnnotationsInto`，在「导出时烧录标注」开启且有标注时，把各标注按 `worldPath` 矢量绘到小源图副本（working→小图变换，文字绘字形；实际绘制复用 `util::paintAnnotation`，与画布图元同一外观）——**只烘焙一次**，随后逐块 blit 自动让标注随像素被切割落位，与真实导出一致；细线在大图上按缩略比例自然淡化（忠实）。
   **A（按需渲染）**：`updateExportPreview` 先缓存 `lastComposition_/lastExportResOk_`，再由 `renderExportPreviewFromCache` 仅在「导出」页为当前选项卡时渲染，否则置 `exportPreviewDirty_`；`onRightTabChanged` 切回该页时用缓存补渲染。烧录开关/标注变更（`onBurnInChanged`/`onAnnoBridgeChanged`，仅烧录开启时）**不重跑 Core**，仅用缓存重渲染缩略图（切割几何不受标注影响），保证预览实时。

## 已核对的 Core API（均来自实际头文件，非假设）

- 编排：`engine::runEngine(const core::Image&, const EngineConfig&) -> EngineResult{ok,error,kept,collapsible,composition}`（内部 apply preprocess）；`engine::runEngineGeometry(const SourceInfo&, const EngineConfig&)`（仅按尺寸跑几何、忽略 preprocess，供预览）。
- 预处理：`preprocess::PreprocessConfig = variant<GrayOp,SplitOp,InvertOp,RotateOp,FlipOp,ResizeOp>`；`PreprocessPipeline{add,insert,remove,setStep,moveUp,moveDown,clear,steps,apply,applyExec,geometryOutput(w,h)->GeometryOut{width,height,rotQuad}}`（声明式队列；其存储 + CRUD **公开继承 `core::Pipeline<PreprocessConfig>`**——与元素类型无关的泛型有序步骤队列骨架，GUI 不直调基类接口；`applyExec` 为执行层融合入口，与 `apply` 逐像素等价）。
- 切割线：`engine::generateCutLines(const CutConfig&, const SourceInfo&) -> CutLineSet`。
- 配置：`EngineConfig{source,preprocess,cut,selectedCells,order,emitParams}`；
  `CutConfig{tier,generator(RECT/HORIZONTAL_LINE/VERTICAL_LINE/MULTI_RECT/GRID),rect,rects,grid,polarity}`。
- 配置存取：`engine::saveEngineConfig(path, cfg[, annotations])` / `engine::loadEngineConfig(path, out&[, annotations])`（SPEC §7 schema JSON，可选标注层指针：保存附带 `annotations` 段、加载还原到该层（缺段＝清空）；nlohmann 在 Core 侧 PRIVATE）。
- 撤销历史：`core::HistoryManager<T>{push,popToRedo,popFromRedo,top,canUndo,canRedo,undoSize,clearAll}`（模板头实现，原 `history` 模块已并入 `core`；GUI 以 `T=EngineConfig` 实例化）。
- 导出：`CompositionParams{mode,layout,mergeOrder,cols,rows,cellWidth,cellHeight,padColor,format,naming,quality,keepMetadata}`
  （`mergeOrder` = 重排填充顺序 `MergeOrder{strategy,reverse,snake}`，仅 REARRANGE、与选择排序正交）；
  `Composition{canvasWidth,canvasHeight,placements,...}`；`engine::exportImage(...)`。
- 区域：`RectRegion{left,top,right,bottom,width(),height(),area()}`；`RegionSet{size(),empty(),fragments(),boundingBox()}`；
  `Fragment{region,index,kind,row,col}`。
- I/O 与处理：`engine::readImageFile`（统一解码为 RGBA）；`core::Image::{resized,colorAt}`（通用像素原语）+ `preprocess::applyOne/applyExec`（预处理像素核与执行层融合）；`core::Image{width,height,empty,getPixel,inBounds,...}`；
  `core::Color{r,g,b,a}`。
- 标注：`geometry::Shape::worldPath()/obbPreviewTransform/applyObbTransform/translateWorld/restoreTransformState`；`annotation::AnnotationSet`（数据/命中/合成 `hitTest/burnIn(base)`，经 `AnnotationEditor` 组合；会话态 `revoke/redo/removeAnnotation/restoreAnnotations` 归 `AnnotationEditor`）。

## 布局与交互要点

### 布局结构与响应式规则

- 顶层 `QSplitter` 水平三分：**左侧 `QScrollArea`（竖排 LeftPanel + ToolPanel + LayerPanel）** | **中央 `CanvasView`** | **右侧 `QScrollArea` 包裹的 `AccordionPanel`（参数页 / 导出页 / 图像页 / 标注页）**。
- **左侧面板最小宽 340px**（容纳 3 列标注工具）、**右侧面板约 300px**，均可随 `QSplitter` 拖拽调整且不可折叠消失；中央画布占最大空间、伸缩优先。
- 左侧用 `QScrollArea` 包裹，窗口变窄/内容变高时出现纵向滚动条而不挤压画布。
- 右侧用 `AccordionPanel` 折叠分组（**多节可同时展开**，整栏外套 `QScrollArea` 滚动触达，横向不出滚动条）；**输出预览缩略图仅在「导出」节展开时才渲染**（决策 #12 按需渲染；`PreviewController` 对两型容器双型分发）。
- 初始分割为左 340 / 中央 860 / 右 300（`split->setSizes`），此后可自由拖拽（尺寸不持久化）；控件用布局管理器（`QVBoxLayout`/`QGridLayout`）自适应，无绝对定位。

### 图层 z 序（`include/canvas/z_order.h`，单一定义点）

| z 值 | 常量                   | 图层                                                                      |
|-----|----------------------|-------------------------------------------------------------------------|
| 0   | `kBase`              | 底图（预处理后）                                                                |
| 10  | `kAnnotation`        | 标注图层（矢量叠加）                                                              |
| 15  | `kAnnotationPreview` | 标注绘制预览（橡皮筋）：恒位于已提交标注之上、遮罩之下（避免与后提交标注同 z 被盖住）                            |
| 20  | `kDelete`            | 删除块遮罩（红，铺满全图作底）                                                         |
| 30  | `kKeep`              | 保留块遮罩（绿，叠加于红之上）                                                         |
| 40  | `kGrid`              | 网格线（L3 / L2 多矩形诱导）                                                      |
| 50  | `kCutLine`           | 切割线层（保留值；切割线已并入选区图元）                                                    |
| 60  | `kSelection`         | 选区边框（橙）+ 手柄 + 贯穿切割线延伸（同一图元）                                             |
| 70  | `kCellNumber`        | 单元格编号角标（L3 自定义序；`CellPickerItem` 的角标子图层，子 z = kCellNumber − kSelection） |

> 遮罩渲染法（为何 `kDelete < kKeep`、GUI 不自算删除集）见决策 #1。场景坐标 = 原图像素坐标，见决策 #2。

### 交互事件流与状态机

```
鼠标/键盘 (CanvasView)
   │  拖拽框选 rubberSelect / 微调 nudgeSelection / 光标 cursorScenePos / 缩放 zoomChanged
   ▼
MainWindow 编排槽 (onRubberSelect / onNudge / onCursor …)
   │  写回 Document（L1/L2 setRect、MULTI_RECT addRect、L3 命中单元 addCells）
   ▼
Document.changed ──► onDocChanged ──► refreshPreview()
   │                                      │ buildEngineConfig + EngineBridge.runPreviewGeometry
   │                                      ▼
   │                          CanvasScene 刷新：切割线 / 遮罩 / 选区 / 网格 / 编号 + 输出预览
   ▼
scheduleHistoryCapture（500ms 防抖）──► docHistory_ 压入 EngineConfig 快照
```

- **选区即切割线**：单矩形诱导的四条切割线是选区边的「贯穿全图延伸」，无独立切割线图元（决策 #6）。
- **拖拽期防抖**：`isDraggingSelection()` / `isDraggingMultiRect()` 期间跳过 `syncPanels()` 与对正在拖图元的回设，
  避免逐帧量化抖动与「拖拽不跟手」；释放时照常同步一次。
- **吸附**：`selection_rect_item` 的 `snap1(v, th, targets)` 在拖动/缩放时对图像边缘 `(0/W/H)` 与中心 `(W/2,H/2)`
  吸附，阈值默认 8px；Move 模式整体平移、Resize 模式逐边吸附（保持尺寸不突变）。
- **L3 单元交互**：`CellPickerItem` 支持单击切换、框选（`cellsIntersecting`）、自定义序拖拽调序
  （被拖单元橙粗框、目标单元黄虚线框），并在 CUSTOM 排序下以设备像素绘制序号角标（字号恒定屏幕大小）。
- **绘制态门控**：SELECT 工具时 `setAnnotationDrawActive(false)`，画布维持橡皮筋选区/单元交互；
  任一绘制工具时置 `true`，左键拖拽路由到标注绘制（`annoDragStart/Move/End`）。
- 两点形状：`begin → updateShape（逐帧 pendingChanged 实时预览）→ commit`；
  折线/画笔：`begin → append → commit`（Esc 或切工具收笔）；文字：`QInputDialog` 取文本 → `addText`。
- 右键在绘制态下＝收笔（`annoFinish`），不弹视图菜单；非绘制态弹「清除切割线 / 重置视图 / 切换预览遮罩」。

### 预处理与标注数据流

- **独立画布尺寸（Resize）**：`ImagePanel` 顶部「画布尺寸」区常驻（有图即可用、不入队列），发 `resizeAppliedRequested(w,h)` /
  `resizeScaleRequested(factor)` / `resizeClearedRequested()` → `PreprocessController` 的 `onSetResizeTarget/onScaleResize/onClearResize`。
  `resize_` 恒最先重放、从原图重采样（`base_` = resize(original)，无 resize 则原图副本）。
- **轻操作队列（两独立列表 → 单一底层队列）**：`ImagePanel` 以「几何操作」「色彩操作」两个列表呈现，行号经 `geoRowCount()` 映射为**扁平下标**（色彩行＝几何行数 + 色彩行号），发队列编辑信号（`stepAppendRequested(cfg)` / `stepReplaceRequested(index,cfg)` /
  `stepRemoveRequested(index)` / `stepMoveRequested(index,down)` / `resetRequested()`）→ `PreprocessController` 对应槽（`onStepAppend/Replace/Remove/Move`、`onResetPreprocess`）。控制器维持「几何块在前 + 色彩块在后」不变式：几何步插到几何块尾、色彩步追加队尾，跨类上下移忽略。
  两者均汇入公共收尾 `commitPipeline(nextSteps, nextResize, okMsg, busyText)`：以「新轻队列 + 新 resize」为结果态，先用有效管线（`[resize?]+steps_`）
  经 `geometryOutput` **推演几何签名**判断尺寸/净旋转象限是否变（变则先清失效选区与标注），再基于 `base_` 缓存重放——**仅 resize 前缀变化（baseDirty）才重烤 `base_`**（唯一重采样点），
  轻编辑复用 `base_` 只跑轻后缀。忙碌框门控收紧为「baseDirty **且**确有 resize 需重采样」才弹（无 resize 的每步编辑不弹框、不改尺寸则不复位缩放，消除卡顿感）。
  实际像素变换经 `EngineBridge` 调 Core，**面板不碰 Core、不持真相**；`resetRequested` 清空 resize 与整条队列、还原原图。原图始终保留。
- **选中跟随**：上/下移某步后，`ImagePanel` 记住被移动项的新下标（`pendingSelectRow_`），`syncFromDocument` 重建列表后据此高亮跟随，而非停留在原下标（那会指向被换上来的另一操作）。
- **配置载入归一化**：`applyLoadedQueue(pipeline)` 把散落的 `ResizeOp` 抽出为独立 `resize_`（多个取最后一个正尺寸），其余按序进轻队列——旧配置载入即归一化为「resize 首位 + 轻操作在后」。序列化仍复用 `preprocess` 数组（`buildSerializableConfig` 把 resize 恒置于首元素），**不改 Core/CLI/§7 schema**。
- 入队的 `cfg` 是轻操作 `PreprocessConfig = variant<GrayOp,SplitOp,InvertOp,RotateOp,FlipOp>`（`steps_` 绝不含 `ResizeOp`）。
- 大图缩放/重采样经 `runWithBusyDialog(text, op)`：应用级模态、不可取消的 `QProgressDialog`，配 `busyResample_` 重入守卫（仅 resize 重采样路径触发）。
- **标注随框选隐柄**：`CanvasView::gestureMarqueeBegin`（空白处起框）发 `marqueeStarted` → `AnnotationCoordinator` 若当前有选中则 `clearSelection()`，使标注 OBB 手柄随框选即时消失（点选/拖动标注由图元接管、不走 marquee）。
- 标注（`AnnotationCoordinator` 编排）：每个 Core `Annotation` 对应一个 `AnnotationItem`（`QGraphicsObject`），用 `Shape::worldPath()`→`QPainterPath`
  矢量叠加绘制（z=`kAnnotation`）；OBB 非破坏性变换（决策 #9）与烧录仅在导出时发生（决策 #8）；
  移动标注经 Core `Shape::translateWorld` 世界系平移，保留具体类型（不退化为 PATH）。
- 撤销/重做与配置保存/加载的机制见决策 #10 / #11；输出预览渲染见决策 #12。

## 扩展指引

### 新增一个右侧面板

1. 在 `include/panels/` 加头文件（`<模块>_panel.h`）、`src/panels/` 加实现，类继承 `QWidget`、`namespace idc::gui`。
2. 面板**只发意图信号 / 只读写 `Document`**，不含业务逻辑；构造用布局管理器，控件加中文 tooltip。
3. 在 `MainWindowUi::buildCentral` 中实例化并加入 `rightTabs_`（`addTab`）；面板意图信号连到对应控制器
   （预处理意图→`PreprocessController`、标注/图层意图→`AnnotationCoordinator`、选项卡切换→`PreviewController`），
   画布交互意图在 `MainWindow::connectAll()` 连到编排槽。
4. 若面板状态需持久化，扩展 `Document::buildEngineConfig/applyEngineConfig`（保持与配置加载互逆）。
5. 在 `include/panels/README.md` 与 `src/panels/README.md` 补声明边界/实现说明（工程规范见 CONTRIBUTING.md）。

### 新增一个画布交互

1. 在 `CanvasView`（视图级手势）或对应图元（`SelectionRectItem`/`CellPickerItem`/`AnnotationItem`）里处理事件，
   发语义信号（如 `nudgeSelection`），**不在图元内直接改 Document**。
2. 在 `MainWindow::connectAll()` 连接信号到编排槽，槽内写回 `Document` → 触发 `changed` → `refreshPreview`。
3. 新增图层图元时在 `z_order.h` 追加 z 常量（保持单一定义点），并在 `CanvasScene` 提供 `update*/clear*` 接口。
4. 拖拽类交互注意：拖拽期跳过 `syncPanels` 与对正在拖图元的回设（避免抖动），释放时同步一次。

### 新增一种标注形状

> ⚠️ 标注几何/命中/光栅化全在 Core，**须先扩展 Core `ShapeType` 与 `ShapeFactory`**（改 Core 前先征得同意）。
> Core 支持后：在 `AnnotationBridge::AnnoTool` 加枚举、`tool_panel.cpp` 的 `kTools[]` 加一行、
> `util::paintAnnotation`/`AnnotationItem` 走 `worldPath()` 自动获得渲染，无需在 GUI 补几何。

### 调用 Core 的纪律

- 任何切割/几何/排序/极性/导出/JSON/编码逻辑**一律调 Core**，GUI 不重复实现。
- 发现 Core 缺接口：不在 GUI 补，改为报告 + `// TODO(core): 需要 Core 提供 xxx` 标注。
- L1/L2/L3 必须走同一引擎路径，模式切换只切参数面板与预览呈现。

## 已知限制

| 项                                  | 现状                                | 根因 / 说明                                                                                                                                                                |
|------------------------------------|-----------------------------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 预处理全局撤销                            | 不纳入 `Ctrl+Z`                      | `docHistory_` 仅快照 `EngineConfig` 参数态、不含像素；靠「重置预处理」回退（产品取舍，决策 #10）                                                                                                      |
| 标注配置还原的撤销                          | 不纳入文档撤销栈                          | `replaceAnnotations` 走 Core `restoreAnnotations`（不推撤销快照）；标注域自身可 `Ctrl+Z` 逐层回退，但「加载配置」这一步不可整体撤销（决策 #11）                                                                 |
| 网格线吸附                              | 未接入                               | `selection_rect_item` 吸附目标仅图像边缘 + 中心（NFR-7 要求三选，尚缺网格线）                                                                                                                 |
| 插值方式下拉                             | 未提供                               | `engine_bridge` resize 固定双线性；Core 支持 `ResampleMode{NEAREST,BILINEAR}`，图像面板未暴露选择                                                                                        |
| 队列重放性能（已用 base_ 缓存 + 独立 resize 根治） | base_ 缓存重放                        | resize 已从可排序队列剥离为独立常驻设置，`base_` = resize(original)（无 resize 则原图副本）：轻操作队列编辑**恒复用 `base_`、不重采样、不弹框**；仅 resize 目标变化才重烤 `base_` 并（有实际重采样时）弹框。**代价**：额外常驻一张 `base_`（见下行内存）。 |
| 队列编辑不可撤销                           | 不纳入 `Ctrl+Z`                      | 删/改/移步骤后文档撤销栈找不回（旧模型同样没有，非回归）；队列模型天然支持单步撤销，属未实现的机会项                                                                                                                    |
| 大图内存驻留                             | `original_`+`base_`+`working_` 三份 | 工作图常驻、预览从全分辨率 `working_` 降采样；`base_` 缓存根治重放性能后新增第三份常驻图（前缀重放产物，随换图 / 重置预处理失效）；「`working_` 不常驻 / `previewBase` 小图重放」的进一步优化未实现                                            |
| 8000×8000 ≥30fps                   | 未正式验证                             | 预览已用降采样副本满足设计目标，但性能验收尚未落地实测（NFR-3）                                                                                                                                     |

## 构建

前置：CMake ≥ 3.28 + vcpkg（设置环境变量 `VCPKG_ROOT` 指向 vcpkg 根目录）。
依赖经 [`vcpkg.json`](../vcpkg.json) 清单自动安装（`qtbase` 首次构建较慢），推荐用 Presets：

```bash
cmake --preset windows-debug   # 以 ImageDiscropperCpp/ 为源目录（Ninja + MSVC，Debug）
cmake --build build/windows-debug --target idc_gui
```

手动方式：`cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake && cmake --build build --target idc_gui`（清单模式自动生效）。
Linux / macOS 用 `linux-debug` / `macos-debug` 预设（Linux 上 qtbase 构建需系统 X11/EGL 等开发库，清单见 CI 工作流）。
GUI 构建验证：每次 push 由 ci.yml 三平台 **Release** 构建（与发布产物同构）；**Debug** 配置的三平台构建手动运行（cpp-verify.yml 的 `debug-all`）；运行验证以 Windows 为主。
根 `CMakeLists.txt` 已在 `add_subdirectory(ImageDiscropperCli)` 之后追加 `add_subdirectory(ImageDiscropperGui)`。

- 版本号经编译期宏注入：`IDC_GUI_VERSION`（本工程版本）与 `IDC_CORE_VERSION`（Core 经 PARENT_SCOPE 回传，与 CLI 同一来源；独立配置本目录时为占位版本）。
- 仅链接 `Qt6::Widgets Qt6::Gui Qt6::Core` + `image_discropper_core`。
- vcpkg qtbase 附带的 `DB2/PPS/OCI/Libb2` 等**可选驱动目标本 GUI 不需要，故不链接**（否则要求额外库、易失败）。
- Core 的第三方依赖（stb / WebP / nlohmann_json）在 Core 侧以 PRIVATE 接入，GUI 无需感知。

## 许可

本模块随仓库整体采用 **GPL-3.0**（见根 [`LICENSE`](../../LICENSE)）。
