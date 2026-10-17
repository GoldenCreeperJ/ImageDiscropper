# include/canvas/items — 图层与交互图元的声明边界

**目录作用**：画布层**七类可叠加图元**的类声明（位于 `include/canvas/items/` 子目录），
与 [src/canvas/items/](../../../src/canvas/items/README.md) 一一同名镜像。
场景 / 视图 / 模式 / 输入适配器仍留在上一层 [include/canvas/](../README.md)。

**分块依据**：按 **z 序图层化**拆分——每类图层 / 交互图元一个头，互不耦合，层序统一由
`z_order.h` 的常量定义（避免魔法数散落）。本层**只渲染 Core 给出的结果**
（切割线来自 `generateCutLines`、保留块来自 `EngineResult.kept`、网格来自 `Grid`），
**不含任何几何计算**；坐标统一为**原图像素坐标**。
「纯显示层」与「可交互图元」是两类关注点：`MaskLayer` / `GridLayer` 只画不收鼠标，
`SelectionRectItem` / `CellPickerItem` / `AnnotationItem` / `ImageLayerItem` 自带手势。

| 文件                      | 声明                       | 职责要点                                                                                                                                                             |
|-------------------------|--------------------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `z_order.h`             | `zorder` 常量（header-only） | 各图层 z 值：`kBase < kImageLayer < kAnnotation < kAnnotationPreview < kMask < kGrid < kSelection < kCellNumber`（遮罩为单一层 `kMask`）；无逻辑、无 `Q_OBJECT`，被场景与各图层实现共同 include |
| `mask_layer.h`          | `MaskLayer`              | 保留(绿) / 删除(红)预览遮罩层——遮罩渲染法，不做几何布尔                                                                                                                                 |
| `grid_layer.h`          | `GridLayer`              | 网格线层：Core `Grid` 单元边界画成贯穿线，两种样式（`asCutLines`）——灰色虚线（L3 网格）/ 橙色实线（L2 诱导切割线）；纯显示                                                                                   |
| `cell_picker_item.h`    | `CellPickerItem`         | L3 单元点选交互图元：单击切换 / 拖拽框选 / Custom 编号角标（子图层 `CellNumberLayer`）+ 拖拽调序                                                                                               |
| `selection_rect_item.h` | `SelectionRectItem`      | 唯一橙色交互图元：选区框 + 标记边贯穿切割线延伸（整体移动 / 四角缩放 / 抓边＝移动切割线 / 吸附）；`setBorderVisible(false)` 同时禁用拖拽（隐藏图层＝不可交互）                                                               |
| `annotation_item.h`     | `AnnotationItem`         | 单个标注的矢量渲染图元：持一份 Core `Annotation` 拷贝，`Shape::worldPath()` → `QPainterPath` 叠加绘制（不改底图像素）；选中时画 OBB + 8 缩放手柄 + 1 旋转手柄                                               |
| `image_layer_item.h`    | `ImageLayerItem`         | 多层画布上单个图片图层的落位框图元：以 `canvas::layerPlacement` 的画布坐标绘制落位框并支持拖动                                                                                                     |

> 视觉规范（配色 / 遮罩渲染法 / 图层 z 序）与渲染细节写在实现层
> [src/canvas/README.md](../../../src/canvas/README.md)；改动须知见 GUI 根 [README.md](../../../README.md)。
