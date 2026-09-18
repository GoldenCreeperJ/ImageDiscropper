// ============================================================================
// 文件：app/main_window_ui.h
// 作用：MainWindowUi——主窗口装配建造者：把中央区/菜单栏/工具栏/状态栏的构建从 MainWindow 拆出，
//       使主窗口只保留「编排 + 转发」（避免上帝文件）。
// 分块依据：
//   - 纯 UI 装配无状态，故用四个静态方法对应原 buildCentral/buildMenus/buildToolbar/buildStatus；
//   - 作为 MainWindow 的 friend，直接回填其成员指针（scene_/view_/面板/rightTabs_/status_/动作），
//     免去 out 参数结构体，也保住私有槽的连接可达性（外部 connect 私有槽需 friend 权限）。
// 说明：帮助/关于两个长正文、版本号回退宏也随 buildMenus 一并迁入本单元。
// ============================================================================
#pragma once

namespace idc::gui {

class MainWindow;

// ---------------------------------------------------------------------------
// MainWindowUi：主窗口装配建造者（MainWindow 的 friend）。
// ---------------------------------------------------------------------------
class MainWindowUi {
public:
    static void buildCentral(MainWindow* w);  // 中央三分 QSplitter（左滚动容器 | 画布 | 右选项卡）
    static void buildMenus(MainWindow* w);    // 文件/编辑/图像/标注/视图/帮助（含帮助/关于正文）
    static void buildToolbar(MainWindow* w);  // 打开/导出、撤销重做（复用 aUndo_/aRedo_）、缩放、模式互斥组
    static void buildStatus(MainWindow* w);   // StatusBar（status_）+ 初始模式名
};

} // namespace idc::gui
