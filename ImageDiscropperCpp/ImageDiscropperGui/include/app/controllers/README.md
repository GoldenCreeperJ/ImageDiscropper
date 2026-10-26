# include/app/controllers — 八控制器 + 未烧录提醒共享件 + 键表分派的声明边界

**目录作用**：主窗口的**编排件声明层**——八个域控制器、单键快捷键分派器
（`CanvasCoordinator` / `SessionController` / `ExportController` / `ConfigController` / `PreprocessController` /
`PreviewController` / `AnnotationCoordinator` / `DocHistory` / `KeyRouter`）各自的类声明，外加一枚被导出与保存配置两处复用的
**未烧录提醒共享件**（`burn_in_reminder.h`，自由函数、非控制器）。
本目录只放**类声明边界**；实现在同名镜像目录 [src/app/controllers/](../../../src/app/controllers/README.md)。

**分块依据**：按**功能域**一控制器一头（与 CLI `src/commands/` 同构：装配壳同层不留编排件）。
切分铁律 = 能按「输入数据 + 输出效果」无歧义归位则独立成类；**依赖方向单向**——控制器只被主窗口持有与调用，
不得反向 include 上层，回指通道（场景 / 视图 / 面板 / 遮罩）由装配壳在构造期注入（全指针、可空），
跨件串联仍归 `MainWindow`。跨域共用的小工具**不留本层**——下沉 `util/` 自由函数（先例：`util::annotation_qt_painter.h` 的 `paintAnnotation`，供画布图元与导出预览共用）。**例外**：`burn_in_reminder.h` 虽为自由函数，但它是编排层的确认交互（弹框 + 回写偏好/面板信号）、与控制器同源，不归呈现工具，故留本层。

| 文件                         | 类                                  | 域与边界                                                                                                                                                                                                                      |
|----------------------------|------------------------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `canvas_coordinator.h`     | `CanvasCoordinator`                | 画布交互域：四态互斥模式落地 `applyCanvasMode` + 选区/单元交互写回（写回链意图 → Document → 场景高亮）+ L3 网格基准释放一次性写回                                                                                                                                     |
| `session_controller.h`     | `SessionController`                | 文档会话生命周期：打开 / 新建画布 / 添加图片图层 / 清空图层 / 重置全部 + 图层编辑面板声明态 CRUD                                                                                                                                                                |
| `export_controller.h`      | `ExportController`                 | 导出编排：重排前 / 未烧录前两道确认门（`UiPrefs` 开关 + 「下次不再提醒」复选框）→ 烧录源组装（图片图层/标注）→ 路径解析（桌面对话框 / Android 相册发布）→ Core 执行                                                                                                                     |
| `config_controller.h`      | `ConfigController`                 | 配置存取（FR-L3.8）：保存（§7 schema JSON；**未烧录提醒确认后**，烧录勾选决定段写入——图层未勾不写 layers、标注未勾不写 annotations）/ 加载（底图像素重生 + 资产解码复原 + **按存在性单向自动开对应烧录**）                                                                                        |
| `burn_in_reminder.h`       | （自由函数 `confirmBurnInBeforeExport`） | 未烧录提醒三键快捷框（`ExportController::onExport` 与 `ConfigController::onSaveConfig` 两处复用）：有标注/图层但未勾对应烧录时弹「开烧录并导出（默认）/按现状导出/取消」，受 `UiPrefs::confirmBurnInOnExport` 门控、内嵌「下次不再提醒」复选框；「开烧录并导出」经 `ExportPanel::enable*` 走信号链回写真相源并同步预览 |
| `preprocess_controller.h`  | `PreprocessController`             | 预处理编排薄壳：独立 resize + 声明式轻操作队列（声明策略位于 `model::PreprocessSession`）                                                                                                                                                           |
| `preview_controller.h`     | `PreviewController`                | 切割预览与导出前输出预览：`refreshPreview` 五分支状态机 + 结果缓存与按需渲染（A/B 规则）+ 留存「仅底图」「仅图层」全分辨率合成（`displayBaseImage`/`displayLayersImage`：状态栏取色真相源）                                                                                            |
| `annotation_coordinator.h` | `AnnotationCoordinator`            | 标注域编排：33 个标注槽 + 手势分派状态机 + 图层显隐/烧录联动                                                                                                                                                                                       |
| `doc_history.h`            | `DocHistory`                       | 文档参数态撤销/重做：防抖采集 / 抑制守卫 / 载入配置冲刷压栈 / 上下文判定 / 启用态信号                                                                                                                                                                         |
| `key_router.h`             | `KeyRouter`                        | 主窗口单键快捷键分派（全静态，`MainWindow` 的 friend）：键表 + 焦点守卫，与装配壳/控制器无耦合                                                                                                                                                               |

> 职责与改动须知见 [app/README.md](../README.md)（装配壳与偏好层）与 GUI 根
> [README.md](../../../README.md)「改动须知」第 5 条（控制器不得塞进 `main_window.cpp`、依赖方向须单向）。
