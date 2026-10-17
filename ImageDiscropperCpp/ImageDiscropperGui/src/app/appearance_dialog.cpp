// ============================================================================
// 文件：app/appearance_dialog.cpp
// 作用：AppearanceDialog 实现——多 Tab 内容：配色（六色 + 三 alpha 写 Palette，另附深浅色主题写 UiPrefs）、其他（交互写 CanvasPrefs /
//       语言与输出目录写 UiPrefs）、快捷键（仅桌面：KeyBindings 命令表逐行重绑，写回单例即时生效）。
// 分块依据：布局与信号连接集中在构造函数（三页各包 QScrollArea，底部提示与按钮盒常驻 Tab 之外）；
//           色块回显走 refreshSwatches，交互数值回显走 refreshInteraction（初始化与恢复默认后），
//           快捷键表行序 = KeyBindings 注册表序 = (int)CommandId，改绑经 handleShortcutEdit 校验提交、syncShortcutRow 回显。
// 语言热切换：本对话框**全程原地重放**（retranslate）——选语言即切全局语言、重放自身文案、并经
//           languageApplied() 信号令宿主主窗原地重放（不关面板、不销毁窗口）。所有面对人的文案一律
//           走 util/retranslate.h 的 bind 助手（设初值 + 登记重放闭包），与面板 retranslate() 同一机制；
//           故本类自持一份 Retranslator。下拉项数据（语言码）不可译，只重放 itemText（保 userData/currentIndex）。
// 说明：色块按钮以背景色直观示色，点开 QColorDialog 预选当前色；alpha/交互项用 QSpinBox。
//       选区吸附不在本对话框（属运行时开关，挂菜单可勾选 QAction）。「恢复默认」回退配色与交互两单例（语言/输出目录不回退）。
// ============================================================================
#include "app/appearance_dialog.h"

#include <QAbstractItemView>
#include <QColorDialog>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyCombination>
#include <QKeySequenceEdit>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringList>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>

#include <QDialogButtonBox>

#include "app/app_prefs.h"
#include "app/canvas_prefs.h"
#include "app/keybindings.h"
#include "i18n/core_i18n.h"   // kSourceLanguage / kLangCodeEn：语言下拉项数据的真相源在 Core
#include "generate/pattern.h"   // generate::kMaxDimension：新建默认尺寸的上限护栏真相源在 Core
#include "mobile/touch_scroll.h"   // installTouchScroll：移动端把 QScrollArea 换装触控拖滑（桌面回落鼠标）
#include "util/gui_nums.h"
#include "util/scrollable_dialog.h"   // limitDialogToScreen：按可用几何钳对话框最大宽 / 高（小屏 / 横屏护栏）
#include "i18n/gui_texts.h"
#include "i18n/gui_i18n.h"
#include "i18n/retranslate.h"
#include "util/theme.h"

namespace idc::gui {

AppearanceDialog::AppearanceDialog(QWidget* parent, const bool shortcutsEnabled)
    : QDialog(parent), shortcutsEnabled_(shortcutsEnabled) {
    setWindowTitle(IDC_GTR(label, kDlgTitleAppearance));
    retranslator_.add([this] { setWindowTitle(IDC_GTR(label, kDlgTitleAppearance)); });
    auto* pal = &Palette::instance();
    auto* prefs = &CanvasPrefs::instance();

    auto* outer = new QVBoxLayout(this);
    // 内容采多 Tab 布局：配色（六色 + 深浅色主题）/ 其他（交互·语言·输出）/ 快捷键（仅桌面）。每页各自包 QScrollArea 适应小屏，
    // 底部提示与「恢复默认/关闭」按钮盒常驻 Tab 之外（快捷键的复位在自身 Tab 内，互不越权）。
    tabs_ = new QTabWidget(this);
    outer->addWidget(tabs_, 1);
    // 加页 + 登记 Tab 标题重放：用 indexOf(page) 定位，避免依赖固定页序（移动端无快捷键页时索引会变）。
    const auto addTab = [this](QWidget* page, const i18n::TextRef& title) {
        tabs_->addTab(page, i18n::qs(title));
        retranslator_.add([this, page, title] { tabs_->setTabText(tabs_->indexOf(page), i18n::qs(title)); });
    };

    // ===================== Tab 一：配色（六色/三 alpha 写 Palette，附深浅色主题写 UiPrefs）=====================
    auto* colorPage = new QScrollArea(tabs_);
    colorPage->setWidgetResizable(true);
    colorPage->setFrameShape(QFrame::NoFrame);
    installTouchScroll(colorPage);   // Android：拖指滑页；桌面：回落 QScroller 鼠标协调器（行为不变）。
    auto* colorContent = new QWidget(colorPage);
    auto* colorVbox = new QVBoxLayout(colorContent);

    // ---- 配色页 · 画布六色（写 Palette，改动即时联动重绘）----
    auto* colorBox = new QGroupBox(colorContent);
    i18n::bindTitle(retranslator_, colorBox, IDC_GT(label, kPaletteBoxTitle));
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
        auto* spin = new QSpinBox;
        spin->setRange(0, 255);
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
    auto* themeBox = new QGroupBox(colorContent);
    i18n::bindTitle(retranslator_, themeBox, IDC_GT(label, kThemeBoxTitle));
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
    addTab(colorPage, IDC_GT(label, kTabColors));

    // ===================== Tab 二：其他（交互 / 语言 / 输出）=====================
    // 本页容器局部名 content/vbox；页内三个 QGroupBox（语言 / 交互 / 输出）在末尾按「语言置顶」重排 addWidget。
    auto* otherPage = new QScrollArea(tabs_);
    otherPage->setWidgetResizable(true);
    otherPage->setFrameShape(QFrame::NoFrame);
    installTouchScroll(otherPage);   // Android：拖指滑页；桌面：回落 QScroller 鼠标协调器（行为不变）。
    auto* content = new QWidget(otherPage);
    auto* vbox = new QVBoxLayout(content);

    // ---- 其他页 · 画布交互（写 CanvasPrefs；手柄/缩放改动即时重算，新建尺寸仅记住下次用）----
    auto* interBox = new QGroupBox(content);
    i18n::bindTitle(retranslator_, interBox, IDC_GT(label, kInteractionBoxTitle));
    auto* iform = new QFormLayout(interBox);
    // 一行 = 数值框 + 行标签（bindFormRow）+ 悬停说明（bindToolTip），文案均登记重放。
    const auto addSpinRow = [this, iform](QSpinBox* spin, const i18n::TextRef& field, const i18n::TextRef& tip) {
        spin->setMinimumHeight(nums::dialog::kTouchFieldMinHeightPx);
        i18n::bindFormRow(retranslator_, iform, spin, field);
        i18n::bindToolTip(retranslator_, spin, tip);
    };
    {
        auto* s = new QSpinBox;
        s->setRange(0, nums::app::kHandlePxMax);
        i18n::bindSpecialValue(retranslator_, s, IDC_GT(label, kAutoSpecialValue));   // 0 = 跟随平台默认（桌面 8 / 触控 12+）
        s->setSuffix(QStringLiteral(" px"));
        s->setValue(prefs->handleScreenPx());
        connect(s, qOverload<int>(&QSpinBox::valueChanged), this, [prefs](const int v) { prefs->setHandleScreenPx(v); });
        handlePxSpin_ = s;
        addSpinRow(s, IDC_GT(label, kHandleSizeField), IDC_GT(tooltip, kHandleSizeSpin));
    }
    {
        auto* s = new QSpinBox;
        s->setRange(0, nums::app::kScrollMarginPercentMax);
        i18n::bindSpecialValue(retranslator_, s, IDC_GT(label, kNoMarginSpecialValue));   // 0 = 图像边缘即滚动极限（无边距观感）
        s->setSuffix(QStringLiteral(" %"));
        s->setValue(prefs->scrollMarginPercent());
        connect(s, qOverload<int>(&QSpinBox::valueChanged), this,
                [prefs](const int v) { prefs->setScrollMarginPercent(v); });
        scrollMarginSpin_ = s;
        addSpinRow(s, IDC_GT(label, kScrollMarginField), IDC_GT(tooltip, kScrollMarginSpin));
    }
    {
        auto* s = new QSpinBox;
        s->setRange(nums::app::kZoomStepPercentMin, nums::app::kZoomStepPercentMax);
        s->setSuffix(QStringLiteral(" %"));
        s->setValue(prefs->zoomStepPercent());
        connect(s, qOverload<int>(&QSpinBox::valueChanged), this, [prefs](const int v) { prefs->setZoomStepPercent(v); });
        zoomStepSpin_ = s;
        addSpinRow(s, IDC_GT(label, kZoomStepField), IDC_GT(tooltip, kZoomStepSpin));
    }
    {
        auto* s = new QSpinBox;
        s->setRange(nums::app::kZoomMinPercentLower, nums::app::kZoomMinPercentUpper);
        s->setSuffix(QStringLiteral(" %"));
        s->setValue(prefs->zoomMinPercent());
        connect(s, qOverload<int>(&QSpinBox::valueChanged), this, [prefs](const int v) { prefs->setZoomMinPercent(v); });
        zoomMinSpin_ = s;
        addSpinRow(s, IDC_GT(label, kZoomMinField), IDC_GT(tooltip, kZoomMinSpin));
    }
    {
        auto* s = new QSpinBox;
        s->setRange(nums::app::kZoomMaxPercentLower, nums::app::kZoomMaxPercentUpper);
        s->setSuffix(QStringLiteral(" %"));
        s->setValue(prefs->zoomMaxPercent());
        connect(s, qOverload<int>(&QSpinBox::valueChanged), this, [prefs](const int v) { prefs->setZoomMaxPercent(v); });
        zoomMaxSpin_ = s;
        addSpinRow(s, IDC_GT(label, kZoomMaxField), IDC_GT(tooltip, kZoomMaxSpin));
    }
    // 新建默认尺寸：任一变更即整对回写（记住下次新建初值）。
    const auto remember = [this, prefs] {
        prefs->rememberNewCanvasSize(newWidthSpin_->value(), newHeightSpin_->value());
    };
    {
        auto* s = new QSpinBox;
        s->setRange(1, generate::kMaxDimension);
        s->setSuffix(QStringLiteral(" px"));
        s->setValue(prefs->newCanvasWidth());
        connect(s, qOverload<int>(&QSpinBox::valueChanged), this, [remember](int) { remember(); });
        newWidthSpin_ = s;
        addSpinRow(s, IDC_GT(label, kNewWidthField), IDC_GT(tooltip, kNewWidthSpin));
    }
    {
        auto* s = new QSpinBox;
        s->setRange(1, generate::kMaxDimension);
        s->setSuffix(QStringLiteral(" px"));
        s->setValue(prefs->newCanvasHeight());
        connect(s, qOverload<int>(&QSpinBox::valueChanged), this, [remember](int) { remember(); });
        newHeightSpin_ = s;
        addSpinRow(s, IDC_GT(label, kNewHeightField), IDC_GT(tooltip, kNewHeightSpin));
    }
    // ---- 其他页 · 应用语言（写 UiPrefs，**对话框内即时热切换、不关面板**）----
    // 下拉选中即：落盘偏好 → 切全局语言 → 重放本对话框自身文案 → 发 languageApplied() 令宿主主窗原地重放。
    // 全 GUI 无工作线程，单线程切换安全；模态期间主窗在信号槽内被程序化更新（不销毁、状态无损）。
    auto* langBox = new QGroupBox(content);
    i18n::bindTitle(retranslator_, langBox, IDC_GT(label, kLangBoxTitle));
    auto* lform = new QFormLayout(langBox);
    langCombo_ = new QComboBox;
    langCombo_->setMinimumHeight(nums::dialog::kTouchFieldMinHeightPx);   // 触控下限，与本页其它数值框一致
    // 三项的**显示文字**可译，**项数据**（语言码）不可译：取 ident 与 Core 常量。
    // “English” 一类语言名以其自身书写展示是惯例，故本条在任何语言下都不进译文表（bindItemTexts 重放时逐字不变）。
    langCombo_->addItem(IDC_GTR(label, kLangFollowSystem),
                        QString::fromUtf8(texts::ident::kLangAuto));
    langCombo_->addItem(IDC_GTR(label, kLangZhCn),
                        QString::fromUtf8(core::kSourceLanguage));
    langCombo_->addItem(IDC_GTR(label, kLangEn),
                        QString::fromUtf8(core::kLangCodeEn));
    i18n::bindItemTexts(retranslator_, langCombo_,
                        {IDC_GT(label, kLangFollowSystem), IDC_GT(label, kLangZhCn), IDC_GT(label, kLangEn)});
    // 回显的是**已存偏好**（应与本次生效语言一致：本对话框切换即同步）；未知旧值落到首项「跟随系统」。
    const QString pref = UiPrefs::instance().language();
    for (int i = 0; i < langCombo_->count(); ++i) {
        if (langCombo_->itemData(i).toString() == pref) { langCombo_->setCurrentIndex(i); break; }
    }
    // activated（仅用户操作）而非 currentIndexChanged：避开上面回显时误触发切换。
    connect(langCombo_, qOverload<int>(&QComboBox::activated), this, [this] {
        const QString code = langCombo_->currentData().toString();
        UiPrefs::instance().setLanguage(code);
        i18n::setupFromPreference(code);   // 立即切全局语言：其后的重放闭包才会取到新译文
        retranslate();                      // 本对话框原地换文（面板不关）
        emit languageApplied();             // 通知宿主（MainWindow）原地重放全部菜单/面板
    });
    i18n::bindFormRow(retranslator_, lform, langCombo_, IDC_GT(label, kLangField));
    auto* langHint = new QLabel(langBox);
    langHint->setWordWrap(true);
    i18n::bindText(retranslator_, langHint, IDC_GT(label, kLangApplyHint));
    lform->addRow(QString(), langHint);

    // ---- 其他页 · 默认输出文件夹（写 UiPrefs，**仅桌面**；即时落盘）----
    // 行内可手输路径 + 「浏览…」选目录；构造时回显一次已存偏好。文案经 bind 助手随语言热切换一并重放。
    // Android 导出走相册固定目录、无文件夹概念，故本项不参与（export_controller 的 Android 分支不读本值）。
    auto* exportBox = new QGroupBox(content);
    i18n::bindTitle(retranslator_, exportBox, IDC_GT(label, kExportDirBoxTitle));
    auto* eform = new QFormLayout(exportBox);
    auto* dirRow = new QWidget(exportBox);
    auto* dirRowLay = new QHBoxLayout(dirRow);
    dirRowLay->setContentsMargins(0, 0, 0, 0);
    defaultExportDirEdit_ = new QLineEdit(dirRow);
    defaultExportDirEdit_->setMinimumHeight(nums::dialog::kTouchFieldMinHeightPx);   // 触控下限，与本页其它数值框一致
    defaultExportDirEdit_->setText(UiPrefs::instance().defaultExportDir());
    i18n::bindPlaceholder(retranslator_, defaultExportDirEdit_, IDC_GT(label, kExportDirPlaceholder));
    auto* dirBrowseBtn = new QPushButton(dirRow);
    dirBrowseBtn->setMinimumHeight(nums::dialog::kTouchFieldMinHeightPx);
    i18n::bindButton(retranslator_, dirBrowseBtn, IDC_GT(label, kBrowseButton));
    dirRowLay->addWidget(defaultExportDirEdit_, 1);
    dirRowLay->addWidget(dirBrowseBtn);
    i18n::bindFormRow(retranslator_, eform, dirRow, IDC_GT(label, kExportDirField));
    auto* dirHint = new QLabel(exportBox);
    dirHint->setWordWrap(true);
    i18n::bindText(retranslator_, dirHint, IDC_GT(label, kExportDirHint));
    eform->addRow(QString(), dirHint);
    // 手输提交（失焦/回车）与浏览选择均即时写 UiPrefs；空串亦落盘（= 清除预设，回退上次使用）。
    connect(defaultExportDirEdit_, &QLineEdit::editingFinished, this, [this] {
        UiPrefs::instance().setDefaultExportDir(defaultExportDirEdit_->text().trimmed());
    });
    connect(dirBrowseBtn, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, IDC_GTR(label, kDlgTitleChooseOutDir),
                                                            defaultExportDirEdit_->text());
        if (d.isEmpty()) return;   // 取消：不改写。
        defaultExportDirEdit_->setText(d);
        UiPrefs::instance().setDefaultExportDir(d);
    });
    // 本页顺序按「语言置顶」（用户要求「放到顶上」）重排：应用语言在最上，其次画布交互，末尾默认输出文件夹。
    vbox->addWidget(langBox);    vbox->addWidget(interBox);   // 语言置顶，交互随后
    // 应用语言排在交互/输出之上（本页顶部）
    vbox->addWidget(exportBox);

    vbox->addStretch(1);
    otherPage->setWidget(content);
    // otherPage 延后到快捷键 Tab 之后添加（用户要求「其他放快捷键和配色后面」）

    // ===================== Tab 三：快捷键（仅桌面 shortcutsEnabled_）=====================
    if (shortcutsEnabled_) buildShortcutsTab();    addTab(otherPage, IDC_GT(label, kTabOther));

    auto* hint = new QLabel(this);
    hint->setWordWrap(true);
    i18n::bindText(retranslator_, hint, IDC_GT(label, kAppearanceHint));
    outer->addWidget(hint);

    auto* buttons = new QDialogButtonBox;
    auto* restoreBtn = buttons->addButton(QString(), QDialogButtonBox::ResetRole);
    i18n::bindButton(retranslator_, restoreBtn, IDC_GT(label, kRestoreDefaultsButton));
    buttons->addButton(QDialogButtonBox::Close);   // 标准按钮文字由 Qt 自身译文承担，不进本表。
    connect(restoreBtn, &QPushButton::clicked, this, [this, pal, prefs] {
        pal->restoreDefaults();
        prefs->restoreDefaults();
        refreshSwatches();
        {
            const QSignalBlocker b1(keepAlphaSpin_);
            const QSignalBlocker b2(deleteAlphaSpin_);
            const QSignalBlocker b3(selectionAlphaSpin_);
            keepAlphaSpin_->setValue(pal->keepAlpha());
            deleteAlphaSpin_->setValue(pal->deleteAlpha());
            selectionAlphaSpin_->setValue(pal->selectionFillAlpha());
        }
        refreshInteraction();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept); // Close：直接关（改动已即时落盘）。
    outer->addWidget(buttons);

    refreshSwatches();
    refreshInteraction();

    // 屏幕尺寸护栏：小屏 / 横屏时按可用几何钳最大宽 / 高，内容超出由各页内部滚动区消化、
    // 底部提示与按钮行常驻 Tab 之外→恒可关闭；旋转后重钳、部件零重建。
    limitDialogToScreen(this);
}

// 语言热切换重放：逐条运行登记的闭包，按当前全局语言改写本对话框全部面对人的文案。
// 数值/色块回显走各自单例，与语言无关，故不在此重跑（retranslate 只换文字）。
void AppearanceDialog::retranslate() const {
    retranslator_.run();
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

// 从 CanvasPrefs 回读交互偏好刷新七个数值框（阻塞信号防止回写循环）。
void AppearanceDialog::refreshInteraction() const {
    const auto& prefs = CanvasPrefs::instance();
    const QSignalBlocker b1(handlePxSpin_);
    const QSignalBlocker b2(scrollMarginSpin_);
    const QSignalBlocker b3(zoomStepSpin_);
    const QSignalBlocker b4(zoomMinSpin_);
    const QSignalBlocker b5(zoomMaxSpin_);
    const QSignalBlocker b6(newWidthSpin_);
    const QSignalBlocker b7(newHeightSpin_);
    handlePxSpin_->setValue(prefs.handleScreenPx());
    scrollMarginSpin_->setValue(prefs.scrollMarginPercent());
    zoomStepSpin_->setValue(prefs.zoomStepPercent());
    zoomMinSpin_->setValue(prefs.zoomMinPercent());
    zoomMaxSpin_->setValue(prefs.zoomMaxPercent());
    newWidthSpin_->setValue(prefs.newCanvasWidth());
    newHeightSpin_->setValue(prefs.newCanvasHeight());
}

// 快捷键 Tab：依 KeyBindings 注册表逐行建表（列 0 命令名 / 列 1 QKeySequenceEdit / 列 2 行内恢复按钮）。
// 行序 = 注册表序 = (int)CommandId，供 changed 信号按行回显；命令名列与表头/提示经 retranslator 随语言热切换重放。
void AppearanceDialog::buildShortcutsTab() {
    const auto& kb = KeyBindings::instance();
    const auto& reg = KeyBindings::registry();

    auto* page = new QWidget(tabs_);
    auto* lay = new QVBoxLayout(page);

    auto* hint = new QLabel(page);
    hint->setWordWrap(true);
    i18n::bindText(retranslator_, hint, IDC_GT(label, kShortcutHint));
    lay->addWidget(hint);

    shortcutTable_ = new QTableWidget(static_cast<int>(reg.size()), 3, page);
    shortcutTable_->verticalHeader()->setVisible(false);
    shortcutTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    shortcutTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    shortcutTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    shortcutTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);   // 命令名列不可编辑（只经编辑器改键）。
    // 表头（命令 / 快捷键）经 retranslator 重放；第 3 列（恢复按钮）无表头。
    retranslator_.add([this] {
        shortcutTable_->setHorizontalHeaderLabels({i18n::qs(IDC_GT(label, kShortcutColCommand)),
                                                   i18n::qs(IDC_GT(label, kShortcutColKey)), QString()});
    });

    for (int r = 0; r < static_cast<int>(reg.size()); ++r) {
        const CommandId id = reg[r].id;
        // 列 0：命令名（取 registry 的 display，可译）；初值与语言重放均经 i18n::qs。
        shortcutTable_->setItem(r, 0, new QTableWidgetItem(i18n::qs(KeyBindings::displayOf(id))));
        retranslator_.add([this, r, id] {
            if (auto* it = shortcutTable_->item(r, 0))
                it->setText(i18n::qs(KeyBindings::displayOf(id)));
        });
        // 列 1：QKeySequenceEdit，回显当前生效键；改动交 handleShortcutEdit 校验提交。
        auto* edit = new QKeySequenceEdit(shortcutTable_);
        edit->setKeySequence(kb.effective(id));
        shortcutTable_->setCellWidget(r, 1, edit);
        connect(edit, &QKeySequenceEdit::keySequenceChanged, this,
                [this, id](const QKeySequence& seq) { handleShortcutEdit(id, seq); });
        // 列 2：行内「恢复默认」小按钮（字符 ↺，不译；悬停提示经 bind 重放）。
        auto* resetOneBtn = new QPushButton(QStringLiteral("\u21ba"), shortcutTable_);
        resetOneBtn->setFixedWidth(nums::dialog::kTouchFieldMinHeightPx);
        i18n::bindToolTip(retranslator_, resetOneBtn, IDC_GT(label, kShortcutResetRowTip));
        shortcutTable_->setCellWidget(r, 2, resetOneBtn);
        connect(resetOneBtn, &QPushButton::clicked, this, [this, id] {
            KeyBindings::instance().resetOne(id);
            scheduleConflictReport();   // 回落默认可能与他命令改绑后的键静默撞键，延后统一提示。
        });
    }
    lay->addWidget(shortcutTable_, 1);   // 表格先铺满；「全部恢复默认」按钮由下方 bottomRow 放到末尾

    // 「全部恢复默认」按钮移到本页末尾（表格之下）：先浏览全部命令，末了才一键复位。
    auto* resetAllBtn = new QPushButton(page);
    i18n::bindButton(retranslator_, resetAllBtn, IDC_GT(label, kShortcutResetAllButton));
    connect(resetAllBtn, &QPushButton::clicked, this, [this] {
        KeyBindings::instance().resetAll();
        scheduleConflictReport();   // 全量回落默认后延后扫描：默认键彼此正交，仅当与尚未回落的自定义项相撞时提示。
    });
    auto* bottomRow = new QHBoxLayout();
    bottomRow->addStretch(1);
    bottomRow->addWidget(resetAllBtn);
    lay->addLayout(bottomRow);

    // 外部改绑（含冲突时“改绑清除”他命令）即时回显对应行。
    connect(&kb, &KeyBindings::changed, this, [this](const CommandId id) { syncShortcutRow(id); });

    tabs_->addTab(page, i18n::qs(IDC_GT(label, kTabShortcuts)));
    retranslator_.add([this, page] {
        tabs_->setTabText(tabs_->indexOf(page), i18n::qs(IDC_GT(label, kTabShortcuts)));
    });
}

// 校验并提交一行快捷键重绑。raw 来自 QKeySequenceEdit（可能含未完成的纯修饰），取其 primary：
//  · 纯修饰（按住 Ctrl 未敲键）：序列不完整，不动，保留上一次绑定；
//  · 空序列 = 解绑（两类命令均允许，setSequence 传空）；
//  · Action 类须含 Ctrl 或 Alt，否则弹提示并回退本行（杜绝裸字母/纯 Shift 抢占文本输入）；
//  · 命中他命令 → 询问「改绑清除」：Yes 先清空占用者再提交，No 回退。提交走 setSequence（即时生效 + 落盘 + changed）。
void AppearanceDialog::handleShortcutEdit(const CommandId id, const QKeySequence& raw) {
    // 重入守卫：QKeySequenceEdit 一次按键可能连发两次 keySequenceChanged，且第一次的模态框嵌套事件循环会把第二次投递进来；
    // 正在处理时直接忽略，避免同一校验/冲突框连弹两次。RAII 保证各 return 分支均复位。
    if (handlingShortcut_) return;
    struct ReentryGuard {
        bool& f;
        explicit ReentryGuard(bool& x) : f(x) { f = true; }
        ~ReentryGuard() { f = false; }
    } guard{handlingShortcut_};
    auto& kb = KeyBindings::instance();
    // 取 primary（第一键序）；忽略多 alternate（编辑器只编单序列）。
    QKeySequence primary;
    if (raw.count() > 0) primary = QKeySequence(raw[0]);
    // 无净变化即忽略：弹模态框会让当前编辑器失焦，失焦可能重提交与生效键相同的 primary（尤其多 alternate 命令会把 alternate 抹成单键）；
    // 只比 primary，同则直接返回，避免误弹校验/冲突框与误抹 alternate。
    const QKeySequence cur = kb.effective(id);
    QKeySequence curPrimary;
    if (cur.count() > 0) curPrimary = QKeySequence(cur[0]);
    if (primary == curPrimary) return;
    if (!primary.isEmpty()) {
        // QKeySequence[0] 在 Qt6 是 QKeyCombination：拆 key/modifiers 判定，不走弃用的 →int。
        const QKeyCombination kc = primary[0];
        const int plainKey = kc.key();
        const Qt::KeyboardModifiers mods = kc.keyboardModifiers();
        if (plainKey == 0) return;   // 纯修饰（按住 Ctrl 未敲键）：不完整，保留上一次绑定。
        // Action 类校验：非空绑定必须含 Ctrl 或 Alt（空=解绑不受此限）。
        if (KeyBindings::kindOf(id) == KeyKind::Action && !(mods & (Qt::ControlModifier | Qt::AltModifier))) {
            QMessageBox::warning(this, IDC_GTR(label, kDlgTitleAppearance), IDC_GTR(msg, kShortcutNeedModifier));
            syncShortcutRow(id);   // 回退到本行已提交值。
            return;
        }
        // 冲突：primary 已被 exclude=id 之外命令占用 → 询问是否抢占。
        if (const auto other = kb.conflictOf(primary, id)) {
            const QString otherName = i18n::qs(KeyBindings::displayOf(*other));
            const auto ans = QMessageBox::question(
                this, IDC_GTR(msg, kShortcutConflictTitle),
                IDC_GTR(msg, kShortcutConflictBody).arg(otherName),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (ans == QMessageBox::Yes) {
                kb.setSequence(*other, QKeySequence());   // 清除占用者（触发其行回显）。
            } else {
                syncShortcutRow(id);                       // 取消：回退本行。
                return;
            }
        }
    }

    kb.setSequence(id, primary);   // 提交（含解绑的空序列）。
}

// 把某行编辑框回显为 effective(id)：阻塞其信号避免回环。既供外部 changed 联动，也供校验失败/取消时回退本行。
void AppearanceDialog::syncShortcutRow(const CommandId id) const {
    if (!shortcutTable_) return;
    const int r = static_cast<int>(id);
    if (r < 0 || r >= shortcutTable_->rowCount()) return;
    if (auto* edit = qobject_cast<QKeySequenceEdit*>(shortcutTable_->cellWidget(r, 1))) {
        const QSignalBlocker b(edit);
        edit->setKeySequence(KeyBindings::instance().effective(id));
    }
}

// 把冲突提示延后到当前点击事件处理完后的事件轮次（QTimer::singleShot(0)）。
// 目的：从 clicked 槽里**同步**弹模态框，会因按钮激活态/焦点回落导致同一次点击派生多次 clicked；仅靠重入守卫拦不住落在后续事件轮次的第二次。
// 延后后，多个 clicked 均先落到本方法：靠 pending 标志合并为一次排期，定时器触发时只扫一次、只弹一次。
void AppearanceDialog::scheduleConflictReport() {
    if (conflictReportPending_) return;   // 已有待处理的提示：合并同一次点击的重复触发。
    conflictReportPending_ = true;
    QTimer::singleShot(0, this, [this] {
        reportShortcutConflicts();
        conflictReportPending_ = false;   // 弹窗关闭后再放行：令合并窗口覆盖整个模态存续期，期间的重复 clicked 均归入本次。
    });
}

// 重置（单行 ↺ / 全部）把命令回落成注册表默认键；若他命令已被用户改绑到同一键，则二者静默撞键
//（Action 类由 Qt 按注册序优先分派、Route 类 matchRoute 首命中）。此处在重置后统一扫一遍全表、
// 逐对（按 id 序去重）汇总冲突并一次性弹窗，交由用户回到对应行自行处置——本层不自动抢占（抢占只用于主动改绑）。
void AppearanceDialog::reportShortcutConflicts() {
    if (reportingConflicts_) return;   // 重入守卫：下面的 QMessageBox::warning 跑嵌套事件循环，期间若 clicked 被再投递则直接忽略，避免同一提示连弹两次。
    const auto& kb = KeyBindings::instance();
    QStringList pairs;
    for (const CommandDef& def : KeyBindings::registry()) {
        const QKeySequence seq = kb.effective(def.id);
        if (seq.count() == 0) continue;   // 解绑不与他人冲突。
        // conflictOf 返回首个占用者；只在 id < 对方时记录，避免一对冲突正反各记一次。
        if (const auto other = kb.conflictOf(seq, def.id); other && def.id < *other) {
            pairs << QStringLiteral("%1 \u2194 %2")
                         .arg(i18n::qs(KeyBindings::displayOf(def.id)), i18n::qs(KeyBindings::displayOf(*other)));
        }
    }
    if (pairs.isEmpty()) return;
    reportingConflicts_ = true;   // 括住模态弹窗的嵌套事件循环。
    QMessageBox::warning(this, IDC_GTR(msg, kShortcutConflictTitle),
                         IDC_GTR(msg, kShortcutResetConflictBody).arg(pairs.join(QStringLiteral("\n"))));
    reportingConflicts_ = false;
}

// 焦点若在某个 QKeySequenceEdit（或其内部编辑子控件）上，就地消费 Esc 仅放弃本次录制，
// 不让事件冒泡到 QDialog::keyPressEvent 的 reject()——否则编辑「清除选区」行时按 Esc 会直接关掉整个对话框。
void AppearanceDialog::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        for (QWidget* w = focusWidget(); w != nullptr; w = w->parentWidget()) {
            if (qobject_cast<QKeySequenceEdit*>(w) != nullptr) {
                event->accept();
                return;
            }
        }
    }
    QDialog::keyPressEvent(event);
}

} // namespace idc::gui
