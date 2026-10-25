// ============================================================================
// 文件：app/main_window_ui.h
// 作用：MainWindowUi——主窗口 UI 装配建造者 + UI 状态回同步：承担中央区/菜单栏/工具栏/状态栏
//       的构建与「面板/菜单勾选态 ← Document/标注桥」的全量同步，
//       使主窗口只保留「编排 + 转发」（避免上帝文件）。
// 分块依据：
//   - 纯 UI 装配与 UI 状态同步均无内部状态，故用静态方法：四个 build* + syncUi/3 个勾选态同步助手；
//   - 作为 MainWindow 的 friend，直接回填/读取其成员指针（scene_/view_/面板/rightTabs_/status_/动作），
//     免去 out 参数结构体，也保住私有槽的连接可达性（外部 connect 私有槽需 friend 权限）。
// 说明：帮助/关于两个长正文、版本号回退宏同在本单元（随 buildMenus 组织）；
//       syncUi 汇集面板 syncFromDocument + 菜单/工具栏启停与勾选态回同步，
//       MainWindow::syncPanels 转发调用它。
// ============================================================================
#pragma once

class QMenuBar;

namespace idc::gui {

class MainWindow;

// ---------------------------------------------------------------------------
// MainWindowUi：主窗口 UI 装配建造者与状态同步（MainWindow 的 friend）。
// ---------------------------------------------------------------------------
class MainWindowUi {
public:
    static void buildCentral(MainWindow* w);  // 中央三分 QSplitter（左滚动容器 | 画布 | 右折叠栏）
    static void buildMenus(MainWindow* w);    // 文件/编辑/模式/预处理/图片/标注/视图/帮助（含帮助/关于正文）
    static void buildToolbar(MainWindow* w);  // 新建/打开/导出、撤销重做（复用 aUndo_/aRedo_）、缩放、模式/极性互斥组、重置全部
    static void buildStatus(MainWindow* w);   // StatusBar（status_）+ 初始模式名

    // 全量同步：左侧/右侧面板 syncFromDocument + 菜单/工具栏动作启停与勾选态回同步
    // （const 引用入参——成员均为指针，改动直达所指对象）。
    static void syncUi(const MainWindow& w);
    // 画布四态菜单勾选态回同步（applyCanvasMode 落地后由 MainWindow::connectAll 第二条连接调用）。
    static void syncCanvasModeActions(const MainWindow& w);
    // 标注工具菜单勾选态回同步（AnnotationBridge::toolChanged 与 syncUi 调用）。
    static void syncToolActions(const MainWindow& w);
    // 标注叠放次序启用态与「填充」勾选态回同步（AnnotationBridge::selectionChanged/changed 与 syncUi 调用）。
    static void syncAnnoSelActions(const MainWindow& w);
    // 图层可用性门控：图片/标注图层隐藏时，画布模式入口、属性面板与整域菜单一并置灰；
    // 当前正处被禁模式时回落框选（syncUi 与两图层显隐信号触发）。
    static void syncLayerAvailability(const MainWindow& w);

private:
    // 按菜单域拆分的静态构建 helper（定义在 main_window_menus.cpp；buildMenus 依序调用）。
    // 声明为 MainWindowUi 静态成员以继承其 friend 权限（自由函数无法访问 MainWindow 私有成员）。
    static void buildFileMenu(MainWindow* w, QMenuBar* bar);
    static void buildEditMenu(MainWindow* w, QMenuBar* bar);
    static void buildModeMenu(MainWindow* w, QMenuBar* bar);
    static void buildPreprocessMenu(MainWindow* w, QMenuBar* bar);
    static void buildLayerMenu(MainWindow* w, QMenuBar* bar);
    static void buildAnnoMenu(MainWindow* w, QMenuBar* bar);
    static void buildViewMenu(MainWindow* w, QMenuBar* bar);
    static void buildHelpMenu(MainWindow* w, QMenuBar* bar);
};

} // namespace idc::gui
