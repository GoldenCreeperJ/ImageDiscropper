# app/ — 主窗口装配与程序入口

顶层装配与编排。MainWindow 只做「装配 + 编排 + 状态栏呈现」，不含切割/几何/导出实现（A-0.1）——
Core 调用集中在 `EngineBridge`，状态集中在 `Document`。

| 文件 | 职责 |
| ---- | ---- |
| `main_window.{h,cpp}` | 主窗口：菜单/工具栏/状态栏/快捷键/引导 + 信号槽编排 |
| `main.cpp`            | 程序入口：`QApplication` + 显示 MainWindow + 事件循环 |

## 布局（guideline §4.1）

`QSplitter` 水平三分：**左侧面板** | **中央画布（CanvasView）** | **右侧 QTabWidget（参数页 / 导出页 / 图像页）**。

## 编排（connectAll）

```
Document.imageChanged → onImageChanged → rebuildPreviewPixmap + syncPanels + refreshPreview
Document.changed      → onDocChanged   → refreshPreview + syncPanels
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

`onExport()` 在 `buildEngineConfig` 后、解析路径前调 `exportPanel_->rearrangeWarning()`：若重排参数存在风险
（cols×rows < 保留块数、或单元宽/高 < 网格单元宽/高）则弹 `QMessageBox::warning` 二次确认，选 No 则中止导出。

## 预处理（FR-1 / G-3）

`ImagePanel` 发意图信号（`rotateRequested`/`flipRequested`/`scaleRequested`/`resizeRequested`/`grayRequested`/
`invertRequested`/`splitRequested`/`resetRequested`）→ `MainWindow` 对应 `on*` 槽经 `EngineBridge` 调 Core `processing::*`
变换 `doc_.working()` → 公共收尾 `applyWorkingImage(next, okMsg)`：结果为空图则报错；**维度变化（旋转 90/270、缩放）
时先清除失效选区**（`clearRect`/`clearRects`/`clearCells`，因坐标基于旧尺寸）再 `doc_.setWorkingImage`（触发
`imageChanged`→重建预览底图+刷新）。`onResetPreprocess` 将工作图还原为 `original_`（原图始终保留）。图像菜单与图像页共用同一批槽。

**色道反色**：`invertRequested(invR,invG,invB)` 与 `splitRequested(keepR,keepG,keepB)` 共用面板上的 R/G/B 复选框作为
「作用通道」选择器——反色＝对勾选通道取反、分离＝仅保留勾选通道；两槽各自把三个 bool 拼成长度 3 的掩码交 Core
（`invertChannels` 本就支持掩码，故无需改 Core）。图像菜单的「反色（全通道）」经 lambda 固定传 `(true,true,true)`。

**缩放忙碌对话框（防御性编程）**：`onScale`/`onResize` 在大图上重采样可能耗时，故经 `runWithBusyDialog(text, op)`
执行——弹出**应用级模态、不可取消**的 `QProgressDialog`（不确定进度条），`processEvents` 先绘制再同步跑 Core，期间
阻断其余一切输入；配合 `busyResample_` 重入守卫，避免处理未结束时被再次触发。

## 菜单 / 工具栏 / 快捷键

- **菜单**：文件（打开/导出/退出）、编辑（撤销/重做占位禁用、清除选区）、图像（预处理：左转/右转/180°、水平/垂直翻转、
  黑白、反色（全通道）、重置预处理、打开图像处理面板）、标注（占位禁用）、视图（缩放/适应/重置/切换遮罩）、帮助（使用说明/关于）。撤销重做与标注仍先禁用占位，明确交付边界。
- **工具栏**：打开/导出、放大/缩小/适应、模式切换（`QActionGroup`，L2 强调、L3 禁用）、遮罩切换。
- **快捷键**：`Ctrl+O`/`Ctrl+S`、`Ctrl +`/`Ctrl -`/`Ctrl+0`、`1`/`2`/`3` 切模式、`K`/`R` 切极性、`Esc` 清选区、方向键微调。
  单键快捷键（1/2/3/K/R/方向键）在焦点位于数值/文本输入控件时被 `focusInTextInput()` 守卫忽略，避免打字误触。
- **首次引导**：`showFirstRunGuide()` 一次性说明 L1/L2/L3 三种模式的区别（§5.2）。
- **非模态提示**：错误经 `notify()` 写状态栏并限时显示，不打断用户（§5.2）。

## main.cpp

创建 `QApplication`，设置应用/组织名（供后续 QSettings 持久化），用 `QCommandLineParser` 解析命令行；
若首位置参数为图像路径，窗口显示后调用 `MainWindow::openImageFromPath()` 自动打开（与菜单「打开」共用载入逻辑）。
