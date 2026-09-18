# canvas/ — QGraphicsView 画布与图层

> **include/src 分离**：本模块头文件在 `include/canvas/`，实现（`.cpp`）与本 README 在 `src/canvas/`。

按 **z 序图层化**组织画布：底图 → 标注矢量叠加 → 删除遮罩 → 保留遮罩 → 网格 → 切割线 → 选区框 → 角标。
场景坐标统一为**原图像素坐标**；底图用降采样 pixmap 经变换铺回原图尺寸，各叠加层即可直接按原图坐标绘制。
本目录**只渲染 Core 给出的结果**（切割线来自 `generateCutLines`、保留块来自 `EngineResult.kept`），不含几何计算。

| 文件                            | 职责                                                                                                                                                                                                                                                                                                               |
|-------------------------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `z_order.h`                   | 各图层 z 值常量（统一层序，避免魔法数散落）                                                                                                                                                                                                                                                                                          |
| `canvas_scene.{h,cpp}`        | 场景：装配并刷新底图 / 遮罩 / 网格线 / 选区（含贯穿切割线延伸）/ L2 多矩形可拖拽选区 / 标注矢量叠加（增量维护），转发选区编辑与标注选中/移动/变换信号（`annotationMoved`/`annotationTransformed` 提交、`annotationTransformPreview` 拖拽逐帧回显）；并提供各图层显隐开关 `setMasksVisible`/`setGridVisible`/`setCutLinesVisible`/`setSelectionVisible`/`setBaseVisible`/`setAnnotationsVisible`（图层面板驱动） |
| `canvas_view.{h,cpp}`         | 视图：滚轮缩放、空格/中键平移、方向键微调、框选、右键菜单；标注绘制态门控下路由左键手势为 `annoDrag*`、悬停移动为 `annoHover`（折线橡皮筋预览）、右键为 `annoFinish`（折线收笔）                                                                                                                                                                                                      |
| `mask_layer.{h,cpp}`          | 保留(绿)/删除(红)遮罩层（见下方渲染法）                                                                                                                                                                                                                                                                                           |
| `grid_layer.{h,cpp}`          | 网格线层：把 Core `Grid` 的单元边界画成贯穿线，两种样式（`asCutLines`）——灰色虚线（L3 网格）或橙色实线（L2 多矩形诱导切割线）；纯显示、不接收鼠标                                                                                                                                                                                                                        |
| `cell_picker_item.{h,cpp}`    | L3 单元点选交互图元：单击切换 / 拖拽框选 / CUSTOM 编号角标 + 拖拽调序（命中测试只用 Core 单元区域）                                                                                                                                                                                                                                                   |
| `selection_rect_item.{h,cpp}` | 唯一橙色交互图元：选区框 + 标记边的贯穿切割线延伸（可整体移动 / 四角缩放 / 直接抓边拖动＝移动切割线 / 边缘中心吸附）；**逐元素可见即可拖**：`setCutLinesVisible` 门控贯穿延伸段的绘制与可抓性，`setBorderVisible` 门控矩形边框/手柄绘制与其专有交互（四角缩放/框内平移/选区段拖边）；图元整体仅在**两者都隐藏**时才置 `Qt::NoButton` 彻底不可交互（切割线可见、边框隐藏时仍可拖切割线延伸段）                                                                           |
| `annotation_item.{h,cpp}`     | 单个标注的矢量渲染图元：`Shape::worldPath()`（含非破坏性变换）→`QPainterPath` 叠加绘制（不改底图像素），选中时绘制**定向包围盒（OBB）+ 8 缩放手柄 + 1 旋转手柄**（拖拽缩放/拉伸/旋转/翻转、实时矢量预览）+ 可拖动平移                                                                                                                                                                          | 

## z 序（z_order.h）

`kBase=0 < kAnnotation=10 < kDelete=20 < kKeep=30 < kGrid=40 < kCutLine=50 < kSelection=60 < kCellNumber=70`

`kDelete < kKeep` 是遮罩渲染法的关键：红色删除底在下、绿色保留块在上。

## 遮罩渲染法（不做几何布尔）

红色删除底铺满全图 + 绿色保留块叠加其上，GUI 无需自算删除集——机制与为何 `kDelete < kKeep`
见 Gui README「关键设计决策」#1。

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

## L3 网格线（grid_layer）

L3 模式下，`CanvasScene::updateGrid(grid, true)` 委托 `GridLayer::rebuild` 依 Core `Grid` 的单元边界（去重、裁剪到图像内）
画**灰色虚线**贯穿线（`QColor(150,150,150)` 1px `DashLine` cosmetic，z=`kGrid`），纯显示、`setAcceptedMouseButtons(Qt::NoButton)`
不遮挡交互。非 L3 模式或换图时 `clearGrid()` 清空。L3 **隐藏橙色选区框**（`syncSelection(rect,false)`）——网格由参数定义、
不依赖矩形选区；单元点选/编号角标交互由 `cell_picker_item` 承担（见下）。**单元选择高亮（picker）受 `selectionVisible_` 门控**：
图层面板关闭「选取边框」时 `updateGrid` 令 `pickerItem_->setVisible(false)`，那层蓝色单元高亮即隐藏且不可点选（这就是
「L3 选中后无法隐藏的网格」的真身——它是 `CellPickerItem` 的单元选择高亮层，非 `GridLayer` 的灰色网格线）。网格的几何推导全在 Core `Grid::build`。

## L3 单元点选（cell_picker_item；FR-L3.3 / FR-L3.5）

L3 下与选区框**互斥显隐**的第二个交互图元，覆盖整幅图像，依 Core `Grid` 的单元区域做命中测试：

- **单击**某单元 → `emit cellToggled(index)`，上层在 `Document` 里切换其选中态（已选移除、未选追加到末尾）。
- **拖拽框选** → 实时画半透明蓝框选带，松开 → `emit cellsMarqueeSelected(indices)`（框内单元序号，去重并入选择集）。
- **已选单元高亮**：半透明蓝填充 + 蓝边框（独立于极性的绿/红遮罩）；悬停单元白边框反馈（局部重绘，避免大选集整层重绘）。
- **CUSTOM 编号角标**（`SortStrategy::CUSTOM`）：在每个已选单元左上角以**设备像素**绘制其 1-based 自定义序号
  （= 该单元在 `selectedCells_` 中的位次 + 1）；角标字号恒定屏幕大小（`worldTransform` → `resetTransform` 后绘制），
  单元在屏幕上 < 14px 时跳过角标（避免缩小时糊成一片）。非 CUSTOM 不显示角标（输出序由 Core 按行/列主序计算，网格上直观可见）。
  角标由 `CellPickerItem` 的**角标子图层** `CellNumberLayer` 绘制（子 z = kCellNumber − kSelection，展示为独立图层，随选区图元显隐），
  受图层面板「单元编号」开关门控。
- **CUSTOM 拖拽调序**（FR-L3.5）：在已选单元上拖动 → 被拖单元橙色粗边框、目标单元黄色虚线边框；松开且目标为另一已选单元时
  → `emit cellReordered(from, to)`，上层把 `from` 移到 `to` 的原序位（重排 `selectedCells_` 顺序即自定义输出序）。

> 手势以覆盖层尺度（`overlayScale_`，≈8 屏幕px 的场景单位）为单击/拖拽阈值；**只在释放时 emit 一次**，拖拽中仅重绘图元自身
> （不逐帧回写 Document），规避拖拽图元「emit 触发上层 clear()+重建 → 删掉正在处理事件的图元 → 崩溃」与「逐帧取整回设 → 抖动」两类陷阱。
> 命中测试只用 Core 单元的 `area.contains` / 矩形重叠，不含任何网格几何推导；选择集运算（toggle/union/reorder）全落在 `Document`。
> 事件链：`CellPickerItem` → `CanvasScene`（转发）→ `MainWindow`（写 `Document`）→ `refreshPreview` 回灌 `updateCellSelection` 刷新高亮与角标。

## 切割线（即选区边的贯穿延伸、唯一橙色图元）

单矩形诱导的四条切割线恰好落在选区矩形的四条边上，是选区边的「贯穿全图延伸」。故**不再有独立的切割线图元**：
`CanvasScene::updateCutLines` 依 Core `generateCutLines` 的结果，判定选区哪几条边有内部贯穿切割线（单矩形/十字四边皆有、
横带仅上下、竖带仅左右；落在图像边界的不算），经 `SelectionRectItem::setCutEdges` 下发；选区框用**同一支橙色画笔**把这些边
延伸绘制为贯穿全图的切割线（竖边贯穿全高、横边贯穿全宽）。

**为何天然可拖且绝不错位**：切割线与选区边是**同一个 `SelectionRectItem` 的同一条边**——抓任一条边（含贯穿延伸段）
沿法向拖动＝移动该选区边＝移动对应切割线；线与选区始终同源同位，从物理上根除旧实现的「蓝/橙双线并存、手柄错位」等
双重表示问题。机制与取舍详见 Gui README「关键设计决策」#6。

**可见即可拖、隐藏即不可拖（逐元素）**：图元的交互按「边框」与「切割线延伸段」两个可见性分别门控：
- `hitEdge`：标记边的「贯穿全图可抓」仅在 `cutLinesVisible_` 为真时生效（延伸段）；选区矩形自身那段仅在 `borderVisible_` 为真时可抓；boundingRect 也仅在切割线可见时才并入全图范围。
- `hitHandle`（四角缩放）与框内整体平移均需 `borderVisible_`（手柄/填充区属边框视觉）。
- 图元 `acceptedMouseButtons` 由「`borderVisible_ || cutLinesVisible_`」决定：两者都隐藏才置 `Qt::NoButton`。

故：切割线隐藏+边框显示 → 延伸段不可拖（仅选区段可拖）；切割线显示+边框隐藏 → 仍可拖切割线延伸段（但无手柄缩放/框内平移）；两者隐藏 → 选区完全不可交互（对齐标注）。setCutLinesVisible 因影响 boundingRect 需 `prepareGeometryChange()`。

## L2 多矩形可拖拽选区（canvas_scene；FR-L2）

L2「多矩形并集剔除」下，选区来自 `Document::rects()`。`CanvasScene::updateMultiRects(rects)` 为**每个矩形创建一个
可交互的橙色 `SelectionRectItem`**（cutEdges 全 false），**增量维护**（按数量增删末位、逐个刷新几何，绝不 clear+重建）；
`rectChanged` 按指针查下标 → `multiRectEdited(index, rect)` 写回 `Document.updateRect(index)`；框外 `ignore` 鼠标，
空白处拖拽仍可框选追加矩形。选中态是**视图状态**（不写 Document）：`setActiveMultiRect(index)` 高亮对应框，
与参数面板列表选中行双向联动；`setMultiRectHandleSize`/`isDraggingMultiRect()` 供缩放换算与拖拽期跳过面板回同步。
诱导切割线经 `updateMultiRectCutLines(grid, true)` 以**橙色切割线样式**（`asCutLines=true`，受 `cutLinesVisible_` 门控）绘制，
**不启动 `cell_picker_item`**（其 grab 会阻断框选）；单选区框隐藏。机制与取舍详见 Gui README「关键设计决策」#7。

## 标注矢量叠加（annotation_item / canvas_scene / canvas_view）

标注层与底图**分离**：每个 Core `Annotation` 对应一个 `AnnotationItem`（`QGraphicsObject`），用
`Shape::worldPath()` → `toQPainterPath()` **矢量叠加**渲染（z=`kAnnotation`=10），**不改底图像素**；烧录仅在导出时发生。
文字标注 `dynamic_cast<TextShape*>` 取 `text()/fontSize()`，`xform_` 非单位阵时经 `painter->setTransform(..., true)`
在局部坐标画真实字形（矢量仿射不失真）。描边/填充/字形**统一委托 `util::paintAnnotation`**（与导出预览烘焙共用，消除重复）。
设计机制（遮罩式分层、烧录时点、OBB 变换语义）见 Gui README「关键设计决策」#8/#9 与 `include/geometry`。

- **增量维护**：`CanvasScene::updateAnnotations(model)` 按数量增删末位图元、逐个刷新几何与选中态，**绝不 clear+重建**
  （否则拖拽中会删掉正在处理事件的图元→崩溃）；拖拽中跳过对正在拖图元的回设。
- **拖拽预览**：`model.pendingShape()` 非空时用惰性创建的 `pendingAnnoItem_`（不可交互）画橡皮筋矢量，不进 Core 历史。
- **选中/移动**：SELECT 下图元自行响应鼠标——press → `annotationSelectRequested` → `AnnotationCoordinator` 用 Core `hitTest` 选中；
  拖动释放 → `annotationMoved(index,dx,dy)` → 委托 Core 平移。
- **OBB 手柄变换**：选中时依 `computeObb(xf)` 画虚线 OBB + 8 缩放手柄 + 1 旋转手柄（抓取区半径 `handleSize_*2.0` 大于可视手柄）；
  拖拽中仅调 Core `obbPreviewTransform` 做**矢量预览**并逐帧 `emit transformPreview(累积绝对值)` 供属性面板回显；
  释放时 `emit transformRequested(sx,sy,deg)`（增量）→ Core `applyObbTransform` 一次性提交（预览与提交同一算子，所见即所得）。
  变换全走 Core，GUI 不自算矩阵。
- **绘制态门控**：任一绘制工具激活时左键手势被视图拦截路由为 `annoDrag*`；SELECT 时关闭门控，维持选区/单元交互。
- **折线（POLYLINE）点击式绘制**：**左键落顶点 + 悬停橡皮筋预览 + 右键收笔**——`annoDragStart` 落正式顶点、
  `annoHover` 实时预览「已落顶点 + 到光标连线」、右键 `annoFinish` 收笔提交（绘制态下右键不弹视图菜单）。
- **手柄尺寸**：`setAnnotationHandleSize` 随视图缩放换算，屏幕观感恒定（NFR-7）。
- **图层显隐**：`setAnnotationsVisible`/`setBaseVisible` 仅切图元可见性（图层面板开关），不删数据、不影响导出。
