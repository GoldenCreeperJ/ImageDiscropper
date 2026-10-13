# src/app/forms — 新建画布对话框的采参组件实现

**目录作用**：[include/app/forms/](../../../include/app/forms/README.md) 两个采参组件的实现层。
声明、采参范围与复用理由不在此重复，见该目录 README。

**分块依据**：**一组件一 TU**，与头文件同名。两个文件都是**自 `NewCanvasDialog` 原封迁出**
（控件构建 / 采参汇编 / 显隐与文案 / 取色），迁出时的唯一行为改动是把原先直接调用的
`refreshPreview` 收尾改为发 `paramsChanged()` 交上游触发——组件不得反向依赖预览编排与 Document。
数值字面量一律取 `util::gui_nums.h`，可译文字一律走 `IDC_GTR`（文案纪律见 GUI 根 README）。

| 文件                  | 内容                                                                     |
|-----------------------|--------------------------------------------------------------------------|
| `pattern_form.cpp`    | `PatternFormWidget`：控件构建、`params()` 汇编、图案相关显隐与文案、取色框 |
| `placement_row.cpp`   | `PlacementRow`：9 向锚点/枢轴条目与 X/Y 偏移行（两组重复装配合一）        |

> 上游宿主 `NewCanvasDialog`（同层，未迁出）见 [src/app/README.md](../README.md)。
