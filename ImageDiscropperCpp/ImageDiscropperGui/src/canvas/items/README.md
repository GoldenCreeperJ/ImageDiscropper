# src/canvas/items — 图层与交互图元的实现

**目录作用**：[include/canvas/items/](../../../include/canvas/items/README.md) 六类图元的实现层（
`z_order.h` 为纯常量头，无对应 `.cpp`）。声明与职责边界不在此处重复。

**分块依据**：**一图元一 TU**，每个文件内部再按**渲染（`boundingRect`/`paint`）与交互
（press/move/release）**分块；几何与选择集运算一律不外推——命中测试只用 Core 单元的
`area.contains` / 矩形重叠，落位几何来自 Core `layerPlacement`，颜色与线样式常量集中在各自文件顶部。
`rebuild` 只做「按 Core 结果生成矩形 / 收集去重坐标 → 画线」，**无任何几何运算**。

| 文件                       | 实现要点                                                                                     |
|----------------------------|----------------------------------------------------------------------------------------------|
| `mask_layer.cpp`           | 单一遮罩图元绘制（保留/删除合成于一层）+ 遮罩层的重建 / 显隐 / 清理                            |
| `grid_layer.cpp`           | 网格线层的重建 / 显隐 / 清理：按 Core 单元边界收集去重坐标画贯穿线                            |
| `cell_picker_item.cpp`     | L3 单元点选与框选：单击切换 / 拖拽框选 / 编号角标 / 拖拽调序（选择集运算全落在 `Document`）   |
| `selection_rect_item.cpp`  | 选区框的移动 / 四角缩放 / 单边拖动＝移动切割线 / 吸附 / 自绘高亮                               |
| `annotation_item.cpp`      | 单个标注的矢量渲染与拖动平移（描边/填充/字形委托 `util::paintAnnotation`，与导出预览共用）      |
| `image_layer_item.cpp`     | 落位框绘制 + 拾取拖拽手势（无落位几何计算）                                                    |

> 拖拽类图元的两类陷阱（拖拽期 `emit` 触发上层重建会删掉正在处理事件的图元；逐帧取整回设会抖动）
> 与规避办法见上一层 [src/canvas/README.md](../README.md)；视觉规范与图层 z 序同见该文件。
