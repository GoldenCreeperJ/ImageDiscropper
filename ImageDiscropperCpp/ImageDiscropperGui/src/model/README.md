# model/ — 状态源与 Core 桥接

会话状态与 Core 调用的中枢。三者分离：`Document` 只管状态（内部**组合** `PreprocessSession` 承载预处理会话态），`PreprocessSession` 只管预处理声明态与纯策略，`EngineBridge` 只管调 Core。

| 文件                              | 职责                                                                                                                                                                                                                                                                                                     |
|---------------------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `document.{h,cpp}`              | 会话状态单一真相源，组装 `EngineConfig`，变更时发信号；「画布/图层/预处理会话」域实现在独立 TU `document_canvas.cpp`（类不拆）                                                                                                                                                                                                                   |
| `document_canvas.cpp`           | `Document` 的「画布/图层/预处理会话」域成员实现（TU 拆分）：图像载入/换底图/工作图与预处理会话声明/预处理目标路由/图层栈 CRUD/画布尺寸/不透明度                                                                                                                                                                                                                  |
| `config_codec.{h,cpp}`          | `ConfigCodec`（`Document` 的 friend）：`Document ↔ EngineConfig` 字段映射（`buildEngineConfig`/`buildSerializableConfig`/`applyEngineConfig` 的 `ConfigCodec` 静态函数）；`Document` 保留一行转发                                                                                                                            |
| `preprocess_session.{h,cpp}`    | 预处理会话（纯 C++、零 Qt）：声明式轻队列 `steps_` + 独立 `resize_` + `base_` 重放缓存 + geo→color 不变式/块内移动限幅/载入归一化/`effectivePipeline`/`baseDirtyFor`/`geomSig`/`producesGray` 等纯策略（由 `Document` 组合持有、`PreprocessController` 经 Document 驱动）                                                                                  |
| `layer_session_store.{h,cpp}`   | `LayerSessionStore`（纯 C++、零 Qt）：按 layerId 单点持有每层「预处理声明态会话（`LayerSession`＝`PreprocessSession` + `LayerSig` 物化签名）+ 逐层物化位图（并列 `unordered_map<int,core::Image>`，零拷贝直传 `composeCanvas`）」，统一生命周期回收（`find`/`ensure`/`remove`/`clear`/`prune`；由 `Document` 组合持有、`PreprocessController` 经 Document 驱动，只存数据不跑像素） |
| `engine_bridge.{h,cpp}`         | GUI 中**唯一**触碰切割引擎 API 的类                                                                                                                                                                                                                                                                               |
| `annotation_editor.{h,cpp}`     | 标注会话编辑器（纯 C++、零 Qt 依赖）：组合 Core `annotation::AnnotationSet` + 撤销/重做双栈 + `EditType`/选中/`current*` 属性模板 + 底图持有                                                                                                                                                                                            |
| `annotation_bridge_pending.cpp` | `AnnotationBridge` 的「绘制流程」域实现（TU 拆分）：pending 草稿重建/两点形状 begin·update·commit/折线·画笔路径/文字添加                                                                                                                                                                                                                |
| `annotation_bridge.{h,cpp}`     | 标注域桥：唯一持有并驱动 `AnnotationEditor`（其下组合 Core `annotation::AnnotationSet`）                                                                                                                                                                                                                                 |

## Document（QObject）

持有：原图 `original_`（不可变）/ **预处理会话 `preprocess_`（`PreprocessSession`：组合声明式轻队列 `steps_` + 独立 `resize_` + `base_` 重放缓存，详见下「PreprocessSession」小节）** / 工作图 `working_`（＝在 `preprocess_` 有效管线上重放的物化产物；新载入图时会话清空、`working_`≡`original_`）、模式 `Tier`（默认 L1）、
极性 `Polarity`（默认 Keep）、L1 形状 / L2 子功能、选区矩形 `RectRegion` + `hasRect_`、
**L2 多矩形列表 `rects_`（仅 `MultiRect` 生效，其余子功能恒空）**、
导出参数（`EmitMode`/`MergeLayout`/`ExportFormat`/质量/命名/输出目录/输出文件）、
**重排合并参数 `mergeCols/Rows/CellW/CellH`（0=未指定，交 Core 自动推导）+ `padColor`（空位/余量填充色，默认透明）**、
**重排填充顺序 `MergeOrder mergeOrder_`（行/列优先 + 蛇形? + 倒序?，与选择排序正交）**、
L3 网格参数 `GridParams`（默认单元 100×100，避免 `GridParams` 默认 1×1 在大图产生海量单元）、
派生行列数（只读回显）、排序 `SequenceParams`、选择集 `selectedCells`（L1/L2 恒空，由 Core 自动推导）。

- 枚举映射：`L1Shape{Rect,Hband,Vband}`、`L2Sub{Cross,Hline,Vline,MultiRect}` → Core `CutGenerator`
  （`Rect` / `HorizontalLine` / `VerticalLine` / `MultiRect`）；L3 → `Grid`（携带 `GridParams`）。
  `MultiRect` 时 `buildCutConfig` 另把 `rects_` 搬运进 `CutConfig::rects`（Core 对每个矩形诱导十字带后取并集，方案 A）。
- `buildCutConfig()` / `buildEngineConfig()`：**纯字段搬运 + 枚举映射**，不含任何几何/极性判定。`buildEngineConfig` 为**运行配置**（送预览/导出，`cfg.preprocess` 恒空——队列已物化进 `working_`）。
- `buildSerializableConfig()`：**序列化配置**（供保存配置）——与 `buildEngineConfig` 同构但携带 `preprocess_.effectivePipeline()`（`[resize?]+steps_`）、`source` 取**原图尺寸**（与加载时重放基准一致）。
- 预处理访问（均**转委托 `preprocess_`**、公开签名不变）：`steps()`/`setSteps(pipeline)`（回灌整条轻队列）、`resizeTarget()`/`setResize()`、`geomSig()`（`preprocess_.geomSig(original_.w, original_.h)`→尺寸/净旋转象限，供 `PreprocessController` 尺寸门）、`hasPreprocess()`/`hasResize()`/`preprocessProducesGray()`、base 缓存系列（`hasBase`/`base`/`basePrefix`/`setBase`/`clearBase`/`baseDirtyFor`）。`setImage`/`resetPreprocess` 走 `preprocess_.clear()` 使会话失效。
- `applyEngineConfig(cfg)`：`buildEngineConfig` 的**互逆反向映射**（配置加载与撤销重做共用）——生成器+tier 还原 L1 形状/L2 子功能，搬运几何/选择集/排序/导出参数（optional 画布/单元尺寸 nullopt↔0）；直接改字段后只发一次 `changed()`；不还原 source 尺寸（图像不随配置/快照变）；守卫「非 L3 不得重排」「网格单元尺寸为正」不变式。
- 信号：`imageChanged()`（换图，需重建预览 pixmap）、`changed()`（任意参数变更，仅需刷新预览）。
- `setMode()` 套用模式定义极性（L1→Keep、L2→Remove；L1≡keep、L2≡remove）；**切入 L3 保留当前极性不变**（L3 极性独立，从 L2 进 L3 沿用 remove）；**离开 L3 时若 `layout_==Rearrange` 自动复位为 Collapse**（模型层维持「重排仅 L3」不变式）。
- `setPolarity()` 反向耦合：在 L1/L2 下切换极性会同步切换模式（Keep→L1、Remove→L2）；L3 极性独立、不改模式。
- L3 网格：`setGridOrigin`/`setCellSize`（拒绝非正值）/`setRemainder` 改值后发 `changed`；`setDerivedGridSize(rows,cols)`
  由 `PreviewController` 依 Core `Grid` 回灌、**不发 `changed`**（避免回环），仅供面板只读显示。
- L3 选择集：`setSelectedCells`/`selectAllCells`/`invertCells`/`clearCells`（全选/反选依派生行列数）；
  `selectedCells` 的顺序在 Custom 排序下即自定义序列（Core `runEngine` 约定）。
- L3 排序：`setSortStrategy`/`setSortReverse`/`setSortSnake` → `SequenceParams`（FR-L3.5）。
- L2 多矩形：`addRect`/`updateRect(index,r)`/`removeRect(index)`/`clearRects`（增删改均拒绝退化矩形、发 `changed`）；
  `rects()` 只读访问。GUI 只维护列表并搬运进 `CutConfig::rects`，**不做任何并集/几何计算**。
- L2→L3 转换：`convertRectToGrid()` 以当前选区左上为基准点、宽高为单元尺寸切到 L3（清空选择集、极性置 Keep，纯字段搬运）；若无有效单选区但多矩形列表（MultiRect）非空，则取其**包围盒**作为单元基准。
- 重排合并：`setMergeGrid(cols,rows)`/`setMergeCellSize(w,h)`（负值钳 0）/`setPadColor(c)`；`buildEngineConfig` 中
  `>0` 才写入 `CompositionParams` 的 `cols/rows/cellWidth/cellHeight`（optional），否则保持 `nullopt` 交 Core 自动推导。
- 重排填充顺序：`setMergeOrderStrategy(s)`（仅 RowMajor/ColumnMajor，Custom 按行优先）/`setMergeOrderReverse(on)`/`setMergeOrderSnake(on)`
  改值后发 `changed`；`buildEngineConfig` 中透传 `cfg.emitParams.mergeOrder = mergeOrder_`。

## PreprocessSession（纯 C++、零 Qt）

预处理「会话态 + 域不变式」的内聚对象（与 `AnnotationEditor` 同构——都是零 Qt GUI 会话对象，可被无头
`unit_tests` 直编回归）。集中承载预处理声明态与纯策略（几何/色彩不变式、移动限幅、载入归一化、baseDirty 比对、
effectivePipeline）。**只持声明态与派生查询，绝不跑像素**：`base_` 重放物化（`EngineBridge`/Core `applyExec`）、
忙碌框、信号、选区/标注清理均由 `PreprocessController` 承担。

- **声明态**：`steps_`（声明式轻队列，恒守「全部几何块」不变式、绝不含 `ResizeOp`）+
  `resize_`（独立画布尺寸，恒在 `steps_` 之外、最先重放）+ `base_`/`basePath_`（`resize(original)` 物化缓存与其前缀）。
- **静态纯策略**（作用于给定管线、不改当前态，供 Controller 在提交前算候选态）：`geoBlockLen`、
  `appendKeepingInvariant`（几何插几何块尾、色彩追加队尾）、`moveWithinBlock`（仅相邻同类才换序、越界/跨块返回 false）、
  `makeEffective`（`[resize?]+steps`）、`normalizeLoaded`（载入时抽出散落 `ResizeOp` 为独立 resize、其余重排 geo 前 color 后）。
- **实例查询/维护**（已提交态）：`effectivePipeline`/`geomSig(origW,origH)`/`producesGray`/`hasPreprocess`/`hasResize`；
  base 缓存 `setBase`/`clearBase`/`baseDirtyFor(prefix)`；改写 `setSteps`/`setResize`/`clear`（换图/重置复用）。
- `ResizeTarget`（独立画布尺寸）定义于本头，使 session 零 Qt 可独立编译（`document.h` 经 include 传递可见）。

## EngineBridge（无状态、可拷贝）

| 方法                                   | 委托的 Core API                                                                                                                                                                                                                                 |
|--------------------------------------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `loadImage(path, out, err)`          | `io::readImageFile`（统一解码为 RGBA）                                                                                                                                                                                                              |
| `runPreviewGeometry(dims, cfg)`      | `engine::runEngineGeometry`（给定已预处理输出尺寸直接跑几何流水线、不应用 cfg.preprocess，供预览脱离全分辨率工作图）                                                                                                                                                              |
| `cutLines(cut, src)`                 | `engine::generateCutLines`（取贯穿切割线供渲染）                                                                                                                                                                                                        |
| `buildGrid(cut, src)`                | Grid→`Grid::build`、其余→`generateCutLines`+`induceGrid`（镜像 `runEngine` 网格产出，供 L3 网格线渲染）                                                                                                                                                        |
| `exportResult(work, cfg, out, err)`  | `engine::runEngine` + `engine::exportImage`（全分辨率落盘，无损）                                                                                                                                                                                       |
| `saveConfig(path, cfg, err[, anno])` | `engine::saveEngineConfig`（SPEC §7 schema JSON 落盘；可选 `AnnotationSet*` 非空时附带 `annotations` 段）                                                                                                                                                 |
| `loadConfig(path, out, err[, anno])` | `engine::loadEngineConfig`（从 JSON 还原配置；可选 `AnnotationSet*` 非空时一并还原 `annotations` 段到该集合、缺段＝清空；Core 的第 4 出参 `std::string* annoErr`（标注段逐条报因）本层恒传 `nullptr`——状态栏为单行会把多行报因压平（产品决定，见 Gui README「已核对的 Core API」）；nlohmann 在 Core 侧 PRIVATE，GUI 不感知） |
| `applyPipeline(src, pipeline)`       | `preprocess::PreprocessPipeline::applyExec`（声明式队列物化：执行层融合把整条流水线应用到 src——色彩折单次 `channelMap`、几何折单次 `D4 remap`、与逐步 `apply` 逐像素等价，供 `PreprocessController` 从 `original_` 重放 `steps_` 得 `working_`）                                               |

> 其余 GUI 代码只与 `EngineBridge` 交互，不直接 include 引擎实现细节，
> 以此保证「GUI 不含切割/几何/导出逻辑」。

## AnnotationBridge（QObject）——标注域桥

类比 `EngineBridge` 之于切割引擎：`AnnotationBridge` 是 GUI 中**唯一**持有并驱动 `AnnotationEditor`
（其下组合 Core `annotation::AnnotationSet` 数据列表 + zOrder 不变式 + hitTest/burnIn）的类，把画布/面板意图翻译成对 editor 会话 API 的调用，**自身不含任何几何/光栅化**。

- `enum class AnnoTool`：Select + 各形状 + Polyline + Text + Brush；`shapeTypeForTool()` 映射到 Core `geometry::ShapeType`。
- **工具/属性**：`setTool`（Select→Core Edit 模式、其余→Draw）、`setColor`（边框色）/`setFillColor`（独立填充色）/`setStrokeWidth`/`setFill`/`setCornerRadius`（转角圆角半径）/`setText`/`setFontSize`（委托 Core `change*`：Edit 作用选中项、Draw 改下一次绘制默认）。回显走 `displayColor/displayFillColor/displayStrokeWidth/displayFill/displayCornerRadius`（有选中取选中项、否则当前默认）。
- **能力查询**（供属性面板置灰不适用控件，判定均在 GUI 桥层、不改 Core）：`contextSupportsFill()`（仅直线 / 文字不可填充，折线 / 画笔隐式闭合可填充）、`contextSupportsCornerRadius()`（仅顶点型多边形 / 折线可倒角）；有选中按选中形状类型、绘制工具按工具类型、Select 无选中一律可用（避免上一工具残值错禁默认值编辑）。
- **绘制流程**：两点形状 `beginShape/updateShape/commitShape/cancelPending`；画笔 `beginPath/appendPathPoint/commitPath`（按住连续追点）；**折线点击式** `beginPath`（首次落顶点）/`appendPathPoint`（左键再落顶点）/`previewPolyline`（悬停时以草稿+到光标临时连线构造橡皮筋预览、**不落草稿**）/`commitPath`（右键/Esc 收笔，依 `pathDraft_` 正式顶点重建、丢弃橡皮筋临时段）/`hasPathDraft`；文字 `addText`。几何均走 Core `buildShape`。
- **选择/移动/删除**：`selectAt`（Core `hitTest`+`selectAnnotation`）、`moveSelectedBy`（委托 Core `Shape::translateWorld` 世界系平移—内部把世界位移换算到局部系再偏移参数，**保留具体类型**不退化为 Path、且旋转/缩放后拖动方向仍跟随光标）、`removeSelected`（Core `removeAnnotation`）。
- **层序（叠放次序）**：`raiseSelected/lowerSelected/bringSelectedToFront/sendSelectedToBack` 作用于选中标注，取 `selectedAnnotationIndex()` 与 `annotations().size()` 算目标下标后委托 Core `moveAnnotation`（`Annotation::zOrder` 恒等于列表下标、大＝顶，增删移后由 Core 统一规范化），发 `changed()+selectionChanged()` 驱动画布重绘与面板回显；已在目标边界（顶/底）则早退。
- **变换（缩放/旋转/翻转）**：`transformSelected(sx,sy,rotateDeg)` 委托 Core `Shape::applyObbTransform`（非破坏性仿射：绕局部盒中心沿自身轴缩放 + 绕世界中心刚性旋转，保留具体类型与字形不退化）；sx/sy 为**增量系数**（1.0=不变、负即翻转）、rotateDeg 为增量角。`displayObbScaleX/Y/RotationDeg()` 读出 Core 累积的**绝对带符号值**供属性面板忠实回显（变换语义与累积参数机制见 `include/geometry`）。属性面板与画布手柄**共用此入口**；无选中或增量恒等（1,1,0）则忽略。
- **撤销/重做/清除**：`undo/redo/clearAll` → editor `revoke/redo/clear`（条目级撤销双栈：列表即主栈、撤销弹尾入重做栈、新操作清重做，GUI 不自建历史栈）。
- **图层可见/导出烧录**：`setLayerVisible/setBurnIn` 为 GUI 侧标志；`burnIn(base)` 以传入的**当前工作图**为底逐个调 editor 的合成入口（委托 `AnnotationSet::burnIn(base)`，不复用会话 `AnnotationEditor` 所持可能陈旧的底图 `image_`，确保与预处理后最新底图一致）。
- **配置还原**（SPEC §7 `annotations` 段）：`set()` 只读引用内部 Core `AnnotationSet`，供保存配置时交给 `EngineBridge::saveConfig` 序列化；`replaceAnnotations(items)` 委托 editor `restoreAnnotations` 整体替换当前列表（不推撤销快照、取消选中）后发 `changed()/selectionChanged()`；调用方（`ConfigController::onLoadConfig`）需保证在预处理队列重放**之后**执行（尺寸门现仅清框选、不清标注）；还原不纳入 DocHistory 文档撤销栈（已知限制）。
- **信号**：`changed()`（标注列表/预览变化→画布重绘）、`selectionChanged()`（选中变化→高亮+属性面板）、`toolChanged()`（工具变化→面板同步）。
