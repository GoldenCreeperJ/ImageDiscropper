# app/ — 主窗口装配与程序入口

顶层装配与编排。MainWindow 只做「装配 + 编排 + 状态栏呈现」，不含切割/几何/导出实现（A-0.1）——
Core 调用集中在 `EngineBridge`，状态集中在 `Document`。

| 文件 | 职责 |
| ---- | ---- |
| `main_window.{h,cpp}` | 主窗口：菜单/工具栏/状态栏/快捷键/引导 + 信号槽编排 |
| `main.cpp`            | 程序入口：`QApplication` + 显示 MainWindow + 事件循环 |

## 布局（guideline §4.1）

`QSplitter` 水平三分：**左侧可滚动容器（QScrollArea 竖排 LeftPanel + ToolPanel + LayerPanel）** | **中央画布（CanvasView）** | **右侧 QTabWidget（参数页 / 导出页 / 图像页 / 标注页）**。

## 编排（connectAll）

```
Document.imageChanged → onImageChanged → rebuildPreviewPixmap + syncPanels + refreshPreview
Document.changed      → onDocChanged   → refreshPreview + syncPanels
Document.changed      → scheduleHistoryCapture → 防抖 500ms 后 onDocHistoryTimeout 压入 EngineConfig 快照（G-12）
CanvasView.rubberSelect / CanvasScene.selectionEdited → onRubberSelect：L1/L2 写回 Document.setRect（MULTI_RECT 则 addRect）；
    L3 时把框选矩形经 scene_->cellsIntersecting 换成命中单元并 addCells（含从图像外起拖、未被 CellPickerItem grab 而落到视图橡皮筋的情形）
CanvasScene.multiRectEdited(index, rect) → onMultiRectEdited：L2 MULTI_RECT 下拖动/缩放第 index 个选区框 → Document.updateRect(index)（与单选区同为可拖拽 SelectionRectItem）；
    拖拽期 syncPanels 被跳过，故另调 param_->selectRectRow(index)（列表跟随选中+高亮）与 param_->updateRectListItem(index)（实时回显新尺寸）
ParamPanel.rectSelected(index) → onRectSelected → scene_->setActiveMultiRect(index)：高亮画布上对应选区框（颜色略微加强，-1 清除）
CanvasView.nudgeSelection → onNudge（方向键平移选区，钳制到图像内）
CanvasView.cursorScenePos → onCursor（状态栏坐标 + 像素 RGB）
CanvasView.zoomChanged → onZoomChanged（状态栏缩放倍数；所有缩放入口汇聚于 updateHandleSize 发出）
CanvasView.clearCutRequested / toggleMasksRequested / resetViewRequested → 对应动作
ExportPanel.exportRequested → onExport
```

`refreshPreview()` 是核心：无图→清空；无选区→提示拖拽（不跑引擎，避免 E-1 噪声）；
有选区→`buildEngineConfig` + `EngineBridge.runPreview` → 刷新切割线（`bridge_.cutLines`）/遮罩/选区，
并回写状态栏与面板提示（保留块数、输出画布尺寸、坍缩可行性）。
L2 MULTI_RECT 分支：`scene_->updateMultiRects(doc_.rects())` 增量刷新可拖拽选区框（不 clear+重建）。
拖拽期间（`isDraggingSelection()` 或 `isDraggingMultiRect()`）跳过 `syncPanels()` 与对正在拖图元的回设，避免逐帧量化抖动与卡顿；释放时照常同步一次。
重排上下文回灌：顶部先 `exportPanel_->setRearrangeContext(0,0,0)` 清零（非 L3），L3 分支再回灌真实的
保留块数与网格单元宽/高（供导出面板自动 cols/rows 与警告判定）。

**输出图像预览（G-11 / §4.6）**：每个分支在 `scene_->updateMasks(...)` 后调 `updateExportPreview(res)`——渲染已**下沉到 `util/output_preview_renderer`**，`MainWindow` 只做编排。
**A（按需渲染 + 缓存）**：`updateExportPreview` 先缓存 `res.ok/composition` 到 `lastExportResOk_/lastComposition_`，再转 `renderExportPreviewFromCache`；后者仅当「导出」页为当前选项卡（`rightTabs_->currentWidget()==exportPanel_`）时才渲染，否则置 `exportPreviewDirty_` 直接返回，`onRightTabChanged` 切回该页且已脏时用缓存补渲染（**不重跑 Core**）。`res.ok` 时调 `util::composeOutputThumbnail(lastComposition_, exportPreviewSource(), 1/exportSrcScaleX_, 1/exportSrcScaleY_, kExportPreviewMaxDim=192)` 回灌 `exportPanel_->setPreviewPixmap`，否则传空 pixmap（回退占位文案）。渲染**不跑 Core、不落盘**：按已算好的 `Composition.placements` 把源图 `drawPixmap` blit 到小画布——合并模式按 `dest` 等比落位（所见即所得）、分离模式拼成触图（cols=ceil(√n)）。
**B（烧录可见）**：源图取自 `exportPreviewSource()`——默认即 `rebuildPreviewPixmap` 缓存的小源图 `exportSrcPixmap_`（最长边 ≤ `kExportSrcMaxDim=512`，与画布底图分离）；若 `annoBridge_.burnInEnabled() && count()>0`，则委托 `util::bakeAnnotationsInto` 把各标注按 `worldPath` 矢量烘焙到其副本（内部逐条调 `util::paintAnnotation`，与画布 `AnnotationItem` 同一渲染），**只烘焙一次**，随后逐块 blit 自然让标注随像素被切割落位，与真实导出一致。坐标经 `exportSrcScale*`（working→小图放大系数）映射，故即便原图 8000×8000 也只在小图上运算（像素少、速度快，A-0.1 仅渲染不重算）。
烧录开关/标注变更（`onBurnInChanged`/`onAnnoBridgeChanged`，仅烧录开启时）因不影响切割几何，只调 `renderExportPreviewFromCache` 用缓存重渲染、**不重跑 Core**。

`onExport()` 在 `buildEngineConfig` 后、解析路径前调 `exportPanel_->rearrangeWarning()`：若重排参数存在风险
（cols×rows < 保留块数、或单元宽/高 < 网格单元宽/高）则弹 `QMessageBox::warning` 二次确认，选 No 则中止导出。
**导出烧录（G-4）**：解析路径后、调引擎前，若 `annoBridge_.burnInEnabled() && count()>0` 则 `exportSrc = annoBridge_.burnIn(doc_.working())`
（以当前工作图为底逐个调 Core `rasterize` 合成标注）再送 `bridge_.exportResult(exportSrc, ...)`；否则直接送 `doc_.working()`。切割预览几何不受标注影响（标注随像素被切开，A-0.15/A-0.16）。

## 预处理（FR-1 / G-3）

`ImagePanel` 发意图信号（`rotateRequested`/`flipRequested`/`scaleRequested`/`resizeRequested`/`grayRequested`/
`invertRequested`/`splitRequested`/`resetRequested`）→ `MainWindow` 对应 `on*` 槽经 `EngineBridge` 调 Core `pixel_ops::*`
变换 `doc_.working()` → 公共收尾 `applyWorkingImage(next, okMsg)`：结果为空图则报错；**维度变化（旋转 90/270、缩放）
时先清除失效选区**（`clearRect`/`clearRects`/`clearCells`，因坐标基于旧尺寸）再 `doc_.setWorkingImage`（触发
`imageChanged`→重建预览底图+刷新）。`onResetPreprocess` 将工作图还原为 `original_`（原图始终保留）。图像菜单与图像页共用同一批槽。

**色道反色**：`invertRequested(invR,invG,invB)` 与 `splitRequested(keepR,keepG,keepB)` 共用面板上的 R/G/B 复选框作为
「作用通道」选择器——反色＝对勾选通道取反、分离＝仅保留勾选通道；两槽各自把三个 bool 拼成长度 3 的掩码交 Core
（`invertChannels` 本就支持掩码，故无需改 Core）。图像菜单的「反色（全通道）」经 lambda 固定传 `(true,true,true)`。

**缩放忙碌对话框（防御性编程）**：`onScale`/`onResize` 在大图上重采样可能耗时，故经 `runWithBusyDialog(text, op)`
执行——弹出**应用级模态、不可取消**的 `QProgressDialog`（不确定进度条），`processEvents` 先绘制再同步跑 Core，期间
阻断其余一切输入；配合 `busyResample_` 重入守卫，避免处理未结束时被再次触发。

## 标注（G-4 / G-5）

`MainWindow` 持有 `AnnotationBridge annoBridge_`（标注域桥，唯一驱动 Core `AnnotationLayer`），只做编排：把面板/画布意图转交模型，
再把模型变化下发画布与属性面板（不含几何/光栅化，A-0.1）。

```
AnnotationBridge.changed         → onAnnoBridgeChanged     → scene_->updateAnnotations + annoPropPanel_->syncFromModel
AnnotationBridge.selectionChanged→ onAnnoSelectionChanged → 同上（高亮选中项）
AnnotationBridge.toolChanged     → onAnnoToolChanged      → toolPanel_->syncFromModel + view_->setAnnotationDrawActive(tool!=SELECT)
ToolPanel.toolSelected          → onToolSelected         → 先 commitPath 收笔→ annoBridge_.setTool
CanvasView.annoDragStart/Move/End→ onAnnoDrag*           → 依工具分派：两点形状 begin/update/commit；折线·画笔 begin/append/commit；文字 QInputDialog 取文本→addText
CanvasView.annoEscape           → onAnnoEscape           → 有草稿则 commitPath，否则 cancelPending
CanvasScene.annotationSelectRequested → onAnnotationSelect → annoBridge_.selectAt（Core hitTest）
CanvasScene.annotationMoved     → onAnnotationMoved      → 下标校验后 annoBridge_.moveSelectedBy（Core 平移）
LayerPanel.*VisibilityChanged/burnInChanged → scene_->setBaseVisible/setMasksVisible/setGridVisible/setCutLinesVisible/setSelectionVisible/setAnnotationsVisible + annoBridge_.setLayerVisible/setBurnIn（遮罩已迁入图层面板；视图菜单/右键切换后调 layerPanel_->setMaskVisible 反向同步）
AnnotationPropPanel.colorPicked/stroke/fill/text/fontSize → annoBridge_.setColor/setStrokeWidth/...（toCoreColor 转色）
```

- **绘制态门控**：SELECT 工具时 `setAnnotationDrawActive(false)`，画布维持既有橡皮筋选区/单元交互，标注图元自行响应选中/拖动；任一绘制工具时置 true，左键拖拽路由到标注绘制。
- **base 同步**：`openImageFromPath` 载新图后 `annoBridge_.setBaseImage(doc_.working())`（新图＝新标注会话，Core setImage 清空旧标注）；`onImageChanged` 末尾 `scene_->updateAnnotations(annoBridge_)`（同维度预处理不清标注，矢量叠加）；`applyWorkingImage` 维度变化分支一并 `annoBridge_.clearAll()`。
- **撤销/重做**：标注菜单「撤销标注/重做标注」驱动 `annoBridge_.undo()/redo()`（→ Core `revoke()/redo()` 分层快照，**不再绑 Ctrl+Z/Y**，快捷键统一交编辑菜单上下文路由）；Delete 键/菜单删除选中→`removeSelected()`（→ Core `removeAnnotation`）；清除全部二次确认→`clearAll()`。全局撤销/重做（G-12）见下节。

## 全局撤销/重做（G-12）与配置文件（G-13）

**双历史 + 上下文路由（不改 Core，A-0.10）**：撤销分两条独立历史——
①**文档参数态**：`MainWindow` 持 `HistoryManager<EngineConfig> docHistory_`，快照＝`Document::buildEngineConfig`、还原＝`Document::applyEngineConfig`（与 G-13 配置同构、复用同一对互逆映射）。栈顶恒为「当前态」：`undoDocument` 先 `popToRedo()` 弹出当前态、再还原新栈顶（上一态）；`redoDocument` 反之。`undoSize()>1` 才可撤销（仅基线时无可撤销）。
②**标注**：沿用 Core `AnnotationLayer` 内建分层快照（`annoBridge_.undo/redo`），GUI 不自建。
`Document::changed()` → `scheduleHistoryCapture()` 重启 **500ms 防抖定时器**，到点 `onDocHistoryTimeout()` 压入当前快照——把拖拽/连点的连续变更合并为一条历史；还原期以 `suppressHistory_` 抑制再采集（防 undo/redo 自身入栈回环）。换图（`openImageFromPath`）与构造时 `resetDocHistory()` 以当前态为唯一基线（快照不含图像，跨图撤销无意义）。
**上下文路由**：编辑菜单/工具栏的 `Ctrl+Z/Y` → `onUndo/onRedo` → `annotationContextActive()`（标注工具非 SELECT 或存在选中标注）为真则转发 `onAnnoUndo/onAnnoRedo`，否则 `undoDocument/redoDocument`。`updateUndoRedoEnabled()` 依文档历史深度与标注上下文动态启/禁动作（在构造、换图、历史变更、标注 changed/selection/tool 变更时刷新）。**预处理不纳入全局撤销**（快照仅参数态），靠「重置预处理」回退。
**配置文件（G-13）**：文件菜单「保存配置」`onSaveConfig` → `EngineBridge::saveConfig`（委托 Core `saveEngineConfig`，§9 schema JSON）；「加载配置」`onLoadConfig` → `EngineBridge::loadConfig`（Core `loadEngineConfig`）→ 先冲刷未落定防抖变更 → `Document::applyEngineConfig` 反向映射 → 新态压入同一撤销栈（**加载本身可一步撤销**）。

## 菜单 / 工具栏 / 快捷键

- **菜单**：文件（打开/导出/**加载配置/保存配置（G-13）**/退出）、编辑（**全局撤销/重做 Ctrl+Z/Y（G-12，上下文路由）**、清除选区）、图像（预处理：左转/右转/180°、水平/垂直翻转、
  黑白、反色（全通道）、重置预处理、打开图像处理面板）、**标注（G-4/G-5：撤销标注 / 重做标注（不再绑快捷键）/ 删除选中 / 清除全部 / 标注属性定位）**、视图（缩放/适应/重置/切换遮罩）、帮助（使用说明/关于）。
- **工具栏**：打开/导出、**撤销/重做（G-12，复用编辑菜单同一 QAction）**、放大/缩小/适应、模式切换（`QActionGroup`，L2 强调、L3 禁用）。（遮罩切换已迁入左侧「图层」面板；视图菜单与画布右键仍保留快捷切换。）
- **快捷键**：`Ctrl+O`/`Ctrl+S`、`Ctrl +`/`Ctrl -`/`Ctrl+0`、`1`/`2`/`3` 切模式、`K`/`R` 切极性、`Esc` 清选区（绘制态下收笔/取消预览）、`Delete` 删除选中标注、`Ctrl+Z`/`Ctrl+Y` **全局撤销/重做（上下文路由：标注上下文→标注撤销，否则→文档参数撤销）**、方向键微调。
  单键快捷键（1/2/3/K/R/Delete/方向键）在焦点位于数值/文本输入控件时被 `focusInTextInput()` 守卫忽略，避免打字误触。
- **首次引导**：`showFirstRunGuide()` 一次性说明 L1/L2/L3 三种模式的区别（§5.2）。
- **非模态提示**：错误经 `notify()` 写状态栏并限时显示，不打断用户（§5.2）。

## main.cpp

创建 `QApplication`，设置应用/组织名（供后续 QSettings 持久化），用 `QCommandLineParser` 解析命令行；
若首位置参数为图像路径，窗口显示后调用 `MainWindow::openImageFromPath()` 自动打开（与菜单「打开」共用载入逻辑）。
