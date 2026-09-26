# app/ — 主窗口装配壳、四个控制器与程序入口

顶层装配与编排。MainWindow 只做「装配 + 编排 + 转发」，不含切割/几何/导出实现——
Core 调用集中在 `EngineBridge`，状态集中在 `Document`。为避免 `main_window.cpp` 成为上帝文件，
编排按域拆到四个控制器，纯 UI 装配拆到 `MainWindowUi`，状态栏拆为 `StatusBar` 组件。

| 文件                               | 职责                                                                                                                                                                            |
|----------------------------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `main_window.{h,cpp}`            | 装配壳：装配 + 实例化/注入控制器 + 剩余信号槽串联（Document 信号 / 画布交互写回 / 历史启用态回灌）+ 文件/编辑动作 + 单键快捷键；构造拆为「骨架装配 → `initCore()` 接线」两段，`initCore` 为桌面/移动（mobile/MobileShell）逐行共用的唯一接线序列（GuideLine 阶段 3） |
| `main_window_ui.{h,cpp}`         | `MainWindowUi` 静态建造者（MainWindow 的 friend）：buildCentral/buildMenus/buildToolbar/buildStatus 纯 UI 装配，回填部件指针                                                                     |
| `status_bar.{h,cpp}`             | `StatusBar`：六标签（坐标/RGB/模式/保留块/缩放/提示）+ 非模态 `notify`；`setCompactMode` 供移动端只留模式/块数/缩放（SPEC §8.3）                                                                                 |
| `doc_history.{h,cpp}`            | `DocHistory`：文档参数态撤销/重做（防抖采集/抑制守卫/载入配置冲刷压栈/上下文判定/启用态信号）                                                                                                                       |
| `preview_controller.{h,cpp}`     | `PreviewController`：refreshPreview 五分支状态机 + 导出输出预览缓存与按需渲染（A/B 规则）                                                                                                             |
| `preprocess_controller.{h,cpp}`  | `PreprocessController`：预处理 8 槽 + 维度失效清理 + 忙碌对话框；`setBusyOverlayMode` 供移动端改全屏遮罩（SPEC §8.2，阶段 4）                                                                                |
| `annotation_coordinator.{h,cpp}` | `AnnotationCoordinator`：标注 33 槽 + 手势分派状态机 + 图层显隐/烧录联动                                                                                                                         |
| `main.cpp`                       | 程序入口（双产物各编一份：`idc_gui` 纯桌面 / `idc_gui_mobile` 经宏恒选移动壳；Android 单目标），含 `QApplication` + 事件循环                                                                                    |

## 布局

`QSplitter` 水平三分：**左侧可滚动容器（QScrollArea 竖排 LeftPanel + ToolPanel + LayerPanel）** | **中央画布（CanvasView）** | **右侧 QScrollArea 包裹的 AccordionPanel（参数页 / 导出页 / 图像页 / 标注页，多节可同开）**。装配细节在 `MainWindowUi::buildCentral`（创建顺序、`setDocument`/`setModel` 注入、初始分割 {340,860,300}）。

## 编排（连接住在属主旁）

构造顺序（承重，不可重排）：骨架装配（桌面 `MainWindowUi` 四步；移动 `MobileShellUi` 同模式）→ `MainWindow::initCore()`：
依次实例化并注入四个控制器（各自 `connectSignals()`）→
`DocHistory::start()` → `MainWindow::connectAll()` → `syncPanels` → `refreshPreview` → `history_->reset()`（发
`availabilityChanged` → 撤销/重做动作置灰，故 **connectAll 必须先于 reset**）→ 欢迎提示。
**本序列桌面/移动逐行共用**（SPEC §8.3 行为同构的结构性保证；差异只在骨架装配）。

**MainWindow::connectAll（剩余跨件串联）**：

```
Document.imageChanged → onImageChanged → preview_->rebuildPreviewPixmap + syncPanels + preview_->refreshPreview
Document.changed      → onDocChanged   → preview_->refreshPreview + syncPanels（拖拽期跳过）
CanvasView.rubberSelect / CanvasScene.selectionEdited → onRubberSelect：L1/L2 写回 Document.setRect（MULTI_RECT 则 addRect）；
    L3 时把框选矩形经 scene_->cellsIntersecting 换成命中单元并 addCells（含从图像外起拖、未被 CellPickerItem grab 而落到视图橡皮筋的情形）
CanvasScene.multiRectEdited(index, rect) → onMultiRectEdited：L2 MULTI_RECT 下拖动/缩放第 index 个选区框 → Document.updateRect(index)（与单选区同为可拖拽 SelectionRectItem）；
    拖拽期 syncPanels 被跳过，故另调 param_->selectRectRow(index)（列表跟随选中+高亮）与 param_->updateRectListItem(index)（实时回显新尺寸）
ParamPanel.rectSelected(index) → onRectSelected → scene_->setActiveMultiRect(index)：高亮画布上对应选区框（颜色略微加强，-1 清除）
CanvasView.nudgeSelection → onNudge（方向键平移选区，钳制到图像内）
CanvasView.cursorScenePos → onCursor（状态栏坐标 + 像素 RGB，经 StatusBar setter）
CanvasView.zoomChanged → onZoomChanged（状态栏缩放倍数；所有缩放入口汇聚于 updateHandleSize 发出）
CanvasView.clearCutRequested / toggleMasksRequested / resetViewRequested → 对应动作
ExportPanel.exportRequested → onExport
DocHistory.availabilityChanged → onHistoryAvailabilityChanged → aUndo_/aRedo_ setEnabled
AnnotationBridge.changed/selectionChanged/toolChanged → history_->updateEnabled（标注上下文变化 → 重算启用态；
    替代原标注槽内三处 updateUndoRedoEnabled() 调用，同源触发）
```

**各控制器自持的连接**：

- `DocHistory::start()`：`Document.changed → scheduleCapture`（500ms 防抖采集）。
- `PreviewController::connectSignals()`：`rightTabs currentChanged → onRightTabChanged`（导出页切回补渲染）。
- `PreprocessController::connectSignals()`：`ImagePanel` 8 个意图信号 → 8 槽。
- `AnnotationCoordinator::connectSignals()`：标注域全部（`AnnotationBridge` 4 信号、`CanvasView` 6 手势、`CanvasScene` 4 标注、
  `ToolPanel`/`LayerPanel`（7，含单元编号 lambda）/`ExportPanel.burnInChanged`/`AnnotationPropPanel` 6），并做
  `exportPanel_->setBurnInChecked(anno->burnInEnabled())` 初始同步。
- `MainWindowUi`：全部菜单/工具栏动作触发（图像菜单 lambda 直调 `preprocess_->on*`，标注菜单 lambda 直调 `annotation_->onAnno*`）。

`PreviewController::refreshPreview()` 是核心：无图→清空；无选区→提示拖拽（不跑引擎，避免 E-1 噪声）；
有选区→`buildEngineConfig` + `EngineBridge.runPreview` → 刷新切割线（`EngineBridge::cutLines`）/遮罩/选区，
并回写状态栏（`StatusBar::setCount/setHint`）与面板提示（保留块数、输出画布尺寸、坍缩可行性）。
L2 MULTI_RECT 分支：`scene_->updateMultiRects(doc_.rects())` 增量刷新可拖拽选区框（不 clear+重建）。
拖拽期间（`isDraggingSelection()` 或 `isDraggingMultiRect()`）跳过 `syncPanels()` 与对正在拖图元的回设，避免逐帧量化抖动与卡顿；释放时照常同步一次。
重排上下文回灌：顶部先 `exportPanel_->setRearrangeContext(0,0,0)` 清零（非 L3），L3 分支再回灌真实的
保留块数与网格单元宽/高（供导出面板自动 cols/rows 与警告判定）。

**输出图像预览**：每个分支在 `scene_->updateMasks(...)` 后调 `updateExportPreview(res)`——渲染已**下沉到 `util/output_preview_renderer`**，`PreviewController` 只做编排：缓存 `lastComposition_/lastExportResOk_`，导出页为当前选项卡时才渲染、否则置 `exportPreviewDirty_` 待切回补渲染（**不重跑 Core**）；烧录可见时经 `util::bakeAnnotationsInto` 把标注烘焙到小源图 `exportSrcPixmap_`（最长边 ≤512，只烘焙一次），故原图 8000×8000 也只在小图上运算。机制详见 Gui README「关键设计决策」#12。

`onExport()` 在 `buildEngineConfig` 后、解析路径前调 `exportPanel_->rearrangeWarning()`：若重排参数存在风险
（cols×rows < 保留块数、或单元宽/高 < 网格单元宽/高）则弹 `QMessageBox::warning` 二次确认，选 No 则中止导出。
**导出烧录**：解析路径后、调引擎前，若 `annoBridge_.burnInEnabled() && count()>0` 则 `exportSrc = annoBridge_.burnIn(doc_.working())`
（以当前工作图为底逐个调 Core `rasterize` 合成标注）再送 `EngineBridge::exportResult(exportSrc, ...)`；否则直接送 `doc_.working()`。切割预览几何不受标注影响（标注随像素被切开）。

## 预处理（FR-1）——PreprocessController

`ImagePanel` 发意图信号 → `PreprocessController` 对应槽经 `EngineBridge` 调 Core `pixel_ops::*` 变换 `doc_.working()` → 公共收尾
`applyWorkingImage(next, okMsg)`：结果为空图则报错；**维度变化（旋转 90/270、缩放）时先清除失效选区与标注**再 `setWorkingImage`；
`onResetPreprocess` 还原为 `original_`（原图始终保留）。图像菜单（`MainWindowUi` 的 lambda）与图像页共用同一批槽。
色道反色 / 分离共用 R/G/B 复选框作「作用通道」选择器（`invertChannels` 支持掩码，无需改 Core）。
大图重采样经 `runWithBusyDialog` 弹**应用级模态、不可取消**的 `QProgressDialog`（父窗口为注入的 MainWindow；`busyResample_` 重入守卫）。
流程详见 Gui README「预处理与标注数据流」。

## 标注——AnnotationCoordinator

`AnnotationCoordinator` 持有 `AnnotationBridge*`（标注域桥，唯一驱动 Core `AnnotationLayer`），只做编排：把面板/画布意图转交模型，
再把模型变化下发画布与属性面板（不含几何/光栅化）。

```
AnnotationBridge.changed         → onAnnoBridgeChanged     → scene_->updateAnnotations + annoPropPanel_->syncFromModel
                                                                 + 烧录开启时 preview_->refreshExportPreviewFromCache（B 规则，不重跑 Core）
AnnotationBridge.selectionChanged→ onAnnoSelectionChanged → 同上（高亮选中项）
AnnotationBridge.toolChanged     → onAnnoToolChanged      → toolPanel_->syncFromModel + view_->setAnnotationDrawActive(tool!=SELECT)
ToolPanel.toolSelected          → onToolSelected         → 先 commitPath 收笔→ anno_->setTool
CanvasView.annoDragStart/Move/End→ onAnnoDrag*           → 依工具分派：两点形状 begin/update/commit；折线·画笔 begin/append/commit；文字 QInputDialog 取文本→addText
CanvasView.annoEscape           → onAnnoEscape           → 有草稿则 commitPath，否则 cancelPending
CanvasScene.annotationSelectRequested → onAnnotationSelect → anno_->selectAt（Core hitTest）
CanvasScene.annotationMoved     → onAnnotationMoved      → 下标校验后 anno_->moveSelectedBy（Core 平移）
LayerPanel.*VisibilityChanged   → scene_->setBaseVisible/setMasksVisible/setGridVisible/setCutLinesVisible/setSelectionVisible/setAnnotationsVisible
                                                                 + anno_->setLayerVisible（网格/切割线/选取边框三开关再 preview_->refreshPreview 门控重建）
ExportPanel.burnInChanged       → onBurnInChanged        → anno_->setBurnIn + preview_->refreshExportPreviewFromCache
AnnotationPropPanel.colorPicked/stroke/fill/text/fontSize → anno_->setColor/setStrokeWidth/...（toCoreColor 转色）
```

- **绘制态门控**：SELECT 工具时 `setAnnotationDrawActive(false)`，画布维持既有橡皮筋选区/单元交互；任一绘制工具时置 true，左键拖拽路由到标注绘制。
- **base 同步**：`openImageFromPath` 载新图后 `annoBridge_.setBaseImage(doc_.working())`（新图＝新标注会话）；`onImageChanged` 末尾 `scene_->updateAnnotations(annoBridge_)`（同维度预处理不清标注）；`PreprocessController::applyWorkingImage` 维度变化分支一并 `anno_->clearAll()`。
- **撤销/重做**：标注菜单驱动 `anno_->undo()/redo()`（→ Core 分层快照，**不再绑 Ctrl+Z/Y**）；Delete 键（经 `MainWindow::keyPressEvent` 转发）/菜单删除选中→`removeSelected()`；清除全部二次确认（父窗口为注入的 MainWindow）→`clearAll()`。
- **对话框父窗口**：文字输入 `QInputDialog` 与清除确认 `QMessageBox` 均以注入的 `dialogParent_`（MainWindow）为父，保证模态落位。

## 全局撤销/重做与配置文件——DocHistory

**双历史 + 上下文路由（不改 Core）**：①文档参数态用 `HistoryManager<EngineConfig> history_`
（快照＝`buildEngineConfig`、还原＝`applyEngineConfig`，与配置加载同构；栈顶恒为当前态）；②标注沿用 Core `AnnotationLayer`
内建分层快照。`Document::changed()` 重启 **500ms 防抖定时器**（成员 `QTimer`）把拖拽/连点合并为一条历史，还原期 `suppress_` 防回环；
换图 / 构造时 `reset()` 以当前态为唯一基线。`Ctrl+Z/Y` 经 `MainWindow::onUndo/onRedo` 依 `annotationContextActive()` **上下文路由**
（标注上下文→`AnnotationCoordinator::onAnnoUndo/Redo`，否则→`DocHistory::undo/redo`）；启用态经 `availabilityChanged`
回灌 `MainWindow::onHistoryAvailabilityChanged` 动态启停 `aUndo_/aRedo_`。**预处理不纳入全局撤销**（快照仅参数态），
靠「重置预处理」回退。配置经 `EngineBridge::saveConfig/loadConfig` 委托 Core，加载经 `DocHistory::recordConfigLoad`
（冲刷防抖 → 抑制采集地 `applyEngineConfig` → 压栈）且**入同一撤销栈**（可一步撤销）。
机制详见 Gui README「关键设计决策」#10/#11。

## 菜单 / 工具栏 / 快捷键（MainWindowUi 装配）

- **菜单**：文件（打开/导出/**加载配置/保存配置**/退出）、编辑（**全局撤销/重做 Ctrl+Z/Y**、清除选区）、图像（预处理：左转/右转/180°、水平/垂直翻转、
  黑白、反色（全通道）、重置预处理、打开图像处理面板）、**标注（撤销标注 / 重做标注（不再绑快捷键）/ 删除选中 / 清除全部 / 标注属性定位）**、视图（缩放/适应/重置/切换遮罩）、帮助（使用说明/关于）。
- **工具栏**：打开/导出、**撤销/重做**、放大/缩小/适应、模式切换（`QActionGroup`，L2 强调、L3 禁用）。（遮罩切换已迁入左侧「图层」面板；视图菜单与画布右键仍保留快捷切换。）
- **快捷键**（`MainWindow::keyPressEvent`）：`Ctrl+O`/`Ctrl+S`、`Ctrl +`/`Ctrl -`/`Ctrl+0`、`1`/`2`/`3` 切模式、`K`/`R` 切极性、`Esc` 清选区（绘制态下收笔/取消预览）、`Delete` 删除选中标注、`Ctrl+Z`/`Ctrl+Y` **全局撤销/重做（上下文路由：标注上下文→标注撤销，否则→文档参数撤销）**、方向键微调。
  单键快捷键（1/2/3/K/R/Delete/方向键）在焦点位于数值/文本输入控件时被 `focusInTextInput()` 守卫忽略，避免打字误触。
- **非模态提示**：错误经 `notify()`（薄封装 → `StatusBar::notify`）写状态栏并限时显示，不打断用户。

## main.cpp

创建 `QApplication`，设置应用/组织名（供后续 QSettings 持久化），用 `QCommandLineParser` 解析命令行；
若首位置参数为图像路径，窗口显示后调用 `MainWindow::openImageFromPath()` 自动打开（与菜单「打开」共用载入逻辑）。
