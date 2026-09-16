# model/ — 状态源与 Core 桥接

会话状态与 Core 调用的中枢。二者分离：`Document` 只管状态，`EngineBridge` 只管调 Core。

| 文件 | 职责 |
| ---- | ---- |
| `document.{h,cpp}`     | 会话状态单一真相源，组装 `EngineConfig`，变更时发信号 |
| `engine_bridge.{h,cpp}` | GUI 中**唯一**触碰 Core API 的类 |

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

> 其余 GUI 代码只与 `EngineBridge` 交互，不直接 include 引擎实现细节，
> 以此保证「GUI 不含切割/几何/导出逻辑」（A-0.1/A-0.3）。
