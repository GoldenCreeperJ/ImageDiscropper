# include/annotation

**目录作用**：**标注集合**，把「几何数据」装配为「可绘制标注」，并光栅化到 `core::Image`；
承载标注数据列表（含 zOrder 叠放不变式）、命中检测与烧录合成，并作为 SPEC §7 config `annotations` 段的读写容器。
对应 SPEC §3.1（FR-1.3）「标注：作为独立图层存在，可在导出时选择是否烧录（burn-in）」。

> 编辑会话态（编辑模式 / 当前属性模板 / 选中 / 撤销重做）**不在 Core**，已拆至 GUI `AnnotationEditor`（app 层）；
> Core 只保留「无头侧也成立」的数据与合成能力。

> 本目录由早期的 `drawing`（绘图）模块重命名、重定位而来。设计约束（SPEC §3.1）：
> 标注图层与切割操作解耦——切割时标注随像素一起被切开，而非单独处理。

**分块依据**：
1. `shape_factory.h` —— 只负责「给定两个锚点 + `ShapeType`，构造 `Shape`」。
2. `rasterizer.h` —— 只负责「把 `Shape` 画到 `Image` 上」：`PaintStyle` 区分**描边色 `color` 与填充色 `fillColor`**；
   当 `fill` 为真且形状非 LINE 时先用 `fillColor` 填充内部（折线 / 画笔按首尾隐式闭合），随后始终按 `color` 描边（填充形状因此也带边框）；支持 Alpha 混合与抗锯齿。
3. `annotation_set.h` —— 纯数据标注集合 `AnnotationSet`：拥有 `std::vector<Annotation>`，提供结构操作（`add` / `removeAt` / `moveItem` / `popBack` / `clear` / `restore`）
   与查询合成（`hitTest` 顶层优先、`burnIn(base)` 逐条光栅合成）；每条 `Annotation` 携带显式 `zOrder` **恒等于列表下标（大＝顶）**，
   任何结构操作后统一规范化为下标（`add` / `removeAt` / `moveItem` / `restore` 内部自动规范化；随 SPEC §7 `annotations` 段逐条导入/导出，导入按 `zOrder` 稳定排序）；
   `restore(items)` 供配置还原整体替换（跳过空 shape、末尾规范化）；`mutableAnnotations()` + `canonicalizeZOrder()` 供上层 EDIT 就地改样式。
   **不持底图、不含交互态**：无参 `burnIn()` 已取消（改由调用方显式传 base），编辑模式 / 属性模板 / 选中 / 撤销重做等会话态归 GUI `AnnotationEditor`（其组合本集合并自行编排条目级撤销）。
4. `view_transform.h` —— 屏幕坐标 ↔ 逻辑坐标换算、以指定点缩放、图像适配视口；Core 提供的 Qt 无关视口工具，当前由 examples/tests 使用（GUI 改用 QGraphicsView 自带变换，未直接用本类）。

| 文件                 | 职责                                                                                             |
|--------------------|------------------------------------------------------------------------------------------------|
| `shape_factory.h`  | 依据 `ShapeType` 与锚点构造 `Shape`（`buildShape`）                                                     |
| `rasterizer.h`     | 光栅化：边框色 / 填充色分离、描边、填充、Alpha 混合（`rasterize` / `PaintStyle`）                                     |
| `annotation_set.h` | 标注集合 `AnnotationSet` 与标注记录 `Annotation`（数据 + zOrder 不变式 + 命中 / 合成；会话态见 GUI `AnnotationEditor`） |
| `view_transform.h` | 视图坐标变换 `ViewTransform`                                                                         |
