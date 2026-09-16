# app/ — 主窗口装配与程序入口

顶层装配与编排。MainWindow 只做「装配 + 编排 + 状态栏呈现」，不含切割/几何/导出实现（A-0.1）——
Core 调用集中在 `EngineBridge`，状态集中在 `Document`。

| 文件 | 职责 |
| ---- | ---- |
| `main_window.{h,cpp}` | 主窗口：菜单/工具栏/状态栏/快捷键/引导 + 信号槽编排 |
| `main.cpp`            | 程序入口：`QApplication` + 显示 MainWindow + 事件循环 |

## 布局（guideline §4.1）

`QSplitter` 水平三分：**左侧面板** | **中央画布（CanvasView）** | **右侧 QTabWidget（参数页 / 导出页）**。

## 编排（connectAll）

```
Document.imageChanged → onImageChanged → rebuildPreviewPixmap + syncPanels + refreshPreview
Document.changed      → onDocChanged   → refreshPreview + syncPanels
CanvasView.rubberSelect / CanvasScene.selectionEdited → 写回 Document.setRect
CanvasView.nudgeSelection → onNudge（方向键平移选区，钳制到图像内）
CanvasView.cursorScenePos → onCursor（状态栏坐标 + 像素 RGB）
CanvasView.zoomChanged → onZoomChanged（状态栏缩放倍数；所有缩放入口汇聚于 updateHandleSize 发出）
CanvasView.clearCutRequested / toggleMasksRequested / resetViewRequested → 对应动作
ExportPanel.exportRequested → onExport
```

`refreshPreview()` 是核心：无图→清空；无选区→提示拖拽（不跑引擎，避免 E-1 噪声）；
有选区→`buildEngineConfig` + `EngineBridge.runPreview` → 刷新切割线（`bridge_.cutLines`）/遮罩/选区，
并回写状态栏与面板提示（保留块数、输出画布尺寸、坍缩可行性）。

## 菜单 / 工具栏 / 快捷键

- **菜单**：文件（打开/导出/退出）、编辑（撤销/重做占位禁用、清除选区）、图像（预处理占位禁用）、
  标注（占位禁用）、视图（缩放/适应/重置/切换遮罩）、帮助（使用说明/关于）。后续阶段项先禁用占位，明确交付边界。
- **工具栏**：打开/导出、放大/缩小/适应、模式切换（`QActionGroup`，L2 强调、L3 禁用）、遮罩切换。
- **快捷键**：`Ctrl+O`/`Ctrl+S`、`Ctrl +`/`Ctrl -`/`Ctrl+0`、`1`/`2`/`3` 切模式、`K`/`R` 切极性、`Esc` 清选区、方向键微调。
  单键快捷键（1/2/3/K/R/方向键）在焦点位于数值/文本输入控件时被 `focusInTextInput()` 守卫忽略，避免打字误触。
- **首次引导**：`showFirstRunGuide()` 一次性说明 L1/L2/L3 三种模式的区别（§5.2）。
- **非模态提示**：错误经 `notify()` 写状态栏并限时显示，不打断用户（§5.2）。

## main.cpp

创建 `QApplication`，设置应用/组织名（供后续 QSettings 持久化），用 `QCommandLineParser` 解析命令行；
若首位置参数为图像路径，窗口显示后调用 `MainWindow::openImageFromPath()` 自动打开（与菜单「打开」共用载入逻辑）。
