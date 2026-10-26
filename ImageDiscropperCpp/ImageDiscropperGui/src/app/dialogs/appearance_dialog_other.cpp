// ============================================================================
// 文件：app/dialogs/appearance_dialog_other.cpp
// 作用：AppearanceDialog「其他」页构建（buildOtherTab）+ 交互数值回显（refreshInteraction）——
//       画布交互偏好（写 CanvasPrefs）、应用语言（写 UiPrefs，对话框内即时热切换）、默认输出文件夹
//       （仅桌面，写 UiPrefs）、「确认与吸附」组（吸附写 CanvasPrefs、两项导出前确认写 UiPrefs）。
// 分块依据：见 appearance_dialog.cpp 头注（按页四 TU 拆分）；本 TU 只构建回传页面部件，
//       tab 登记与页序由装配壳负责，页内控件/连接自持。
// ============================================================================
#include "app/appearance_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include "app/app_prefs.h"        // UiPrefs（语言偏好 / 默认输出目录 / 两项导出前确认）
#include "app/canvas_prefs.h"     // CanvasPrefs（画布交互偏好 / 选区吸附）
#include "i18n/core_i18n.h"       // kSourceLanguage / kLangCodeEn：语言下拉项数据的真相源在 Core
#include "generate/pattern.h"     // generate::kMaxDimension：新建默认尺寸的上限护栏真相源在 Core
#include "mobile/touch_scroll.h"  // installTouchScroll：移动端把 QScrollArea 换装触控拖滑（桌面回落鼠标）
#include "util/gui_nums.h"        // nums::dialog / nums::app（触控下限与各数值范围）
#include "util/widget_factory.h"  // makeGroupBox / makeSpin / makeButton / makeCheckBox
#include "i18n/gui_texts.h"
#include "i18n/gui_i18n.h"
#include "i18n/retranslate.h"

namespace idc::gui {

// 其他页（声明见 appearance_dialog.h）：构建即接线——数值/复选框改即写对应单例、语言选中即
// 即时热切换并原地重放；页面部件回传由装配壳 addTab。
QWidget* AppearanceDialog::buildOtherTab() {
    auto* prefs = &CanvasPrefs::instance();
    // ===================== Tab 二：其他（交互 / 语言 / 输出）=====================
    // 本页容器局部名 content/vbox；页内三个 QGroupBox（语言 / 交互 / 输出）在末尾按「语言置顶」重排 addWidget。
    auto* otherPage = new QScrollArea(tabs_);
    otherPage->setWidgetResizable(true);
    otherPage->setFrameShape(QFrame::NoFrame);
    installTouchScroll(otherPage);   // Android：拖指滑页；桌面：回落 QScroller 鼠标协调器（行为不变）。
    auto* content = new QWidget(otherPage);
    auto* vbox = new QVBoxLayout(content);

    // ---- 其他页 · 画布交互（写 CanvasPrefs；手柄/缩放改动即时重算，新建尺寸仅记住下次用）----
    auto* interBox = makeGroupBox(retranslator_, content, IDC_GT(label, kInteractionBoxTitle));
    auto* iform = new QFormLayout(interBox);
    // 一行 = 数值框 + 行标签（bindFormRow）+ 悬停说明（bindToolTip），文案均登记重放。
    const auto addSpinRow = [this, iform](QSpinBox* spin, const i18n::TextRef& field, const i18n::TextRef& tip) {
        spin->setMinimumHeight(nums::dialog::kTouchFieldMinHeightPx);
        i18n::bindFormRow(retranslator_, iform, spin, field);
        i18n::bindToolTip(retranslator_, spin, tip);
    };
    {
        auto* s = makeSpin(nullptr, 0, nums::app::kHandlePxMax, QStringLiteral(" px"));
        i18n::bindSpecialValue(retranslator_, s, IDC_GT(label, kAutoSpecialValue));   // 0 = 跟随平台默认（桌面 8 / 触控 12+）
        s->setValue(prefs->handleScreenPx());
        connect(s, qOverload<int>(&QSpinBox::valueChanged), this, [prefs](const int v) { prefs->setHandleScreenPx(v); });
        handlePxSpin_ = s;
        addSpinRow(s, IDC_GT(label, kHandleSizeField), IDC_GT(tooltip, kHandleSizeSpin));
    }
    {
        auto* s = makeSpin(nullptr, 0, nums::app::kScrollMarginPercentMax, QStringLiteral(" %"));
        i18n::bindSpecialValue(retranslator_, s, IDC_GT(label, kNoMarginSpecialValue));   // 0 = 图像边缘即滚动极限（无边距观感）
        s->setValue(prefs->scrollMarginPercent());
        connect(s, qOverload<int>(&QSpinBox::valueChanged), this,
                [prefs](const int v) { prefs->setScrollMarginPercent(v); });
        scrollMarginSpin_ = s;
        addSpinRow(s, IDC_GT(label, kScrollMarginField), IDC_GT(tooltip, kScrollMarginSpin));
    }
    {
        auto* s = makeSpin(nullptr, nums::app::kZoomStepPercentMin, nums::app::kZoomStepPercentMax, QStringLiteral(" %"));
        s->setValue(prefs->zoomStepPercent());
        connect(s, qOverload<int>(&QSpinBox::valueChanged), this, [prefs](const int v) { prefs->setZoomStepPercent(v); });
        zoomStepSpin_ = s;
        addSpinRow(s, IDC_GT(label, kZoomStepField), IDC_GT(tooltip, kZoomStepSpin));
    }
    {
        auto* s = makeSpin(nullptr, nums::app::kZoomMinPercentLower, nums::app::kZoomMinPercentUpper, QStringLiteral(" %"));
        s->setValue(prefs->zoomMinPercent());
        connect(s, qOverload<int>(&QSpinBox::valueChanged), this, [prefs](const int v) { prefs->setZoomMinPercent(v); });
        zoomMinSpin_ = s;
        addSpinRow(s, IDC_GT(label, kZoomMinField), IDC_GT(tooltip, kZoomMinSpin));
    }
    {
        auto* s = makeSpin(nullptr, nums::app::kZoomMaxPercentLower, nums::app::kZoomMaxPercentUpper, QStringLiteral(" %"));
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
        auto* s = makeSpin(nullptr, 1, generate::kMaxDimension, QStringLiteral(" px"));
        s->setValue(prefs->newCanvasWidth());
        connect(s, qOverload<int>(&QSpinBox::valueChanged), this, [remember](int) { remember(); });
        newWidthSpin_ = s;
        addSpinRow(s, IDC_GT(label, kNewWidthField), IDC_GT(tooltip, kNewWidthSpin));
    }
    {
        auto* s = makeSpin(nullptr, 1, generate::kMaxDimension, QStringLiteral(" px"));
        s->setValue(prefs->newCanvasHeight());
        connect(s, qOverload<int>(&QSpinBox::valueChanged), this, [remember](int) { remember(); });
        newHeightSpin_ = s;
        addSpinRow(s, IDC_GT(label, kNewHeightField), IDC_GT(tooltip, kNewHeightSpin));
    }
    // ---- 其他页 · 应用语言（写 UiPrefs，**对话框内即时热切换、不关面板**）----
    // 下拉选中即：落盘偏好 → 切全局语言 → 重放本对话框自身文案 → 发 languageApplied() 令宿主主窗原地重放。
    // 全 GUI 无工作线程，单线程切换安全；模态期间主窗在信号槽内被程序化更新（不销毁、状态无损）。
    auto* langBox = makeGroupBox(retranslator_, content, IDC_GT(label, kLangBoxTitle));
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
    auto* exportBox = makeGroupBox(retranslator_, content, IDC_GT(label, kExportDirBoxTitle));
    auto* eform = new QFormLayout(exportBox);
    auto* dirRow = new QWidget(exportBox);
    auto* dirRowLay = new QHBoxLayout(dirRow);
    dirRowLay->setContentsMargins(0, 0, 0, 0);
    defaultExportDirEdit_ = new QLineEdit(dirRow);
    defaultExportDirEdit_->setMinimumHeight(nums::dialog::kTouchFieldMinHeightPx);   // 触控下限，与本页其它数值框一致
    defaultExportDirEdit_->setText(UiPrefs::instance().defaultExportDir());
    i18n::bindPlaceholder(retranslator_, defaultExportDirEdit_, IDC_GT(label, kExportDirPlaceholder));
    auto* dirBrowseBtn = makeButton(retranslator_, dirRow, IDC_GT(label, kBrowseButton));
    dirBrowseBtn->setMinimumHeight(nums::dialog::kTouchFieldMinHeightPx);
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
    // ---- 其他页 · 确认与吸附（选区吸附写 CanvasPrefs，两项导出前确认写 UiPrefs；均即时落盘）----
    // 吸附属画布交互语义，故与两项导出前确认同归本组；三项均构造时读单例初值、toggled 即时写回对应单例。
    auto* confirmBox = makeGroupBox(retranslator_, content, IDC_GT(label, kConfirmSnapBoxTitle));
    auto* cbl = new QVBoxLayout(confirmBox);
    snapCheck_ = makeCheckBox(retranslator_, confirmBox, IDC_GT(menu, kSnapToggle));
    snapCheck_->setChecked(CanvasPrefs::instance().snapEnabled());
    i18n::bindToolTip(retranslator_, snapCheck_, IDC_GT(tooltip, kSnap));
    connect(snapCheck_, &QCheckBox::toggled, this, [](const bool on) { CanvasPrefs::instance().setSnapEnabled(on); });
    confirmBurnInCheck_ = makeCheckBox(retranslator_, confirmBox, IDC_GT(menu, kConfirmBurnIn));
    confirmBurnInCheck_->setChecked(UiPrefs::instance().confirmBurnInOnExport());
    i18n::bindToolTip(retranslator_, confirmBurnInCheck_, IDC_GT(tooltip, kConfirmBurnIn));
    connect(confirmBurnInCheck_, &QCheckBox::toggled, this, [](const bool on) { UiPrefs::instance().setConfirmBurnInOnExport(on); });
    confirmRearrangeCheck_ = makeCheckBox(retranslator_, confirmBox, IDC_GT(menu, kConfirmRearrange));
    confirmRearrangeCheck_->setChecked(UiPrefs::instance().confirmRearrangeRisk());
    i18n::bindToolTip(retranslator_, confirmRearrangeCheck_, IDC_GT(tooltip, kConfirmRearrange));
    connect(confirmRearrangeCheck_, &QCheckBox::toggled, this, [](const bool on) { UiPrefs::instance().setConfirmRearrangeRisk(on); });
    cbl->addWidget(snapCheck_);
    cbl->addWidget(confirmBurnInCheck_);
    cbl->addWidget(confirmRearrangeCheck_);

    // 本页顺序按「语言置顶」（用户要求「放到顶上」）重排：应用语言在最上，其次画布交互与确认/吸附，末尾默认输出文件夹。
    vbox->addWidget(langBox);    vbox->addWidget(interBox);   // 语言置顶，交互随后
    // 应用语言排在交互/输出之上（本页顶部）
    vbox->addWidget(confirmBox);   // 确认与吸附：紧跟交互组（吸附同属画布交互语义）
    vbox->addWidget(exportBox);

    vbox->addStretch(1);
    otherPage->setWidget(content);
    return otherPage;
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
    // 吸附复选框同步（属 CanvasPrefs）：「恢复默认」回退后需刷新；阻信号避免回环。
    if (snapCheck_) { const QSignalBlocker b(snapCheck_); snapCheck_->setChecked(prefs.snapEnabled()); }
}

} // namespace idc::gui
