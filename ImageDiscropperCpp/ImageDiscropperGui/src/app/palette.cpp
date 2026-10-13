// ============================================================================
// 文件：app/palette.cpp
// 作用：Palette 单例实现——QSettings 载入/落盘、六项语义色与填充 alpha 的存取、恢复默认。
// 分块依据：持久化组名 / 键名取 texts::ident（常量层，载入 / 落盘 / 恢复默认三处同源），
//           出厂缺省值集中在匿名 namespace（本目录颜色表，不进常量层）；
//           取值器全部内联在头文件，本件只有变更路径。
// 说明：色相以 #RRGGBB 存（alpha 独立成键，避免 QColor.name(QColor::HexArgb) 把
//       「色相」与「填充浓度」耦合在一个字符串里）。
// ============================================================================
#include "app/palette.h"

#include <algorithm>

#include <QSettings>

#include "i18n/gui_texts.h"

namespace idc::gui {
namespace {

// QSettings 组名与键名见 texts::ident（不可译契约）；下列为 palette/ 组下的出厂缺省值（本目录 README「画布视觉规范」配色表）。
constexpr QColor kDefaultKeep(0, 200, 0);
constexpr QColor kDefaultDelete(200, 0, 0);
constexpr QColor kDefaultSelection(255, 140, 0);
constexpr QColor kDefaultAnnotation(0, 160, 230);
constexpr QColor kDefaultGrid(150, 150, 150);
constexpr QColor kDefaultBackground(45, 45, 45);
constexpr int kDefaultKeepAlpha = 60;
constexpr int kDefaultDeleteAlpha = 60;
constexpr int kDefaultSelectionFillAlpha = 30;

// 选区高亮态派生：R 通道压暗（255→255-165=90，与原硬编码 rgb(255,90,0) 一致），G/B 不变。
QColor deriveSelectionHighlight(const QColor& c) {
    return {qMax(0, c.red() - 165), c.green(), c.blue()};
}

} // namespace

Palette& Palette::instance() {
    // Meyers 单例：C++11 起线程安全；生命周期随进程（QSettings 写入在变更点即时完成）。
    static Palette s;
    return s;
}

Palette::Palette(QObject* parent)
    : QObject(parent) {
    QSettings st;
    st.beginGroup(QString::fromUtf8(texts::ident::kGroupPalette));
    keep_ = QColor(st.value(QString::fromUtf8(texts::ident::kPaletteKeep), kDefaultKeep.name()).toString());
    delete_ = QColor(st.value(QString::fromUtf8(texts::ident::kPaletteDelete), kDefaultDelete.name()).toString());
    selection_ = QColor(st.value(QString::fromUtf8(texts::ident::kPaletteSelection), kDefaultSelection.name()).toString());
    annotation_ = QColor(st.value(QString::fromUtf8(texts::ident::kPaletteAnnotation), kDefaultAnnotation.name()).toString());
    grid_ = QColor(st.value(QString::fromUtf8(texts::ident::kPaletteGrid), kDefaultGrid.name()).toString());
    background_ = QColor(st.value(QString::fromUtf8(texts::ident::kPaletteBackground), kDefaultBackground.name()).toString());
    keepAlpha_ = st.value(QString::fromUtf8(texts::ident::kPaletteKeepAlpha), kDefaultKeepAlpha).toInt();
    deleteAlpha_ = st.value(QString::fromUtf8(texts::ident::kPaletteDeleteAlpha), kDefaultDeleteAlpha).toInt();
    selectionFillAlpha_ = st.value(QString::fromUtf8(texts::ident::kPaletteSelectionFillAlpha), kDefaultSelectionFillAlpha).toInt();
    st.endGroup();
    selectionHi_ = deriveSelectionHighlight(selection_);
    selectionFillHiAlpha_ = selectionFillAlpha_ * 2;
}

void Palette::touched() {
    save();
    emit changed();
}

void Palette::save() const {
    QSettings st;
    st.beginGroup(QString::fromUtf8(texts::ident::kGroupPalette));
    st.setValue(QString::fromUtf8(texts::ident::kPaletteKeep), keep_.name());
    st.setValue(QString::fromUtf8(texts::ident::kPaletteDelete), delete_.name());
    st.setValue(QString::fromUtf8(texts::ident::kPaletteSelection), selection_.name());
    st.setValue(QString::fromUtf8(texts::ident::kPaletteAnnotation), annotation_.name());
    st.setValue(QString::fromUtf8(texts::ident::kPaletteGrid), grid_.name());
    st.setValue(QString::fromUtf8(texts::ident::kPaletteBackground), background_.name());
    st.setValue(QString::fromUtf8(texts::ident::kPaletteKeepAlpha), keepAlpha_);
    st.setValue(QString::fromUtf8(texts::ident::kPaletteDeleteAlpha), deleteAlpha_);
    st.setValue(QString::fromUtf8(texts::ident::kPaletteSelectionFillAlpha), selectionFillAlpha_);
    st.endGroup();
    st.sync();
}

void Palette::setKeepColor(const QColor& c) {
    if (!c.isValid()) return;
    keep_ = QColor(c.red(), c.green(), c.blue());
    touched();
}

void Palette::setDeleteColor(const QColor& c) {
    if (!c.isValid()) return;
    delete_ = QColor(c.red(), c.green(), c.blue());
    touched();
}

void Palette::setSelectionColor(const QColor& c) {
    if (!c.isValid()) return;
    selection_ = QColor(c.red(), c.green(), c.blue());
    selectionHi_ = deriveSelectionHighlight(selection_);
    touched();
}

void Palette::setAnnotationColor(const QColor& c) {
    if (!c.isValid()) return;
    annotation_ = QColor(c.red(), c.green(), c.blue());
    touched();
}

void Palette::setGridColor(const QColor& c) {
    if (!c.isValid()) return;
    grid_ = QColor(c.red(), c.green(), c.blue());
    touched();
}

void Palette::setBackgroundColor(const QColor& c) {
    if (!c.isValid()) return;
    background_ = QColor(c.red(), c.green(), c.blue());
    touched();
}

void Palette::setKeepAlpha(const int a) {
    keepAlpha_ = std::clamp(a, 0, 255);
    touched();
}

void Palette::setDeleteAlpha(const int a) {
    deleteAlpha_ = std::clamp(a, 0, 255);
    touched();
}

void Palette::setSelectionFillAlpha(const int a) {
    selectionFillAlpha_ = std::clamp(a, 0, 255);
    selectionFillHiAlpha_ = selectionFillAlpha_ * 2;
    touched();
}

void Palette::restoreDefaults() {
    {
        QSettings st;
        st.beginGroup(QString::fromUtf8(texts::ident::kGroupPalette));
        st.remove(QString::fromUtf8(texts::ident::kPaletteKeep));
        st.remove(QString::fromUtf8(texts::ident::kPaletteDelete));
        st.remove(QString::fromUtf8(texts::ident::kPaletteSelection));
        st.remove(QString::fromUtf8(texts::ident::kPaletteAnnotation));
        st.remove(QString::fromUtf8(texts::ident::kPaletteGrid));
        st.remove(QString::fromUtf8(texts::ident::kPaletteBackground));
        st.remove(QString::fromUtf8(texts::ident::kPaletteKeepAlpha));
        st.remove(QString::fromUtf8(texts::ident::kPaletteDeleteAlpha));
        st.remove(QString::fromUtf8(texts::ident::kPaletteSelectionFillAlpha));
        st.endGroup();
        st.sync();
    }
    keep_ = kDefaultKeep;
    delete_ = kDefaultDelete;
    selection_ = kDefaultSelection;
    annotation_ = kDefaultAnnotation;
    grid_ = kDefaultGrid;
    background_ = kDefaultBackground;
    keepAlpha_ = kDefaultKeepAlpha;
    deleteAlpha_ = kDefaultDeleteAlpha;
    selectionFillAlpha_ = kDefaultSelectionFillAlpha;
    selectionHi_ = deriveSelectionHighlight(selection_);
    selectionFillHiAlpha_ = selectionFillAlpha_ * 2;
    touched();
}

} // namespace idc::gui
