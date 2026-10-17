# src/annotation

**目录作用**：`include/annotation` 下头文件的实现代码，即「标注集合」——在原图上绘制矩形 / 椭圆 /
直线 / 文字等矢量标注，并支持把标注烧录（burn-in）进像素。对应 SPEC §3.1（FR-1.3）「标注图层」。

**分块依据**：按「构造 → 光栅化 → 标注集合 → 视图坐标 → 段级 JSON」五种关注点拆分，互不耦合。
- `shape_factory.cpp` 只关心「锚点 → Shape」的构造分支，与像素无关。
- `rasterizer.cpp` 只关心「Shape → Image」的像素写入：先用 `fillColor`（`fill` 为真且非 Line，折线 / 画笔隐式闭合）扫描线填充内部，再用 `color` 抗锯齿描边，并做 Alpha 混合。
- `annotation_set.cpp` 只关心标注数据列表的结构操作（`add` / `removeAt` / `moveItem` / `popBack` / `clear` / `restore`）与 zOrder 不变式规范化、命中检测（`hitTest` 顶层优先）与烧录合成（`burnIn(base)`）——**不持底图、不含会话态**：当前工具 / 编辑模式 / 选中 / 撤销重做归 GUI `AnnotationEditor`（其组合本集合并自行编排条目级撤销）。
- `view_transform.cpp` 只关心屏幕 / 逻辑坐标换算与视图适配。
- `annotation_json.cpp` 只关心 SPEC §7 `annotations` **段元素**的 JSON ↔ `Annotation`：段级重建与逐条校验在此（不拼装整档文档）。
  对内供 `engine_config_json` 调用（`annotationsSegmentToJson` / `annotationsFromSegmentJson`），对外实现 `annotation_json.h` 的
  `parseAnnotationsFromJsonText`；非法条目的报因文案走 `IDC_KTR` / `IDC_KTRF` 取 Core 常量层（可译），拼接后的逐条串经
  `loadEngineConfig` 的第 4 出参 `annoErr` 上抛给 CLI。

| 文件                    | 职责                                                                                                                                                                        |
|-----------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `shape_factory.cpp`   | 依据 `ShapeType` 与锚点构造 `Shape`                                                                                                                                              |
| `rasterizer.cpp`      | 光栅化：边框色 / 填充色分离、描边、填充、Alpha 混合                                                                                                                                            |
| `annotation_set.cpp`  | 标注集合：结构操作（`add`/`removeAt`/`moveItem`/`popBack`/`restore`）、zOrder 不变式、命中（`hitTest`）、烧录合成（`burnIn(base)`）                                                                  |
| `annotation_json.cpp` | 标注段 JSON：`annotationsSegmentToJson` / `annotationsFromSegmentJson`（段级重建 + 逐条校验，报因经 `annoErr` 上抛）与 `parseAnnotationsFromJsonText`（CLI `--annotation` / `--annotations` 入口） |
| `view_transform.cpp`  | 屏幕 / 逻辑坐标换算，以指定点缩放，图像适配视口                                                                                                                                                 |
