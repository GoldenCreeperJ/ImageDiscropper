// ============================================================================
// 文件：include/i18n/retranslate.h
// 作用：热切换重放基建——Retranslator（闭包登记表）+ 各控件族的绑定助手。
//       语言在运行期于静止点切换（模态外观对话框关闭后），常驻窗口的构造期文案不会自行重译，
//       故「设值的同时登记一条重放闭包」；切换语言后先设全局语言、再 run() 重跑全部闭包即原地换文。
// 架构定位：与 gui_i18n.h 同族的 util 基建。门面（qs/TextRef/IDC_GTR/IDC_GT）在 gui_i18n.h；
//       本头在其上叠「把一次 setText 变成一处可重放绑定」，并集中拉入所需 Qt 控件头，
//       以免这些控件依赖反向污染被广泛 include 的 gui_i18n.h。
// 设计取舍：
//   · 每个常驻类（面板/状态栏/主窗骨架）自持一份 Retranslator + 一个 retranslate()，
//     面板不感知 MainWindow；顶层再按依赖序聚合调用（见 app/main_window.cpp retranslateUi）。
//   · 助手一律「先按当前语言设初值，再登记同逻辑闭包」，与既有 IDC_GTR 取值逐字等价，
//     只是多存一条 lambda；闭包捕获裸指针 + constexpr TextRef，生命周期随宿主控件（同析构域）。
//   · 下拉项用 setItemText 原地改文字，绝不用 clear+add（保 currentIndex 与 userData）；
//     表单行标签预建 QLabel 再 addRow，取回指针可靠。
//   · 成员声明顺序纪律（隐含约定、务必守）：宿主类持有的 Retranslator 成员（惯例名 retranslator_）必须
//     声明在该类所有「被闭包捕获的控件成员」**之后**（置于成员末尾）。C++ 成员按声明逆序析构，
//     retranslator_ 最后声明即最先析构，保证「闭包销毁时其捕获的控件仍存活」——拆解期任何 run() 或异常
//     路径都不会触到已释放控件。status_bar / 各属性面板均依此序；把 retranslator_ 挪到控件成员中间会
//     打破该不变量、埋下悬垂指针的雷（新添常驻类时照此把 retranslator_ 放最后）。
// ============================================================================
#pragma once

#include <functional>
#include <initializer_list>
#include <utility>
#include <vector>

#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QAction>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QMenu>
#include <QString>
#include <QTabWidget>
#include <QWidget>

#include "i18n/gui_i18n.h"   // TextRef / qs

namespace idc::gui::i18n {

// 可重放文案的登记表：宿主在构造期登记闭包，语言切换后 run() 逐条按新语言重放。
class Retranslator {
public:
    void add(std::function<void()> apply) { items_.push_back(std::move(apply)); }
    void run() const { for (auto& f : items_) f(); }

private:
    std::vector<std::function<void()>> items_;
};

// —— 逐控件族绑定助手：均「设当前值 + 登记重放闭包」，入参用 IDC_GT(...) 产出的 TextRef ——

// QLabel（行标签、说明、回显初值等）。
inline void bindText(Retranslator& r, QLabel* w, const TextRef& ref) {
    w->setText(qs(ref));
    r.add([w, ref] { w->setText(qs(ref)); });
}

// 任意可 setText 的控件/条目（QLineEdit 等的显示文本走 setText；占位符另有 bindPlaceholder）。
inline void bindButton(Retranslator& r, QAbstractButton* b, const TextRef& ref) {
    b->setText(qs(ref));
    r.add([b, ref] { b->setText(qs(ref)); });
}

// QAction（菜单项 / 工具栏动作 / QActionGroup 项）。
inline void bindAction(Retranslator& r, QAction* a, const TextRef& ref) {
    a->setText(qs(ref));
    r.add([a, ref] { a->setText(qs(ref)); });
}

// QAction 悬停说明（statusTip 显示在状态栏）。
inline void bindStatusTip(Retranslator& r, QAction* a, const TextRef& ref) {
    a->setStatusTip(qs(ref));
    r.add([a, ref] { a->setStatusTip(qs(ref)); });
}

// QGroupBox 标题。
inline void bindTitle(Retranslator& r, QGroupBox* g, const TextRef& ref) {
    g->setTitle(qs(ref));
    r.add([g, ref] { g->setTitle(qs(ref)); });
}

// 任意控件/动作的 tooltip（模板：QWidget 与 QAction 均有 setToolTip）。
template <class W>
void bindToolTip(Retranslator& r, W* w, const TextRef& ref) {
    w->setToolTip(qs(ref));
    r.add([w, ref] { w->setToolTip(qs(ref)); });
}

// QMenu 标题（子菜单入口）。
inline void bindMenuTitle(Retranslator& r, QMenu* m, const TextRef& ref) {
    m->setTitle(qs(ref));
    r.add([m, ref] { m->setTitle(qs(ref)); });
}

// QLineEdit/QTextEdit 等输入框占位符（模板：凡有 setPlaceholderText 的控件均可）。
template <class W>
void bindPlaceholder(Retranslator& r, W* w, const TextRef& ref) {
    w->setPlaceholderText(qs(ref));
    r.add([w, ref] { w->setPlaceholderText(qs(ref)); });
}

// 数值框最小值的特显文字（QSpinBox / QDoubleSpinBox 均有，经 QAbstractSpinBox）。
inline void bindSpecialValue(Retranslator& r, QAbstractSpinBox* s, const TextRef& ref) {
    s->setSpecialValueText(qs(ref));
    r.add([s, ref] { s->setSpecialValueText(qs(ref)); });
}

// 下拉：初次逐项 addItem（按当前语言），闭包按索引 setItemText 原地改文字（不动 currentIndex/userData）。
inline void addItemsI18n(Retranslator& r, QComboBox* c, const std::initializer_list<TextRef> refs) {
    std::vector items(refs.begin(), refs.end());
    for (const TextRef& ref : items) c->addItem(qs(ref));
    r.add([c, items] {
        for (std::size_t i = 0; i < items.size() && static_cast<int>(i) < c->count(); ++i)
            c->setItemText(static_cast<int>(i), qs(items[i]));
    });
}

// 已逐项 addItem（可携 userData）的下拉：仅登记按 index 重放 itemText 的闭包（不动 userData/currentIndex）。
// 适用于 addItem(text, data) 形式（如旋转角度/翻转方向）——addItemsI18n 无法携 data。
inline void bindItemTexts(Retranslator& r, QComboBox* c, const std::initializer_list<TextRef> refs) {
    std::vector items(refs.begin(), refs.end());
    r.add([c, items] {
        for (std::size_t i = 0; i < items.size() && static_cast<int>(i) < c->count(); ++i)
            c->setItemText(static_cast<int>(i), qs(items[i]));
    });
}

// QFormLayout 行标签：预建 QLabel 再 addRow（取回指针可靠），闭包重放该标签文字。
inline void bindFormRow(Retranslator& r, QFormLayout* form, QWidget* field, const TextRef& ref) {
    auto* label = new QLabel(qs(ref), form->parentWidget());
    form->addRow(label, field);
    r.add([label, ref] { label->setText(qs(ref)); });
}

// QTabWidget 页签标题：初次已 setTabText，登记按索引重放。
inline void bindTabText(Retranslator& r, QTabWidget* tabs, const int index, const TextRef& ref) {
    tabs->setTabText(index, qs(ref));
    r.add([tabs, index, ref] { tabs->setTabText(index, qs(ref)); });
}

} // namespace idc::gui::i18n
