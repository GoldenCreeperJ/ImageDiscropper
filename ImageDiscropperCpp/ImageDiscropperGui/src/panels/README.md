# panels/ — 模式/极性、参数、导出、图像与标注面板

八个 QWidget 面板，均**不直接调 Core 几何/光栅化**。模式/参数/导出面板只读写 `Document`（用户操作 → 写回 Document →
Document 发 `changed()` → app 层 `PreviewController` 刷新预览；反向同步用 `blockSignals` 防回环）。
（例外：图像处理面板是**声明式预处理队列编辑器**，只发队列意图信号，由 `PreprocessController` 经 EngineBridge 调 Core 从原图重放物化工作图；标注工具/图层/属性面板发意图信号，由 `AnnotationCoordinator` 写回 `AnnotationBridge`。）

> **已迁出**：`pattern_form`/`placement_row`（新建画布对话框的采参组件，非面板）已迁至 `app/forms/`，与 `NewCanvasDialog` 同域。

| 文件                              | 职责                                                                                                                                                                                                                                                                                                                             |
|---------------------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `accordion_panel.{h,cpp}`       | **桌面右侧多页容器**：AccordionPanel（多节可同时展开，整栏外套 QScrollArea 滚动触达）；标题按钮 checkable + 前缀 ▾/▸                                                                                                                                                                                                                                             |
| `left_panel.{h,cpp}`            | 左侧：模式切换 L1/L2/L3、极性开关（保留绿 / 删除红）（工具/图层已迁出）                                                                                                                                                                                                                                                                                     |
| `param_panel.{h,cpp}`           | 右侧「参数」页：QStackedWidget 分 L1/L2/L3 三页；堆叠容器高度只随当前页（PageSizedStack 关 heightForWidth + reflowStack 钳 min=max + 根布局尾部 stretch 保顶对齐（移动端 tab 页），规避 SizeAll 下隐藏页撑高/居中）；三页构建器拆独立 TU `param_panel_pages.cpp`（共享件 `param_panel_internal.h`）                                                                                               |
| `param_panel_pages.cpp`         | `ParamPanel::buildL1Page/buildL2Page/buildL3Page` 三页构建器实现（TU 拆分，自 param_panel.cpp 原封迁出）                                                                                                                                                                                                                                        |
| `image_panel_sections.cpp`      | `ImagePanel` 的三个分区构建（buildResizeSection/buildGeoSection/buildColorSection，TU 拆分）：画布尺寸独立区/几何操作区/色彩操作区（自构造函数原封提方法迁出，行为不变）                                                                                                                                                                                                        |
| `export_panel_merge.cpp`        | `ExportPanel` 的「合并重排」域实现（TU 拆分）：重排参数输入区（列/行/单元尺寸/填充色）与重排编排区（上下文回灌/自动行列/警告判定/排序与蛇形逆序）                                                                                                                                                                                                                                           |
| `export_panel.{h,cpp}`          | 右侧「导出」页：输出模式、目录/文件、格式、质量、命名、**导出时烧录标注开关**、**输出图像预览缩略图**、导出按钮                                                                                                                                                                                                                                                                   |
| `image_panel.{h,cpp}`           | 右侧「图像」页：**声明式预处理队列编辑器**（几何/色彩两独立列表 + 顶部独立 Resize 区 + 重置），发 stepAppend/Replace/Remove/Move/reset 队列信号                                                                                                                                                                                                                           |
| `tool_panel.{h,cpp}`            | 左侧「标注工具」组：选择/各形状/多线段/文字/画笔互斥按钮                                                                                                                                                                                                                                                                                                 |
| `layer_panel.{h,cpp}`           | 左侧「图层」组：底图/标注/遮罩/网格线/切割线/选取边框/单元编号显示开关（按 z 序；标注烧录开关已迁至导出面板）                                                                                                                                                                                                                                                                    |
| `layer_edit_panel.{h,cpp}`      | 左侧「图层编辑」组（**多层画布**，SPEC §8.2）：列图片图层（按 `zOrder` 升序）+ 点选切换预处理当前目标 + 上/下移/删除 + 添加图片图层（复用 `NewCanvasDialog`：未勾附加图→程序图案产 `recipe` 层；勾选附加图→对话框**两段合成**（图片按适应模式先入图层框、再叠到图案/底色 base，落位恒等）整幅结果入 `file` 层；不撑大）+ 编辑落位 `transform`（画布锚点 + 图片枢轴 9 向 + 自由 x/y + 目标宽高）+ 改画布尺寸（仅清框选）；仅 `hasLayers()` 下启用；发意图信号交 `MainWindow` 落 `Document` CRUD |
| `annotation_prop_panel.{h,cpp}` | 右侧「标注」属性页：颜色/粗细/填充/文字/字号+ 叠放区（上移/下移一层、置顶/置底，仅选中标注可用，层序控件已从标注菜单与 Ctrl+Shift 快捷键迁入）+ 变换区（缩放%/旋转° 显示选中形状的**绝对累积变换**、含负=翻转，**改动即生效无应用按钮**，作用于当前选中标注；`setTransformPreview` 与画布 OBB 手柄拖拽实时联动）                                                                                                                                        |

## left_panel

- 模式组 `QButtonGroup`（id=1/2/3），L2 用橙色强调样式（差异化内核）；L1/L2/L3 均可选。
- 极性组（id=0 保留=绿 / 1 删除=红）。
- `onModeToggled`→`doc_->setMode`；`onPolarityToggled`→`doc_->setPolarity`。
- `syncFromDocument()` 在 `blockSignals` 下回填选中态（供 MainWindow 统一同步）。

## param_panel

- **L1 页**：形状 combo（矩形/横带/竖带）+ x1/y1/x2/y2 数值输入（`QSpinBox`，`keyboardTracking(false)`）。
- **L2 页**：子功能 combo（十字/横线/竖线/**多矩形并集**）+ 坐标 + 坍缩可行性提示标签 `collapseHint_`
  + 「转为网格模式编辑…」按钮 `l2ToGridBtn_`（有可转换选区几何时可用：单选区 `rect_` 有效，或多矩形列表非空；`onConvertToGrid`→`doc_->convertRectToGrid`）。
  选「多矩形并集」时显示矩形列表 `l2RectList_`（容器 `l2MultiBox_`）：在画布**空白处拖拽框选逐个追加**，矩形本身**可直接拖动/四角缩放/拖边调整**（每个矩形一个 `SelectionRectItem`，见 canvas/），选中项坐标回填 x1/y1/x2/y2 微调，
  另有「删除选中 / 清空」按钮（`onRectDel/ClearClicked`）；重建列表时 `syncingRectList_` 抑制选中信号回环。（「添加当前坐标」按钮已移除：画布拖拽追加已是主入口，该按钮冗余。）
  选中行变化经信号 `rectSelected(index)` 通知 MainWindow 高亮画布上对应选区框（-1 清除）；`selectRectRow(index)`/`updateRectListItem(index)` 供画布拖拽时定向跟随选中与实时回显尺寸（仅改单行文本/选中行，不重建整表）。
- **L3 页**：网格定义（基准点 x0/y0 + 单元宽/高 cw/ch + 余量策略 combo）+ 选择集（全选/反选/清空按钮）
  + 排序（策略 combo row-major/column-major/custom + 整体逆序/蛇形复选框）；派生行列数与已选单元数为**只读回显**。
  行列数由 Core 依图像边界自动推导（面板不输入）；枚举↔combo 索引与 Core `RemainderPolicy`/`SortStrategy` 一致（直接 `static_cast`）。
  画布单元点选/自定义拖拽调序由 `cell_picker_item` 承担（单击切换/拖拽框选/CUSTOM 拖拽调序），本页提供按钮式选择集与排序策略。
- `setMode()` 切换页；`syncFromDocument()` 回填形状/子功能/坐标与 L3 网格参数/排序/只读回显，并依图像尺寸收紧 spin 上限；
- `setCollapseHint(bool, reason)` 显示 Core `isCollapsible` 结果（不可行时禁用坍缩）；
- `onCoordEdited`→`applyCoordsToDocument`：取当前页 spin 组装**规范化** `RectRegion`；L2 多矩形下写回 `updateRect(选中行)`，其余 → `doc_->setRect`。

## export_panel

- 输出模式 combo：分离导出 / 合并坍缩 / 合并重排（映射到 `EmitMode` + `MergeLayout`）。
  **重排项仅 L3 启用**：`refreshModeItemStates()` 依 `doc_->mode()==L3` 与 `collapsible_` 置 item 的 enabled（非 L3 时重排项置灰）。
- 目录行 / 文件行均为**可手动键入**的 `QLineEdit`（`editingFinished` 写回 Document），并各带「浏览…」按钮
  （`getExistingDirectory` / `getSaveFileName` + 依格式的过滤器）作为等价选择方式；目录/文件/命名模板行随输出模式显隐。
  **移动端「浏览…」禁用**：导出统一走相册公共目录 `Pictures/ImageDiscropper`，目录选择的 SAF 适配已移除
  （见 export_panel.cpp 注释与 src/mobile README「移动端实现状态与遗留事项」）。
- 格式 combo（PNG/JPEG/WebP/BMP）；质量 spin 仅在有损格式（JPEG/WebP）可见。
- `setCollapsible(bool)`：存 `collapsible_` + `refreshModeItemStates()`；不可行且当前选中坍缩时按 L3-aware 回退（L3→重排，否则→分离）。
- **合并重排**（输出模式=重排时显示 `rearrangeRow_`）：列/行/单元宽/单元高 spin（0=自动，`setSpecialValueText("自动")`）
  + 「按格数自动」按钮 `autoGridBtn_` + 填充色按钮 `padColorBtn_`（`QColorDialog` 含 alpha，背景色块回显）；写回 `setMergeGrid`/`setMergeCellSize`/`setPadColor`。
- **重排填充顺序**（与选择排序正交，写回 `Document` 的 `MergeOrder`）：`mergeSortCombo_`（行优先/列优先）+ `mergeSnake_`/`mergeReverse_` 复选框
  → `onMergeSortChanged`/`onMergeSnakeToggled`/`onMergeReverseToggled` → `setMergeOrderStrategy`/`setMergeOrderSnake`/`setMergeOrderReverse`。
- **自动 cols/rows（开方）**：`setRearrangeContext(keptCount,cellW,cellH)` 由 `PreviewController` 回灌保留块数与网格单元尺寸；
  首次切到重排且用户未手改过（`mergeGridTouched_==false`）时 `applyAutoGrid()` 自动填入 cols=ceil(√n)、rows=ceil(n/cols)（具体数字，用户可再改）。
- **双警告**：`rearrangeWarning()` 当 cols×rows < 保留块数、或单元宽/高 < 网格单元宽/高时返回警告文本；
  `updateRearrangeWarning()` 将其以**内联红字** `rearrangeWarn_` 呈现；导出前的**弹窗确认**由 MainWindow 调 `rearrangeWarning()` 完成。
- `setPreviewInfo(text)`：以文字回显输出画布尺寸/保留块数/可行性。
- **输出图像预览缩略图**：`setPreviewPixmap(pm)` 把 `PreviewController` 按 Core `Composition` 渲染的低分辨率缩略图
  显示在固定尺寸（200×160）的 `previewLabel_` 上（等比缩到框内、仅缩不放）；传空 pixmap 时回退到占位文案「（无输出预览）」。
  与 `setPreviewInfo` 的文字信息并存；渲染逻辑（小源图 blit）在 `PreviewController`，面板只负责展示。
- **导出时烧录标注**（`burnIn_`，默认 false）：勾选后导出把标注合成进像素（随像素一起被切割）；`toggled`→`burnInChanged(bool)` 交 `AnnotationCoordinator` 写回 `AnnotationBridge::setBurnIn`；`setBurnInChecked(bool)` 供反向同步（`blockSignals` 防回环）。此开关原属图层面板，因属导出行为而迁入导出面板。
- 导出按钮 `emit exportRequested()`，实际导出由 MainWindow 经 EngineBridge 完成。

## image_panel

预处理（FR-1）**声明式队列编辑器**。面板**不碰 Core、不做像素运算、不持真相**，只把控件值翻译为队列意图信号；
实际重放物化由 `PreprocessController` 经 `EngineBridge` 调 Core（从原图重放整条队列）完成，结果写回 `Document` 工作图（原图始终保留）。

轻操作在界面上**拆为几何、色彩两个独立列表**（表面两列表、底层一条队列），另加顶部独立的画布尺寸（Resize）常驻区：

- **画布尺寸（Resize）独立区**（`resizeW_`・`resizeH_` + `resizeKeepRatio_`（改宽联动算高）+ `applySizeBtn_`；`resizeScalePct_`（1..1000%）+ `scaleSizeBtn_`；`clearSizeBtn_`；`currentSizeLabel_` 只读回显当前输出尺寸）：**常驻、有图即可用、不入队列**，发 `resizeAppliedRequested(w,h)` / `resizeScaleRequested(factor)` / `resizeClearedRequested()`；resize 恒最先从原图重采样。
- **几何操作区**：`geoTypeCombo_`（旋转 / 翻转）+ `rotAngle_`（-90/90/180）/ `flipDir_`（水平/垂直）；`geoAddBtn_`→`buildGeoConfig()` 发 `stepAppendRequested(cfg)`、`geoReplaceBtn_`→`stepReplaceRequested(index,cfg)`；`geoList_` 展示几何步（`describeStep()` 生成中文行），`geoDelBtn_`/`geoUpBtn_`/`geoDownBtn_` 删除/上移/下移。
- **色彩操作区**：`colorTypeCombo_`（黑白 / 色道反色 / 色道分离）+ `chR_`・`chG_`・`chB_`（反色＝反相它 / 分离＝保留它）；`colorAddBtn_`→`buildColorConfig()` 发 `stepAppendRequested(cfg)`、`colorReplaceBtn_`→`stepReplaceRequested(index,cfg)`；`colorList_` 展示色彩步，`colorDelBtn_`/`colorUpBtn_`/`colorDownBtn_` 删除/上移/下移。
- **两列表→扁平下标映射**：`geoList_` 行号即底层队列扁平下标；`colorList_` 行号经 `colorFlatIndex()`（= `geoRowCount()` + 色彩行号）映到几何块之后的下标。底层 `steps` 恒为「全部几何步在前 + 全部色彩步在后」，`PreprocessController` 据此维持不变式（几何步插到几何块尾、色彩步追加队尾，跨类上下移忽略）。
- **重置**：`resetBtn_`→`resetRequested()`：清空 resize 与整条队列，仅当 `doc_->hasPreprocess()` 时可用。

发信号：`stepAppendRequested(PreprocessConfig)` / `stepReplaceRequested(int,cfg)` / `stepRemoveRequested(int)` / `stepMoveRequested(int,bool down)` / `resetRequested()`（index 均为底层**扁平下标**，均不持真相）。

**按目标驱动**（多层画布，SPEC §8.2）：面板顶部 `targetLabel_` 显式标注当前预处理目标——选中图片图层时＝「图片图层 #id」，否则＝「画布」；队列编辑意图经 `PreprocessController::commitActive`（及 `activeSteps`/`activeResize` 读取）落到 `Document::activeSession()`（选中层会话，否则画布会话），不再固定绑定单一预处理队列。

- `setDocument(Document*)` 绑定会话状态源；`syncFromDocument()`：无图时整板禁用；从 Document 反向同步时按类型把 `steps` 重分为几何段 / 色彩段重建两列表文本（上/下移后靠 `pendingSelectRow_` 使高亮跟随被移动项）、把 `resizeW_`・`resizeH_` 回灌为当前输出尺寸（`blockSignals` 防联动回调）、
  **工作图为灰度（黑白后）时禁用「黑白」与「色道分离」**（灰度无 R/G/B 可分、黑白幂等），反色保持可用；按 `hasPreprocess()`/选中态启停按钮。
- 「颜色选取（取色器）」与标注属性（描边/填充色）强相关，已由 `annotation_prop_panel` 统一提供（见下）。

## tool_panel

左侧「标注工具」互斥按钮组。面板只采集意图，真正切工具由 `AnnotationCoordinator` 写回 `AnnotationBridge`。

- 一个 `QButtonGroup`（互斥）承载全部工具按钮，`id = static_cast<int>(AnnoTool)`，避免额外映射表；`QGridLayout` 3 列排布，工具提示齐备。
- 按钮样式未选态为硬编码浅底，已显式指定深色文字（`color:#1b1b1b`），避免系统深色主题下白字浅底不可读。
- 3 列网格含「等腰直角三角形」等宽标签，需较宽左栏（左栏最小宽由主窗 `leftScroll->setMinimumWidth` 控制）。
- `onToolToggled(id, checked)` 仅 checked 时 `emit toolSelected(static_cast<AnnoTool>(id))`；默认勾选 SELECT。
- `setModel()`/`syncFromModel()`：从模型反向同步当前工具选中态（`blockSignals` 防回环）。

## layer_panel

左侧「图层」组：集中管理画布各图层显示/隐藏（底图 / 标注 / 遮罩 / 网格线 / 切割线 / 选取边框 / 单元编号，按 z 序）。遵循「隐藏图层=不可交互」原则。

- 七个 `QCheckBox`（按 z 序）：`baseVisible_`/`annoVisible_`/`maskVisible_`/`gridVisible_`/`cutLineVisible_`/`selectionVisible_`/`numberVisible_`（均默认 true）；toggled 直接转发 `baseVisibilityChanged`/`annotationVisibilityChanged`/`maskVisibilityChanged`/`gridVisibilityChanged`/`cutLineVisibilityChanged`/`selectionVisibilityChanged`/`numberVisibilityChanged`。（标注「导出时烧录」开关已迁至导出面板，见 export_panel。）
- **遮罩开关已从工具栏/视图菜单迁入本面板**（成为正式图层项）；视图菜单与画布右键仍可切换，切换后由 MainWindow 调 `setMaskVisible(bool)` 反向同步复选框（`blockSignals` 防回环）。
- 网格线/切割线/选取边框的落地：`AnnotationCoordinator` 把信号分派到 `CanvasScene::setGridVisible`（仅置标志）/`setCutLinesVisible`/`setSelectionVisible`，三者均随后调 `PreviewController::refreshPreview` 重建落地（L3 网格依 `gridVisible_`、L2 诱导切割线依 `cutLinesVisible_`、L3 单元选择高亮依 `selectionVisible_`）；选取边框隐藏同时禁用选区拖拽交互（`SelectionRectItem::setBorderVisible` 切 `setAcceptedMouseButtons`）。
- `syncFromModel()`：标注显示开关 = `layerVisible()`（`blockSignals` 防回环）。
- **各图层的「隐藏」仅切换画布预览可见性与交互性，不影响导出**（见 Gui README「图层与烧录」）。

## layer_edit_panel

左侧「图层编辑」组（**多层画布**，SPEC §8.2）。管理 `Document` 内的图片图层数组（`std::vector<canvas::ImageLayer>`），与上面 `layer_panel`（画布叠加图层显示开关）职责不同。面板**只发意图信号、不持真相**，由 `MainWindow` 落到 `Document` 声明态 CRUD。

- **仅在 `Document::hasLayers()`（载入含 `layers` 段的图层画布配置）下启用编辑**；单图态下控件禁用并以 `hintLabel_` 显式提示。
- **图片图层列表**（`layerList_`）：`syncFromDocument()` 按 `zOrder` 升序重建，行文本含来源（配方 / 文件 `<name>` / 空占位），`item->setData(Qt::UserRole, id)` 携带层 id；`currentListLayerId()` 取当前行。点选行发 `layerSelectRequested(id)` → `doc_.selectLayer` → 切换预处理当前目标。
- **上/下移一层、删除、添加图片图层**：发 `layerMoveRequested(id, up)`/`layerRemoveRequested(id)`/`addImageRequested()`；`MainWindow` 分别落 `moveLayerZOrder`/`removeLayer`/`onAddImageLayer`（据对话框「附加图片开关」(`producesLayerCanvas()`) 分派：勾选→两段合成整幅结果作 `ImageLayer{File}` 内容、**落位恒等**；未勾→取 `resultParams()` 产 `recipe` 层；均不撑大画布）。
- **落位 `transform` 编辑区**（`anchorCombo_`/`pivotCombo_` 各 9 向、`posX/Y`/`width/heightSpin_`、`applyTransformBtn_`）：「画布锚点（`inCanvasAnchor`）+ 图片枢轴（`layerPivot`）+ 自由 x/y + 目标宽高（≤0 用原生）」，组装 `canvas::ImageTransform` 发 `layerTransformRequested(id, transform)`；枚举↔combo index 直接 `static_cast`（与 `generate::ImageAnchor` 序一致）。
- **画布尺寸区**（`canvasW/HSpin_` + `applyCanvasBtn_`）：发 `canvasSizeRequested(w, h)` → `doc_.setCanvasSize`（**仅清框选**，图片图层与标注保留）。
- `syncing_` 标志在回填控件时抑制 `currentRowChanged`/`valueChanged` 等信号回环；`setControlsEnabled` 按 `hasLayerCanvas`（列表/添加/尺寸）与 `hasSelection`（移/删/transform）分组启停。

## annotation_prop_panel

右侧「标注」属性页。面板只采集属性意图并发信号，`AnnotationCoordinator` 写回 `AnnotationBridge`（EDIT 作用选中项、DRAW 作下一次绘制默认）。

- `QFormLayout` 逐行排布：**边框色**色块按钮（`QColorDialog` 取色，**开启 `ShowAlphaChannel`**；标注颜色含 alpha，Core `Color` 有 a 分量、rasterizer 已做 src-over 混合；色块以 rgba 背景 + RGBA 文案回显）/ **填充色**独立色块按钮 / 粗细 spin（1..200）/ 填充开关 / 转角圆角半径 spin（0＝直角）/ 文字内容 / 字号 spin（1..2000）。
- 信号：`colorPicked(QColor)`/`fillColorPicked(QColor)`/`strokeChanged(int)`/`fillChanged(bool)`/`cornerRadiusChanged(double)`/`textChanged(QString)`/`fontSizeChanged(double)`。
- **叠放区**（`stackGroup_`，标题「叠放次序（选中标注）」，4 按钮横排）：上移一层 / 下移一层 / 置于顶层 / 置于底层，发 `stackRaiseRequested`/`stackLowerRequested`/`stackToFrontRequested`/`stackBackRequested` → `AnnotationCoordinator`（`onAnnoRaise/Lower/BringToFront/SendToBack`）→ `AnnotationBridge::raiseSelected` 等 → Core `moveAnnotation`（就地重排并规范化 `zOrder`＝下标）；层序控件**原属标注菜单，已迁入本面板**（菜单项与 Ctrl+Shift 快捷键一并移除）；`syncFromModel` 按 `selectedIndex().has_value()` 置 `stackGroup_` 的 enabled。
- **删除选中 / 清除全部标注**（页尾两按钮）：`deleteSelBtn_`「删除选中标注」发 `deleteSelectedRequested` → `AnnotationCoordinator::onAnnoDeleteSelected`（与标注菜单/Delete 键同一入口；`syncFromModel` 同叠放区按选中置灰；**移动端唯一入口**——工具条「删标注」已移除）；`clearAllBtn_`「清除全部标注」发 `clearAllRequested` → `onAnnoClearAll`（弹确认后清空，可撤销）。
- `syncFromModel()`：边框色 / 填充色 / 粗细 / 填充 / 圆角半径取 `displayXxx`（选中项属性，否则当前默认）、文字 / 字号取 `currentXxx`（`blockSignals` 防回环）。
- **Core `Annotation` 区分边框色 `color` 与填充色 `fillColor`，故面板提供两个独立取色按钮**；新建标注的填充色默认跟随边框色，显式改过后断开联动。
- **能力置灰**（`syncFromModel` 末尾按形状类型自动执行）：选中 / 绘制的形状不可填充（仅直线 / 文字）或未勾选填充时，禁用填充色按钮；无转角形状（直线 / 圆 / 椭圆 / 文字）时禁用圆角 spin。SELECT 且无选中时一律可用（此时编辑的是下一次绘制的默认值，不受上一工具残值错禁）；判定均由 `AnnotationBridge::contextSupportsFill/contextSupportsCornerRadius` 提供，面板只据此 `setEnabled`。
- **变换区**（`transformGroup_`，非破坏性变换，方案 A；**绝对累积值语义、改动即生效**）：`scaleXSpin_`/`scaleYSpin_`（**-100000..100000 %**，默认 100；含负=翻转）+ `rotateSpin_`（**-36000..36000 °**，默认 0；容纳多次旋转的累积角），三 spin 均 `keyboardTracking(false)`（键入不逐字符触发，回车/失焦或点箭头才更新）。**无「应用变换」按钮**——改动任一 spin 即直接下发。
  `syncFromModel` 从 `model_->displayObbScaleX/Y/RotationDeg()` 读选中形状的**绝对累积变换**回显（系数×100 为百分比、含负；`blockSignals` 防回环），并把该基准存入 `curObbSx_/Sy_/Rot_`；spin 显示的就是底层真实参数（忠实反映、**不回弹**）。
  每个 spin 的 `valueChanged` **只按其单轴**把目标绝对值换算为相对基准的增量（缩放取比 `v/100/curObbSx_`、旋转取差 `v-curObbRot_`，其余两轴传恒等 `1.0`/`0.0`）再 `emit transformApplyRequested` → `AnnotationCoordinator` → `AnnotationBridge::transformSelected` → Core `Shape::applyObbTransform`（累积到 `xform_` 与带符号 OBB 参数）。因 `curObb*` 即当前底层绝对值，增量恰把该轴移到目标绝对值，故**连续编辑无漂移、且单轴改动不牵连其余两轴**（规避 % 取整误差扰动）；提交后模型发 `changed` → `syncFromModel` 依最新累积值回显（**无回弹**，所见即所得）。
  变换区仅在**有选中标注**时启用（`syncFromModel` 末尾按 `selectedIndex().has_value()` 置 `enabled`）。
- **`setTransformPreview(sx, sy, rotateDeg)`**（手柄联动，收**绝对值**）：画布拖拽 OBB 手柄时，`AnnotationCoordinator` 逐帧调本方法把预览的**绝对累积**缩放/旋转回显到变换区数值（`blockSignals` 防回环、不触发 `transformApplyRequested`）；缩放 spin 范围含负，故拖手柄越过对边翻转时数值会从正连续变小、**穿过 0 进入负值**（不再被钳在下限）。释放提交后靠模型 `changed` → `syncFromModel` 回显最终累积值（**不回弹**）。
  **翻转无面板按钮**：由画布 OBB 手柄拖过对边（产生负缩放系数）实现（见 `src/canvas`）。
