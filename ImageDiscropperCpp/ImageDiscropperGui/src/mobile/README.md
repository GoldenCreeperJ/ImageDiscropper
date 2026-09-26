# src/mobile — 移动端 UI 实现

**目录作用**：`MobileShell` 装配实现与补偿控件（与 `src/app/` 并列，GuideLine 阶段 3）。
声明边界见 [include/mobile/](../../include/mobile/README.md)。

| 文件                    | 职责                                                                                                                                                                                                                                                                                                                             |
|-----------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `mobile_shell.cpp`    | `MobileShell` 壳本体：装配顺序（`buildCentral→buildDrawer→buildStatus→buildBottomBar` → `initCore()`（与桌面逐行共用的唯一接线序列）→ `enableTouchErgonomics`）与 `MobileShell::resizeEvent`（按宽高比翻转重排抽屉与工具条，竖屏底部 / 横屏右侧，只重排不重建 → 状态无损，阶段 4） |
| `mobile_shell_ui.cpp` | 建造者 `MobileShellUi` 实现（`MainWindow` 的 friend，与 `MainWindowUi` 同模式、同拆分纪律）：`buildCentral`（画布独占中央区）→ `buildDrawer`（桌面 7 面板原样复用入五页抽屉）→ `buildStatus`（紧凑状态栏）→ `buildBottomBar`（工具条 + 无键盘补偿）→ `enableTouchErgonomics`（忙碌全屏遮罩，SPEC §8.2）；触控样式、滚轮横滚过滤器等装配细节全在此 |
| `mobile_step_pad.cpp` | 8 向步进盘实现：QToolButton autoRepeat 长按连发；`nudgeRequested` → 上层接 `CanvasView::requestNudge`（与方向键同信号链路）                                                                                                                                                                                                                              |

## 底部工具条 → 既有链路映射（无键盘补偿，SPEC §8.3）

| 按钮           | 落点（全部为桌面同源入口）                                              |
|--------------|------------------------------------------------------------|
| 打开 / 导出      | `MainWindow::onOpen` / `onExport`                          |
| 撤销 / 重做（常驻）  | `onUndo`/`onRedo` 上下文路由；动作即 `aUndo_`/`aRedo_`，启用态回灌同源      |
| 缩小 / 放大 / 适应 | `CanvasView::zoomOut/zoomIn/fitToWindow`（适应 = 双击同源）        |
| L1 / L2 / L3 | `onModeAction(1/2/3)` + `modeAction*` 互斥组（syncPanels 回灌同源） |
| 保留 / 删除（极性）  | `onPolarityShortcut(false/true)`（K/R 键同源）                  |
| 删标注          | `AnnotationCoordinator::onAnnoDeleteSelected`（Delete 键同源）  |
| 取消           | 绘制态 → `view->requestAnnoEscape()`（Esc 同源）；否则 `onClearCut`  |
| 步进           | `StepPadWidget` → `view->requestNudge(dx,dy)`（方向键同源）       |
| 更多           | 加载/保存配置（FR-L3.8）、清除全部标注（菜单同源）                              |
| 面板           | 开合抽屉（竖屏底部占高 3/5 / 横屏右侧占宽 2/5）                              |

> 长按弹画布右键等价菜单、双指捏合缩放、双击适应等**手势**在
> `canvas/canvas_touch_input_adapter`（见 src/canvas README「交互」节），不在本目录。

## 触控精度与方向（阶段 4 已落实）

- 工具条按钮 ≥44pt 经 `MobileShell` 样式表（`QToolBar QToolButton { min-width/height: 44px }`）；
  抽屉内复用的桌面面板统一触控字号/控件最小高/列表行高，选择器限定 `#mobileDrawer` 子树，
  不波及画布与工具条。
- 竖/横屏双布局：`resizeEvent` 仅在宽高比跨越 1:1（方向真正翻转）时重排抽屉停靠区，
  普通 resize 直接返回（天然防抖）；面板状态全在既有 widget 内，重排零触碰 → 旋转状态无损（§8.3）。
- 忙碌全屏遮罩：预处理耗时操作经 `PreprocessController::setBusyOverlayMode(true)` 把桌面
  居中小对话框铺满屏幕（SPEC §8.2 形态差异，模态阻断语义不变）。
- 防误触：画布手势与抽屉面板滚动由 widget 边界天然分区（捏合仅 view grabGesture，面板内滑动只驱动其
  QScrollArea）；触控下 `TouchInputAdapter::attach` 额外关闭 hover 鼠标跟踪，免拖拽途中悬停抖动。
- 底部工具条窄屏可滑动：全部按钮住在 `QScrollArea` 条带里（藏滚动条），`QScroller` 接管拖动——
  Android 手指横扫 flick（TouchGesture），桌面预览鼠标左键拖（LeftMouseButtonGesture）；
  超启动阈值才判拖拽，轻点仍触发按钮，点击与滑动共存；鼠标滚轮经 `WheelToHScrollFilter`
  重定向为横滚（QScrollArea 默认滚轮驱动垂直条，横条带里会错乱成上下滚）；
  `QToolBar` 原生溢出「>>」问题由此消除。
- 工具条方向自适应（实测修复）：竖屏＝底部横向单行（高 48），横屏＝右侧竖向单列（
  `applyOrientationLayout` 迁移停靠区并重排同一批 widget，零重建、状态无损）。
- 抽屉最小尺寸＝设计下限而非内容自然 hint 回压（实测修复）：`rightTabs_` 显式 `setMinimumSize(320, 240)`
  （宽贴合竖屏手机常态可用宽下限），窗口下限可控且真机横/竖屏均放得下；
  参数/导出/图像/标注四页各套纵向 `QScrollArea`（横向 AlwaysOff，内容随宽自适应），
  高度不够时页内上下滚动；`PreviewController` 的导出页判定同步放宽为
  「面板位于当前页内」（`showingExportPage`，桌面等价）。
- 真机验证（实际旋转事件、触控字号观感）归**阶段 5**（风险 5：模拟器不可信）。
- 产物形态（双可执行）：桌面一次构建产出 `idc_gui`（纯桌面，不含任何移动码）与
  `idc_gui_mobile`（本目录源 + `IDC_GUI_HAS_MOBILE`/`IDC_GUI_FORCE_MOBILE` 宏，双击即移动骨架，
  无需命令行）；是否额外产出 `idc_gui_mobile` 由 CMake 开关 `IDC_GUI_MOBILE` 控制（默认 ON）。
  Android/iOS 不读该开关：只出单目标 `idc_gui`（common+移动壳合体），既有打包脚本零改动。
  两目标共享 `idc_gui_common`（OBJECT 库，main.cpp 各编一份）；切换开关后若 mocs 残留
  LNK2019，删构建目录 `ImageDiscropperGui/idc_gui*_autogen` 重建。
- 运行时选壳：`idc_gui_mobile`/Android 恒为移动骨架；`idc_gui` 恒为桌面——无命令行分支，
  桌面既有路径零改动；拖 `idc_gui_mobile` 窗口跨越宽高比 1:1 即可验证方向翻转重排。
