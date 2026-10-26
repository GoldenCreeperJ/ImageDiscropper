# src/app/controllers — 八控制器 + 未烧录提醒共享件 + 键表分派的实现

**目录作用**：[include/app/controllers/](../../../include/app/controllers/README.md) 十个声明的**实现层**，
含 `PreprocessController` 的两处 TU 拆分与一枚 `burn_in_reminder.cpp` 共享件，共 12 个 `.cpp`。声明与本目录同名镜像目录一一对应，不在此重复。

**分块依据**：**一控制器一 TU**；单类实现体量过大时按**域**继续拆 TU（文件名后缀即域名，
类名不变、头文件不变，仅 CMake 源列表增加一项）——先例：`preprocess_controller_canvas.cpp`（多层画布域）与
`preprocess_controller_commit.cpp`（提交链域）。跨件串联（Document 信号 / 状态栏回报 / 历史启用态回灌 /
菜单勾选态回同步）**留在 `MainWindow`**，不得下沉到本目录任何控制器。

| 文件                                 | 内容                                                                                                         |
|------------------------------------|------------------------------------------------------------------------------------------------------------|
| `canvas_coordinator.cpp`           | 画布四态落地 `applyCanvasMode` 与选区/单元交互写回编排；含 `onGridParamsEdited`（L3 网格基准拖拽释放后一次性 `Document::setGridParams` 写回） |
| `session_controller.cpp`           | 会话生命周期编排与图层编辑面板接线（打开/新建/加层/清空/重置）                                                                          |
| `export_controller.cpp`            | 导出编排：烧录源组装 → 路径解析 → Core 执行                                                                                |
| `config_controller.cpp`            | 配置存取编排：§7 schema JSON 保存 / 加载复原                                                                            |
| `burn_in_reminder.cpp`             | 未烧录提醒三键框实现（`confirmBurnInBeforeExport`）：依 `UiPrefs::confirmBurnInOnExport` 门控、内嵌「下次不再提醒」复选框；导出与保存配置两处复用    |
| `preprocess_controller.cpp`        | 预处理编排薄壳：独立 resize + 声明式轻操作队列的调度                                                                            |
| `preprocess_controller_canvas.cpp` | `PreprocessController` 的**多层画布域**（TU 拆分）：图层会话物化缓存刷新等                                                       |
| `preprocess_controller_commit.cpp` | `PreprocessController` 的**提交链域**（TU 拆分）：`commitPipeline` 等编辑公共提交                                           |
| `preview_controller.cpp`           | `refreshPreview` 五分支状态机 + 导出输出预览缓存与按需渲染 + 重建时留存「仅底图」「仅图层」全分辨率合成（状态栏取色真相源）                                  |
| `annotation_coordinator.cpp`       | 33 个标注槽 + 手势分派状态机 + 图层显隐/烧录联动                                                                              |
| `doc_history.cpp`                  | 撤销/重做栈控制：防抖采集 / 抑制守卫 / 载入配置冲刷压栈 / 上下文判定 / 启用态信号                                                            |
| `key_router.cpp`                   | 键表分派与焦点守卫（全静态，`MainWindow` 的 friend）                                                                       |

> 控制器归属与依赖方向的规约见 GUI 根 [README.md](../../../README.md)「改动须知」第 5 条；
> 装配壳与偏好层见 [src/app/README.md](../README.md)。
