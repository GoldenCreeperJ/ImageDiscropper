# app/ — 主窗口装配壳、八个控制器与程序入口

顶层装配与编排。MainWindow 只做「装配 + 编排 + 转发」，不含切割/几何/导出实现——
Core 调用集中在 `EngineBridge`，状态集中在 `Document`。为避免 `main_window.cpp` 成为上帝文件，
编排按域拆到八个控制器，纯 UI 装配与 UI 状态回同步拆到 `MainWindowUi`，单键快捷键分派拆到
`KeyRouter`，状态栏拆为 `StatusBar` 组件。八个控制器 + 键表分派 `KeyRouter` 位于 `controllers/` 子目录（清单见 [controllers/README.md](controllers/README.md)），菜单栏装配四 TU 位于 `menus/` 子目录（见 [menus/README.md](menus/README.md)），对话框（外观/交互 + 新建画布）位于 `dialogs/` 子目录（见 [dialogs/README.md](dialogs/README.md)），新建画布对话框的采参组件 `pattern_form`/`placement_row` 在 `forms/` 子目录（见 [forms/README.md](forms/README.md)），与 CLI `src/commands/` 同构：装配壳同层只留 main_window/main_window_ui/status_bar/palette/canvas_prefs/app_prefs/keybindings/main。

| 文件                                                                         | 职责                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           |
|----------------------------------------------------------------------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `main_window.{h,cpp}`                                                      | 装配壳：装配 + 实例化/注入八个控制器 + 剩余跨件串联（Document 信号 / 状态栏回报 / 历史启用态回灌 / 菜单勾选态回同步）+ 模式/极性动作 + 撤销重做路由 + 快捷键事件入口转发；构造拆为「骨架装配 → `initCore()` 接线」两段，`initCore` 为桌面/移动（mobile/MobileShell）逐行共用的唯一接线序列；`openImageFromPath` 保留一行转发（main.cpp 公共面）                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| `main_window_ui.{h,cpp}`                                                   | `MainWindowUi` 静态建造者与 UI 状态同步（MainWindow 的 friend）：buildCentral/buildToolbar/buildStatus 装配 + `syncUi`/`syncCanvasModeActions`/`syncToolActions`/`syncAnnoSelActions` 全量与定向回同步（`MainWindow::syncPanels` 转发至此）；buildMenus 按菜单域拆四个 TU 于 `menus/`（见 `menus/main_window_menus*.cpp`），私有静态 helper 声明为类成员以继承 friend 权限                                                                                                                                                                                                                                                                                                                                                                                                                                                             |
| `menus/main_window_menus*.cpp`（四 TU）+ `menus/main_window_menus_internal.h` | `MainWindowUi::buildMenus` 按菜单域四 TU：`main_window_menus.cpp`（文件/编辑 + 总装配 + 共享绑定助手 bindAddAction/bindAddMenu 定义）、`_mode`（模式/预处理）、`_canvas`（图片/标注）、`_view`（视图/帮助，含帮助/关于正文）；绑定助手声明集中 `main_window_menus_internal.h`（私有内部头，四 TU 共用）                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| `controllers/canvas_coordinator.{h,cpp}`                                   | `CanvasCoordinator`：画布交互域——四态互斥模式落地 `applyCanvasMode` + 选区/单元交互写回（框选/拖改/点选/框选/调序/微调/清选区/全选反选清空/L3 网格基准一次性写回）+ 状态栏交互提示；画布/场景/参数面板/工具面板相关信号接线住本控制器 `connectSignals`                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            |
| `controllers/session_controller.{h,cpp}`                                   | `SessionController`：文档会话生命周期——打开/新建画布/添加图片图层/清空图层/重置全部（重置全部确认框内嵌「同时删除底图」复选框，勾选则 `resetToEmpty()` 连底图删至空白）+ 图层编辑面板声明态 CRUD 接线（含图层面板图片图层整体显隐开关）                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                |
| `controllers/export_controller.{h,cpp}`                                    | `ExportController`：导出编排——烧录源组装（图片图层/标注）→ 路径解析（桌面对话框 / Android 相册发布）→ EngineBridge 导出 → 结果提示；Android 发布子编排在 `mobile/android_export_publish`                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| `controllers/config_controller.{h,cpp}`                                    | `ConfigController`：配置存取编排——保存（§7 schema JSON，含标注与图层段）/ 加载（底图像素重生 + 资产解码 + 历史压栈可撤销 + 预处理队列重放 + 标注还原）                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          |
| `controllers/key_router.{h,cpp}`                                           | `KeyRouter`（全静态，MainWindow 的 friend）：单键快捷键分派（1/2/3 模式、K/R 极性、Esc 清选区、Delete/Backspace 删标注）+ 数值/文本输入焦点守卫；`MainWindow::keyPressEvent` 只留转发                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     |
| `status_bar.{h,cpp}`                                                       | `StatusBar`：四标签（缩放/坐标/像素RGBA/提示）+ 非模态 `notify`；当前模式名与保留块数并入提示线（不占独立标签）；`setCompactMode` 供移动端隐藏坐标/RGBA/提示、只留缩放（SPEC §8.3）                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     |
| `palette.{h,cpp}`                                                          | `Palette`（QObject 单例）：画布六项核心语义色（保留/删除遮罩、选区、标注、网格、背景）+ 三项填充 alpha 的读写与 QSettings **应用级**持久化；改色发 `changed()` 联动画布重绘。渲染图元（canvas/items/*）绘制期取色。纯呈现偏好，不进 §7 配置、不进撤销栈                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             |
| `canvas_prefs.{h,cpp}`                                                     | `CanvasPrefs`（QObject 单例）：画布**交互/视图偏好**（与 Palette 分家、各存 `canvas/` 组）——选区吸附开关（**运行时可切换**）、手柄/命中屏幕基准（0=跟随平台默认、>0 覆盖）、内容外滚动余量（视口各边百分比，0=不留）、缩放步进与上下限区间、新建默认尺寸的读写与 QSettings **应用级**持久化；需即时联动的 setter 经 `touched()`＝`save()`+发 `changed()`（`CanvasView` 借此重算手柄与滚动余量/钳制缩放/回设吸附），`rememberNewCanvasSize` 只落盘不发。取值器全内联头文件，本件只变更路径；纯呈现/交互偏好，不进 §7、不进撤销栈                                                                                                                                                                                                                                                                                                                                                                                                                              |
| `app_prefs.{h,cpp}`                                                        | `UiPrefs`（**非 QObject** 单例，存 `ui/` 组）：GUI **应用级会话记忆/杂项交互偏好**——最近打开文件 MRU（有序、上限 8、`content://` 不入）、上次打开目录 / 上次导出目录（文件对话框起始种子）、导出「重排前二次确认」（`confirmRearrangeRisk`）与「未烧录时导出前提醒」（`confirmBurnInOnExport`）开关（均默认开）的读写与 QSettings 持久化；无即时联动消费者，故不发 `changed()`（由「外观/交互→其他→确认与吸附」复选框即时写、导出/保存配置调用点即时读），变更即落盘。纯呈现/交互偏好，不进 §7、不进撤销栈                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| `keybindings.{h,cpp}`                                                      | `KeyBindings`（QObject 单例）+ `CommandId`/`KeyKind`/`CommandDef`：快捷键**命令注册表**数据层（与 Palette/CanvasPrefs/UiPrefs 同属应用级偏好单例、走 QSettings `shortcuts/` 组）——共 15 条命令（8 组合键 `Action` + 7 画布单键 `Route`），`Action` 类经 `QAction::setShortcut` 原生处理、`Route` 类**不注册 QAction/QShortcut**、交 `KeyRouter` 表驱动分派；`effective/setSequence/resetOne/resetAll/matchRoute/conflictOf/registerAction`，改后发 `changed(id)`；不进 §7、不进撤销栈                                                                                                                                                                                                                                                                                                                                                                     |
| `dialogs/appearance_dialog.{h,cpp}`                                        | `AppearanceDialog`（QDialog）：「视图→外观/交互…」对话框（移动「更多」同源），**QTabWidget 三页**——① 配色页（六色取色 + 三 alpha 写 Palette，含**深浅色主题下拉**写 UiPrefs·选中即 theme::apply + 发 themeApplied）；② 其他页（交互七数值框写 CanvasPrefs + **「确认与吸附」组三复选框**（吸附写 CanvasPrefs、未烧录提醒/重排前二次确认写 UiPrefs，即时读写）+ 应用语言三选下拉写 UiPrefs·即时热切换 + **默认输出文件夹**〔仅桌面〕写 UiPrefs）；③ 快捷键页（仅桌面，移动壳传 `shortcutsEnabled=false` 关此页）：依 KeyBindings 注册表逐行 QKeySequenceEdit 重绑 + 冲突改绑询问 + 行内/全量恢复默认；内容包 QScrollArea + 恢复默认（**只回退配色与交互两单例（含吸附），不重置语言/主题/输出目录/快捷键/确认偏好**）；自身不持偏好态（单一真相源在 Palette/CanvasPrefs/UiPrefs/KeyBindings 四单例），颜色/交互改动即时写对应单例（生效+落盘+联动）、语言选定即调 setupFromPreference 切全局 + 发 languageApplied 令宿主主窗原地重放；选区吸附与两道导出确认开关均居本页「确认与吸附」组（菜单不再单列）；实现按页拆四 TU：装配壳 `appearance_dialog.cpp`（tab 组装/恢复默认/重放）+ `_colors`/`_other`/`_shortcuts` 三页 |
| `dialogs/appearance_dialog_{colors,other,shortcuts}.cpp`                   | `AppearanceDialog` 按页三 TU：`_colors`（配色页 buildColorsTab + 取色/色块回显）、`_other`（其他页 buildOtherTab + 交互回显）、`_shortcuts`（快捷键页 buildShortcutsTab + 改绑校验/冲突报告）；装配壳 `dialogs/appearance_dialog.cpp` 只做 tab 组装与恢复默认/重放                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  |
| `controllers/doc_history.{h,cpp}`                                          | `DocHistory`：文档参数态撤销/重做（防抖采集/抑制守卫/载入配置冲刷压栈/上下文判定/启用态信号）                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      |
| `controllers/preview_controller.{h,cpp}`                                   | `PreviewController`：refreshPreview 五分支状态机 + 导出输出预览缓存与按需渲染（A/B 规则）+ 画布右键遮罩切换 `onToggleMasks`（同步图层面板复选框）                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       |
| `controllers/preprocess_controller_commit.cpp`                             | `PreprocessController` 的「提交链」域实现（TU 拆分）：commitPipeline（geomSig 尺寸门 + base_ 缓存重放 + 回灌）/commitActive（按目标提交）/忙碌框（runInBusyDialog + runWithBusyDialog）                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           |
| `controllers/preprocess_controller_canvas.cpp`                             | `PreprocessController` 的「多层画布」域实现（TU 拆分）：syncLayerMaterialCache/recomposeLayerCanvas/composeLayerCanvasForExport                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             |
| `controllers/preprocess_controller.{h,cpp}`                                | `PreprocessController`：预处理编排薄壳（声明策略位于 `model::PreprocessSession`）——轻操作走 stepAppend/Replace/Remove/Move/reset 五信号 + 菜单「追加一步」入口，画布尺寸走 onSetResizeTarget/onScaleResize/onClearResize；两路径均汇入 `commitPipeline`（先调 session 静态策略算候选态）几何签名尺寸门 + `base_` 缓存复用重放；忙碌框仅在有 resize 需重采样时弹；`setBusyOverlayMode` 供移动端改全屏遮罩（SPEC §8.2）                                                                                                                                                                                                                                                                                                                                                                                                                                                        |
| `controllers/annotation_coordinator.{h,cpp}`                               | `AnnotationCoordinator`：标注 33 槽 + 手势分派状态机 + 图层显隐/烧录联动                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        |
| `dialogs/new_canvas_dialog.cpp`                                            | 「文件→新建画布」与「添加图片图层」共用的模态对话框实现（SPEC §2.2a 采参视图）：三段纵向折叠（节点外观/图案元素/图片元素节，节内左右并排）、外观组三实例（几何/色彩两区列表、按语境分化）、预览独立固定列 + 可拖分隔条、`accept()` 经 `EngineBridge` 产全尺寸画布（采参子件在 `forms/`）                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      |
| `forms/pattern_form.cpp`                                                   | 图案采参表单实现（两语境共用），见 [forms/README.md](forms/README.md)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         |
| `forms/placement_row.cpp`                                                  | 「锚点 / 枢轴 / 自由偏移」落位行实现（两组重复装配合一），同上 [forms/README.md](forms/README.md)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        |
| `main.cpp`                                                                 | 程序入口（双产物各编一份：`idc_gui` 纯桌面 / `idc_gui_mobile` 经宏恒选移动壳；Android 单目标），含 `QApplication` + 事件循环                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |

## 布局

`QSplitter` 水平三分：**左侧可滚动容器（QScrollArea 竖排 LeftPanel + ModeToolPanel + LayerPanel）** | **中央画布（CanvasView）** | **右侧 QScrollArea 包裹的 AccordionPanel（图片页 / 预处理页 / 标注页 / 参数页 / 导出页，多节可同开）**。装配细节在 `MainWindowUi::buildCentral`（创建顺序、`setDocument`/`setModel` 注入、初始分割 {330,860,375}）。

## 编排（连接住在属主旁）

构造顺序（承重，不可重排）：骨架装配（桌面 `MainWindowUi` 四步；移动 `MobileShellUi` 同模式）→ `MainWindow::initCore()`：
依次实例化并注入八个控制器（各自 `connectSignals()`）→
`DocHistory::start()` → `MainWindow::connectAll()` → `syncPanels` → `refreshPreview` → `history_->reset()`（发
`availabilityChanged` → 撤销/重做动作置灰，故 **各控制器 connectSignals 与 connectAll 必须先于 reset**）→ 初始态提示。
**本序列桌面/移动逐行共用**（SPEC §8.3 行为同构的结构性保证；差异只在骨架装配）。

**MainWindow::connectAll（剩余跨件串联；各域接线住在属主控制器）**：

```
Document.imageChanged → onImageChanged → preview_->rebuildPreviewPixmap + syncPanels + preview_->refreshPreview
Document.changed      → onDocChanged   → preview_->refreshPreview + syncPanels（拖拽期跳过）
Document.layersChanged → preprocess_->recomposeLayerCanvas（多层画布重组）
CanvasView.cursorScenePos → onCursor（状态栏坐标 + 像素 RGBA；取色按「底图/图片图层」两个独立图元的显隐四态选源：
    均可见→working()、仅底图→displayBaseImage、仅图层→displayLayersImage（透明像素即空白→清色）、均隐藏→清色，保证所见＝所取）
CanvasView.zoomChanged → onZoomChanged（状态栏缩放倍数；所有缩放入口汇聚于 updateHandleSize 发出）
CanvasView.resetViewRequested → view_->fitToWindow
ExportPanel.layersBurnInChanged / burnInChanged / collapsibleChanged → 菜单「导出设置」勾选态/置灰回同步
    （落地连接在 ExportController / AnnotationCoordinator；初始同步 setLayersBurnInChecked）
DocHistory.availabilityChanged → onHistoryAvailabilityChanged → aUndo_/aRedo_ setEnabled
ModeToolPanel.modeChanged → MainWindowUi::syncCanvasModeActions（画布模式菜单勾选态；CanvasCoordinator 的模式落地
    连接先于本条——先 applyCanvasMode 落地、再读视图态回同步）
AnnotationBridge.changed/selectionChanged/toolChanged → history_->updateEnabled（标注上下文变化 → 重算启用态）
    + MainWindowUi::syncToolActions / syncAnnoSelActions（标注工具/叠放/填充菜单勾选态回同步）
```

**各控制器自持的连接**：

- `DocHistory::start()`：`Document.changed → scheduleCapture`（500ms 防抖采集）。
- `PreviewController::connectSignals()`：`rightTabs currentChanged → onRightTabChanged`（导出页切回补渲染）+
  `view_.toggleMasksRequested → onToggleMasks`（画布右键遮罩切换，同步图层面板复选框）。
- `PreprocessController::connectSignals()`：`PreprocessPanel` 轻队列意图信号（`stepAppendRequested`/`stepReplaceRequested`/`stepRemoveRequested`/`stepMoveRequested`/`resetRequested`）+ 独立尺寸区意图信号（`resizeAppliedRequested`/`resizeScaleRequested`/`resizeClearedRequested`）→ 对应槽。
- `CanvasCoordinator::connectSignals()`：画布/场景/参数面板/工具面板意图信号——`rubberSelect`/`nudgeSelection`/
  `clearCutRequested`/`selectionEdited`/`multiRectEdited`/`cellToggled`/`cellsMarqueeSelected`/`cellReordered`/
  `rectSelected`/`modeChanged`/`imageLayerPressed`/`basePickRequested`/`pickDeselectRequested`/`imageLayerMoved`/
    `gridParamsEdited`（L3 网格基准手柄释放：一次性 `Document::setGridParams`，拖拽中不写 Document） → 对应槽。
- `SessionController::connectSignals()`：`LayerEditPanel` 9 意图信号（层序/落位/画布尺寸/添加/清空/不透明度）+
  `LayerPanel.imageLayerVisibilityChanged` → Document 声明态 CRUD。
- `ExportController::connectSignals()`：`ExportPanel.exportRequested → onExport`。
- `ConfigController`：无面板信号（菜单/移动「更多」动作直连槽）。
- `AnnotationCoordinator::connectSignals()`：标注域全部（`AnnotationBridge` 4 信号、`CanvasView` 6 手势、`CanvasScene` 4 标注、
  `ModeToolPanel`/`LayerPanel`（7，含单元编号 lambda）/`ExportPanel.burnInChanged`/`AnnotationPropPanel` 6），并做
  `exportPanel_->setBurnInChecked(anno->burnInEnabled())` 初始同步。
- `MainWindowUi`：全部菜单/工具栏动作触发（预处理菜单 lambda 直调 `preprocess_->on*`，标注菜单 lambda 直调 `annotation_->onAnno*`）。

`PreviewController::refreshPreview()` 是核心：无图→清空；无选区→提示拖拽（不跑引擎，避免 E-1 噪声）；
有选区→`buildEngineConfig` + `EngineBridge.runPreviewGeometry`（以工作图尺寸仅跑几何、忽略 preprocess）→ 刷新切割线（`EngineBridge::cutLines`）/遮罩/选区，
并回写状态栏提示线（`StatusBar::setHint`，模式名与保留块数折入其中）与导出面板预览信息（`ExportPanel::setPreviewInfo`：保留块数、输出画布尺寸、坍缩可行性）。
L2 MULTI_RECT 分支：`scene_->updateMultiRects(doc_.rects())` 增量刷新可拖拽选区框（不 clear+重建）。
拖拽期间（`isDraggingSelection()` 或 `isDraggingMultiRect()`）跳过 `syncPanels()` 与对正在拖图元的回设，避免逐帧量化抖动与卡顿；释放时照常同步一次。
重排上下文回灌：顶部先 `exportPanel_->setRearrangeContext(0,0,0)` 清零（非 L3），L3 分支再回灌真实的
保留块数、网格单元宽/高与**保留块是否长宽全等**（`res.uniformBlocks`，供导出面板自动 cols/rows、警告判定与
重排准入门限：块尺寸不全等时面板从重排回退到坍缩/分离）。

**输出图像预览**：每个分支在 `scene_->updateMasks(...)` 后调 `updateExportPreview(res)`——渲染**位于 `util/output_preview_renderer`**，`PreviewController` 只做编排：缓存 `lastComposition_/lastExportResOk_`，导出页为当前可见节时才渲染、否则置 `exportPreviewDirty_` 待切回补渲染（**不重跑 Core**）；烧录可见时经 `util::bakeAnnotationsInto` 把标注烘焙到小源图 `exportSrcPixmap_`（最长边 ≤512，只烘焙一次），故原图 8000×8000 也只在小图上运算。机制详见 Gui README「关键设计决策」#12。

`onExport()` 在 `buildEngineConfig` 后、解析路径前调 `exportPanel_->rearrangeWarning()`：若重排参数存在风险
（cols×rows < 保留块数、或单元宽/高 < 网格单元宽/高）则弹 `QMessageBox::warning` 二次确认，选 No 则中止导出。
该模态确认受 `UiPrefs::confirmRearrangeRisk()`（「外观/交互→其他→确认与吸附」复选框，默认开）门控，关掉则跳过直接导出；弹框内嵌「下次不再提醒」复选框，勾选即关该偏好。导出文件/目录对话框以 `UiPrefs::lastExportDir()` 为起始目录并在选定后回写。
**未烧录提醒**：`onExport()` 在重排确认后、解析路径前调 `confirmBurnInBeforeExport()`（共享件 `controllers/burn_in_reminder`，`ConfigController::onSaveConfig` 亦复用）：若存在标注/叠加图层但未勾对应烧录，弹「开烧录并导出（默认）/按现状导出/取消」三键框，受 `UiPrefs::confirmBurnInOnExport()`（「外观/交互→其他→确认与吸附」复选框，默认开）门控、同样内嵌「下次不再提醒」；「开烧录并导出」经 `ExportPanel::enableBurnIn/enableLayersBurnIn` 走信号链写回真相源并同步预览，取消则中止。保存配置时烧录勾选还决定 §7 是否写入 layers/annotations 段，载入则按存在性单向自动开对应烧录。
**导出烧录**：解析路径后、调引擎前，若 `annoBridge_.burnInEnabled() && count()>0` 则 `exportSrc = annoBridge_.burnIn(doc_.working())`
（以当前工作图为底逐个调 Core `rasterize` 合成标注）再送 `EngineBridge::exportResult(exportSrc, ...)`；否则直接送 `doc_.working()`。切割预览几何不受标注影响（标注随像素被切开）。

## 预处理（FR-1）——PreprocessController（声明策略位于 PreprocessSession 的编排薄壳）

工作图恒为「轻操作队列(resize(original))」：**resize 不入队列**，为常驻独立尺寸区，永远作用于原图重采样、恒最先重放；`steps_`（与 `resize_`/`base_` 一同内聚于 `PreprocessSession`，由 `Document` 组合持有）仅承载 黑白/反色/色道分离/旋转/翻转 的可增删改上下移。**geo→color 不变式追加/块内移动限幅/载入归一化/`makeEffective`/`baseDirtyFor` 等声明策略均为 `PreprocessSession` 的静态纯策略与实例查询**，控制器只“克隆候选态 → 调这些策略 → 物化 → 回灌 `Document`”，不自持预处理态。
`PreprocessPanel` 作轻队列编辑器发意图信号（`stepAppendRequested(cfg)`/`stepReplaceRequested(index,cfg)`/`stepRemoveRequested(index)`/
`stepMoveRequested(index,down)`/`resetRequested()`）→ `PreprocessController` 对应槽；菜单/快捷键入口（`onRotate`/`onFlip`/`onGray`/`onInvert`/`onSplit`）均委托「追加一步」（`onStepAppend`）。
独立尺寸区发 `resizeAppliedRequested(w,h)`/`resizeScaleRequested(factor)`/`resizeClearedRequested()` → `onSetResizeTarget`/`onScaleResize`/`onClearResize`（“按比例”经当前输出尺寸换算为绝对目标）。
两条路径均汇入公共收尾 `commitPipeline(nextSteps, nextResize, okMsg, busyText)`：候选有效管线/前缀经 `PreprocessSession::makeEffective` 组装（`[resize?]+steps_`），
**先比较几何签名 `geomSig` 前后变化**（尺寸或净旋转象限变 → 先清失效框选；标注/图层/色彩预处理恒保留），再复用 `base_`＝`resize(original)` 缓存、仅重放轻后缀经 `EngineBridge::applyPipeline` 物化 `working_`（`setWorkingImage`）。
**原图始终保留**，resize 恒从原图重采样（不累积误差）；轻编辑不改 `base_`，仅重放轻后缀。`applyLoadedQueue(pipeline)` 委托 `PreprocessSession::normalizeLoaded` 将数组内 `ResizeOp` 归一化抽出为独立 resize、余下重排 geo 前 color 后再重放。
反色 / 分离共用 R/G/B 复选框作通道选择器（`PreprocessConfig = variant<GrayOp,SplitOp,InvertOp,RotateOp,FlipOp,ResizeOp>`）。
仅当确有 resize 需重采样（`baseDirty && 前缀非空`）时，经 `runWithBusyDialog` 弹**应用级模态、不可取消**的 `QProgressDialog`（父窗口为注入的 MainWindow；`busyResample_` 重入守卫）——无 resize 的轻编辑不弹框。
流程详见 Gui README「预处理与标注数据流」。

## 标注——AnnotationCoordinator

`AnnotationCoordinator` 持有 `AnnotationBridge*`（标注域桥，驱动标注编辑会话 `AnnotationEditor`），只做编排：把面板/画布意图转交模型，
再把模型变化下发画布与属性面板（不含几何/光栅化）。

```
AnnotationBridge.changed         → onAnnoBridgeChanged     → scene_->updateAnnotations + annoPropPanel_->syncFromModel
                                                                 + 烧录开启时 preview_->refreshExportPreviewFromCache（B 规则，不重跑 Core）
AnnotationBridge.selectionChanged→ onAnnoSelectionChanged → 同上（高亮选中项）
AnnotationBridge.toolChanged     → onAnnoToolChanged      → toolPanel_->syncFromModel + view_->setAnnotationDrawActive(tool!=Select)
ModeToolPanel.toolSelected       → onToolSelected         → 先 commitPath 收笔→ anno_->setTool
CanvasView.annoDragStart/Move/End→ onAnnoDrag*           → 依工具分派：两点形状 begin/update/commit；折线·画笔 begin/append/commit；文字 QInputDialog 取文本→addText
CanvasView.annoEscape           → onAnnoEscape           → 有草稿则 commitPath，否则 cancelPending
CanvasScene.annotationSelectRequested → onAnnotationSelect → anno_->selectAt（Core hitTest）
CanvasScene.annotationMoved     → onAnnotationMoved      → 下标校验后 anno_->moveSelectedBy（Core 平移）
LayerPanel.*VisibilityChanged   → scene_->setBaseVisible/setMasksVisible/setGridLinesVisible/setSelectionVisible/setAnnotationsVisible
                                                                 + anno_->setLayerVisible（网格线与切割线同一开关、与选取边框均再 preview_->refreshPreview 门控重建）
ExportPanel.burnInChanged       → onBurnInChanged        → anno_->setBurnIn + preview_->refreshExportPreviewFromCache
AnnotationPropPanel.colorPicked/stroke/fill/text/fontSize → anno_->setColor/setStrokeWidth/...（toCoreColor 转色）
```

- **绘制态门控**：Select 工具时 `setAnnotationDrawActive(false)`，画布维持既有橡皮筋选区/单元交互；任一绘制工具时置 true，左键拖拽路由到标注绘制。
- **隐藏即禁用（图片/标注图层）**：两图层隐藏时 `MainWindow::syncPanels` → `MainWindowUi::syncLayerAvailability` 把对应入口整域置灰——画布模式动作与工具面板按钮（图片→图片拾取，标注→标注选取/绘制）、右侧属性页（`layerEditPanel_`/`annoPropPanel_`）、整域菜单（`mLayers_`/`mAnno_`）；若当前正处被禁模式则回落框选。信号源：`LayerPanel::imageLayerVisibilityChanged`/`annotationVisibilityChanged` → `syncPanels`。
- **base 同步**：`openImageFromPath` 载新图后 `annoBridge_.setBaseImage(doc_.working())`（新图＝新标注会话）；`onImageChanged` 末尾 `scene_->updateAnnotations(annoBridge_)`（预处理恒不清标注）；`PreprocessController::commitPipeline` 尺寸门仅清框选（不清标注/图层）。
- **撤销/重做**：标注菜单驱动 `anno_->undo()/redo()`（→ Core 分层快照，**不绑 Ctrl+Z/Y**）；Delete 键（经 `MainWindow::keyPressEvent` 转发）/菜单删除选中→`removeSelected()`；清除全部二次确认（父窗口为注入的 MainWindow）→`clearAll()`。
- **对话框父窗口**：文字输入 `QInputDialog` 与清除确认 `QMessageBox` 均以注入的 `dialogParent_`（MainWindow）为父，保证模态落位。

## 全局撤销/重做与配置文件——DocHistory

**双历史 + 上下文路由（不改 Core）**：①文档参数态用 `HistoryManager<EngineConfig> history_`
（快照＝`buildEngineConfig`、还原＝`applyEngineConfig`，与配置加载同构；栈顶恒为当前态）；②标注沿用 GUI `AnnotationEditor`
内建条目级会话态。`Document::changed()` 重启 **500ms 防抖定时器**（成员 `QTimer`）把拖拽/连点合并为一条历史，还原期 `suppress_` 防回环；
换图 / 构造时 `reset()` 以当前态为唯一基线。`Ctrl+Z/Y` 经 `MainWindow::onUndo/onRedo` 依 `annotationContextActive()` **上下文路由**
（标注上下文→`AnnotationCoordinator::onAnnoUndo/Redo`，否则→`DocHistory::undo/redo`）；启用态经 `availabilityChanged`
回灌 `MainWindow::onHistoryAvailabilityChanged` 动态启停 `aUndo_/aRedo_`。**预处理不纳入全局撤销**（快照仅参数态），
靠删改队列步骤 / 「重置预处理」回退。配置经 `EngineBridge::saveConfig/loadConfig` 委托 Core（保存走 `Document::buildSerializableConfig`、携带 `preprocess_.effectivePipeline()`（`[resize?]+steps_`）与 `annoBridge_.set()` 标注段），
加载经 `DocHistory::recordConfigLoad`（冲刷防抖 → 抑制采集地 `applyEngineConfig`→ 参数还原后由 `ConfigController::onLoadConfig` 调 `PreprocessController::applyLoadedQueue` 回灌预处理队列）
且**入同一撤销栈**（可一步撤销）；标注段先收进**临时层**，队列重放**之后**才 `AnnotationBridge::replaceAnnotations` 覆盖真实层
（顺序纪律：先重放预处理、再还原标注；还原不入文档撤销栈，见 Gui README 决策 #11）。
机制详见 Gui README「关键设计决策」#10/#11。

## 菜单 / 工具栏 / 快捷键（MainWindowUi 装配）

定位原则：**菜单栏＝应用几乎全部功能的入口，工具栏＝常用功能快速入口，主面板＝核心交互与复杂场景**。
面板与菜单镜像一律「面板/模型为真相源，菜单经共享 QAction 或面板公共入口回写 + `syncPanels` 回同步勾选态」。

- **菜单**：文件（新建画布/打开/导出/**导出设置（输出方式：分离/坍缩/重排；格式：PNG/JPG/WebP/BMP；烧录标注/烧录图片图层——
  经 `ExportPanel::selectExportMode/selectFormat/setBurnInChecked` 走与面板同一路径，含重排 auto-grid；置灰规则与面板
  `refreshModeItemStates` 一致：坍缩依 Core 坍缩可行性（`collapsibleChanged` 回灌）、重排需 L3 且保留块长宽全等）**/加载配置/保存配置/退出）、
  编辑（**全局撤销/重做 Ctrl+Z/Y**、清除选区、**L3 全选/反选/清空单元**（仅 L3 启用）、重置全部）、
  **模式（L1/L2/L3 + 极性保留/删除框内——与工具栏共用同一批 `QAction`；画布模式子菜单（框选/图片拾取/标注选取/标注绘制，
  经 `ModeToolPanel::setMode` 走同一 modeChanged 路径）；切割参数子菜单：L1 形状（矩形/横带/竖带，仅 L1）、L2 方式（十字/横线/竖线/
  多矩形并集剔除，仅 L2）、转为网格模式编辑（仅 L2 且有选区）、L3 排序（横优先/竖优先/自定义 + 整体逆序/蛇形，仅 L3）、
  L3 余量（丢弃残缺/保留残缺/补白，仅 L3，勾选态随「参数」页余量下拉回同步）、
  **网格定义（两级：横向/纵向 → 周期/分割数/比例，仅 L3，勾选态随参数页两轴「定义方式」下拉回同步；
  只切形态不填数值——经 `ParamPanel::setGridAxisMode` 走面板同一写回路径，数值仍在参数页输入）**——子菜单按
  当前切割模式启停）**、预处理（仅预处理：左转/右转/180°、水平/垂直翻转、黑白、**反色（7 种按通道组合：全通道/红+绿/
  红+蓝/绿+蓝/仅红/仅绿/仅蓝，任意组合去预处理面板）**、**色道分离（6 种常用保留组合）**、
  重置预处理）、**图片（多层画布：添加图片图层/清空全部图片图层 + 上移/下移/移至顶/底层/删除 +
  **锚点/枢轴（各 9 向，作用画布当前拾取层落位 `transform`，无拾取层置灰，勾选态随拾取层回同步）**）**、标注（撤销标注/重做标注（不绑快捷键）/删除选中/清除全部/**工具子菜单 13 种绘制形状（镜像工具
  面板，非绘制态自动切「标注绘制」画布模式）**/**叠放次序（上移/下移/置顶/置底，无选中置灰）**/填充开关（勾选态随选中项或默认值
  回同步））、视图（缩放/适应/重置/**图层显隐 7 项 checkable：底图/图片图层/标注/遮罩/网格线与切割线（同一开关）/选区边框/单元编号——与图层面板复选框双向镜像，复选框为唯一真相源**）、帮助（使用说明/关于）。
  **无「定位到某面板」类菜单项**（右侧折叠面板多节可同开、无需定位）。
  动态置灰：无图像时预处理菜单动作（含反色/分离子菜单项）统一置灰；当前预处理目标会话已产出灰度时「黑白」「色道分离」置灰（与预处理面板
  `activeSession().producesGray()` 同源）。
- **工具栏**：新建/打开/导出、**加载配置/保存配置**、**撤销/重做**、放大/缩小/适应、重置全部。
  （模式切换 L1/L2/L3 与极性保留/删除框内均不走工具栏，各走左栏「模式」组或「模式」菜单；遮罩等图层显隐开关位于左侧「图层」面板，视图菜单与画布右键仍保留快捷切换。）
- **快捷键**（`MainWindow::keyPressEvent`）：`Ctrl+O`/`Ctrl+S`/`Ctrl+N`、`Ctrl +`/`Ctrl -`/`Ctrl+0`、`1`/`2`/`3` 切模式、`K`/`R` 切极性、
  `Esc` 清选区（绘制态下由画布先行收笔/取消预览）、`Delete`/`Backspace` 删除选中标注、`Ctrl+Z`/`Ctrl+Y` **全局撤销/重做（上下文路由：标注上下文→标注撤销，否则→文档参数撤销）**、方向键微调。
  `Esc` 不注册为 `QAction` 快捷键（会与画布绘制态收笔在快捷键表互相抢占），由 `keyPressEvent` 统一分派。
  单键快捷键（1/2/3/K/R/Esc/Delete/方向键）在焦点位于数值/文本输入控件时被 `focusInTextInput()` 守卫忽略，避免打字误触。
- **自定义**：以上快捷键（组合键 + 单键共 15 条）收进 `app/keybindings.{h,cpp}` 的命令注册表（单一真相源），
  桌面可在「视图 → 外观/交互…」对话框的「快捷键」Tab 逐条重绑（移动端无键盘，此 Tab 由构造参数关闭）；
  改后即生效、跨会话持久化（QSettings `shortcuts/` 组），组合键类命令强制含 Ctrl/Alt，冲突时自动询问「改绑清除」，支持行内/全量恢复默认。
- **非模态提示**：错误经 `notify()`（薄封装 → `StatusBar::notify`）写状态栏并限时显示，不打断用户。

## main.cpp

创建 `QApplication`，设置应用/组织名（供后续 QSettings 持久化），用 `QCommandLineParser` 解析命令行；
若首位置参数为图像路径，窗口显示后调用 `MainWindow::openImageFromPath()` 自动打开（与菜单「打开」共用载入逻辑）。
