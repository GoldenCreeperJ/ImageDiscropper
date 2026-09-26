// ============================================================================
// 文件：mobile/mobile_shell_ui.h
// 作用：MobileShellUi——移动端骨架建造者（纯静态，MainWindow 的 friend），
//       与桌面 main_window_ui.h 的 MainWindowUi 同一模式：把桌面三栏骨架折叠为
//       「画布为主 + 底部工具条 + 抽屉」（SPEC §8.3 布局），并落实无键盘补偿。
// 分块依据：
//   - 部件创建/回填与连接全在本类（.cpp 内实现）；MobileShell 构造只规定装配顺序
//     → initCore()（基类共用接线，序列不可打乱）——壳与建造者分文件，与桌面
//     main_window.cpp / main_window_ui.cpp 的拆分纪律一致；
//   - 桌面 7 面板类**原样复用**入抽屉（不做移动专供面板——零复刻纪律）；
//   - 底部工具条按钮 → MainWindow 既有私有槽 / CanvasView request*/gesture* 入口，
//     与菜单/快捷键/右键同一信号链路（行为同构）；「更多」菜单收纳低频桌面菜单能力。
// 说明：44pt 触控下限经本壳样式表（kTouchChromeStyle）作用于工具条按钮；
//       抽屉内桌面面板统一触控字号/间距（样式表限定 #mobileDrawer 子树，不波及画布）；
//       预处理忙碌改全屏遮罩（SPEC §8.2）；方向切换所需的部件 objectName 常量
//       （kDrawerName/kToolBarName/kStripName）随本头共驻（壳与建造者共用）。
// ============================================================================
#pragma once

namespace idc::gui {

class MainWindow;

// 触控 chrome 样式（GuideLine 阶段 4）：
//   - 工具条/浮动按钮 ≥44×44pt（SPEC §8.3 可点控件下限）；
//   - #mobileDrawer 子树：抽屉内复用的桌面面板统一放大字号/控件最小高/列表行高，
//     选择器限定在抽屉 objectName 下，不波及画布与工具条（防误触只在面板区生效）。
inline constexpr auto kTouchChromeStyle =
    "QToolBar QToolButton { min-width: 44px; min-height: 44px; }"
    "QToolBar { spacing: 4px; padding: 2px; }"
    "#mobileDrawer { font-size: 14px; }"
    "#mobileDrawer QPushButton, #mobileDrawer QCheckBox { min-height: 36px; }"
    "#mobileDrawer QCheckBox::indicator { width: 28px; height: 28px; }"
    "#mobileDrawer QCheckBox { spacing: 8px; }"
    "#mobileDrawer QComboBox, #mobileDrawer QLineEdit, #mobileDrawer QSpinBox { min-height: 32px; }"
    "#mobileDrawer QTabBar::tab { min-height: 36px; padding: 6px 14px; }"
    "#mobileDrawer QListWidget::item { min-height: 40px; }"
    "#mobileDrawer QGroupBox::title { subcontrol-position: top left; padding: 4px; }";

inline constexpr auto kDrawerName = "mobileDrawer";   // 抽屉 QDockWidget（工具条开合按钮 + 方向切换用）。
inline constexpr auto kToolBarName = "mobileToolBar"; // 工具条（方向切换换停靠区用）。
inline constexpr auto kStripName = "mobileStrip";     // 工具条内滑动条带（方向切换重排用）。

// ---------------------------------------------------------------------------
// MobileShellUi：移动端骨架建造者（纯静态、friend 回填私有成员）。
// ---------------------------------------------------------------------------
class MobileShellUi {
public:
    static void buildCentral(MainWindow* w);      // scene_/view_ + 7 面板（原样复用，尚未入容器）
    static void buildDrawer(MainWindow* w);       // rightTabs_（五页）入底部 QDockWidget（隐藏）
    static void buildStatus(MainWindow* w);       // StatusBar 紧凑模式（模式/块数/缩放，§8.3）
    static void buildBottomBar(MainWindow* w);    // 底部工具条 + 模式动作 + 无键盘补偿按钮
    /// initCore 之后调用：开启预处理忙碌全屏遮罩（SPEC §8.2 移动端形态；friend 访问 preprocess_）。
    static void enableTouchErgonomics(const MainWindow* w);
};

} // namespace idc::gui
