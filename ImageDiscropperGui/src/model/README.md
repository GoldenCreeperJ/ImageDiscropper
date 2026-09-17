# model/ — 状态源与 Core 桥接

会话状态与 Core 调用的中枢。二者分离：`Document` 只管状态，`EngineBridge` 只管调 Core。

| 文件 | 职责 |
| ---- | ---- |
| `document.{h,cpp}`     | 会话状态单一真相源，组装 `EngineConfig`，变更时发信号 |
| `engine_bridge.{h,cpp}` | GUI 中**唯一**触碰切割引擎 API 的类 |
| `annotation_bridge.{h,cpp}` | 标注域桥：唯一持有并驱动 Core `annotation::AnnotationLayer` |

## Document（QObject）

持有：原图 `original_` / 工作图 `working_`（第一阶段二者相同，无预处理）、模式 `Tier`（默认 L1）、
极性 `Polarity`（默认 KEEP）、L1 形状 / L2 子功能、选区矩形 `RectRegion` + `hasRect_`、
**L2 多矩形列表 `rects_`（仅 `MULTI_RECT` 生效，其余子功能恒空）**、
导出参数（`EmitMode`/`MergeLayout`/`ExportFormat`/质量/命名/输出目录/输出文件）、
**重排合并参数 `mergeCols/Rows/CellW/CellH`（0=未指定，交 Core 自动推导）+ `padColor`（空位/余量填充色，默认透明）**、
**重排填充顺序 `MergeOrder mergeOrder_`（行/列优先 + 蛇形? + 倒序?，与选择排序正交）**、
L3 网格参数 `GridParams`（默认单元 100×100，避免 `GridParams` 默认 1×1 在大图产生海量单元）、
派生行列数（只读回显）、排序 `SequenceParams`、选择集 `selectedCells`（L1/L2 恒空，由 Core 自动推导）。

- 枚举映射：`L1Shape{RECT,HBAND,VBAND}`、`L2Sub{CROSS,HLINE,VLINE,MULTI_RECT}` → Core `CutGenerator`
  （`RECT` / `HORIZONTAL_LINE` / `VERTICAL_LINE` / `MULTI_RECT`）；L3 → `GRID`（携带 `GridParams`）。
  `MULTI_RECT` 时 `buildCutConfig` 另把 `rects_` 搬运进 `CutConfig::rects`（Core 对每个矩形诱导十字带后取并集，方案 A）。
- `buildCutConfig()` / `buildEngineConfig()`：**纯字段搬运 + 枚举映射**，不含任何几何/极性判定。
- `applyEngineConfig(cfg)`：`buildEngineConfig` 的**互逆反向映射**（G-13 配置加载 / G-12 撤销重做共用）——生成器+tier 还原 L1 形状/L2 子功能，搬运几何/选择集/排序/导出参数（optional 画布/单元尺寸 nullopt↔0）；直接改字段后只发一次 `changed()`；不还原 source 尺寸（图像不随配置/快照变）；守卫「非 L3 不得重排」「网格单元尺寸为正」不变式。
- 信号：`imageChanged()`（换图，需重建预览 pixmap）、`changed()`（任意参数变更，仅需刷新预览）。
- `setMode()` 套用模式定义极性（L1→KEEP、L2→REMOVE；L1≡keep、L2≡remove）；**切入 L3 保留当前极性不变**（L3 极性独立，从 L2 进 L3 沿用 remove）；**离开 L3 时若 `layout_==REARRANGE` 自动复位为 COLLAPSE**（模型层维持「重排仅 L3」不变式）。
- `setPolarity()` 反向耦合：在 L1/L2 下切换极性会同步切换模式（KEEP→L1、REMOVE→L2）；L3 极性独立、不改模式。
- L3 网格：`setGridOrigin`/`setCellSize`（拒绝非正值）/`setRemainder` 改值后发 `changed`；`setDerivedGridSize(rows,cols)`
  由 MainWindow 依 Core `Grid` 回灌、**不发 `changed`**（避免回环），仅供面板只读显示。
- L3 选择集：`setSelectedCells`/`selectAllCells`/`invertCells`/`clearCells`（全选/反选依派生行列数）；
  `selectedCells` 的顺序在 CUSTOM 排序下即自定义序列（Core `runEngine` 约定）。
- L3 排序：`setSortStrategy`/`setSortReverse`/`setSortSnake` → `SequenceParams`（FR-L3.5）。
- L2 多矩形：`addRect`/`updateRect(index,r)`/`removeRect(index)`/`clearRects`（增删改均拒绝退化矩形、发 `changed`）；
  `rects()` 只读访问。GUI 只维护列表并搬运进 `CutConfig::rects`，**不做任何并集/几何计算**（A-0.1）。
- L2→L3 转换：`convertRectToGrid()` 以当前选区左上为基准点、宽高为单元尺寸切到 L3（清空选择集、极性置 KEEP，纯字段搬运）；若无有效单选区但多矩形列表（MULTI_RECT）非空，则取其**包围盒**作为单元基准。
- 重排合并：`setMergeGrid(cols,rows)`/`setMergeCellSize(w,h)`（负值钳 0）/`setPadColor(c)`；`buildEngineConfig` 中
  `>0` 才写入 `CompositionParams` 的 `cols/rows/cellWidth/cellHeight`（optional），否则保持 `nullopt` 交 Core 自动推导。
- 重排填充顺序：`setMergeOrderStrategy(s)`（仅 ROW_MAJOR/COLUMN_MAJOR，CUSTOM 按行优先）/`setMergeOrderReverse(on)`/`setMergeOrderSnake(on)`
  改值后发 `changed`；`buildEngineConfig` 中透传 `cfg.emitParams.mergeOrder = mergeOrder_`。

## EngineBridge（无状态、可拷贝）

| 方法 | 委托的 Core API |
| ---- | --------------- |
| `loadImage(path, out, err)`        | `engine::readImageFile`（统一解码为 RGBA） |
| `runPreview(work, cfg)`            | `engine::runEngine`（仅区域数学，适合实时预览） |
| `cutLines(cut, src)`               | `engine::generateCutLines`（取贯穿切割线供渲染） |
| `buildGrid(cut, src)`              | GRID→`Grid::build`、其余→`generateCutLines`+`induceGrid`（镜像 `runEngine` 网格产出，供 L3 网格线渲染） |
| `exportResult(work, cfg, out, err)`| `engine::runEngine` + `engine::exportImage`（全分辨率落盘，无损） |
| `saveConfig(path, cfg, err)`       | `engine::saveEngineConfig`（§9 schema JSON 落盘，G-13） |
| `loadConfig(path, out, err)`       | `engine::loadEngineConfig`（从 JSON 还原配置，G-13；nlohmann 在 Core 侧 PRIVATE，GUI 不感知） |

> 其余 GUI 代码只与 `EngineBridge` 交互，不直接 include 引擎实现细节，
> 以此保证「GUI 不含切割/几何/导出逻辑」（A-0.1/A-0.3）。

## AnnotationBridge（QObject）——标注域桥

类比 `EngineBridge` 之于切割引擎：`AnnotationBridge` 是 GUI 中**唯一**持有并驱动 Core
`annotation::AnnotationLayer` 的类，把画布/面板意图翻译成对 Core 标注 API 的调用，**自身不含任何几何/光栅化**（A-0.1/A-0.3）。

- `enum class AnnoTool`：SELECT + 各形状 + POLYLINE + TEXT + BRUSH；`shapeTypeForTool()` 映射到 Core `geometry::ShapeType`（**本轮无箭头**，Core 无 ARROW）。
- **工具/属性**：`setTool`（SELECT→Core EDIT 模式、其余→DRAW）、`setColor/setStrokeWidth/setFill/setText/setFontSize`（委托 Core `change*`：EDIT 作用选中项、DRAW 改下一次绘制默认）。
- **绘制流程**：两点形状 `beginShape/updateShape/commitShape/cancelPending`；画笔 `beginPath/appendPathPoint/commitPath`（按住连续追点）；**折线点击式** `beginPath`（首次落顶点）/`appendPathPoint`（左键再落顶点）/`previewPolyline`（悬停时以草稿+到光标临时连线构造橡皮筋预览、**不落草稿**）/`commitPath`（右键/Esc 收笔，依 `pathDraft_` 正式顶点重建、丢弃橡皮筋临时段）/`hasPathDraft`；文字 `addText`。几何均走 Core `buildShape`。
- **选择/移动/删除**：`selectAt`（Core `hitTest`+`selectAnnotation`）、`moveSelectedBy`（委托 Core `Shape::translateWorld` 世界系平移—内部把世界位移换算到局部系再偏移参数，**保留具体类型**不退化为 PATH、且旋转/缩放后拖动方向仍跟随光标）、`removeSelected`（Core `removeAnnotation`）。
- **变换（缩放/旋转/翻转）**：`transformSelected(sx,sy,rotateDeg)` 委托 Core `Shape::applyObbTransform`（**非破坏性仿射矩阵**：绕局部盒中心沿自身轴缩放 + 绕世界中心刚性旋转；旋转走世界系，故与既有非均匀缩放复合也不剪切），保留具体类型与文字字形不退化；sx/sy 为**增量系数**（1.0=不变、负即翻转）、rotateDeg 为增量角。Core 同时把本次增量**累积进带符号 OBB 参数**（缩放连乘、旋转累加），`displayObbScaleX/Y/RotationDeg()` 读出该**绝对累积值**供属性面板忠实回显——**不从 `xform_` 矩阵分解**（分解有二重歧义，无法区分 sx<0 与 θ+180°&sy<0，会丢掉翻转的负号）。属性面板「应用变换」与画布手柄拖拽**共用此入口**；无选中或增量恒等（1,1,0）则忽略。
- **撤销/重做/清除**：`undo/redo/clearAll` → Core `revoke/redo/clear`（分层快照，GUI 不自建历史栈）。
- **图层可见/导出烧录**：`setLayerVisible/setBurnIn` 为 GUI 侧标志；`burnIn(base)` 以传入的**当前工作图**为底逐个调 Core `rasterize` 合成（不复用 `AnnotationLayer::burnIn()` 内部可能陈旧的 `image_`，确保与预处理后最新底图一致）。
- **信号**：`changed()`（标注列表/预览变化→画布重绘）、`selectionChanged()`（选中变化→高亮+属性面板）、`toolChanged()`（工具变化→面板同步）。
