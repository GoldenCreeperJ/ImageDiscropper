# canvas/ — QGraphicsView 画布与图层

> **include/src 分离**：本模块头文件在 `include/gui/canvas/`，实现（`.cpp`）与本 README 在 `src/canvas/`。

按 guideline §4.2 的 **z 序图层化**组织画布：底图 → 删除遮罩 → 保留遮罩 → 网格 → 切割线 → 选区框 → 角标。
场景坐标统一为**原图像素坐标**；底图用降采样 pixmap 经变换铺回原图尺寸，各叠加层即可直接按原图坐标绘制。
本目录**只渲染 Core 给出的结果**（切割线来自 `generateCutLines`、保留块来自 `EngineResult.kept`），不含几何计算（A-0.1）。

| 文件 | 职责 |
| ---- | ---- |
| `z_order.h`                  | 各图层 z 值常量（统一层序，避免魔法数散落） |
| `canvas_scene.{h,cpp}`       | 场景：装配并刷新底图 / 遮罩 / 网格线 / 选区（含贯穿切割线延伸）/ L2 多矩形可拖拽选区，转发选区编辑信号 |
| `canvas_view.{h,cpp}`        | 视图：滚轮缩放、空格/中键平移、方向键微调、框选、右键菜单 |
| `mask_layer.{h,cpp}`         | 保留(绿)/删除(红)遮罩层（见下方渲染法） |
| `grid_layer.{h,cpp}`         | L3 网格线层：把 Core `Grid` 的单元边界画成灰色虚线（纯显示、不接收鼠标） |
| `cell_picker_item.{h,cpp}`   | L3 单元点选交互图元：单击切换 / 拖拽框选 / CUSTOM 编号角标 + 拖拽调序（命中测试只用 Core 单元区域） |
| `selection_rect_item.{h,cpp}`| 唯一橙色交互图元：选区框 + 标记边的贯穿切割线延伸（可整体移动 / 四角缩放 / 直接抓边拖动＝移动切割线 / 边缘中心吸附） |

## z 序（z_order.h）

`kBase=0 < kAnnotation=10 < kDelete=20 < kKeep=30 < kGrid=40 < kCutLine=50 < kSelection=60 < kCellNumber=70`

`kDelete < kKeep` 是遮罩渲染法的关键：红色删除底在下、绿色保留块在上。

## 遮罩渲染法（不做几何布尔）

`EngineResult.kept` 只给出保留块。`MaskLayer::rebuild` 先铺一层覆盖全图的红色半透明“删除底”，
再按 `kept` 各片段的区域叠加绿色半透明“保留块”。绿色覆盖处显示保留、透出红色处即删除——
GUI 无需自行计算“删除集”，完全符合 A-0.1。

## 交互（canvas_view）

- **缩放**：滚轮以鼠标为锚点缩放；`zoomIn/zoomOut/resetZoom/fitToWindow`。
- **平移**：按住空格 + 左键，或中键拖拽。
- **框选**：空白处左键拖拽出橡皮筋框选，松开后 `emit rubberSelect(QRectF)`（交 MainWindow 写回 Document）。
- **微调**：方向键 `emit nudgeSelection(±1)`，Shift+方向键 `±10`。
- **右键菜单**：清除切割线 / 重置视图 / 切换遮罩，分别 `emit *Requested`。
- **坐标回报**：鼠标移动 `emit cursorScenePos(QPointF)`（供状态栏显示图像坐标与像素 RGB）。
- 选区框手柄尺寸与抓边条带随缩放换算为场景单位（`updateHandleSize`：`8.0 / m11`），保持屏幕像素大小恒定。

> 选区框的移动/缩放/拖边统一经 `SelectionRectItem::rectChanged` → `CanvasScene::selectionEdited` 转发到 MainWindow；
> 拖动选区边＝移动对应切割线（同一图元），故 MainWindow 只连接场景这一个稳定信号，无需感知惰性创建/销毁的选区图元。

## L3 网格线（grid_layer；§4.2）

L3 模式下，`CanvasScene::updateGrid(grid, true)` 委托 `GridLayer::rebuild` 依 Core `Grid` 的单元边界（去重、裁剪到图像内）
画**灰色虚线**贯穿线（`QColor(150,150,150)` 1px `DashLine` cosmetic，z=`kGrid`），纯显示、`setAcceptedMouseButtons(Qt::NoButton)`
不遮挡交互。非 L3 模式或换图时 `clearGrid()` 清空。L3 **隐藏橙色选区框**（`syncSelection(rect,false)`）——网格由参数定义、
不依赖矩形选区；单元点选/编号角标交互由 `cell_picker_item` 承担（见下）。网格的几何推导全在 Core `Grid::build`（A-0.1）。

## L3 单元点选（cell_picker_item；FR-L3.3 / FR-L3.5）

L3 下与选区框**互斥显隐**的第二个交互图元，覆盖整幅图像，依 Core `Grid` 的单元区域做命中测试：

- **单击**某单元 → `emit cellToggled(index)`，上层在 `Document` 里切换其选中态（已选移除、未选追加到末尾）。
- **拖拽框选** → 实时画半透明蓝框选带，松开 → `emit cellsMarqueeSelected(indices)`（框内单元序号，去重并入选择集）。
- **已选单元高亮**：半透明蓝填充 + 蓝边框（独立于极性的绿/红遮罩）；悬停单元白边框反馈（局部重绘，避免大选集整层重绘）。
- **CUSTOM 编号角标**（`SortStrategy::CUSTOM`）：在每个已选单元左上角以**设备像素**绘制其 1-based 自定义序号
  （= 该单元在 `selectedCells_` 中的位次 + 1）；角标字号恒定屏幕大小（`worldTransform` → `resetTransform` 后绘制），
  单元在屏幕上 < 14px 时跳过角标（避免缩小时糊成一片）。非 CUSTOM 不显示角标（输出序由 Core 按行/列主序计算，网格上直观可见）。
- **CUSTOM 拖拽调序**（FR-L3.5）：在已选单元上拖动 → 被拖单元橙色粗边框、目标单元黄色虚线边框；松开且目标为另一已选单元时
  → `emit cellReordered(from, to)`，上层把 `from` 移到 `to` 的原序位（重排 `selectedCells_` 顺序即自定义输出序）。

> 手势以覆盖层尺度（`overlayScale_`，≈8 屏幕px 的场景单位）为单击/拖拽阈值；**只在释放时 emit 一次**，拖拽中仅重绘图元自身
> （不逐帧回写 Document），规避拖拽图元「emit 触发上层 clear()+重建 → 删掉正在处理事件的图元 → 崩溃」与「逐帧取整回设 → 抖动」两类陷阱。
> 命中测试只用 Core 单元的 `area.contains` / 矩形重叠，不含任何网格几何推导（A-0.1）；选择集运算（toggle/union/reorder）全落在 `Document`。
> 事件链：`CellPickerItem` → `CanvasScene`（转发）→ `MainWindow`（写 `Document`）→ `refreshPreview` 回灌 `updateCellSelection` 刷新高亮与角标。

## 切割线（即选区边的贯穿延伸、唯一橙色图元；A-0.11 / §4.2）

单矩形诱导的四条切割线恰好落在选区矩形的四条边上，是选区边的「贯穿全图延伸」。故**不再有独立的切割线图元**：
`CanvasScene::updateCutLines` 依 Core `generateCutLines` 的结果，判定选区哪几条边有内部贯穿切割线（单矩形/十字四边皆有、
横带仅上下、竖带仅左右；落在图像边界的不算），经 `SelectionRectItem::setCutEdges` 下发；选区框用**同一支橙色画笔**把这些边
延伸绘制为贯穿全图的切割线（竖边贯穿全高、横边贯穿全宽）。

**为何天然可拖且绝不错位**：切割线与选区边是**同一个 `SelectionRectItem` 的同一条边**——直接抓取任一条边（含其延伸到图像
边界的那段）沿法向拖动，就是移动该选区边＝移动对应切割线（四角手柄仍优先做对角缩放、框内拖动整体平移）。拖动经
`rectChanged` → `selectionEdited` → MainWindow 写回 `Document` → Core 重算 → `updateCutLines`/`syncSelection` 刷新，
线与选区始终同源同位。这既提供了「直接拖动切割线」的能力，又从物理上根除了旧独立实现的「蓝/橙双线并存、手柄错位、
右线达上限后与选区分离」等双重表示问题（NFR-6）。

## L2 多矩形可拖拽选区（canvas_scene；FR-L2）

L2「多矩形并集剔除」下，选区来自 `Document::rects()`（多个矩形）而非单 `rect_`。`CanvasScene::updateMultiRects(rects)`
为**每个矩形创建一个可交互的橙色 `SelectionRectItem`**（cutEdges 全 false，不画贯穿线以免噪声），与单矩形选区体验一致：
可整体移动 / 四角缩放 / 拖边微调。**增量维护**——按数量增删末位图元、逐个刷新几何，拖拽中跳过对正在拖图元的回设
（规避「逐帧取整回设抖动」与「重建场景自删崩溃」两类陷阱）；图元 `rectChanged` 发射时按指针查当前下标 → `multiRectEdited(index, rect)`
交 MainWindow 写回 `Document.updateRect(index)`。`SelectionRectItem` 在框外 `ignore` 鼠标，故空白处拖拽仍能框选逐个追加矩形
（见 `MainWindow::onRubberSelect`）；`clearMultiRects()` 在切回其他子功能/模式或换图时清空。

选中态作为**视图状态**（不写入 Document）：`setActiveMultiRect(index)` 令对应选区框 `setHighlighted(true)`（颜色 / 线宽 / 填充略微加强），
与参数面板矩形列表的选中行经 `ParamPanel::rectSelected` ↔ `MainWindow::onRectSelected` 双向联动；`setMultiRectHandleSize` 随视图缩放
换算手柄尺寸（恒约 8 屏幕px），`isDraggingMultiRect()` 供上层在拖拽期跳过面板回同步。

此模式还调用 `updateGridLines(grid, true)`：只委托 `GridLayer` 画 Core 诱导网格（各矩形十字带并集诱导）的灰色网格线，
**不启动 `cell_picker_item`**（`updateGrid` 会显示 picker，而 picker 会 grab 鼠标、阻断框选）；单选区框隐藏（`syncSelection(rect,false)`）。
