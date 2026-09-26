// ============================================================================
// 文件：panels/accordion_panel.h
// 作用：桌面右侧「折叠面板」容器——多节（section）竖排，每节一条标题按钮 +
//       内容 widget；**多节可同时展开**，整体放进外层 QScrollArea 滚动触达。
// 定位：与 QToolBox（互斥展开、单节可见）语义不同，本容器专供桌面右栏
//       「参数/导出/图像/标注」四面板；移动端抽屉仍用 QTabWidget 选择式。
// 分文件：声明在本头，实现在 src/panels/accordion_panel.cpp（AUTOMOC 扫本头）。
// ============================================================================
#ifndef IDC_GUI_PANELS_ACCORDION_PANEL_H
#define IDC_GUI_PANELS_ACCORDION_PANEL_H

#include <QList>
#include <QString>
#include <QWidget>

class QPushButton;
class QVBoxLayout;

namespace idc::gui {

/// 多节可同时展开的折叠面板（手风琴）。
/// 用法：外层 QScrollArea::setWidget(accordion) + widgetResizable(true)，
/// 宽度随栏自适应、高度为全部展开节之和（超出视口即整栏滚动）。
class AccordionPanel : public QWidget {
    Q_OBJECT
public:
    explicit AccordionPanel(QWidget* parent = nullptr);

    /// 追加一节：content 由本容器接管（reparent）；初始为展开态。
    void addSection(QWidget* content, const QString& title);

    /// content 所在节是否展开（不在本容器内返回 false）。
    /// PreviewController 的「导出页可见才渲染」判定用（替代 tab 的 currentWidget 比较）。
    bool isSectionExpanded(const QWidget* content) const;

    /// 展开 content 所在节（已展开则无副作用；状态变化会发 sectionToggled）。
    void expandSection(const QWidget* content);

signals:
    /// 某节展开/收起时发出（内容显隐已在本类处理，外部只做联动编排）。
    void sectionToggled(QWidget* content, bool expanded);

private:
    struct Section {
        QPushButton* header{nullptr};
        QWidget* content{nullptr};
        QString title;
    };

    static void refreshHeaderText(const Section& section, bool expanded);

    QList<Section> sections_;
    QVBoxLayout* body_{nullptr};   // 逐节堆叠，尾部常驻 stretch（新节 insert 在其前，节自身不被拉伸）。
};

} // namespace idc::gui

#endif // IDC_GUI_PANELS_ACCORDION_PANEL_H
