# include/mobile — 移动端 UI 对外声明

**目录作用**：GUI **移动端形态**（Android / iOS，双布局与触控细节已落实）的公共头：顶层壳 `MobileShell`、
骨架建造者 `MobileShellUi`（声明）与无键盘补偿控件 `StepPadWidget`。本目录只放**类声明边界**；装配实现与详尽说明见
[src/mobile/](../../src/mobile/README.md)。

**分块依据**：与桌面 `app/`（顶层壳）**并列**的新模块——移动端是并列前端而非功能子集
（SPEC §8.3 行为同构）：`MobileShell` 继承 `MainWindow`，复用其全部业务槽、八控制器接线与
`Document` 单一真相源，仅替换骨架装配（三栏 → 画布为主 + 底部工具条/抽屉）；
本目录**不含任何编排/引擎逻辑**（零复刻纪律，SPEC §8.3 一个引擎 / 行为同构）。

| 文件                         | 声明                                                      | 职责                                                                                                                                                                     |
|----------------------------|---------------------------------------------------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `mobile_shell.h`           | `MobileShell`（`MainWindow`）                             | 移动端顶层壳：经保护构造换装骨架（画布独占中央区 + 底部工具条 + 六页抽屉 + 紧凑状态栏），`initCore()` 共用桌面接线序列；`resizeEvent` 宽高比翻转时仅重排抽屉（竖屏底部/横屏右侧，状态无损）；公共面继承 `openImageFromPath`                             |
| `mobile_shell_ui.h`        | `MobileShellUi`（纯静态建造者）                                 | 移动端骨架建造者声明（`MainWindow` 的 friend，与桌面 `MainWindowUi` 同模式、同拆分纪律）：五个装配方法签名 + 触控样式表与部件名常量（`kTouchChromeStyle`/`kDrawerName` 等，壳与建造者共用）                                     |
| `touch_scroll.h`           | `WheelToHScrollFilter`（`QObject`）+ `installTouchScroll` | 移动端滚动接管（自 mobile_shell_ui 拆出）：滚轮→横滚重定向（公开类，底部工具条直接 new 用）+ 全部滚动区统一换装触控滚动（Android/iOS 用文件内件 TouchScrollFilter 替代 QScroller，桌面保持 QScroller 鼠标协调器）                        |
| `mobile_step_pad.h`        | `StepPadWidget`                                         | 8 向步进盘（方向键微调的触控等价物）：3×3 网格、×1/×10 倍率、按钮 ≥44pt，只发 `nudgeRequested` 意图信号                                                                                                 |
| `android_export_publish.h` | 自由函数集（`idc::gui`）                                       | Android 导出发布编排（仅 Q_OS_ANDROID 编译）：`exportFormatExt`/`fallbackToDocuments`/`publishExport`（自 `ExportController` 的 Android 分支拆出，与媒体库发布层同居 mobile/）                       |
| `android_media_store.h`    | 自由函数集（`idc::gui`）                                       | Android 媒体库发布层（SPEC §8.3 形态差异）：本地文件 → MediaStore 无 pending 直插 + fd 流式写入相册公共目录（Pictures/ImageDiscropper）；头文件平台中立——QJniObject 依赖隔离在 .cpp 的 `Q_OS_ANDROID` 内，桌面无调用者不编 JNI |

> 命名空间统一 `idc::gui`。触控事件→业务手势的翻译不在本目录（在 `canvas/` 的
> `TouchInputAdapter`，实现与验证状态见 src/mobile README「移动端实现状态与遗留事项」）；
> 本目录只负责**布局与补偿 UI**。
