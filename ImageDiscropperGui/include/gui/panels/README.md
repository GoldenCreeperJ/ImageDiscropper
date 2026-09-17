# include/gui/panels — 参数、导出与图像处理面板对外声明

**目录作用**：GUI **面板层**的公共头，声明左侧模式 / 极性面板、右侧参数面板、导出面板与图像处理面板四个 `QWidget`。
本目录只放**类声明边界**；实现与详尽控件 / 信号说明见 [src/panels/](../../../src/panels/README.md)。

**分块依据**：四面板均**不直接调 Core**。前三个只读写 `Document`（用户操作 → 写回 `Document` →
`Document` 发 `changed()` → `MainWindow` 刷新预览；反向同步用 `blockSignals` 防回环）；图像处理面板则发「意图信号」，
由 `MainWindow` 经 `EngineBridge` 调 Core `processing` 变换工作图后写回 `Document`。按「模式选择 / 参数录入 /
导出装配 / 图像处理」四种关注点各成一文件。

| 文件 | 声明 | 职责 |
|---|---|---|
| `left_panel.h` | `LeftPanel`（`QWidget`） | 左侧：模式切换 L1/L2/L3、极性开关（保留绿 / 删除红）、工具 / 图层占位 |
| `param_panel.h` | `ParamPanel`（`QWidget`） | 右侧「参数」页：`QStackedWidget` 分 L1/L2/L3 三页；含 L2 多矩形列表与选中同步（`rectSelected` / `selectRectRow` / `updateRectListItem`） |
| `export_panel.h` | `ExportPanel`（`QWidget`） | 右侧「导出」页：输出模式 / 目录 / 文件 / 格式 / 质量 / 命名 + 合并重排（自动 cols·rows、双警告）+ 导出按钮 |
| `image_panel.h` | `ImagePanel`（`QWidget`） | 右侧「图像」页：预处理（旋转 / 翻转 / 缩放 / 尺寸 / 黑白 / 色道反色 / 色道分离 / 重置，反色与分离共用 R/G/B 通道选择），发 `*Requested` 意图信号 |

> 命名空间统一 `idc::gui`。面板不实现几何 / 切割；L2 多矩形并集、L3 网格行列推导等全部由 Core 完成（A-0.1）。
