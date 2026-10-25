# canvas/ — QGraphicsView 画布与图层

> **include/src 分离**：本模块头文件在 `include/canvas/`，实现（`.cpp`）与本 README 在 `src/canvas/`。

按 **z 序图层化**组织画布：底图 → 图片图层像素 → 图片图层落位框 → 标注矢量叠加 → 标注绘制预览 → 遮罩 → 网格 → 选区框 → 单元编号角标 → 网格基准手柄。
场景坐标统一为**原图像素坐标**；底图用降采样 pixmap 经变换铺回原图尺寸，各叠加层即可直接按原图坐标绘制。
本目录**只渲染 Core 给出的结果**（切割线来自 `generateCutLines`、保留块来自 `EngineResult.kept`），不含几何计算。

> **目录分层**：六类图层/图元（mask/grid/cell_picker/selection_rect/annotation/image_layer + z_order 常量）位于 `items/` 子目录（清单见 [items/README.md](items/README.md)，含可平移拖拽基类 `draggable_canvas_item`）；本层只留场景（canvas_scene）、视图（canvas_view）、模式（canvas_mode）与输入适配器，外加零 Qt 手势/循环值类型（`drag_gesture.h` / `pick_cycle.h`）。

| 文件                                  | 职责                                                                                                                                                                                                                                                                                                                                                                                                                                        |
|-------------------------------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `items/z_order.h`                   | 各图层 z 值常量（统一层序，避免魔法数散落）                                                                                                                                                                                                                                                                                                                                                                                                                   |
| `canvas_desktop_input_adapter.cpp`  | 桌面输入适配器实现（输入适配器分层）：画布交互链路中**唯一**允许出现鼠标键角色/滚轮刻度/QMenu 的地方，把原始事件翻译为 `CanvasView::gesture*` 业务手势（映射表见头注释）                                                                                                                                                                                                                                                                                                                                    |
| `canvas_touch_input_adapter.cpp`    | 触控输入适配器实现（SPEC §8.3）：单指走 QPA 合成鼠标序列委托 `DesktopInputAdapter` + 叠加双击=适应窗口；双指 `Qt::PinchGesture` → 捏合缩放 / 中心位移平移；长按菜单原样复用右键路径；抓取带屏幕基准 12px（≥12pt）。仅 Android 构建实例化，桌面不经本适配器                                                                                                                                                                                                                                                                   |
| `canvas_scene.{h,cpp}`              | 场景：装配并刷新底图 / 遮罩 / 网格线 / 选区（含贯穿切割线延伸）/ L2 多矩形可拖拽选区 / 标注矢量叠加（增量维护），转发选区编辑与标注选中/移动/变换信号（`annotationMoved`/`annotationTransformed` 提交、`annotationTransformPreview` 拖拽逐帧回显）；并提供各图层显隐开关 `setMasksVisible`/`setGridLinesVisible`/`setSelectionVisible`/`setBaseVisible`/`setAnnotationsVisible`（图层面板驱动；网格线与切割线合并为同一开关，同置 `gridVisible_`/`cutLinesVisible_`）；「L2 多矩形」与「标注」两域实现拆独立 TU `canvas_scene_rects.cpp`/`canvas_scene_annotations.cpp`（类不拆） |
| `canvas_scene_rects.cpp`            | `CanvasScene` 的「L2 多矩形」图元域实现（TU 拆分）：多矩形诱导切割线/可拖拽选区框组/手柄尺寸/拖拽态与高亮                                                                                                                                                                                                                                                                                                                                                                          |
| `canvas_scene_annotations.cpp`      | `CanvasScene` 的「标注」图元域实现（TU 拆分）：标注矢量图元增量维护/拖拽预览/显隐/清空/手柄尺寸                                                                                                                                                                                                                                                                                                                                                                                |
| `canvas_view.{h,cpp}`               | 视图：业务手势执行面（`gesturePan*`/`gestureStroke*`/`gestureMarquee*`/`gestureStepZoom` 的平移/绘制/框选/缩放状态机与意图信号）；Qt 事件入口一行转发给输入适配器；键盘（Esc 收笔/空格平移预备/方向键微调）为桌面专属通道；标注绘制态门控下悬停移动为 `annoHover`（折线橡皮筋预览）、右键为 `annoFinish`（折线收笔）                                                                                                                                                                                                                            |
| `items/mask_layer.{h,cpp}`          | 保留(绿)/删除(红)遮罩层（见下方渲染法）                                                                                                                                                                                                                                                                                                                                                                                                                    |
| `items/grid_layer.{h,cpp}`          | 网格线层：把 Core `Grid` 的单元边界画成贯穿线，两种样式（`asCutLines`）——灰色虚线（L3 网格）或橙色实线（L2 多矩形诱导切割线）；线图元纯显示、不接收鼠标（层内另持两个 L3 网格基准手柄，见下）                                                                                                                                                                                                                                                                                                                         |
| `items/grid_handle_item.{h,cpp}`    | L3 网格基准手柄（`Role::Origin` 基准点 / `Role::Cell` 单元尺寸）：拖动只发 `previewed`（场景本地重铺线），释放发一次 `committed`（上层一次性写 Document）；整数像素吸附 + 图内钳制                                                                                                                                                                                                                                                                                                              |
| `items/cell_picker_item.{h,cpp}`    | L3 单元点选交互图元：单击切换 / 拖拽框选 / Custom 编号角标 + 拖拽调序（命中测试只用 Core 单元区域）                                                                                                                                                                                                                                                                                                                                                                            |
| `items/selection_rect_item.{h,cpp}` | 唯一橙色交互图元：选区框 + 标记边的贯穿切割线延伸（可整体移动 / 四角缩放 / 直接抓边拖动＝移动切割线 / 边缘中心吸附）；**逐元素可见即可拖**：`setCutLinesVisible` 门控贯穿延伸段的绘制与可抓性，`setBorderVisible` 门控矩形边框/手柄绘制与其专有交互（四角缩放/框内平移/选区段拖边）；图元整体仅在**两者都隐藏**时才置 `Qt::NoButton` 彻底不可交互（切割线可见、边框隐藏时仍可拖切割线延伸段）                                                                                                                                                                                                    |
| `items/image_layer_item.{h,cpp}`    | 多层画布上单个图片图层的落位框绘制 + 送达翻译（平移手势循环在基类 `DraggableCanvasItem`，本图元发 `moved(id,dx,dy)`；无落位几何运算）                                                                                                                                                                                                                                                                                                                                                  |
| `items/annotation_item.{h,cpp}`     | 单个标注的矢量渲染图元：`Shape::worldPath()`（含非破坏性变换）→`QPainterPath` 叠加绘制（不改底图像素），选中时绘制**定向包围盒（OBB）+ 8 缩放手柄 + 1 旋转手柄**（拖拽缩放/拉伸/旋转/翻转、实时矢量预览）+ 平移送达翻译（手势循环在基类，本图元发 `clicked`/`moveRequested`）                                                                                                                                                                                                                                                          | 

## z 序（z_order.h）

`kBase=0 < kImageLayerPixels=5 < kImageLayer=10 < kAnnotation=20 < kAnnotationPreview=25 < kMask=30 < kGrid=40 < kSelection=50 < kCellNumber=55 < kGridHandle=60`

> `kAnnotationPreview`＝标注绘制预览（橡皮筋），新建标注时逐帧更新的临时图元；须**抬到已提交标注之上**——它与 `kAnnotation` 同属一个 `AnnotationItem` 类（构造默认落在 `kAnnotation`），若与 `kAnnotation` 同值则靠插入次序叠放，会话中后提交的标注会盖住先创建的预览图元，致橡皮筋看不见。

遮罩为**单一层 `kMask`**：一个图元内先铺红色删除底、再叠绿色保留块，绿覆盖处为保留、透出红处即删除。

## 遮罩渲染法（不做几何布尔）

红色删除底铺满全图 + 绿色保留块叠加其上（单一 `kMask` 层内先红后绿），GUI 无需自算删除集——机制与取舍
见 Gui README「关键设计决策」#1。遮罩图元随每次预览重建，`visible` 只决定其可见性：隐藏期照常按最新 Core 结果
构建（仅为不可见），所以切回显隐开关时无需重建、开回即显。

## 交互（canvas_view / canvas_desktop_input_adapter）

> **输入适配器分层**：下述桌面手势的「设备语义→业务意图」翻译与路由全部在
> `DesktopInputAdapter`；`CanvasView` 只保留与输入设备无关的状态机与信号发射（`gesture*` 执行面）。
> 触控适配器 `TouchInputAdapter`：单指委托同一个 `DesktopInputAdapter` 路由（行为同构），
> 叠加双击适应/捏合缩放/双指平移/长按菜单，调同一 `gesture*` 入口，不进入桌面路径；
> 触控路径不进入桌面实现；移动端行为详见 src/mobile README「移动端」。

- **缩放**：滚轮以鼠标为锚点缩放（一格 ×1.15）；`zoomIn/zoomOut/resetZoom/fitToWindow`。
- **平移**：按住空格 + 左键，或中键拖拽。
- **滚动范围 = 内容边界 + 外圈余量**（`canvas_scene::applySceneExtent` / `contentBounds`，余量口径由 `canvas_view` 定）：
  视图滚动条范围来自场景矩形，若场景矩形恒等于图像尺寸，「图大于窗」就推不出图像边缘一步，边缘处的选区手柄 /
  标注 / 补白区块被视口边贴死。两层各自口径分明，不可混为一谈：
  ① **内容边界** `contentBounds()` = 原图 ∪ 全部网格单元区域（`updateGrid` 逐单元 `united` 维护，`clearGrid` 退回原图）。
  补白(Pad) 的越界块属**内容**，必须**完整**进滚动范围——一旦被按屏幕口径算出的值夹住，放大后可滚进来的部分就
  越来越小、边界块永远看不全。内容可以不对称（补白只在一侧伸出），场景矩形随之不对称：整幅场景小于视口时 Qt 按
  场景矩形居中，居中的正是「内容」。
  ② **余量** `CanvasPrefs::scrollMarginPercent`（**视口各边百分比**，缺省 3、0＝不留，在 `外观/交互…` 「其他」页「边缘滚动余量」改）：
  横向取视口宽、纵向取视口高（故左右与上下可不等量），由 `CanvasView::updateScrollMargins()` 折算成 `百分比×视口边 / m11`
  场景单位注入 `setScrollMargins`。本方法既挂在 `CanvasView::updateHandleSize()`（全站唯一的「缩放基准变了」汇聚点：滚轮 /
  捏合 / 逐步缩放 / 1:1 / 适应 / 偏好变更均过它）里，也由 `CanvasView::resizeEvent()` 重推一次——余量按视口口径给，
  **窗口尺寸变了幅度就得跟着变**。效果：**与图幅与缩放档无关**（1:1 与 2000% 下都能把内容往外推同一视口比例；若按图幅
  同比给，高倍率下会把整幅画布推出视口）。非 L3 / 非补白时内容边界就是原图 → 各模式、各余量策略完全同口径；设 0 即回到无边距观感（图像边缘即滚动极限）。余量改动经 `CanvasPrefs::changed()` → 同一入口即时重算（无需重启，与手柄基准共享一条联动链）。
  `fitToWindow` 因此不 `fitInView(sceneRect)`，而是按内容自算倍率 `min(视口宽×keep/内容宽, 视口高×keep/内容高)`
  后 `setTransform` + `centerOn(内容中心)`：fitInView 会把余量也纳入适配、且 fit 改倍率后余量按新倍率重折算、
  场景矩形随之再变，一步到位不了（几何收敛但不精确）。适配的 `keep` 用**固定小边距** `kFitToWindowKeepRatio`＝0.94
  （内容最长边铺到视口的 94%）：滚动余量是**平移范围**偏好，不参与适配缩放——否则余量调大时适配会把内容挤到
  (1－2×百分比) 乃至趋零。默认余量 3% 下 0.94＋2×3%＝1.00，适配后场景矩形恰等于视口、不出滚动条；余量更大时
  适配后仍留可滚空白，属余量偏好本身（上限 `kScrollMarginPercentMax`＝45 保证适配档下空白 90% 视口仍小于内容 94%）。
- **框选**：空白处左键拖拽出橡皮筋框选，松开后 `emit rubberSelect(QRectF)`（交 CanvasCoordinator 写回 Document）。
  **框选恒按整数像素吸附**：橡皮筋几何与上报矩形走同一条 `snappedMarqueeSceneRect`（两端 `round` 后规范化），故非整数缩放档下
  「看到的框」＝「提交的矩形」，不会出现松手跳位或整像素错半格（L1 单矩形与 L2 多矩形追加框同理）。
  已有选区框的移动/缩放/拖边同样以整数像素收尾：`SelectionRectItem::mouseReleaseEvent` 在发 `rectChanged`
  之前把四边量化（并保底 1px、推回图内），故不依赖上层回设也能落在像素网格上。
  图层面板隐藏「选取边框」时 L1/L2/L3 一律不起带（选区框与单元选择都不接受框选，无结果可出）。
- **微调**：方向键 `emit nudgeSelection(±1)`，Shift+方向键 `±10`。
- **右键菜单**：清除切割线 / 重置视图 / 切换遮罩，分别 `emit *Requested`。
- **坐标回报**：鼠标移动 `emit cursorScenePos(QPointF)`（供状态栏显示图像坐标与像素 RGBA）。
- 选区框手柄尺寸与抓边条带随缩放换算为场景单位（`updateHandleSize`：`适配器.screenOverlayPx() / m11`，
  桌面 8px / 触控 ≥12pt，SPEC §8.3），保持屏幕观感恒定；图元无需知晓平台差异。

> 选区框的移动/缩放/拖边统一经 `SelectionRectItem::rectChanged` → `CanvasScene::selectionEdited` 转发到 CanvasCoordinator；
> 拖动选区边＝移动对应切割线（同一图元），故 CanvasCoordinator 只连接场景这一个稳定信号，无需感知惰性创建/销毁的选区图元。

## L3 网格线（grid_layer）

L3 模式下，`CanvasScene::updateGrid(grid, true)` 委托 `GridLayer::rebuild` 依 Core `Grid` 的单元边界（去重、裁剪到图像内）
画**灰色虚线**贯穿线（`QColor(150,150,150)` 1px `DashLine` cosmetic，z=`kGrid`），纯显示、`setAcceptedMouseButtons(Qt::NoButton)`
不遮挡交互。非 L3 模式或换图时 `clearGrid()` 清空。L3 **隐藏橙色选区框**（`syncSelection(rect,false)`）——网格由参数定义、
不依赖矩形选区；单元点选/编号角标交互由 `cell_picker_item` 承担（见下）。**单元选择高亮（picker）受 `selectionVisible_` 门控**：
图层面板关闭「选取边框」时 `updateGrid` 令 `pickerItem_->setVisible(false)`，那层蓝色单元高亮即隐藏且不可点选（该层是
`CellPickerItem` 的单元选择高亮，非 `GridLayer` 的灰色网格线）。
同一条门控**也覆盖框选通道**：`CanvasView::gestureMarqueeBegin` 以 `CanvasScene::selectionVisible()` 为唯一门控
——该图层隐藏时 L1/L2/L3 一律不起橡皮筋；L3 另由 `CanvasScene::cellSelectionInteractive()`
（= `gridActive_ && selectionVisible_`）让 `cellsIntersecting` 恒返回空。隐藏即整体不可交互，
不会出现「点选失效、拖选照旧」的两条通道分叉。网格的几何推导全在 Core `Grid::build`。

## L3 网格基准手柄（grid_handle_item；做在网格线层）

网格线层里唯二可交互的图元（z=`kGridHandle`=60，高于单元点选图元，否则按下被其抢走），
两个角色合为一个类：`Role::Origin` 位于基准点 `(x0,y0)`、拖动改**相位**（x0/y0）；
`Role::Cell` 位于 `(x0+cw, y0+ch)`、拖动改**单元尺寸**（cw/ch，基准点不动）——四个参数全覆盖，与参数面板同义。

- **显隐与可交互**：随「网格线与切割线」图层开关（`gridVisible_`）与 `gridActive_`；另在图片拾取 / 标注选取态
  只留可见、置 `Qt::NoButton` 让位（`refreshGridHandleInteraction()` 是唯一判据点）。
- **拖拽不逐帧写 Document**：拖动中只发 `previewed` → 场景用草稿参数交 Core `Grid::build` 重铺网格线
  （整数几何、廉价），并隐藏单元点选图元避免旧高亮误导；**释放才发一次** `committed`
  → `CanvasScene::gridParamsEdited` → `CanvasCoordinator::onGridParamsEdited` → `Document::setGridParams`
  （只一次 `changed()`，不触发逐帧 Core 全量预览重算）。
- **整数像素吸附 + 越界不允许**：增量先 `round` 再叠加到按下快照（基线不累计漂移），基准点钳在
  `[0,W-cw]×[0,H-ch]`（单元尺寸不变，上界同时保证另一手柄 `(x0+cw,y0+ch)` 不出图）、
  单元宽高钳在 `[1,W-x0]×[1,H-y0]`；两手柄恒在图内、单元恒为正尺寸（`Grid::build` 要求）。
  参数面板的上限同此口径：基准点 `[0,W-cw]`、单元尺寸 `[1,W]`（无图时回落到坐标输入上限）。
- **清空行为不变**：几何变即按序号的选择集失效，`setGridParams` 与面板入口同语义地清空 `selectedCells_`。
- `GridLayer::rebuild` 先 `clearLines()` 再建线（**不动手柄**）：否则逐帧预览重建会把正在拖的手柄一并隐藏。
  切割线样式（L2）下基准手柄退场——基准只在 L3 有意义。

## L3 单元点选（cell_picker_item；FR-L3.3 / FR-L3.5）

L3 下与选区框**互斥显隐**的第二个交互图元，覆盖整幅图像，依 Core `Grid` 的单元区域做命中测试：

- **单击**某单元 → `emit cellToggled(index)`，上层在 `Document` 里切换其选中态（已选移除、未选追加到末尾）。
- **拖拽框选** → 实时画半透明蓝框选带，松开 → `emit cellsMarqueeSelected(indices)`（框内单元序号，去重并入选择集）。
- **已选单元高亮**：半透明蓝填充 + 蓝边框（独立于极性的绿/红遮罩）；悬停单元白边框反馈（局部重绘，避免大选集整层重绘）。
- **Custom 编号角标**（`SortStrategy::Custom`）：在每个已选单元左上角以**设备像素**绘制其 1-based 自定义序号
  （= 该单元在 `selectedCells_` 中的位次 + 1）；角标字号恒定屏幕大小（`worldTransform` → `resetTransform` 后绘制），
  单元在屏幕上 < 14px 时跳过角标（避免缩小时糊成一片）。非 Custom 不显示角标（输出序由 Core 按行/列主序计算，网格上直观可见）。
  角标由 `CellPickerItem` 的**角标子图层** `CellNumberLayer` 绘制（子 z = kCellNumber − kSelection，展示为独立图层，随选区图元显隐），
  受图层面板「单元编号」开关门控；角标隐藏时下面的拖拽调序也一并停用（`badgesVisible_` 门控：自定义序无可见标识即无法校序）。
- **Custom 拖拽调序**（FR-L3.5）：在已选单元上拖动 → 被拖单元橙色粗边框、目标单元黄色虚线边框；松开落在任一单元（非起点）即
  → `emit cellReordered(from, to)`，上层依目标是否已选中重排 `selectedCells_`（即自定义输出序）：
  目标已选 → `from` 与 `to` 互换位次、其余单元不动；目标未选 → `to` 顶替 `from` 的原序位（`from` 移出选择集），数量与各位次不变。

> 手势以覆盖层尺度（`overlayScale_`，屏幕基准随适配器：桌面≈8px / 触控≥12pt 的场景单位）为单击/拖拽阈值；**只在释放时 emit 一次**，拖拽中仅重绘图元自身
> （不逐帧回写 Document），规避拖拽图元「emit 触发上层 clear()+重建 → 删掉正在处理事件的图元 → 崩溃」与「逐帧取整回设 → 抖动」两类陷阱。
> 命中测试只用 Core 单元的 `area.contains` / 矩形重叠，不含任何网格几何推导；选择集运算（toggle/union/reorder）全落在 `Document`。
> 事件链：`CellPickerItem` → `CanvasScene`（转发）→ `CanvasCoordinator`（写 `Document`）→ `refreshPreview` 回灌 `updateCellSelection` 刷新高亮与角标。

- **余量策略 Pad 的越界绘制与滚动外扩**（`cell_picker_item` / `mask_layer` / `canvas_scene`）：
  ① **绘制面**：`Grid::build` 在补白策略下保留残缺单元的完整尺寸（`area` 可伸出图像右下、甚至落在负坐标；见 Core
  `layoutAxis`），故 `CellPickerItem::boundingRect` **不能只取整幅原图**，而是 `setGrid` 期预算的 `bounds_` = 原图 ∪ 全部
  单元区域；`MaskLayerItem` 同理 = 原图 ∪ 全部保留块（`keeps_` 来自 Core `EngineResult`）。包围盒不覆盖越界部分时，
  Qt 不会把那部分纳入重绘区域、`exposedRect` 也取不到，越界覆盖要等到全量重绘才被补上——故 `bounds_` 恒须并入单元
  区域，越界部分随常规 `update()` 即时绘制、也能直接点选。
  ② **滚动面**：`updateGrid(grid, true)` 把各单元区域 `united` 成 `gridBounds_`，内容边界（`contentBounds` = 原图 ∪
  `gridBounds_`）随之包住越界那截，`applySceneExtent` 再在外面包一层视口口径余量（横、纵各按视口边）→ 越界块在**任何缩放档位**下都能整体
  平移进视口（若拿屏幕口径的上限夹它，放大后可滚进来的部分就会越来越小）。余量策略本身不传进场景（三种策略共用
  同一套相位 `virtualStart`，差别只在越界那截怎么处理）：Discard（跳过残缺单元）/ KeepPartial（已夹回图内）的单元
  恒在原图内 → 内容边界就是原图，与其余模式同口径。`clearGrid`（切出 L3 / 换图）把 `gridBounds_` 复位为空矩形。
  灰色网格线（`GridLayer`）仍只画严格落在图像内部的边，图像外的补白区不铺线（其范围由选区高亮与遮罩绿块表达）。

## 切割线（即选区边的贯穿延伸、唯一橙色图元）

单矩形诱导的四条切割线恰好落在选区矩形的四条边上，是选区边的「贯穿全图延伸」。故**无独立的切割线图元**：
`CanvasScene::updateCutLines` 依 Core `generateCutLines` 的结果，判定选区哪几条边有内部贯穿切割线（单矩形/十字四边皆有、
横带仅上下、竖带仅左右；落在图像边界的不算），经 `SelectionRectItem::setCutEdges` 下发；选区框用**同一支橙色画笔**把这些边
延伸绘制为贯穿全图的切割线（竖边贯穿全高、横边贯穿全宽）。

**为何天然可拖且绝不错位**：切割线与选区边是**同一个 `SelectionRectItem` 的同一条边**——抓任一条边（含贯穿延伸段）
沿法向拖动＝移动该选区边＝移动对应切割线；线与选区始终同源同位，从物理上杜绝「蓝/橙双线并存、手柄错位」等
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
空白处拖拽仍可框选追加矩形。**选框手势的整数收尾由图元自持**：`updateRect` 对新整数与上一帧相同时早退（不发
`changed`），场景便不会再回设该图元，故 `SelectionRectItem` 在释放时自行量化四边（不依赖回设），否则松手后框会停在浮点位置。选中态是**视图状态**（不写 Document）：`setActiveMultiRect(index)` 高亮对应框，
与参数面板列表选中行双向联动；`setMultiRectHandleSize`/`isDraggingMultiRect()` 供缩放换算与拖拽期跳过面板回同步。
诱导切割线经 `updateMultiRectCutLines(grid, true)` 以**橙色切割线样式**（`asCutLines=true`）绘制，
**不启动 `cell_picker_item`**（其 grab 会阻断框选）；单选区框隐藏。该诱导线与 L3 灰色网格线共用「网格线与切割线」一个图层开关（两模式互斥使用，故无重叠）：非 L3 时开关直接切
`GridLayer` 可见性、重建期另按 `cutLinesVisible_` 门控；L3 时开关作用于灰色网格线 `gridVisible_`。`GridLayer::rebuild` 恒按最新
网格建出线图元、`visible` 只决定其可见性，故隐藏期发生重建也不会丢图元，开关开回即时可见。
**进入该子功能时「带入现有单选区」**：`Document` 在列表为空且有有效单选区时把该矩形作为首个多矩形（单选区框在本模式下
不显示，选区改由多矩形轮廓表示），故切换瞬间即有一个可拖选框与对应诱导线。机制与取舍详见 Gui README「关键设计决策」#7。

## 标注矢量叠加（annotation_item / canvas_scene / canvas_view）

标注层与底图**分离**：每个 Core `Annotation` 对应一个 `AnnotationItem`（继承 `DraggableCanvasItem`，见 items/README），用
`Shape::worldPath()` → `toQPainterPath()` **矢量叠加**渲染（z=`kAnnotation`），**不改底图像素**；烧录仅在导出时发生。
文字标注 `dynamic_cast<TextShape*>` 取 `text()/fontSize()`，`xform_` 非单位阵时经 `painter->setTransform(..., true)`
在局部坐标画真实字形（矢量仿射不失真）。描边/填充/字形**统一委托 `util::paintAnnotation`**（与导出预览烘焙共用，消除重复）。
设计机制（遮罩式分层、烧录时点、OBB 变换语义）见 Gui README「关键设计决策」#8/#9 与 `include/geometry`。

- **增量维护**：`CanvasScene::updateAnnotations(model)` 按数量增删末位图元、逐个刷新几何与选中态，**绝不 clear+重建**
  （否则拖拽中会删掉正在处理事件的图元→崩溃）；拖拽中跳过对正在拖图元的回设。
- **拖拽预览**：`model.pendingShape()` 非空时用惰性创建的 `pendingAnnoItem_`（不可交互）画橡皮筋矢量，不进 Core 历史。
- **选中/移动**：Select 下图元自行响应鼠标——press → `annotationSelectRequested` → `AnnotationCoordinator` 用 Core `hitTest` 选中；
  平移手势由场景驱动（`beginAnnotationDrag` 起手势，move/release 由场景转发给**选中项**——重叠时被按项与拖动项可以不是同一个）；
  拖动释放 → `annotationMoved(index,dx,dy)` → 委托 Core 平移。**原地单击按同点连击循环**（`PickCycle`，与图片图层拾取共用同一实现）：
  同点同栈反复单击沿叠放栈下探选中（到底回顶）、换点或栈变复位栈顶；拖动不推进循环（提交时清循环态）。空白反选两条通道：框选态由 `marqueeStarted`（空白起带即发）承担；
  标注选取态禁框选，由桌面适配器判定「交付后无抓取者」→ `CanvasView::annoBlankPressed` 专用信号取消选中（触控同构）。
- **OBB 手柄变换**：选中时依 `computeObb(xf)` 画虚线 OBB + 8 缩放手柄 + 1 旋转手柄（抓取区半径 `handleSize_*2.0` 大于可视手柄）；
  拖拽中仅调 Core `obbPreviewTransform` 做**矢量预览**并逐帧 `emit transformPreview(累积绝对值)` 供属性面板回显；
  释放时 `emit transformRequested(sx,sy,deg)`（增量）→ Core `applyObbTransform` 一次性提交（预览与提交同一算子，所见即所得）。
  变换全走 Core，GUI 不自算矩阵。
- **绘制态门控**：任一绘制工具激活时左键手势被视图拦截路由为 `annoDrag*`；Select 时关闭门控，维持选区/单元交互。
- **折线（Polyline）点击式绘制**：**左键落顶点 + 悬停橡皮筋预览 + 右键收笔**——`annoDragStart` 落正式顶点、
  `annoHover` 实时预览「已落顶点 + 到光标连线」、右键 `annoFinish` 收笔提交（绘制态下右键不弹视图菜单）。
- **手柄尺寸**：`setAnnotationHandleSize` 随视图缩放换算，屏幕观感恒定（NFR-7）。
- **图层显隐**：`setAnnotationsVisible`/`setBaseVisible`/`setImageLayersVisible` 仅切图元可见性（图层面板开关），不删数据、不影响导出；`baseVisible()`/`imageLayersVisible()` 把状态交给上层状态栏取色判定「所见」是否含该图层。
