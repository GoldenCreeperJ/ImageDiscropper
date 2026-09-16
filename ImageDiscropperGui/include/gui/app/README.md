# include/gui/app — 应用装配层对外声明

**目录作用**：GUI **应用装配层**的公共头，声明主窗口 `MainWindow`——顶层装配与编排入口。
本目录只放**类声明边界**；实现（`.cpp`）与详尽编排说明见 [src/app/](../../../src/app/README.md)。

**分块依据**：`app` 是 GUI 的装配根，只做「装配 + 信号槽编排 + 状态栏呈现」，不含切割 / 几何 / 导出实现
（A-0.1）——Core 调用集中在 `model/EngineBridge`、状态集中在 `model/Document`、渲染集中在 `canvas/`。
故本目录仅一个头文件；程序入口 `main.cpp` 无对应头，直接位于 `src/app/`。

| 文件 | 声明 | 职责 |
|---|---|---|
| `main_window.h` | `MainWindow`（`QMainWindow`） | 菜单 / 工具栏 / 状态栏 / 快捷键 / 首次引导 + 连接 Document↔Canvas↔Panels 的信号槽编排；`refreshPreview` 为核心刷新入口 |

> 命名空间统一 `idc::gui`。数据流：交互 → `Document`（单一真相源）→ `EngineBridge`（唯一触 Core）→ `CanvasScene` 渲染 → `MainWindow` 回显状态栏。
