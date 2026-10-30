# src/canvas/items — 图层与交互图元的实现

**目录作用**：[include/canvas/items/](../../../include/canvas/items/README.md) 七类图元的实现层（
`z_order.h` 为纯常量头，无对应 `.cpp`），外加可平移拖拽图元基类 `draggable_canvas_item.cpp`。
声明与职责边界不在此处重复。

**分块依据**：**一图元一 TU**，每个文件内部再按**渲染（`boundingRect`/`paint`）与交互**分块；
几何与选择集运算一律不外推——命中测试只用 Core 单元的
`area.contains` / 矩形重叠，落位几何来自 Core `layerPlacement`，颜色与线样式常量集中在各自文件顶部。
`rebuild` 只做「按 Core 结果生成矩形 / 收集去重坐标 → 画线」，**无任何几何运算**。
可平移拖拽图元（annotation / image_layer）的手势循环（press/move/release 三段 + `setPos` 视觉反馈）
统一在基类 `DraggableCanvasItem`，其实现文件只留送达翻译 `onDragFinished`。

| 文件                          | 实现要点                                                                                                                                                                                                               |
|-----------------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `mask_layer.cpp`            | 单一遮罩图元绘制（保留/删除合成于一层）+ 遮罩层的重建 / 显隐 / 清理（重建恒建图元，显隐只切可见性）                                                                                                                                                             |
| `grid_layer.cpp`            | 网格线层的重建 / 显隐 / 清理：按 Core 单元边界收集去重坐标画贯穿线（重建恒建图元，显隐只切可见性）；重建只清线（`clearLines`）不动手柄                                                                                                                                    |
| `grid_handle_item.cpp`      | L3 网格基准手柄的落位 / 绘制 / 拖动：整数像素吸附 + 图内钳制（基准点 `[0,W-cw]`、单元 `[1,W-x0]`；基准拖动时另一手柄同样不出图），拖动发 `previewed`、释放发 `committed`                                                                                                  |
| `cell_picker_item.cpp`      | L3 单元点选与框选：单击切换 / 拖拽框选 / 编号角标 / 拖拽调序（已选目标互换位次、未选目标原位替换；选择集运算全落在 `Document`；角标隐藏时禁调序）                                                                                                                               |
| `selection_rect_item.cpp`   | 选区框的移动 / 四角缩放 / 单边拖动＝移动切割线 / 吸附 / 自绘高亮；命中解析集中在 `resolvePress`（手柄 > 边 > 框内），`mousePressEvent` 与场景重叠命中栈查询（`hitTestPress`）同一口径；**吸附与四边量化只在本次手势发生过位移时才做**（释放与 `cancelDrag` 同条件）——原地单击没写过几何，若仍走吸附会把没拖的框磁吸到图像边/中线并白写一次模型 |
| `draggable_canvas_item.cpp` | 可平移拖拽图元基类：手势三态（`beginTranslationDrag`/`dragMoveTo`/`endTranslationDrag`，内部持零 Qt `DragGesture`）+ `setPos` 视觉反馈 + 起收复位；收尾先复位再交子类 `onDragFinished` 送达（拖拽安全）                                                           |
| `annotation_item.cpp`       | 单个标注的矢量渲染与送达翻译（手势循环在基类；描边/填充/字形委托 `util::paintAnnotation`，与导出预览共用）                                                                                                                                                 |
| `image_layer_item.cpp`      | 落位框绘制 + 送达翻译（手势循环在基类；无落位几何计算）                                                                                                                                                                                      |

> 拖拽类图元的两类陷阱（拖拽期 `emit` 触发上层重建会删掉正在处理事件的图元；逐帧取整回设会抖动）
> 与规避办法见上一层 [src/canvas/README.md](../README.md)；视觉规范与图层 z 序同见该文件。
> 标注 / 图层图元平移手势的规避（拖动期仅 `setPos` 反馈、释放先复位再送达）在
> `DraggableCanvasItem` 基类统一落实，两图元各自只留信号翻译。
