# panels/ — 模式/极性、参数与导出面板

三个 QWidget 面板，均**只读写 `Document`**（不各自持有真相、不直接调 Core）。用户操作 → 写回 Document →
Document 发 `changed()` → MainWindow 刷新预览。反向同步用 `blockSignals` 防回环。

| 文件 | 职责 |
| ---- | ---- |
| `left_panel.{h,cpp}`  | 左侧：模式切换 L1/L2/[L3 占位]、极性开关（保留绿 / 删除红）、工具/图层占位 |
| `param_panel.{h,cpp}` | 右侧「参数」页：QStackedWidget 分 L1/L2/[L3 占位] 三页 |
| `export_panel.{h,cpp}`| 右侧「导出」页：输出模式、目录/文件、格式、质量、命名、导出按钮 |

## left_panel

- 模式组 `QButtonGroup`（id=1/2/3），L2 用橙色强调样式（差异化内核），L3 `setEnabled(false)`（第二阶段接入）。
- 极性组（id=0 保留=绿 / 1 删除=红）。
- `onModeToggled`→`doc_->setMode`；`onPolarityToggled`→`doc_->setPolarity`。
- `syncFromDocument()` 在 `blockSignals` 下回填选中态（供 MainWindow 统一同步）。

## param_panel

- **L1 页**：形状 combo（矩形/横带/竖带）+ x1/y1/x2/y2 数值输入（`QSpinBox`，`keyboardTracking(false)`）。
- **L2 页**：子功能 combo（十字/横线/竖线，多矩形项禁用占位）+ 坐标 + 坍缩可行性提示标签 `collapseHint_`。
- **L3 页**：占位（第二阶段）。
- `setMode()` 切换页；`syncFromDocument()` 回填形状/子功能/坐标，并依图像尺寸收紧 spin 上限；
- `setCollapseHint(bool, reason)` 显示 Core `isCollapsible` 结果（不可行时禁用坍缩）；
- `onCoordEdited`→`applyCoordsToDocument`：取当前页 spin 组装**规范化** `RectRegion` → `doc_->setRect`。

## export_panel

- 输出模式 combo：分离导出 / 合并坍缩 / 合并重排（映射到 `EmitMode` + `MergeLayout`）。
- 目录行 / 文件行均为**可手动键入**的 `QLineEdit`（`editingFinished` 写回 Document），并各带「浏览…」按钮
  （`getExistingDirectory` / `getSaveFileName` + 依格式的过滤器）作为等价选择方式；目录/文件/命名模板行随输出模式显隐。
- 格式 combo（PNG/JPEG/WebP/BMP）；质量 spin 仅在有损格式（JPEG/WebP）可见。
- `setCollapsible(bool)`：禁用/启用“合并坍缩”项；不可行且当前选中坍缩时自动切到重排。
- `setPreviewInfo(text)`：以文字回显输出画布尺寸/保留块数/可行性（本阶段替代缩略图）。
- 导出按钮 `emit exportRequested()`，实际导出由 MainWindow 经 EngineBridge 完成。
