# include/app/controllers — 八控制器 + 键表分派的声明边界

**目录作用**：主窗口的**编排件声明层**——八个域控制器与单键快捷键分派器
（`CanvasCoordinator` / `SessionController` / `ExportController` / `ConfigController` / `PreprocessController` /
`PreviewController` / `AnnotationCoordinator` / `DocHistory` / `KeyRouter`）各自的类声明。
本目录只放**类声明边界**；实现在同名镜像目录 [src/app/controllers/](../../../src/app/controllers/README.md)。

**分块依据**：按**功能域**一控制器一头（与 CLI `src/commands/` 同构：装配壳同层不留编排件）。
切分铁律 = 能按「输入数据 + 输出效果」无歧义归位则独立成类；**依赖方向单向**——控制器只被主窗口持有与调用，
不得反向 include 上层，回指通道（场景 / 视图 / 面板 / 遮罩）由装配壳在构造期注入（全指针、可空），
跨件串联仍归 `MainWindow`。跨域共用的小工具**不留本层**——下沉 `util/` 自由函数（先例：`util::annotation_qt_painter.h` 的 `paintAnnotation`，供画布图元与导出预览共用）。

| 文件                         | 类                       | 域与边界                                                                                                                           |
|----------------------------|-------------------------|--------------------------------------------------------------------------------------------------------------------------------|
| `canvas_coordinator.h`     | `CanvasCoordinator`     | 画布交互域：四态互斥模式落地 `applyCanvasMode` + 选区/单元交互写回（写回链意图 → Document → 场景高亮）+ L3 网格基准释放一次性写回                                          |
| `session_controller.h`     | `SessionController`     | 文档会话生命周期：打开 / 新建画布 / 添加图片图层 / 清空图层 / 重置全部 + 图层编辑面板声明态 CRUD                                                                     |
| `export_controller.h`      | `ExportController`      | 导出编排：烧录源组装（图片图层/标注）→ 路径解析（桌面对话框 / Android 相册发布）→ Core 执行                                                                       |
| `config_controller.h`      | `ConfigController`      | 配置存取（FR-L3.8）：保存（§7 schema JSON，含标注与图层段）/ 加载（底图像素重生 + 资产解码复原）                                                                  |
| `preprocess_controller.h`  | `PreprocessController`  | 预处理编排薄壳：独立 resize + 声明式轻操作队列（声明策略位于 `model::PreprocessSession`）                                                                |
| `preview_controller.h`     | `PreviewController`     | 切割预览与导出前输出预览：`refreshPreview` 五分支状态机 + 结果缓存与按需渲染（A/B 规则）+ 留存「仅底图」「仅图层」全分辨率合成（`displayBaseImage`/`displayLayersImage`：状态栏取色真相源） |
| `annotation_coordinator.h` | `AnnotationCoordinator` | 标注域编排：33 个标注槽 + 手势分派状态机 + 图层显隐/烧录联动                                                                                            |
| `doc_history.h`            | `DocHistory`            | 文档参数态撤销/重做：防抖采集 / 抑制守卫 / 载入配置冲刷压栈 / 上下文判定 / 启用态信号                                                                              |
| `key_router.h`             | `KeyRouter`             | 主窗口单键快捷键分派（全静态，`MainWindow` 的 friend）：键表 + 焦点守卫，与装配壳/控制器无耦合                                                                    |

> 职责与改动须知见 [app/README.md](../README.md)（装配壳与偏好层）与 GUI 根
> [README.md](../../../README.md)「改动须知」第 5 条（控制器不得塞进 `main_window.cpp`、依赖方向须单向）。
