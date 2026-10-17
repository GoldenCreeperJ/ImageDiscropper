# src/canvas

**目录作用**：`include/canvas` 多层画布数据模型与合成的**实现**。与头文件一一对应，
无额外私有子模块（像素核复用 `core::Image` / `core::compositeOver`，预处理物化复用
`preprocess::PreprocessPipeline::applyExec`）。

**分块依据**：单一职责——`layer.cpp` 实现 `materializeLayer`（配方→`generate::makePattern`、
文件→`decodedPixels`、空→空图，再组装含底色框、按 `ImageTransform` 缩放、末跑该层 `preprocess.applyExec`）与 `composeCanvas`
（先铺全透明底→落底图 base→按 zOrder 升序逐层物化→施 `transform.opacity`→依锚点 / 枢轴 / 自由偏移定位→
source-over 落位到 `width×height` RGBA 缓冲，越界随缓冲写入自然裁剪）。合成期只搬像素、
不读磁盘：`File` 源依赖上层已注入 `decodedPixels`，未注入按空图（占位）处理。`composeCanvas`
与 `layerPlacement`（单层落位矩形）共用 `materializeLayer`（物化 + 合成期缩放，落位尺寸口径一致）与文件内私有 `placementOffset`（锚点 / 枢轴 / 自由偏移）
两个入口，保证拾取几何与合成落位单一真相、不漂移。

| 文件          | 职责                                                                                                                                                                                 |
|-------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `layer.cpp` | `materializeLayer` / `composeCanvas` / `composeBase` / `applyAlpha` / `layerPlacement` 实现；`composeCanvas` 非法尺寸（≤0 或超护栏）回空图，与 `makePattern` 同口径；`layerPlacement` 无可呈现位图时回 `nullopt` |
