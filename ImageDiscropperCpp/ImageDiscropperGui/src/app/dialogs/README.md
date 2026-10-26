# app/dialogs

**目录作用**：app 编排层的模态对话框实现——「视图→外观/交互…」（`AppearanceDialog`）与
「文件→新建画布 / 添加图片图层」（`NewCanvasDialog`）。两对话框均为采参视图：自身不持偏好态，
改动即时写对应单例（Palette/CanvasPrefs/UiPrefs/KeyBindings）或经 `EngineBridge` 产像素
（新建画布），接线由组合处（`menus/`、`mobile/`、`controllers/`）负责。

**分块依据**：

| 文件                                | 职责                                                                                                                                                                            |
|-----------------------------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `appearance_dialog.cpp`           | `AppearanceDialog` 装配壳：多 Tab 容器的组装、页序（配色 → 快捷键〔仅桌面〕→ 其他）、底部「恢复默认/关闭」常驻区、语言热切换重放（`retranslate`）与快捷键录制态 Esc 拦截                                                                  |
| `appearance_dialog_colors.cpp`    | 配色页（`buildColorsTab`）：六项语义色 + 三项填充 alpha 写 Palette、深浅色主题写 UiPrefs（即时 `theme::apply`）+ 取色/色块回显                                                                                 |
| `appearance_dialog_other.cpp`     | 其他页（`buildOtherTab`）：画布交互写 CanvasPrefs、应用语言与默认输出文件夹写 UiPrefs、「确认与吸附」组 + 交互数值回显                                                                                                |
| `appearance_dialog_shortcuts.cpp` | 快捷键页（`buildShortcutsTab`，仅桌面）：依 KeyBindings 注册表逐行 QKeySequenceEdit 重绑 + 缺修饰校验 + 冲突改绑询问 + 行内/全量恢复默认与延迟冲突报告                                                                     |
| `new_canvas_dialog.cpp`           | `NewCanvasDialog`：尺寸 + 程序化图案 + 附加图片（两级落位）采参、小尺寸代理预览、`accept()` 经 `EngineBridge::generatePattern` 产全尺寸画布（采参子件 `pattern_form`/`placement_row` 在 [`forms/`](../forms/README.md)） |

对话框页构建器只回传页面部件，tab 登记与页序统一留在装配壳构造；无「确定/取消」回滚语义
（改后即生效、即落盘）。
