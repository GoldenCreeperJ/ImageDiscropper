# src/annotation

**目录作用**：`include/annotation` 下头文件的实现代码，即「标注层」——在原图上绘制矩形 / 椭圆 /
箭头 / 文字等矢量标注，并支持把标注烧录（burn-in）进像素。对应终稿 FR-1.3「标注图层」。

**分块依据**：按「构造 → 光栅化 → 会话状态 → 视图坐标」四种关注点拆分，互不耦合。
- `shape_factory.cpp` 只关心「锚点 → Shape」的构造分支，与像素无关。
- `rasterizer.cpp` 只关心「Shape → Image」的像素写入，包括扫描线填充、抗锯齿描边与 Alpha 混合。
- `annotation_layer.cpp` 只关心会话状态（标注列表、当前工具、编辑模式）、命中选中、删除指定项（`removeAnnotation`）、撤销重做（复用 `history::HistoryManager`）与烧录合成。
- `view_transform.cpp` 只关心屏幕 / 逻辑坐标换算与视图适配。

| 文件                     | 职责                                                           |
|------------------------|--------------------------------------------------------------|
| `shape_factory.cpp`    | 依据 `ShapeType` 与锚点构造 `Shape`                                 |
| `rasterizer.cpp`       | 光栅化：填充、描边、Alpha 混合                                           |
| `annotation_layer.cpp` | 标注会话状态机：提交、命中、选中、删除指定项（`removeAnnotation`）、撤销重做、烧录（`burnIn`） |
| `view_transform.cpp`   | 屏幕 / 逻辑坐标换算，以指定点缩放，图像适配视口                                    |
