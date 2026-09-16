# include/gui/canvas — 画布与图层对外声明

**目录作用**：GUI **画布层**的公共头，声明基于 QGraphicsView/Scene 的图层化画布：底图、遮罩、网格线、
切割线、选区框与 L3 单元点选交互图元。本目录只放**类声明边界**；实现与详尽渲染 / 交互说明见
[src/canvas/](../../../src/canvas/README.md)。

**分块依据**：按 guideline §4.2 的 **z 序图层化**拆分——每类图层 / 交互图元一个头，互不耦合；场景坐标统一为
**原图像素坐标**。本目录**只渲染 Core 给出的结果**（切割线来自 `generateCutLines`、保留块来自 `EngineResult.kept`、
网格来自 `Grid`），不含任何几何计算（A-0.1）。

| 文件 | 声明 | 职责 |
|---|---|---|
| `z_order.h` | `zorder` 常量 | 各图层 z 值（`kBase<kAnnotation<kDelete<kKeep<kGrid<kCutLine<kSelection<kCellNumber`），统一层序、避免魔法数散落 |
| `canvas_scene.h` | `CanvasScene`（`QGraphicsScene`） | 装配并刷新底图 / 遮罩 / 网格线 / 选区（含贯穿切割线延伸）/ L2 多矩形可拖拽选区；转发选区编辑信号 |
| `canvas_view.h` | `CanvasView`（`QGraphicsView`） | 交互：滚轮缩放、空格 / 中键平移、方向键微调、橡皮筋框选、右键菜单 |
| `mask_layer.h` | `MaskLayer` | 保留(绿) / 删除(红)遮罩层（遮罩渲染法，不做几何布尔） |
| `grid_layer.h` | `GridLayer` | L3 / L2 网格线层：把 Core `Grid` 单元边界画成灰色虚线（纯显示、不接收鼠标） |
| `cell_picker_item.h` | `CellPickerItem` | L3 单元点选交互图元：单击切换 / 拖拽框选 / CUSTOM 编号角标 + 拖拽调序 |
| `selection_rect_item.h` | `SelectionRectItem` | 唯一橙色交互图元：选区框 + 标记边贯穿切割线延伸（整体移动 / 四角缩放 / 抓边拖动＝移动切割线 / 吸附 / 选中高亮） |

> 命名空间统一 `idc::gui`。选区与 L2 多矩形的移动·缩放·拖边统一经 `SelectionRectItem::rectChanged` →
> `CanvasScene::selectionEdited` / `multiRectEdited` 转发到 `MainWindow` 写回 `Document`；拖拽期跳过面板回同步与对正在拖图元的回设（避免抖动 / 自删崩溃）。
