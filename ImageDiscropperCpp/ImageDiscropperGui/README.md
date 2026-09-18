# ImageDiscropperGui — Qt 6 图形前端

本目录是 ImageDiscropper 的 **GUI 层**（可执行目标 `idc_gui`），基于 **Qt 6.x + Widgets + QGraphicsView**。
GUI 是 Core（`image_discropper_core` 静态库）的**纯消费者**：只采集交互、把交互翻译为 Core 参数、
调用 Core、渲染 Core 的结果；**不实现任何切割 / 几何 / 排序 / 极性 / 导出逻辑**。

> 命名空间统一为 `idc::gui`。本文件由原独立文档 `USAGE.md`（用户操作手册）与 `DESIGN.md`（设计说明）合并而来：
> 最终用户读「用户操作速览」，维护者读「关键设计决策」「布局与交互要点」与「扩展指引」。
> 功能规格（唯一行为基线）见 [`../SPEC.md`](../SPEC.md)，贡献约定见根 [`CONTRIBUTING.md`](../../CONTRIBUTING.md)。

## 目录结构与职责

采用与 Core / CLI 一致的 **include/src 分离**：头文件与其**声明边界 `README.md`** 在 `include/<模块>/`，
实现与其**实现说明 `README.md`** 在 `src/<模块>/`（与 Core 的 include/src 双侧 README 约定一致）。构建包含根为 `include`，故源码内以 `"canvas/…"`、`"model/…"` 形式互相引用。

| 模块       | 头文件               | 实现 + README                         | 职责                                                                              |
|----------|-------------------|-------------------------------------|---------------------------------------------------------------------------------|
| `util`   | `include/util/`   | [src/util/](src/util/README.md)     | `core::Image` ↔ Qt 图像适配、`geometry::Path`/`core::Color` ↔ Qt 绘图类型适配、降采样预览（视图关注点） |
| `model`  | `include/model/`  | [src/model/](src/model/README.md)   | 会话状态单一真相源 `Document`、唯一触碰切割引擎的 `EngineBridge`、唯一驱动 Core 标注的 `AnnotationBridge`  |
| `canvas` | `include/canvas/` | [src/canvas/](src/canvas/README.md) | QGraphicsView/Scene 画布：图层化渲染底图/遮罩/切割线/选区/标注矢量叠加 + 交互                            |
| `panels` | `include/panels/` | [src/panels/](src/panels/README.md) | 左侧模式/极性面板与标注工具/图层面板、右侧参数/导出/图像/标注属性面板                                           |
| `app`    | `include/app/`    | [src/app/](src/app/README.md)       | 主窗口装配与编排、程序入口                                                                   |

**依赖方向**：`app → panels/canvas/model → util → Core`。`model` 是唯一触碰 Core 引擎/标注的层，
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
- **右侧面板**（`QTabWidget`，四页）：**参数页**（随模式变化）、**导出页**、**图像页**（预处理）、**标注页**（标注属性）。
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

### 预处理（图像菜单 / 图像页）

预处理在**切割之前**执行，作用于「工作图」；**原图始终保留**，可随时「重置预处理」还原。
入口：**图像菜单** 或 右侧 **图像页**（二者共用同一批操作）。

| 操作    | 说明                             |
|-------|--------------------------------|
| 旋转    | 左转 90° / 右转 90° / 180°（步进按钮）   |
| 翻转    | 水平翻转 / 垂直翻转                    |
| 缩放    | 按比例（%）或按目标宽/高，可勾选「保持宽高比」       |
| 图像尺寸  | 直接指定目标宽/高重采样                   |
| 黑白    | 转灰度                            |
| 反色    | 对勾选的 R/G/B 通道取反（图像菜单「反色」默认全通道） |
| 色道分离  | 仅保留勾选的 R/G/B 通道                |
| 重置预处理 | 把工作图还原为原图                      |

> **注意**：旋转 90°/270° 或缩放会改变图像尺寸，此时**基于旧尺寸的选区会被自动清除**，需重新框选。
> 大图缩放/重采样可能耗时，期间会弹出「处理中」进度对话框（不可取消），完成后自动关闭。
>
> **已知取舍**：预处理**不纳入 Ctrl+Z 全局撤销**（全局撤销只覆盖切割参数态），如需回退请用「重置预处理」。

### 标注工具

标注是**独立矢量图层**，叠加在底图之上、**不改底图像素**；只有导出时勾选「烧录」才会合成进像素。

**添加标注**：

1. 在左侧 **标注工具面板** 选择工具（见下表）。
2. 在画布上拖拽/单击绘制：
   - **两点形状**（矩形、正方形、圆、椭圆等）：按下拖拽实时预览，松手提交。
   - **多线段 / 画笔**：连续单击追加顶点 / 按住拖动绘制自由路径，按 **Esc** 或切换工具收笔。
   - **文字**：单击落点，弹出输入框填写文本后添加。

| 工具                     | 绘制方式                                   |
|------------------------|----------------------------------------|
| 选择/移动                  | 选择并拖动已有标注（`Delete` 删除，`Ctrl+Z/Y` 撤销重做） |
| 矩形 / 正方形               | 两点拖拽                                   |
| 菱形                     | 起点为中心，拖拽绘制                             |
| 圆形                     | 起点为圆心，两点距离为半径                          |
| 椭圆                     | 两点确定外接矩形                               |
| 圆角矩形                   | 两点拖拽                                   |
| 等腰 / 等边 / 直角 / 等腰直角三角形 | 按各自规则拖拽                                |
| 直线                     | 两点拖拽                                   |
| 多线段                    | 连续单击追加顶点，Esc 收笔                        |
| 文字                     | 单击落点后输入                                |
| 画笔                     | 按住拖动绘制自由路径，Esc 收笔                      |

**编辑标注属性**：选中标注后，右侧 **标注页** 可设置：**颜色、粗细、填充、文字内容、字号**，以及 **OBB 变换**（带符号缩放 X/Y、旋转角度，改动即生效、无「应用」按钮）。
画布上选中标注会出现 **8 个缩放手柄 + 1 个旋转手柄**（定向包围盒 OBB），拖拽即做缩放/拉伸/旋转/翻转（拖手柄越过对边＝翻转）。

**标注菜单**：`标注` 菜单提供：撤销标注 / 重做标注 / 删除选中 / 清除全部（二次确认）/ 标注属性定位。

> **已知限制**：当前标注类型**不含「箭头」与「扇形」**——底层 Core 的 `ShapeType` 尚未提供这两种类型
> （可用「直线 + 多线段」或「圆 / 椭圆」近似替代，详见「已知限制」）。

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

- **文件 → 保存配置…**：把当前作业参数（模式、切割、选择集、排序、导出参数）写为 SPEC §7 schema 的 JSON 文件。
- **文件 → 加载配置…**：读取 JSON 反向还原到界面；**加载本身可一步撤销**（入同一撤销栈）。
- 配置**不含图像像素**，仅保存参数态。

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
4. **降采样预览复用 `pixel_ops::resize`**：`makePreview(img, maxDim)` 内部调用 Core 的 resize（NEAREST）生成预览副本，
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
   **烧录仅在导出时发生**：`AnnotationBridge::burnIn(base)` 以**当前工作图**为底逐个调 Core `rasterize` 合成（不复用 `AnnotationLayer::burnIn()` 内部可能陈旧的 `image_`），
   再送引擎切割——标注随像素被切开。标注几何/命中/光栅化全在 Core，GUI 只采集手势/翻译参数/渲染；
   标注撤销/重做复用 Core `AnnotationLayer` 内建 `revoke()/redo()`（分层快照）。
9. **标注 OBB 非破坏性变换（画布手柄 + 面板绝对值）**：选中标注后画布绘**定向包围盒（OBB）** 的 8 缩放手柄 + 1 旋转手柄，
   拖拽经 Core `Shape::obbPreviewTransform`（逐帧矢量预览）/`applyObbTransform`（释放提交）做缩放/拉伸/旋转/翻转——变换以矩阵 `xform_`
   累积、**不改子类参数化几何**（类型/文字字形不退化），渲染/命中走 `worldPath()`；负缩放即翻转（拖手柄越过对边自然产生）。
   属性面板变换区 spin 显示 Core 累积的**绝对带符号值**（`obbScaleX()`/`obbScaleY()`/`obbRotationDeg()`，含负）、**改动即生效无「应用」按钮**
   （每 spin 按单轴换算增量下发，避免取整误差牵连其余轴）。矩阵运算全在 Core，GUI 只采集手柄拖拽、把世界光标经 `worldToLocal` 映回局部算系数。
10. **全局撤销/重做＝双历史 + 上下文路由**：撤销分两条独立历史——①**文档参数态**用 Core `HistoryManager<EngineConfig>`
   快照（`capture=Document::buildEngineConfig`、`restore=Document::applyEngineConfig`，与配置加载同构、复用同一对互逆映射）；②**标注**沿用 Core
   `AnnotationLayer` 内建分层快照（`AnnotationBridge::undo/redo`）。编辑菜单/工具栏的 Ctrl+Z/Y 经 `MainWindow::onUndo/onRedo` **上下文路由**：
   标注工具激活（非 SELECT）或存在选中标注时转发到标注撤销，否则走文档参数撤销——两条历史不再各绑 Ctrl+Z/Y（消除原标注菜单与编辑菜单的快捷键冲突）。
   文档历史用 **500ms 防抖定时器**把拖拽/连点的连续 `changed()` 合并为一条（栈顶恒为「当前态」，`undoSize>1` 才可撤销）；还原期以 `suppressHistory_` 抑制再采集防回环。
   **预处理不纳入全局撤销**（快照仅参数态、不含像素），靠现有「重置预处理」回退——此为产品决策，与「预处理可撤销」的理想态有意取舍。
11. **配置文件加载/保存**：文件菜单「加载配置/保存配置」经 `EngineBridge::saveConfig/loadConfig` 委托 Core `saveEngineConfig/loadEngineConfig`
   （规格 SPEC §7 schema 的 JSON，GUI 不自实现 JSON、不感知 nlohmann）；加载走 `Document::applyEngineConfig` 反向映射回各状态字段（生成器+tier 还原 L1 形状/L2 子功能，
   并守卫「非 L3 不得重排」「网格单元尺寸为正」不变式），且**加载本身入同一撤销栈**（可一步撤销回载入前）。
12. **导出前输出图像预览（只渲染不重算）**：导出面板新增 `previewLabel_` 缩略图，由 `util::composeOutputThumbnail`（`util/output_preview_renderer`，无状态自由函数）按
   `refreshPreview` **已算好**的 `EngineResult.composition` 渲染——**不再跑 Core、不落盘**：把 `rebuildPreviewPixmap` 缓存的**小源图**
   `exportSrcPixmap_`（最长边 ≤ 512，与画布底图分离）按 `placements` 的 `source→dest` 用 `QPainter::drawPixmap` blit 到最长边 ≤ 192 的小画布。
   合并模式（坍缩/重排）按真实画布等比缩放、所见即所得；分离模式（无统一画布）拼成触图（cols=ceil(√n)）。源区域坐标经 `exportSrcScale*`
   （working→小图放大系数）映射，故即便原图 8000×8000 也只在小图上运算（**像素少、速度快**）。面板只负责展示（`setPreviewPixmap`，仅缩不放）；**渲染逻辑下沉到 `util/`（离屏预览），MainWindow 只编排缓存与门控**。
   **B（烧录可见）**：`exportPreviewSource()` 委托 `util::bakeAnnotationsInto`，在「导出时烧录标注」开启且有标注时，把各标注按 `worldPath` 矢量绘到小源图副本（working→小图变换，文字绘字形；实际绘制复用 `util::paintAnnotation`，与画布图元同一外观）——**只烘焙一次**，随后逐块 blit 自动让标注随像素被切割落位，与真实导出一致；细线在大图上按缩略比例自然淡化（忠实）。
   **A（按需渲染）**：`updateExportPreview` 先缓存 `lastComposition_/lastExportResOk_`，再由 `renderExportPreviewFromCache` 仅在「导出」页为当前选项卡时渲染，否则置 `exportPreviewDirty_`；`onRightTabChanged` 切回该页时用缓存补渲染。烧录开关/标注变更（`onBurnInChanged`/`onAnnoBridgeChanged`，仅烧录开启时）**不重跑 Core**，仅用缓存重渲染缩略图（切割几何不受标注影响），保证预览实时。

## 已核对的 Core API（均来自实际头文件，非假设）

- 编排：`engine::runEngine(const core::Image&, const EngineConfig&) -> EngineResult{ok,error,kept,collapsible,composition}`。
- 切割线：`engine::generateCutLines(const CutConfig&, const SourceInfo&) -> CutLineSet`。
- 配置：`EngineConfig{source,preprocess,cut,selectedCells,order,emitParams}`；
  `CutConfig{tier,generator(RECT/HORIZONTAL_LINE/VERTICAL_LINE/MULTI_RECT/GRID),rect,rects,grid,polarity}`。
- 配置存取：`engine::saveEngineConfig(path, cfg)` / `engine::loadEngineConfig(path, out&)`（SPEC §7 schema JSON，nlohmann 在 Core 侧 PRIVATE）。
- 撤销历史：`history::HistoryManager<T>{push,popToRedo,popFromRedo,top,canUndo,canRedo,undoSize,clearAll}`（模板头实现，GUI 以 `T=EngineConfig` 实例化）。
- 导出：`CompositionParams{mode,layout,mergeOrder,cols,rows,cellWidth,cellHeight,padColor,format,naming,quality,keepMetadata}`
  （`mergeOrder` = 重排填充顺序 `MergeOrder{strategy,reverse,snake}`，仅 REARRANGE、与选择排序正交）；
  `Composition{canvasWidth,canvasHeight,placements,...}`；`engine::exportImage(...)`。
- 区域：`RectRegion{left,top,right,bottom,width(),height(),area()}`；`RegionSet{size(),empty(),fragments(),boundingBox()}`；
  `Fragment{region,index,kind,row,col}`。
- I/O 与处理：`engine::readImageFile`（统一解码为 RGBA）；`pixel_ops::resize/rotate/flip/...`；`core::Image{width,height,empty,getPixel,inBounds,...}`；
  `core::Color{r,g,b,a}`。
- 标注：`geometry::Shape::worldPath()/obbPreviewTransform/applyObbTransform/translateWorld`；`annotation::AnnotationLayer`（`revoke/redo/removeAnnotation/rasterize`）。

## 布局与交互要点

### 布局结构与响应式规则

- 顶层 `QSplitter` 水平三分：**左侧 `QScrollArea`（竖排 LeftPanel + ToolPanel + LayerPanel）** | **中央 `CanvasView`** | **右侧 `QTabWidget`（参数页 / 导出页 / 图像页 / 标注页）**。
- **左侧面板最小宽 340px**（容纳 3 列标注工具）、**右侧面板约 300px**，均可随 `QSplitter` 拖拽调整且不可折叠消失；中央画布占最大空间、伸缩优先。
- 左侧用 `QScrollArea` 包裹，窗口变窄/内容变高时出现纵向滚动条而不挤压画布。
- 右侧用 `QTabWidget` 分组，避免面板过长；**输出预览缩略图仅在「导出」页为当前选项卡时才渲染**（决策 #12 按需渲染）。
- 初始分割为左 340 / 中央 860 / 右 300（`split->setSizes`），此后可自由拖拽（尺寸不持久化）；控件用布局管理器（`QVBoxLayout`/`QGridLayout`）自适应，无绝对定位。

### 图层 z 序（`include/canvas/z_order.h`，单一定义点）

| z 值 | 常量            | 图层                                                                      |
|-----|---------------|-------------------------------------------------------------------------|
| 0   | `kBase`       | 底图（预处理后）                                                                |
| 10  | `kAnnotation` | 标注图层（矢量叠加）                                                              |
| 20  | `kDelete`     | 删除块遮罩（红，铺满全图作底）                                                         |
| 30  | `kKeep`       | 保留块遮罩（绿，叠加于红之上）                                                         |
| 40  | `kGrid`       | 网格线（L3 / L2 多矩形诱导）                                                      |
| 50  | `kCutLine`    | 切割线层（保留值；切割线已并入选区图元）                                                    |
| 60  | `kSelection`  | 选区边框（橙）+ 手柄 + 贯穿切割线延伸（同一图元）                                             |
| 70  | `kCellNumber` | 单元格编号角标（L3 自定义序；`CellPickerItem` 的角标子图层，子 z = kCellNumber − kSelection） |

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
   │                                      │ buildEngineConfig + EngineBridge.runPreview
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

- `ImagePanel` 发意图信号（`rotateRequested`/`flipRequested`/`scaleRequested`/`resizeRequested`/`grayRequested`/
  `invertRequested`/`splitRequested`/`resetRequested`）→ `MainWindow` 对应 `on*` 槽经 `EngineBridge` 调 Core
  `pixel_ops::*` 变换 `doc_.working()` → 公共收尾 `applyWorkingImage(next, okMsg)`：结果为空图则报错；
  **维度变化（旋转 90/270、缩放）时先清除失效选区**（`clearRect/clearRects/clearCells`，坐标基于旧尺寸）再
  `setWorkingImage`（触发 `imageChanged` → 重建预览底图 + 刷新）；`onResetPreprocess` 把工作图还原为 `original_`（原图始终保留）。
- 色道反色/分离共用面板 R/G/B 复选框作「作用通道」选择器（`invertChannels` 本就支持掩码，无需改 Core）。
- 大图缩放/重采样经 `runWithBusyDialog(text, op)`：应用级模态、不可取消的 `QProgressDialog`，配 `busyResample_` 重入守卫。
- 标注：每个 Core `Annotation` 对应一个 `AnnotationItem`（`QGraphicsObject`），用 `Shape::worldPath()`→`QPainterPath`
  矢量叠加绘制（z=`kAnnotation`）；OBB 非破坏性变换（决策 #9）与烧录仅在导出时发生（决策 #8）；
  移动标注经 Core `Shape::translateWorld` 世界系平移，保留具体类型（不退化为 PATH）。
- 撤销/重做与配置保存/加载的机制见决策 #10 / #11；输出预览渲染见决策 #12。

## 扩展指引

### 新增一个右侧面板

1. 在 `include/panels/` 加头文件（`<模块>_panel.h`）、`src/panels/` 加实现，类继承 `QWidget`、`namespace idc::gui`。
2. 面板**只发意图信号 / 只读写 `Document`**，不含业务逻辑；构造用布局管理器，控件加中文 tooltip。
3. 在 `MainWindow` 构造中实例化并加入 `rightTabs_`（`addTab`），在 `connectAll()` 里把面板信号连到编排槽。
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

| 项                | 现状           | 根因 / 说明                                                                                         |
|------------------|--------------|-------------------------------------------------------------------------------------------------|
| 箭头标注             | 未实现          | Core `ShapeType` 无 `ARROW`（`annotation_bridge.h`/`tool_panel.h` 已注明），需先扩展 Core；可用「直线 + 多线段」近似替代 |
| 扇形标注             | 未实现          | Core `ShapeType` 无 `SECTOR/PIE`，同上；可用「圆 / 椭圆」近似替代                                               |
| 预处理全局撤销          | 不纳入 `Ctrl+Z` | `docHistory_` 仅快照 `EngineConfig` 参数态、不含像素；靠「重置预处理」回退（产品取舍，决策 #10）                               |
| 网格线吸附            | 未接入          | `selection_rect_item` 吸附目标仅图像边缘 + 中心（NFR-7 要求三选，尚缺网格线）                                          |
| 插值方式下拉           | 未提供          | `engine_bridge` resize 固定双线性；Core 支持 `ResampleMode{NEAREST,BILINEAR}`，图像面板未暴露选择                 |
| 8000×8000 ≥30fps | 未正式验证        | 预览已用降采样副本满足设计目标，但性能验收尚未落地实测（NFR-3）                                                              |

## 构建

前置：CLion（或命令行）以**仓库根**为 CMake 源，并配置好 vcpkg 工具链（`CMAKE_TOOLCHAIN_FILE`），
且 vcpkg 已安装 `qtbase`（classic 模式，仓库无 `vcpkg.json`/`CMakePresets.json`）。

根 `CMakeLists.txt` 已在 `add_subdirectory(ImageDiscropperCli)` 之后追加 `add_subdirectory(ImageDiscropperGui)`。

```bash
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
cmake --build build --target idc_gui
```

- 版本号经编译期宏注入：`IDC_GUI_VERSION`（本工程版本）与 `IDC_CORE_VERSION`（Core 经 PARENT_SCOPE 回传，与 CLI 同一来源；独立配置本目录时为占位版本）。
- 仅链接 `Qt6::Widgets Qt6::Gui Qt6::Core` + `image_discropper_core`。
- vcpkg qtbase 附带的 `DB2/PPS/OCI/Libb2` 等**可选驱动目标本 GUI 不需要，故不链接**（否则要求额外库、易失败）。
- Core 的第三方依赖（stb / WebP / nlohmann_json）在 Core 侧以 PRIVATE 接入，GUI 无需感知。

## 许可

本模块随仓库整体采用 **GPL-3.0**（见根 [`LICENSE`](../../LICENSE)）。
