# include/canvas/items — 图层与交互图元的声明边界

**目录作用**：画布层**八类可叠加图元**的类声明（位于 `include/canvas/items/` 子目录），
与 [src/canvas/items/](../../../src/canvas/items/README.md) 一一同名镜像。
场景 / 视图 / 模式 / 输入适配器仍留在上一层 [include/canvas/](../README.md)。

**分块依据**：按 **z 序图层化**拆分——每类图层 / 交互图元一个头，互不耦合，层序统一由
`z_order.h` 的常量定义（避免魔法数散落）。本层**只渲染 Core 给出的结果**
（切割线来自 `generateCutLines`、保留块来自 `EngineResult.kept`、网格来自 `Grid`），
**不含任何几何计算**；坐标统一为**原图像素坐标**。
「纯显示层」与「可交互图元」是两类关注点：`MaskLayer` / `GridLayer` 的线只画不收鼠标
（`GridLayer` 另持两个基准手柄 `GridHandleItem`，是该层唯一可交互物），
`SelectionRectItem` / `CellPickerItem` 自带手势；`AnnotationItem` / `ImageLayerItem` 的平移手势统一在
共享基类 `DraggableCanvasItem`（`AnnotationItem` 另自管 OBB 手柄缩放/旋转）。

| 文件                        | 声明                                       | 职责要点                                                                                                                                                                                                                |
|---------------------------|------------------------------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `z_order.h`               | `zorder` 常量（header-only）                 | 各图层 z 值：`kBase < kImageLayerPixels < kImageLayer < kAnnotation < kAnnotationPreview < kMask < kGrid < kSelection < kCellNumber < kGridHandle`（遮罩为单一层 `kMask`）；无逻辑、无 `Q_OBJECT`，被场景与各图层实现共同 include                  |
| `mask_layer.h`            | `MaskLayer`                              | 保留(绿) / 删除(红)预览遮罩层——遮罩渲染法，不做几何布尔                                                                                                                                                                                    |
| `grid_layer.h`            | `GridLayer`                              | 网格线层：Core `Grid` 单元边界画成贯穿线，两种样式（`asCutLines`）——灰色虚线（L3 网格）/ 橙色实线（L2 诱导切割线）；线纯显示，另持两个基准手柄                                                                                                                            |
| `grid_handle_item.h`      | `GridHandleItem`                         | L3 网格基准手柄（基准点 / 单元尺寸）：拖动预览参数（`previewed`）、释放一次提交（`committed`）；整数像素吸附 + 图内钳制，不重铺网格、不写 Document                                                                                                                       |
| `cell_picker_item.h`      | `CellPickerItem`                         | L3 单元点选交互图元：单击切换 / 拖拽框选 / Custom 编号角标（子图层 `CellNumberLayer`）+ 拖拽调序                                                                                                                                                  |
| `selection_rect_item.h`   | `SelectionRectItem`                      | 唯一橙色交互图元：选区框 + 标记边贯穿切割线延伸（整体移动 / 四角缩放 / 抓边＝移动切割线 / 吸附）；`setBorderVisible(false)` 同时禁用拖拽（隐藏图层＝不可交互）；另供场景级按下路由三口径 `hitTestPress`/`beginPressAt`/`gestureMoved`（与 `mousePressEvent` 共用同一个 `resolvePress`），供 L2 重叠区循环点选 |
| `draggable_canvas_item.h` | `DraggableCanvasItem`（`QGraphicsObject`） | 可平移拖拽图元的共享基类：场景驱动平移手势三态（`beginTranslationDrag`/`dragMoveTo`/`endTranslationDrag`，内部持零 Qt `DragGesture`）+ `setPos` 视觉反馈 + 起收复位；收尾交受保护虚钩子 `onDragFinished` 由子类翻译成信号。`AnnotationItem` / `ImageLayerItem` 由此继承          |
| `annotation_item.h`       | `AnnotationItem`（`DraggableCanvasItem`）  | 单个标注的矢量渲染图元：持一份 Core `Annotation` 拷贝，`Shape::worldPath()` → `QPainterPath` 叠加绘制（不改底图像素）；选中时画 OBB + 8 缩放手柄 + 1 旋转手柄（手柄缩放/旋转自管）；平移手势在基类，本图元只留送达翻译（`clicked`/`moveRequested`）                                          |
| `image_layer_item.h`      | `ImageLayerItem`（`DraggableCanvasItem`）  | 多层画布上单个图片图层的落位框图元：以 `canvas::layerPlacement` 的画布坐标绘制落位框；平移手势在基类，本图元只留 `moved(id,dx,dy)` 送达                                                                                                                          |

> 视觉规范（配色 / 遮罩渲染法 / 图层 z 序）与渲染细节写在实现层
> [src/canvas/README.md](../../../src/canvas/README.md)；改动须知见 GUI 根 [README.md](../../../README.md)。
