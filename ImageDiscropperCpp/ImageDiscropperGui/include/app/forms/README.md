# include/app/forms — 新建画布对话框的采参组件声明

**目录作用**：`NewCanvasDialog` 的**两个可复用采参组件**的类声明——
程序化图案参数表单 `PatternFormWidget` 与「锚点 / 枢轴 / 自由偏移」落位行 `PlacementRow`。
实现在同名镜像目录 [src/app/forms/](../../../src/app/forms/README.md)。

**分块依据**：按**共用语境**切分，而非按对话框切分——两个组件都被「新建画布」与「添加图片图层」
两处复用，且与「尺寸 / 附加图片 / 预览 / 定稿」等对话框流程无耦合，
故独立成组件，`NewCanvasDialog` 只保留剩余编排。
**组件只采参、不生成像素、不落语义**：像素生成一律交上游（CONTRIBUTING「分层纪律」），
显隐由上游 `setVisible` 控制；这一组件边界亦是 Rust 第二实现（`ImageDiscropperRust/GuideLine.md`）可直接对齐的目标。

| 文件                | 类                   | 采参范围与信号                                                                                                                                  |
|-------------------|---------------------|------------------------------------------------------------------------------------------------------------------------------------------|
| `pattern_form.h`  | `PatternFormWidget` | 底色（RGBA，可透明）/ 前景色（仅非纯色图案）/ 图案类型（11 种）/ 方向（依类型动态重建）/ 周期·格径 / 线宽 / 点半径 / 噪声种子 / 配色模式；任一变化发 `paramsChanged()`，`params()` 汇编 `PatternParams` |
| `placement_row.h` | `PlacementRow`      | 9 向锚点 + 9 向枢轴 + X/Y 偏移三行一组；发 `changed()`，字段与 `canvas::ImageTransform` 一一对应（`inCanvasAnchor` / `layerPivot` / 自由偏移）                       |

> 宿主对话框与装配壳的职责见 [app/README.md](../README.md)；
> 落位语义的 Core 契约见 SPEC §7 与 [Core include/generate/](../../../../ImageDiscropperCore/include/generate/README.md)。
