# canvas/ — QGraphicsView 画布与图层

> **include/src 分离**：本模块头文件在 `include/gui/canvas/`，实现（`.cpp`）与本 README 在 `src/canvas/`。

按 guideline §4.2 的 **z 序图层化**组织画布：底图 → 删除遮罩 → 保留遮罩 → 网格 → 切割线 → 选区框 → 角标。
场景坐标统一为**原图像素坐标**；底图用降采样 pixmap 经变换铺回原图尺寸，各叠加层即可直接按原图坐标绘制。
本目录**只渲染 Core 给出的结果**（切割线来自 `generateCutLines`、保留块来自 `EngineResult.kept`），不含几何计算（A-0.1）。

| 文件 | 职责 |
| ---- | ---- |
| `z_order.h`                  | 各图层 z 值常量（统一层序，避免魔法数散落） |
| `canvas_scene.{h,cpp}`       | 场景：装配并刷新底图 / 遮罩 / 选区（含贯穿切割线延伸），转发选区编辑信号 |
| `canvas_view.{h,cpp}`        | 视图：滚轮缩放、空格/中键平移、方向键微调、框选、右键菜单 |
| `mask_layer.{h,cpp}`         | 保留(绿)/删除(红)遮罩层（见下方渲染法） |
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
