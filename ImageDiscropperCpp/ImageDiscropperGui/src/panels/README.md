# panels/ — 模式/极性、参数、导出、预处理与标注面板

八个 QWidget 面板，均**不直接调 Core 几何/光栅化**。模式/参数/导出面板只读写 `Document`（用户操作 → 写回 Document →
Document 发 `changed()` → app 层 `PreviewController` 刷新预览；反向同步用 `blockSignals` 防回环）。
（例外：图像处理面板是**声明式预处理队列编辑器**，只发队列意图信号，由 `PreprocessController` 经 EngineBridge 调 Core 从原图重放物化工作图；标注工具/图层/属性面板发意图信号，由 `AnnotationCoordinator` 写回 `AnnotationBridge`。）

> `pattern_form`/`placement_row`（新建画布对话框的采参组件）位于 `app/forms/`，与 `NewCanvasDialog` 同域，不在本目录。

| 文件                              | 职责                                                                                                                                                                                                                                                                                                                                                             |
|---------------------------------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `accordion_panel.{h,cpp}`       | **桌面右侧多页容器**：AccordionPanel（多节可同时展开，整栏外套 QScrollArea 滚动触达）；标题按钮 checkable + 前缀 ▾/▸                                                                                                                                                                                                                                                                             |
| `left_panel.{h,cpp}`            | 左侧：模式切换 L1/L2/L3、极性开关（保留绿 / 删除红）（工具/图层各成独立面板）                                                                                                                                                                                                                                                                                                                  |
| `param_panel.{h,cpp}`           | 右侧「参数」页：QStackedWidget 分 L1/L2/L3 三页；堆叠容器高度只随当前页（PageSizedStack 关 heightForWidth + reflowStack 钳 min=max + 根布局尾部 stretch 保顶对齐（移动端 tab 页），规避 SizeAll 下隐藏页撑高/居中）；三页构建器拆独立 TU `param_panel_pages.cpp`（共享件 `param_panel_internal.h`；坐标框 `CoordSpinBox` 本体在 `include/util/coord_spin.h`，整数像素 + `50%` 百分比输入）                                                         |
| `param_panel_pages.cpp`         | `ParamPanel::buildL1Page/buildL2Page/buildL3Page` 三页构建器实现（ParamPanel 的 TU 拆分）                                                                                                                                                                                                                                                                                  |
| `param_panel_grid.cpp`          | `ParamPanel` 的 L3 网格轴槽与按轴三形态实现（TU 拆分）：基准点/单元尺寸/余量写回、三形态页同步与切换、权重文本解析与坐标框基准绑定                                                                                                                                                                                                                                                                                   |
| `preprocess_panel_sections.cpp` | `PreprocessPanel` 的三个分区构建（buildResizeSection/buildGeoSection/buildColorSection，TU 拆分）：画布尺寸独立区（目标宽/高为坐标框，另收 `50%` 百分比、基准当前工作图）/几何操作区/色彩操作区                                                                                                                                                                                                                      |
| `export_panel_merge.cpp`        | `ExportPanel` 的「合并重排」域实现（TU 拆分）：重排参数输入区（列/行/单元尺寸）与重排编排区（上下文回灌/自动行列/警告判定/排序与蛇形逆序）                                                                                                                                                                                                                                                                               |
| `export_panel_output.cpp`       | `ExportPanel` 的输出方式/格式/质量/命名/路径簇实现（TU 拆分）：外部选择入口（文件菜单「导出设置」与用户点 combo 同一路径）、槽写回 Document、字段显隐与保存过滤器、模式项可用性刷新                                                                                                                                                                                                                                                   |
| `export_panel.{h,cpp}`          | 右侧「导出」页：输出模式、目录/文件、格式、质量、命名、**导出时烧录标注开关**、**输出图像预览缩略图**、导出按钮（重排单元宽/高为坐标框，另收 `50%` 百分比、基准画布）                                                                                                                                                                                                                                                                    |
| `preprocess_panel.{h,cpp}`      | 右侧「预处理」页：**声明式预处理队列编辑器**（几何/色彩两独立列表 + 顶部独立 Resize 区 + 重置），发 stepAppend/Replace/Remove/Move/reset 队列信号                                                                                                                                                                                                                                                          |
| `mode_tool_panel.{h,cpp}`       | 左侧「交互模式」：画布四态互斥单选 +「标注绘制」态形状子行                                                                                                                                                                                                                                                                                                                                 |
| `layer_panel.{h,cpp}`           | 左侧「图层」组：底图 / 图片图层 / 标注 / 遮罩 / 网格线 / 切割线 / 选取边框 / 单元编号显示开关（8 项，按画布 z 序；标注烧录开关属导出面板）                                                                                                                                                                                                                                                                             |
| `layer_edit_panel.{h,cpp}`      | 右侧「图片」节（**多层画布**，SPEC §8.2；桌面 `AccordionPanel` 折叠栏首节、移动端抽屉其一）：作用于画布当前拾取层（`selectedLayerId`，无内置图层列表）的上/下移·置顶/置底·删除·清空 + 添加图片图层（复用 `NewCanvasDialog`：未勾附加图→程序图案产 `recipe` 层；勾选附加图→对话框**两段合成**整幅结果入 `file` 层；不撑大）+ 落位 `transform`（画布锚点 + 图片枢轴 9 向 + 自由 x/y + 目标宽高 + 不透明度；偏移与目标宽高为坐标框，另收 `50%` 百分比、基准画布）+ 改画布尺寸（仅清框选）；发意图信号交 `SessionController` 落 `Document` CRUD |
| `annotation_prop_panel.{h,cpp}` | 右侧「标注」属性页：颜色/粗细/填充/文字/字号+ 叠放区（上移/下移一层、置顶/置底，仅选中标注可用，层序控件位于本面板）+ 变换区（缩放%/旋转° 显示选中形状的**绝对累积变换**、含负=翻转，**改动即生效无应用按钮**，作用于当前选中标注；`setTransformPreview` 与画布 OBB 手柄拖拽实时联动）                                                                                                                                                                                           |

## left_panel

- 模式组 `QButtonGroup`（id=1/2/3），L2 用橙色强调样式（差异化内核）；L1/L2/L3 均可选。
- 极性组（id=0 保留=绿 / 1 删除=红）。
- `onModeToggled`→`doc_->setMode`；`onPolarityToggled`→`doc_->setPolarity`。
- `syncFromDocument()` 在 `blockSignals` 下回填选中态（供 MainWindow 统一同步）。

## param_panel

- **L1 页**：形状 combo（矩形/横带/竖带）+ x1/y1/x2/y2 数值输入（`CoordSpinBox`，`keyboardTracking(false)`）；
  坐标框除整数像素外接受百分比串（`50%`，SPEC §2.1）：提交时按当前画布宽/高分方向换算成像素，随后照常以整数显示。
- **L2 页**：子功能 combo（十字/横线/竖线/**多矩形并集**）+ 坐标 + 坍缩可行性提示标签 `collapseHint_`
  + 「转为网格模式编辑…」按钮 `l2ToGridBtn_`（有可转换选区几何时可用：单选区 `rect_` 有效，或多矩形列表非空；`onConvertToGrid`→`doc_->convertRectToGrid`）。
  选「多矩形并集」时显示矩形列表 `l2RectList_`（容器 `l2MultiBox_`）：在画布**空白处拖拽框选逐个追加**，矩形本身**可直接拖动/四角缩放/拖边调整**（每个矩形一个 `SelectionRectItem`，见 canvas/），选中项坐标回填 x1/y1/x2/y2 微调，
  另有「删除选中 / 清空」按钮（`onRectDel/ClearClicked`）；重建列表时 `syncingRectList_` 抑制选中信号回环。（追加经由画布空白处拖拽框选，无独立「添加」按钮。）
  选中行变化经信号 `rectSelected(index)` 通知 CanvasCoordinator 高亮画布上对应选区框（-1 清除）；`selectRectRow(index)`/`updateRectListItem(index)` 供画布拖拽时定向跟随选中与实时回显尺寸（仅改单行文本/选中行，不重建整表）。
- **L3 页**：每轴一个「定义方式」combo（周期 / 分割数 / 比例，索引与 Core `GridAxisMode` 枚举序一致）+ `QStackedWidget` 三形态页——周期（基准点 x0/y0 + 单元宽/高 cw/ch，两者均可写百分比串、按本轴方向取基准换算）、分割数（份数 spin）、比例（权重 `QLineEdit`，收 `1,2,1` 与 `25%,50%,25%`、非法回显旧值）+ 余量策略 combo（仅当至少一个周期轴可用，否则置灰并显同屏说明）；两轴可各用各的，切换定义方式时以屏上现有数值起手。
  + 选择集（全选/反选/清空按钮）
  + 排序（策略 combo row-major/column-major/custom + 整体逆序/蛇形复选框）；派生行列数与已选单元数为**只读回显**。
  行列数由 Core 依图像边界自动推导（面板不输入）；枚举↔combo 索引与 Core `RemainderPolicy`/`SortStrategy` 一致（直接 `static_cast`）。
  画布单元点选/自定义拖拽调序由 `cell_picker_item` 承担（单击切换/拖拽框选/Custom 拖拽调序），本页提供按钮式选择集与排序策略。
- `setMode()` 切换页；`syncFromDocument()` 回填形状/子功能/坐标与 L3 每轴定义方式（combo + 形态页 + 对应控件，`syncGridAxisToUi`）/排序/只读回显，并依图像尺寸收紧 spin 上限（画布拖网格基准手柄释放后同样经 `changed` → `syncPanels` 回填这些控件）；
- `setGridAxisMode(bool horizontal, mode)`：外部（模式菜单「网格定义」）切形态的唯一入口，内部复用 `onGridModeChanged`，故与面板下拉同一写回语义；
- `setCollapseHint(bool, reason)` 显示 Core `isCollapsible` 结果（不可行时禁用坍缩）；
- `onCoordEdited`→`applyCoordsToDocument`：取当前页 spin 组装**规范化** `RectRegion`；L2 多矩形下写回 `updateRect(选中行)`，其余 → `doc_->setRect`。

## export_panel

- 输出模式 combo：分离导出 / 合并坍缩 / 合并重排（映射到 `ExportMode` + `MergeLayout`）。
  **重排项依 `rearrangeAvailable()` 启用**（`doc_->mode()==L3` 且保留块长宽全等，Core 重排准入门限）；`refreshModeItemStates()` 置 item(1) 依 `collapsible_`、item(2) 依该判据，菜单「导出设置·重排」同源置灰。
- 目录行 / 文件行均为**可手动键入**的 `QLineEdit`（`editingFinished` 写回 Document），并各带「浏览…」按钮
  （`getExistingDirectory` / `getSaveFileName` + 依格式的过滤器）作为等价选择方式；目录/文件/命名模板行随输出模式显隐。
  **移动端「浏览…」禁用**：导出统一写入相册公共目录 `Pictures/ImageDiscropper`，不经 SAF 逐目录授权；
  细节见 `export_panel.cpp` 注释与 mobile 目录实现。
- 格式 combo（PNG/JPEG/WebP/BMP）；质量 spin 仅在有损格式（JPEG/WebP）可见。
- `setCollapsible(bool)`：存 `collapsible_` + `refreshModeItemStates()`；不可行且当前选中坍缩时按 L3-aware 回退（L3→重排，否则→分离）。
- **合并重排**（输出模式=重排时显示 `rearrangeRow_`）：列/行/单元宽/单元高 spin（0=自动，`setSpecialValueText("自动")`）
  + 「按格数自动」按钮 `autoGridBtn_`；写回 `setMergeGrid`/`setMergeCellSize`。
  单元宽/高为 `CoordSpinBox`，另收百分比串（`50%`，SPEC §2.1）：基准＝画布宽/高（无画布退回工作图），换算后仍以整数存 Document；`0%` 即「自动」。
- **填充色**（`padRow_`，三模式恒可见）：`padColorBtn_`（`QColorDialog` 含 alpha，背景色块回显）
  → `setPadColor`。该值承担三件事：合并画布底色（重排空位、坍缩下的越界部分）、Pad 余量策略下越界单元的补白
  （分离与合并导出同一语义，Core `exportSeparate` 逐块按完整单元尺寸补白）、有损格式（JPEG/WebP/BMP）压平透明像素的背景色；
  分离导出仅在 Pad 残缺单元上体现该色，完整单元不受影响。
- **重排填充顺序**（与选择排序正交，写回 `Document` 的 `MergeOrder`）：`mergeSortCombo_`（行优先/列优先）+ `mergeSnake_`/`mergeReverse_` 复选框
  → `onMergeSortChanged`/`onMergeSnakeToggled`/`onMergeReverseToggled` → `setMergeOrderStrategy`/`setMergeOrderSnake`/`setMergeOrderReverse`。
- **自动 cols/rows（开方）**：`setRearrangeContext(keptCount,cellW,cellH)` 由 `PreviewController` 回灌保留块数与网格单元尺寸；
  首次切到重排且用户未手改过（`mergeGridTouched_==false`）时 `applyAutoGrid()` 自动填入 cols=ceil(√n)、rows=ceil(n/cols)（具体数字，用户可再改）。
- **双警告**：`rearrangeWarning()` 当 cols×rows < 保留块数、或单元宽/高 < 网格单元宽/高时返回警告文本；
  `updateRearrangeWarning()` 将其以**内联红字** `rearrangeWarn_` 呈现；导出前的**弹窗确认**由 ExportController 调 `rearrangeWarning()` 完成。
- `setPreviewInfo(text)`：以文字回显输出画布尺寸/保留块数/可行性。
- **输出图像预览缩略图**：`setPreviewPixmap(pm)` 把 `PreviewController` 按 Core `Composition` 渲染的低分辨率缩略图
  显示在固定尺寸（200×160）的 `previewLabel_` 上（等比缩到框内、仅缩不放）；传空 pixmap 时回退到占位文案「（无输出预览）」。
  与 `setPreviewInfo` 的文字信息并存；渲染逻辑（小源图 blit）在 `PreviewController`，面板只负责展示。
- **导出时烧录标注**（`burnIn_`，默认 false）：勾选后导出把标注合成进像素（随像素一起被切割）；`toggled`→`burnInChanged(bool)` 交 `AnnotationCoordinator` 写回 `AnnotationBridge::setBurnIn`；`setBurnInChecked(bool)` 供反向同步（`blockSignals` 防回环）。该开关属导出行为，故置于导出面板。
- 导出按钮 `emit exportRequested()`，实际导出由 ExportController 经 EngineBridge 完成。

## preprocess_panel

预处理（FR-1）**声明式队列编辑器**。面板**不碰 Core、不做像素运算、不持真相**，只把控件值翻译为队列意图信号；
实际重放物化由 `PreprocessController` 经 `EngineBridge` 调 Core（从原图重放整条队列）完成，结果写回 `Document` 工作图（原图始终保留）。

轻操作在界面上**拆为几何、色彩两个独立列表**（表面两列表、底层一条队列），另加顶部独立的画布尺寸（Resize）常驻区：

- **画布尺寸（Resize）独立区**（`resizeW_`・`resizeH_` + `resizeKeepRatio_`（改宽联动算高）+ `applySizeBtn_`；`resizeScalePct_`（1..1000%）+ `scaleSizeBtn_`；`clearSizeBtn_`；`currentSizeLabel_` 只读回显当前输出尺寸）：**常驻、有图即可用、不入队列**，发 `resizeAppliedRequested(w,h)` / `resizeScaleRequested(factor)` / `resizeClearedRequested()`；resize 恒最先从原图重采样。目标宽/高为 `CoordSpinBox`：另收百分比串（`50%`），基准＝当前工作图宽/高（与 `currentSizeLabel_` 回显同源），故 `100%` 恒等于框内当前值。
- **几何操作区**：`geoTypeCombo_`（旋转 / 翻转）+ `rotAngle_`（-90/90/180）/ `flipDir_`（水平/垂直）；`geoAddBtn_`→`buildGeoConfig()` 发 `stepAppendRequested(cfg)`、`geoReplaceBtn_`→`stepReplaceRequested(index,cfg)`；`geoList_` 展示几何步（`describeStep()` 生成中文行），`geoDelBtn_`/`geoUpBtn_`/`geoDownBtn_` 删除/上移/下移。
- **色彩操作区**：`colorTypeCombo_`（黑白 / 色道反色 / 色道分离）+ `chR_`・`chG_`・`chB_`（反色＝反相它 / 分离＝保留它）；`colorAddBtn_`→`buildColorConfig()` 发 `stepAppendRequested(cfg)`、`colorReplaceBtn_`→`stepReplaceRequested(index,cfg)`；`colorList_` 展示色彩步，`colorDelBtn_`/`colorUpBtn_`/`colorDownBtn_` 删除/上移/下移。
- **两列表→扁平下标映射**：`geoList_` 行号即底层队列扁平下标；`colorList_` 行号经 `colorFlatIndex()`（= `geoRowCount()` + 色彩行号）映到几何块之后的下标。底层 `steps` 恒为「全部几何步在前 + 全部色彩步在后」，`PreprocessController` 据此维持不变式（几何步插到几何块尾、色彩步追加队尾，跨类上下移忽略）。
- **重置**：`resetBtn_`→`resetRequested()`：清空 resize 与整条队列，仅当 `doc_->hasPreprocess()` 时可用。

发信号：`stepAppendRequested(PreprocessConfig)` / `stepReplaceRequested(int,cfg)` / `stepRemoveRequested(int)` / `stepMoveRequested(int,bool down)` / `resetRequested()`（index 均为底层**扁平下标**，均不持真相）。

**按目标驱动**（多层画布，SPEC §8.2）：面板顶部 `targetLabel_` 显式标注当前预处理目标——选中图片图层时＝「图片图层 #id」，否则＝「画布」；队列编辑意图经 `PreprocessController::commitActive`（及 `activeSteps`/`activeResize` 读取）落到 `Document::activeSession()`（选中层会话，否则画布会话），不固定绑定单一预处理队列。

- `setDocument(Document*)` 绑定会话状态源；`syncFromDocument()`：无图时整板禁用；从 Document 反向同步时按类型把 `steps` 重分为几何段 / 色彩段重建两列表文本（上/下移后靠 `pendingSelectRow_` 使高亮跟随被移动项）、把 `resizeW_`・`resizeH_` 回灌为当前输出尺寸（`blockSignals` 防联动回调）、
  **工作图为灰度（黑白后）时禁用「黑白」与「色道分离」**（灰度无 R/G/B 可分、黑白幂等），反色保持可用；按 `hasPreprocess()`/选中态启停按钮。
- 「颜色选取（取色器）」与标注属性（描边/填充色）强相关，统一由 `annotation_prop_panel` 提供（见下）。

## mode_tool_panel

左侧「交互模式」面板：顶部画布四态互斥单选（框选 / 图片拾取 / 标注选取 / 标注绘制），仅「标注绘制」态展开下方形状子行。面板只采集意图发 `modeChanged(CanvasMode)` / `toolSelected(AnnoTool)`，真正的模式落地由 `CanvasCoordinator` 写回 `CanvasView` / `CanvasScene`、切工具由 `AnnotationCoordinator` 写回 `AnnotationBridge`。

- 两个互斥 `QButtonGroup`：`modeGroup_` 承载四态模式（`id = static_cast<int>(CanvasMode)`，2 列网格），`toolGroup_` 承载形状工具（`id = static_cast<int>(AnnoTool)`，3 列网格，`shapeArea_` 容器仅 `AnnoCreate` 态可见）；无额外映射表，工具提示齐备。
- 按钮未选态走 `palette(...)` 令牌（随深/浅主题自动给正确字色），选中态蓝色高亮（`#3b7ddd` 配白字）。
- 3 列网格含「等腰直角三角形」等宽标签，需较宽左栏（左栏最小宽由主窗 `leftScroll->setMinimumWidth` 控制）。
- `onModeToggled` 仅 checked 时发 `modeChanged` 并联动子行显隐；`onToolToggled` 仅 checked 时发 `toolSelected`；默认勾选「框选」态、子行随之隐藏。
- `setModel()`/`syncFromModel()`：从模型反向同步当前工具选中态（`blockSignals` 防回环）。

## layer_panel

左侧「图层」组：集中管理画布各图层显示/隐藏（底图 / 图片图层 / 标注 / 遮罩 / 网格线与切割线（同一开关）/ 选取边框 / 单元编号，7 项按画布 z 序）。遵循「隐藏图层=不可交互」原则。

- 七个 `QCheckBox`（按画布 z 序：底图 < 图片图层 < 标注）：`baseVisible_`/`imageVisible_`（图片图层整体显隐，单开关控所有图片层、仅影响预览）/`annoVisible_`/`maskVisible_`/`gridVisible_`（网格线与切割线同一开关，按模式各司其职）/`selectionVisible_`/`numberVisible_`（均默认 true）；toggled 直接转发 `baseVisibilityChanged`/`imageLayerVisibilityChanged`/`annotationVisibilityChanged`/`maskVisibilityChanged`/`gridVisibilityChanged`/`selectionVisibilityChanged`/`numberVisibilityChanged`。（标注「导出时烧录」开关在导出面板，见 export_panel。）
- 「单元编号」隐藏时仅切换 `CellNumberLayer` 角标可见性，`CellPickerItem` 内的 Custom 拖动调序一并停用（自定义序无可见标识即无法校序）。
- **遮罩开关是本面板的正式图层项**；视图菜单与画布右键仍可切换，两路语义相反：**视图菜单**经 MainWindowUi 调 `setMaskVisible(bool)` **前向驱动**（**不** blockSignals，复选框 toggled 自然发 `maskVisibilityChanged` 走与用户直接勾选同一条落地链）；**画布右键**经 PreviewController 自行落地后调 `syncMaskVisible(bool)` **纯镜像**勾选态（`blockSignals` 防回环，不重发意图）。
- 网格线与切割线/选取边框的落地：`AnnotationCoordinator` 把信号分派到 `CanvasScene::setGridLinesVisible`/`setSelectionVisible`，两者均随后调 `PreviewController::refreshPreview` 重建落地（L3 网格依 `gridVisible_`、L2 诱导切割线依 `cutLinesVisible_`、L3 单元选择高亮依 `selectionVisible_`）；`setGridLinesVisible` 同置两标志（L1/L2 与 L3 互斥使用，故单开关无重叠），并在非 L3（`gridLayer_` 只承载 L2 多矩形诱导线）时**另直接切换 `GridLayer` 可见性**，使开关不依赖重建即时生效；**L3 网格基准手柄同属该图层**（关开关即隐藏且不可拖）；选取边框隐藏同时禁用选区拖拽交互（`SelectionRectItem::setBorderVisible` 切 `setAcceptedMouseButtons`）；框选通道也随之关断——`CanvasView::gestureMarqueeBegin` 以 `selectionVisible()` 为门控，L1/L2/L3 均不起橡皮筋，L3 另由 `CanvasScene::cellSelectionInteractive()` 让 `cellsIntersecting` 返回空（picker 隐藏 + 不起带 + 命中为空=整体不可交互）。
- `syncFromModel()`：标注显示开关 = `layerVisible()`（`blockSignals` 防回环）。
- **各图层的「隐藏」仅切换画布预览可见性与交互性，不影响导出**（见 Gui README「图层与烧录」）。

## layer_edit_panel

右侧「图片」节（**多层画布**，SPEC §8.2；桌面折叠栏首节）。编辑 `Document` 内的图片图层（`std::vector<canvas::ImageLayer>`），与上面 `layer_panel`（画布叠加图层显示开关）职责不同。面板**只发意图信号、不持真相**，由 `SessionController` 落到 `Document` 声明态 CRUD。图层「选中」由画布拾取承担（`Document::selectedLayerId`），本面板不内置图层列表。

- **启停分组**（`setControlsEnabled`）：「添加图片」只要有图即可用（有图即图层画布；空文档经 `ensureLayerCanvas` 兜底起画布）；上/下移/置顶/置底/删除/落位需画布已拾取某层；清空/画布尺寸/不透明度仅图层画布态可用。
- **上/下移、置顶/置底、删除、清空、添加图片图层**：作用对象取 `Document::selectedLayerId()`（未拾取时无操作），发 `layerMoveRequested(id, up)`/`layerMoveEdgeRequested(id, toTop)`/`layerRemoveRequested(id)`/`clearAllLayersRequested()`/`addImageRequested()`；`SessionController` 分别落 `moveLayerZOrder`/`removeLayer`/清空/`onAddImageLayer`（据对话框「附加图片开关」`producesLayerCanvas()` 分派：勾选→两段合成整幅结果作 `ImageLayer{File}` 内容、**落位恒等**；未勾→取 `resultParams()` 产 `recipe` 层；均不撑大画布）。
- **落位 `transform` 编辑区**（`anchorCombo_`/`pivotCombo_` 各 9 向、`posX/Y`/`width/heightSpin_`、`opacitySpin_`）：「画布锚点（`inCanvasAnchor`）+ 图片枢轴（`layerPivot`）+ 自由 x/y + 目标宽高（≤0 用原生）+ 不透明度（0..255）」，组装 `canvas::ImageTransform` 发 `layerTransformRequested(id, transform)`；枚举↔combo index 直接 `static_cast`（与 `generate::ImageAnchor` 序一致）；任一控件改动即时提交（`applyTransformBtn_` 为等价入口）。无选中层时 `opacitySpin_` 改按预处理目标分派：`PreprocessTarget::Base`→`baseOpacityRequested`、`Canvas`→`canvasOpacityRequested`。偏移与目标宽高为 `CoordSpinBox`：另收百分比串（`50%`），基准＝画布宽/高（`x`/宽 用宽、`y`/高 用高），`0%` 在宽/高上即「原生」。
- **画布尺寸区**（`canvasW/HSpin_` + `applyCanvasBtn_`）：发 `canvasSizeRequested(w, h)` → `doc_.setCanvasSize`（**仅清框选**，图片图层与标注保留）。
- `syncing_` 标志在 `syncFromDocument()` 回填控件时抑制 `valueChanged`/`currentIndexChanged` 等信号回环。

## annotation_prop_panel

右侧「标注」属性页。面板只采集属性意图并发信号，`AnnotationCoordinator` 写回 `AnnotationBridge`（Edit 作用选中项、Draw 作下一次绘制默认）。

- `QFormLayout` 逐行排布：**边框色**色块按钮（`QColorDialog` 取色，**开启 `ShowAlphaChannel`**；标注颜色含 alpha，Core `Color` 有 a 分量、rasterizer 已做 src-over 混合；色块以 rgba 背景 + RGBA 文案回显）/ **填充色**独立色块按钮 / 粗细 spin（1..200）/ 填充开关 / 转角圆角半径 spin（0＝直角）/ 文字内容 / 字号 spin（1..2000）。
- 信号：`colorPicked(QColor)`/`fillColorPicked(QColor)`/`strokeChanged(int)`/`fillChanged(bool)`/`cornerRadiusChanged(double)`/`textChanged(QString)`/`fontSizeChanged(double)`。
- **叠放区**（`stackGroup_`，标题「叠放次序（选中标注）」，4 按钮横排）：上移一层 / 下移一层 / 置于顶层 / 置于底层，发 `stackRaiseRequested`/`stackLowerRequested`/`stackToFrontRequested`/`stackBackRequested` → `AnnotationCoordinator`（`onAnnoRaise/Lower/BringToFront/SendToBack`）→ `AnnotationBridge::raiseSelected` 等 → Core `moveAnnotation`（就地重排并规范化 `zOrder`＝下标）；层序控件位于**本面板**；`syncFromModel` 按 `selectedIndex().has_value()` 置 `stackGroup_` 的 enabled。
- **删除选中 / 清除全部标注**（页尾两按钮）：`deleteSelBtn_`「删除选中标注」发 `deleteSelectedRequested` → `AnnotationCoordinator::onAnnoDeleteSelected`（与标注菜单/Delete 键同一入口；`syncFromModel` 同叠放区按选中置灰）；`clearAllBtn_`「清除全部标注」发 `clearAllRequested` → `onAnnoClearAll`（弹确认后清空，可撤销）。
- `syncFromModel()`：边框色 / 填充色 / 粗细 / 填充 / 圆角半径取 `displayXxx`（选中项属性，否则当前默认）、文字 / 字号取 `currentXxx`（`blockSignals` 防回环）。
- **Core `Annotation` 区分边框色 `color` 与填充色 `fillColor`，故面板提供两个独立取色按钮**；新建标注的填充色默认跟随边框色，显式改过后断开联动。
- **能力置灰**（`syncFromModel` 末尾按形状类型自动执行）：选中 / 绘制的形状不可填充（仅直线 / 文字）或未勾选填充时，禁用填充色按钮；无转角形状（直线 / 圆 / 椭圆 / 文字）时禁用圆角 spin。Select 且无选中时一律可用（此时编辑的是下一次绘制的默认值，不受上一工具残值错禁）；判定均由 `AnnotationBridge::contextSupportsFill/contextSupportsCornerRadius` 提供，面板只据此 `setEnabled`。
- **变换区**（`transformGroup_`，非破坏性变换，方案 A；**绝对累积值语义、改动即生效**）：`scaleXSpin_`/`scaleYSpin_`（**-100000..100000 %**，默认 100；含负=翻转）+ `rotateSpin_`（**-36000..36000 °**，默认 0；容纳多次旋转的累积角），三 spin 均 `keyboardTracking(false)`（键入不逐字符触发，回车/失焦或点箭头才更新）。**无「应用变换」按钮**——改动任一 spin 即直接下发。
  `syncFromModel` 从 `model_->displayObbScaleX/Y/RotationDeg()` 读选中形状的**绝对累积变换**回显（系数×100 为百分比、含负；`blockSignals` 防回环），并把该基准存入 `curObbSx_/Sy_/Rot_`；spin 显示的就是底层真实参数（忠实反映、**不回弹**）。
  每个 spin 的 `valueChanged` **只按其单轴**把目标绝对值换算为相对基准的增量（缩放取比 `v/100/curObbSx_`、旋转取差 `v-curObbRot_`，其余两轴传恒等 `1.0`/`0.0`）再 `emit transformApplyRequested` → `AnnotationCoordinator` → `AnnotationBridge::transformSelected` → Core `Shape::applyObbTransform`（累积到 `xform_` 与带符号 OBB 参数）。因 `curObb*` 即当前底层绝对值，增量恰把该轴移到目标绝对值，故**连续编辑无漂移、且单轴改动不牵连其余两轴**（规避 % 取整误差扰动）；提交后模型发 `changed` → `syncFromModel` 依最新累积值回显（**无回弹**，所见即所得）。
  变换区仅在**有选中标注**时启用（`syncFromModel` 末尾按 `selectedIndex().has_value()` 置 `enabled`）。
- **`setTransformPreview(sx, sy, rotateDeg)`**（手柄联动，收**绝对值**）：画布拖拽 OBB 手柄时，`AnnotationCoordinator` 逐帧调本方法把预览的**绝对累积**缩放/旋转回显到变换区数值（`blockSignals` 防回环、不触发 `transformApplyRequested`）；缩放 spin 范围含负，故拖手柄越过对边翻转时数值会从正连续变小、**穿过 0 进入负值**（不被钳在下限）。释放提交后靠模型 `changed` → `syncFromModel` 回显最终累积值（**不回弹**）。
  **翻转无面板按钮**：由画布 OBB 手柄拖过对边（产生负缩放系数）实现（见 `src/canvas`）。
