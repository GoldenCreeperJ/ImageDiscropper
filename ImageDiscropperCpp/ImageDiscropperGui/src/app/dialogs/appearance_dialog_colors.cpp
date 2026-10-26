// ============================================================================
// 文件：app/dialogs/appearance_dialog_colors.cpp
// 作用：AppearanceDialog「配色」页构建（buildColorsTab）+ 色块回显/取色——
//       六项核心语义色 + 三项填充 alpha（写 Palette）+ 深浅色主题（写 UiPrefs，即时 theme::apply）。
// 分块依据：见 appearance_dialog.cpp 头注（按页四 TU 拆分）；本 TU 只构建回传页面部件，
//       tab 登记与页序由装配壳负责，页内控件/连接自持。
// ============================================================================
#include "app/appearance_dialog.h"

#include <QColorDialog>
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

#include "app/app_prefs.h"        // UiPrefs（深浅色主题偏好 mode 串）
#include "mobile/touch_scroll.h"  // installTouchScroll：移动端把 QScrollArea 换装触控拖滑（桌面回落鼠标）
#include "util/gui_nums.h"        // nums::dialog（色块/触控下限）
#include "util/widget_factory.h"  // makeGroupBox / makeSpin
#include "util/theme.h"           // theme::apply / fromString / toString（深浅色主题即时应用）
#include "i18n/gui_texts.h"
#include "i18n/gui_i18n.h"
#include "i18n/retranslate.h"

namespace idc::gui {

// 配色页（声明见 appearance_dialog.h）：构建即接线——色块/alpha 改即写 Palette（联动重绘 + 落盘）、
// 主题下拉选中即落盘 + theme::apply + 发 themeApplied；页面部件回传由装配壳 addTab。
QWidget* AppearanceDialog::buildColorsTab() {
    auto* pal = &Palette::instance();
    // ===================== Tab 一：配色（六色/三 alpha 写 Palette，附深浅色主题写 UiPrefs）=====================
    auto* colorPage = new QScrollArea(tabs_);
    colorPage->setWidgetResizable(true);
    colorPage->setFrameShape(QFrame::NoFrame);
    installTouchScroll(colorPage);   // Android：拖指滑页；桌面：回落 QScroller 鼠标协调器（行为不变）。
    auto* colorContent = new QWidget(colorPage);
    auto* colorVbox = new QVBoxLayout(colorContent);

    // ---- 配色页 · 画布六色（写 Palette，改动即时联动重绘）----
    auto* colorBox = makeGroupBox(retranslator_, colorContent, IDC_GT(label, kPaletteBoxTitle));
    auto* cform = new QFormLayout(colorBox);
    // 六项语义色：按钮背景即时示色，点开取色对话框。行标签经 bindFormRow 预建 QLabel 并登记重放。
    constexpr i18n::TextRef colorRefs[6] = {
        IDC_GT(label, kSwatchKeepMask), IDC_GT(label, kSwatchDeleteMask),
        IDC_GT(label, kSwatchSelectionCut), IDC_GT(label, kSwatchAnnoSelected),
        IDC_GT(label, kSwatchGridLine), IDC_GT(label, kSwatchCanvasBg)};
    colorRows_[0] = {new QPushButton, &Palette::keepColor, &Palette::setKeepColor};
    colorRows_[1] = {new QPushButton, &Palette::deleteColor, &Palette::setDeleteColor};
    colorRows_[2] = {new QPushButton, &Palette::selectionColor, &Palette::setSelectionColor};
    colorRows_[3] = {new QPushButton, &Palette::annotationColor, &Palette::setAnnotationColor};
    colorRows_[4] = {new QPushButton, &Palette::gridColor, &Palette::setGridColor};
    colorRows_[5] = {new QPushButton, &Palette::background, &Palette::setBackgroundColor};
    for (int i = 0; i < 6; ++i) {
        colorRows_[i].swatch->setMinimumHeight(nums::dialog::kSwatchButtonMinHeightPx);   // 触控下限（桌面观感不变，弹窗不在 #mobileDrawer 子树下故就地取高）。
        i18n::bindFormRow(retranslator_, cform, colorRows_[i].swatch, colorRefs[i]);
        connect(colorRows_[i].swatch, &QPushButton::clicked, this,
                [this, i] { onPickColor(colorRows_[i]); });
    }
    // 三项填充 alpha（0–255）：遮罩/选区填充浓度。改即写 Palette（联动重绘 + 落盘）。行标签同经 bindFormRow。
    const auto makeAlpha = [this, pal, cform](const i18n::TextRef& text, int (Palette::*getter)() const,
                                       void (Palette::*setter)(int)) {
        auto* spin = makeSpin(nullptr, 0, 255);
        spin->setMinimumHeight(nums::dialog::kTouchFieldMinHeightPx);   // 触控下限，与移动端抽屉内控件最小高一致。
        spin->setValue((pal->*getter)());
        i18n::bindFormRow(retranslator_, cform, spin, text);
        connect(spin, &QSpinBox::valueChanged, this, [pal, setter](const int v) { (pal->*setter)(v); });
        return spin;
    };
    keepAlphaSpin_ = makeAlpha(IDC_GT(label, kAlphaKeepSpin),
                               &Palette::keepAlpha, &Palette::setKeepAlpha);
    deleteAlphaSpin_ = makeAlpha(IDC_GT(label, kAlphaDeleteSpin),
                                 &Palette::deleteAlpha, &Palette::setDeleteAlpha);
    selectionAlphaSpin_ = makeAlpha(IDC_GT(label, kAlphaSelectionSpin),
                                    &Palette::selectionFillAlpha, &Palette::setSelectionFillAlpha);

    // ---- 配色页附：外观主题（写 UiPrefs，**对话框内即时 theme::apply + 全量 re-polish、不关面板**）----
    // 深浅色主题与画布六色正交（theme::apply 不触 Palette），同属「看颜色」的诉求，故并入本页而非「其他」。
    // 下拉选中即：落盘 mode 串 → theme::apply（强制 Fusion 已首次完成、重灌 palette + 递归 re-polish）→ 发 themeApplied() 供宿主兜底刷新。
    auto* themeBox = makeGroupBox(retranslator_, colorContent, IDC_GT(label, kThemeBoxTitle));
    auto* tform = new QFormLayout(themeBox);
    themeCombo_ = new QComboBox;
    themeCombo_->setMinimumHeight(nums::dialog::kTouchFieldMinHeightPx);   // 触控下限，与色块/alpha 一致
    // 三项的**显示文字**可译，**项数据**（mode 串）不可译：取 theme::toString 的稳定小写串。
    themeCombo_->addItem(IDC_GTR(label, kThemeSystem), theme::toString(theme::Mode::System));
    themeCombo_->addItem(IDC_GTR(label, kThemeLight), theme::toString(theme::Mode::Light));
    themeCombo_->addItem(IDC_GTR(label, kThemeDark), theme::toString(theme::Mode::Dark));
    i18n::bindItemTexts(retranslator_, themeCombo_,
                        {IDC_GT(label, kThemeSystem), IDC_GT(label, kThemeLight), IDC_GT(label, kThemeDark)});
    i18n::bindToolTip(retranslator_, themeCombo_, IDC_GT(tooltip, kThemeCombo));
    // 回显**已存偏好**（应与本次生效主题一致：本对话框切换即同步）；未知旧值落到首项「跟随系统」。
    const QString themePref = UiPrefs::instance().themeMode();
    for (int i = 0; i < themeCombo_->count(); ++i) {
        if (themeCombo_->itemData(i).toString() == themePref) { themeCombo_->setCurrentIndex(i); break; }
    }
    // activated（仅用户操作）而非 currentIndexChanged：避开上面回显时误触发应用。
    connect(themeCombo_, qOverload<int>(&QComboBox::activated), this, [this] {
        const QString mode = themeCombo_->currentData().toString();
        UiPrefs::instance().setThemeMode(mode);
        theme::apply(theme::fromString(mode));   // 立即重灌 palette + 递归 re-polish（含本对话框）
        emit themeApplied();                      // 通知宿主作兜底刷新（re-polish 已覆盖 chrome）
    });
    i18n::bindFormRow(retranslator_, tform, themeCombo_, IDC_GT(label, kThemeField));
    auto* themeHint = new QLabel(themeBox);
    themeHint->setWordWrap(true);
    i18n::bindText(retranslator_, themeHint, IDC_GT(label, kThemeApplyHint));
    tform->addRow(QString(), themeHint);
    // 深浅色主题置顶（用户要求「放到顶上」）：主题与画布六色正交，先定整体明暗、再调具体配色。
    colorVbox->addWidget(themeBox);    colorVbox->addWidget(colorBox);   // 主题置顶，画布六色随后
    // 深浅色主题排在画布六色之上（本页顶部）

    colorVbox->addStretch(1);
    colorPage->setWidget(colorContent);
    return colorPage;
}

// 弹 QColorDialog 预选当前色；取色即写 Palette（改色 + 落盘 + 联动画布重绘），并刷新本色块。
void AppearanceDialog::onPickColor(const ColorRow& row) {
    auto* pal = &Palette::instance();
    const QColor initial = (pal->*row.getter)();
    const QColor picked = QColorDialog::getColor(initial, this, IDC_GTR(label, kDlgTitlePickColor),
                                                 QColorDialog::ShowAlphaChannel);
    if (!picked.isValid()) return;   // 用户取消。
    (pal->*row.setter)(picked);
    refreshSwatches();
}

// 从 Palette 回读六色，以背景色刷新各按钮（文字取对比色保证可读）。
void AppearanceDialog::refreshSwatches() {
    auto* pal = &Palette::instance();
    for (const ColorRow& row : colorRows_) {
        if (!row.swatch) continue;
        const QColor c = (pal->*row.getter)();
        // 深底用白字、浅底用黑字（灰度阈值粗略判定）。
        const int luminance = (c.red() * 299 + c.green() * 587 + c.blue() * 114) / 1000;
        const QString fg = luminance < 128 ? QStringLiteral("#ffffff") : QStringLiteral("#000000");
        row.swatch->setStyleSheet(
            QStringLiteral("background-color: %1; color: %2; border: 1px solid #808080;")
                .arg(c.name(), fg));
        row.swatch->setText(c.name().toUpper());
    }
}

} // namespace idc::gui
