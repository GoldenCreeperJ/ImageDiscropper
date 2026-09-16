# model/ — 状态源与 Core 桥接

会话状态与 Core 调用的中枢。二者分离：`Document` 只管状态，`EngineBridge` 只管调 Core。

| 文件 | 职责 |
| ---- | ---- |
| `document.{h,cpp}`     | 会话状态单一真相源，组装 `EngineConfig`，变更时发信号 |
| `engine_bridge.{h,cpp}` | GUI 中**唯一**触碰 Core API 的类 |

## Document（QObject）

持有：原图 `original_` / 工作图 `working_`（第一阶段二者相同，无预处理）、模式 `Tier`（默认 L1）、
极性 `Polarity`（默认 KEEP）、L1 形状 / L2 子功能、选区矩形 `RectRegion` + `hasRect_`、
导出参数（`EmitMode`/`MergeLayout`/`ExportFormat`/质量/命名/输出目录/输出文件）、选择集（第一阶段恒空）。

- 枚举映射：`L1Shape{RECT,HBAND,VBAND}`、`L2Sub{CROSS,HLINE,VLINE}` → Core `CutGenerator`
  （`RECT` / `HORIZONTAL_LINE` / `VERTICAL_LINE`）；L3 对应 `GRID`（第二阶段）。
- `buildCutConfig()` / `buildEngineConfig()`：**纯字段搬运 + 枚举映射**，不含任何几何/极性判定。
- 信号：`imageChanged()`（换图，需重建预览 pixmap）、`changed()`（任意参数变更，仅需刷新预览）。
- `setMode()` 会套用该模式的默认极性（L2→REMOVE，其余→KEEP）。
- `setPolarity()` 反向耦合：在 L1/L2 下切换极性会同步切换模式（KEEP→L1、REMOVE→L2）；L3 极性独立、不改模式。

## EngineBridge（无状态、可拷贝）

| 方法 | 委托的 Core API |
| ---- | --------------- |
| `loadImage(path, out, err)`        | `engine::readImageFile`（统一解码为 RGBA） |
| `runPreview(work, cfg)`            | `engine::runEngine`（仅区域数学，适合实时预览） |
| `cutLines(cut, src)`               | `engine::generateCutLines`（取贯穿切割线供渲染） |
| `exportResult(work, cfg, out, err)`| `engine::runEngine` + `engine::exportImage`（全分辨率落盘，无损） |

> 其余 GUI 代码只与 `EngineBridge` 交互，不直接 include 引擎实现细节，
> 以此保证「GUI 不含切割/几何/导出逻辑」（A-0.1/A-0.3）。
