# include/app — 应用装配层对外声明

**目录作用**：GUI **应用装配层**的公共头——主窗口装配壳 `MainWindow` + 四个控制器（预览/预处理/标注/历史）+
状态栏组件 + 装配建造者 `MainWindowUi`。本目录只放**类声明边界**；实现（`.cpp`）与详尽编排说明见 [src/app/](../../src/app/README.md)。

**分块依据**：`app` 是 GUI 的装配根，只做「装配 + 信号槽编排 + 转发」，不含切割 / 几何 / 导出 / 标注实现
——Core 切割调用集中在 `model/EngineBridge`、标注调用集中在 `model/AnnotationBridge`、状态集中在 `model/Document`、渲染集中在 `canvas/`（场景图元）与 `util/`（离屏预览、标注矢量绘制）。
`MainWindow` 因此被拆为「装配壳 + 各司其职的控制器」，避免上帝文件；程序入口 `main.cpp` 无对应头，直接位于 `src/app/`。

| 文件                         | 声明                                        | 职责                                                                                                                       |
|----------------------------|-------------------------------------------|--------------------------------------------------------------------------------------------------------------------------|
| `main_window.h`            | `MainWindow`（`QMainWindow`）               | 装配壳：经 `MainWindowUi` 装配部件、实例化并注入四个控制器、串联剩余信号槽（Document 信号 / 画布交互写回 / 历史启用态回灌）、文件/编辑动作与单键快捷键；公共面为构造 + `openImageFromPath` |
| `main_window_ui.h`         | `MainWindowUi`（静态建造者，MainWindow 的 friend） | 中央区 / 菜单栏 / 工具栏 / 状态栏的纯 UI 装配，回填 MainWindow 成员指针（含帮助/关于正文与版本号回退宏）                                                        |
| `status_bar.h`             | `StatusBar`（`QStatusBar`）                 | 状态栏组件：坐标 / RGB / 模式 / 保留块数 / 缩放 / 提示六标签 + 非模态 `notify`（错误 8s、普通 4s）；纯展示，无编排逻辑                                            |
| `doc_history.h`            | `DocHistory`（`QObject`）                   | 文档参数态撤销/重做：`HistoryManager<EngineConfig>` 快照 + 500ms 防抖采集 + 还原期抑制 + 载入配置冲刷压栈 + 标注上下文判定；启用态经 `availabilityChanged` 信号回灌   |
| `preview_controller.h`     | `PreviewController`（`QObject`）            | 切割预览 `refreshPreview` 五分支状态机 + 预览底图/导出小源图重建 + 导出输出预览缓存与按需渲染（A/B 规则；渲染下沉 `util/output_preview_renderer`）                  |
| `preprocess_controller.h`  | `PreprocessController`（`QObject`）         | 预处理 8 操作编排（图像页面板与图像菜单共用同一批槽）：经 `EngineBridge` 调 Core `pixel_ops`，维度变化清失效选区/标注，耗时重采样包忙碌对话框 + 重入守卫                         |
| `annotation_coordinator.h` | `AnnotationCoordinator`（`QObject`）        | 标注域 33 槽编排 + 绘制手势分派状态机：面板/画布意图 → `AnnotationBridge`（调 Core）→ 画布/属性面板回显；烧录/图层显隐经 `PreviewController` 轻量刷新                 |

> 命名空间统一 `idc::gui`。数据流：交互 → `Document`（单一真相源）→ `EngineBridge`（切割引擎唯一入口）→ `CanvasScene` 渲染 → `MainWindow`/`StatusBar` 回显。
