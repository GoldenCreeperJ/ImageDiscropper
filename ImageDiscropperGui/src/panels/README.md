# panels/ — 模式/极性、参数、导出与图像处理面板

四个 QWidget 面板，均**只读写 `Document`**（不各自持有真相、不直接调 Core）。用户操作 → 写回 Document →
Document 发 `changed()` → MainWindow 刷新预览。反向同步用 `blockSignals` 防回环。
（例外：图像处理面板发的是「意图信号」，由 MainWindow 经 EngineBridge 调 Core processing 变换工作图后写回 `Document`。）

| 文件 | 职责 |
| ---- | ---- |
| `left_panel.{h,cpp}`  | 左侧：模式切换 L1/L2/L3、极性开关（保留绿 / 删除红）、工具/图层占位 |
| `param_panel.{h,cpp}` | 右侧「参数」页：QStackedWidget 分 L1/L2/L3 三页 |
| `export_panel.{h,cpp}`| 右侧「导出」页：输出模式、目录/文件、格式、质量、命名、导出按钮 |
| `image_panel.{h,cpp}` | 右侧「图像」页：预处理（旋转/翻转/缩放/尺寸/黑白/反色/色道分离/重置） |

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
  画布单元点选/自定义拖拽调序由后续增量接入。
- `setMode()` 切换页；`syncFromDocument()` 回填形状/子功能/坐标与 L3 网格参数/排序/只读回显，并依图像尺寸收紧 spin 上限；
- `setCollapseHint(bool, reason)` 显示 Core `isCollapsible` 结果（不可行时禁用坍缩）；
- `onCoordEdited`→`applyCoordsToDocument`：取当前页 spin 组装**规范化** `RectRegion`；L2 多矩形下写回 `updateRect(选中行)`，其余 → `doc_->setRect`。

## export_panel

- 输出模式 combo：分离导出 / 合并坍缩 / 合并重排（映射到 `EmitMode` + `MergeLayout`）。
  **重排项仅 L3 启用**：`refreshModeItemStates()` 依 `doc_->mode()==L3` 与 `collapsible_` 置 item 的 enabled（非 L3 时重排项置灰）。
- 目录行 / 文件行均为**可手动键入**的 `QLineEdit`（`editingFinished` 写回 Document），并各带「浏览…」按钮
  （`getExistingDirectory` / `getSaveFileName` + 依格式的过滤器）作为等价选择方式；目录/文件/命名模板行随输出模式显隐。
- 格式 combo（PNG/JPEG/WebP/BMP）；质量 spin 仅在有损格式（JPEG/WebP）可见。
- `setCollapsible(bool)`：存 `collapsible_` + `refreshModeItemStates()`；不可行且当前选中坍缩时按 L3-aware 回退（L3→重排，否则→分离）。
- **合并重排**（输出模式=重排时显示 `rearrangeRow_`）：列/行/单元宽/单元高 spin（0=自动，`setSpecialValueText("自动")`）
  + 「按格数自动」按钮 `autoGridBtn_` + 填充色按钮 `padColorBtn_`（`QColorDialog` 含 alpha，背景色块回显）；写回 `setMergeGrid`/`setMergeCellSize`/`setPadColor`。
- **重排填充顺序**（与选择排序正交，写回 `Document` 的 `MergeOrder`）：`mergeSortCombo_`（行优先/列优先）+ `mergeSnake_`/`mergeReverse_` 复选框
  → `onMergeSortChanged`/`onMergeSnakeToggled`/`onMergeReverseToggled` → `setMergeOrderStrategy`/`setMergeOrderSnake`/`setMergeOrderReverse`。
- **自动 cols/rows（开方）**：`setRearrangeContext(keptCount,cellW,cellH)` 由 MainWindow 回灌保留块数与网格单元尺寸；
  首次切到重排且用户未手改过（`mergeGridTouched_==false`）时 `applyAutoGrid()` 自动填入 cols=ceil(√n)、rows=ceil(n/cols)（具体数字，用户可再改）。
- **双警告**：`rearrangeWarning()` 当 cols×rows < 保留块数、或单元宽/高 < 网格单元宽/高时返回警告文本；
  `updateRearrangeWarning()` 将其以**内联红字** `rearrangeWarn_` 呈现；导出前的**弹窗确认**由 MainWindow 调 `rearrangeWarning()` 完成。
- `setPreviewInfo(text)`：以文字回显输出画布尺寸/保留块数/可行性（本阶段替代缩略图）。
- 导出按钮 `emit exportRequested()`，实际导出由 MainWindow 经 EngineBridge 完成。

## image_panel

预处理（FR-1 / G-3）面板，对应 guideline §4.5.4。面板**不碰 Core、不做像素运算**，只把控件值翻译为意图信号（A-0.1）；
实际变换由 MainWindow 经 `EngineBridge` 调 Core `processing::*` 完成，结果写回 `Document` 工作图（原图始终保留）。

- **旋转组**：左转 90° / 右转 90° / 180° → `rotateRequested(angleDeg)`（-90/90/180）。
- **翻转组**：水平 / 垂直 → `flipRequested(bool horizontal)`。
- **缩放/尺寸组**：比例百分比 spin（1..1000%）+「按比例应用」→ `scaleRequested(factor)`；
  目标宽/高 spin（1..20000）+「保持宽高比」（改宽联动算高）+「按尺寸应用」→ `resizeRequested(w,h)`。
- **颜色组**：黑白 → `grayRequested()`；反色（全通道）→ `invertRequested()`（灰度图上对单一亮度通道取反，仍有效）；
  色道分离 R/G/B 复选框（勾选=保留）+「色道分离」→ `splitRequested(keepR,keepG,keepB)`（MainWindow 拼成长度 3 掩码）。
- **重置预处理**按钮 → `resetRequested()`：仅当 `doc_->hasPreprocess()` 时可用。
- `syncFromDocument()`：无图时整板禁用；有图时把目标宽/高回灌为当前工作图尺寸（`blockSignals` 防联动回调）；
  **工作图为灰度（黑白后）时禁用「黑白」与「色道分离」组控件**（灰度无 R/G/B 可分、黑白幂等），反色保持可用；并按 `hasPreprocess()` 启停重置按钮。
- §4.5.4「颜色选取（取色器）」与标注属性（描边/填充色）强相关，留待第四阶段标注面板统一提供。
