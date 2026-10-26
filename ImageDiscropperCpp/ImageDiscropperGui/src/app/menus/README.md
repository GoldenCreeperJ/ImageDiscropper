# app/menus

**目录作用**：`MainWindowUi::buildMenus` 的实现——菜单栏装配按菜单域拆四个 TU，与
`app/main_window_ui.cpp`（中央区/工具栏/状态栏装配 + `syncUi` 同步）正交：菜单装配与 UI 状态
同步无共享状态，故独立成件。

**分块依据**：按菜单域四 TU——

| 文件                             | 职责                                                                                                                   |
|--------------------------------|----------------------------------------------------------------------------------------------------------------------|
| `main_window_menus.cpp`        | 文件/编辑域 + `buildMenus` 总装配 + 共享绑定助手（`bindAddAction`/`bindAddMenu` 四种形态，声明见 `main_window_menus_internal.h`，定义在本 TU 单份） |
| `main_window_menus_mode.cpp`   | 模式/预处理域：L1/L2/L3 模式与极性、画布四态、切割参数（形状/方式/排序/余量/网格定义）与预处理快捷菜单                                                           |
| `main_window_menus_canvas.cpp` | 图片/标注域：图层栈 CRUD、锚点枢轴九向子菜单、标注撤销重做/叠放/填充开关/工具子菜单                                                                       |
| `main_window_menus_view.cpp`   | 视图/帮助域：缩放/适应/重置、图层显隐开关、外观对话框入口与使用说明/关于正文                                                                             |
| `main_window_menus_internal.h` | 私有内部头：四个 TU 共用的绑定助手声明（定义单份于 `main_window_menus.cpp`），只被本目录 TU 包含                                                     |

域内控件/连接自持（各 TU 的 connect 接收者均为 `w`，经 `MainWindowUi` 静态成员继承 friend 权限）；
菜单项文字、悬停说明与帮助正文均取 `i18n/gui_texts.h`（`texts::menu` / `tooltip` / `msg`），
桌面菜单与移动「更多」菜单同源。
