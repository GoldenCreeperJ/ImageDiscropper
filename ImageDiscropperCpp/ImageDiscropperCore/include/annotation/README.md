# include/annotation

**目录作用**：**标注层**，把「几何数据」装配为「可绘制标注」，并光栅化到 `core::Image`；
同时管理标注会话状态（标注列表、当前工具、当前属性、编辑模式、撤销重做）。
对应 SPEC §3.1（FR-1.3）「标注：作为独立图层存在，可在导出时选择是否烧录（burn-in）」。

> 本目录由早期的 `drawing`（绘图）模块重命名、重定位而来。设计约束（SPEC §3.1）：
> 标注图层与切割操作解耦——切割时标注随像素一起被切开，而非单独处理。

**分块依据**：
1. `shape_factory.h` —— 只负责「给定两个锚点 + `ShapeType`，构造 `Shape`」。
2. `rasterizer.h` —— 只负责「把 `Shape` 画到 `Image` 上」：`PaintStyle` 区分**描边色 `color` 与填充色 `fillColor`**；
   当 `fill` 为真且形状非 LINE 时先用 `fillColor` 填充内部（折线 / 画笔按首尾隐式闭合），随后始终按 `color` 描边（填充形状因此也带边框）；支持 Alpha 混合与抗锯齿。
3. `annotation_layer.h` —— 管理标注集合、编辑模式、边框色 / **独立填充色** / 线宽 / 填充开关 / **转角圆角半径**属性（新建标注的填充色默认跟随边框色，显式改过后断开联动），删除指定项（`removeAnnotation`），撤销重做复用 `history::HistoryManager<Annotation>`；
   `burnIn()` 把底图与全部标注合成为一张图像（即「烧录」）。
4. `view_transform.h` —— 屏幕坐标 ↔ 逻辑坐标换算、以指定点缩放、图像适配视口；Core 提供的 Qt 无关视口工具，当前由 examples/tests 使用（GUI 改用 QGraphicsView 自带变换，未直接用本类）。

| 文件                   | 职责                                                         |
|----------------------|------------------------------------------------------------|
| `shape_factory.h`    | 依据 `ShapeType` 与锚点构造 `Shape`（`buildShape`）                 |
| `rasterizer.h`       | 光栅化：边框色 / 填充色分离、描边、填充、Alpha 混合（`rasterize` / `PaintStyle`） |
| `annotation_layer.h` | 标注会话状态机 `AnnotationLayer` 与标注记录 `Annotation`               |
| `view_transform.h`   | 视图坐标变换 `ViewTransform`                                     |
