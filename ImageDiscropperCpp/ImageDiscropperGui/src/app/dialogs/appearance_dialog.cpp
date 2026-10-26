// ============================================================================
// 文件：app/dialogs/appearance_dialog.cpp
// 作用：AppearanceDialog 的装配壳实现——多 Tab 容器的组装、底部「恢复默认/关闭」常驻区、
//       语言热切换重放（retranslate）与快捷键录制态的 Esc 拦截。
// 分块依据：对话框按页拆四个 TU——本文件（装配壳）/ appearance_dialog_colors.cpp（配色页：
//       六色 + 三 alpha 写 Palette、深浅色主题写 UiPrefs）/ _other（其他页：交互写 CanvasPrefs、
//       语言与输出目录写 UiPrefs、确认与吸附组）/ _shortcuts（快捷键页：KeyBindings 命令表逐行
//       重绑，写回单例即时生效）。每页构建器只回传页面部件，tab 登记与页序统一留在本构造
//       （页序＝配色 → 快捷键（仅桌面）→ 其他，用户要求「其他放快捷键和配色后面」）。
// 语言热切换：本对话框**全程原地重放**（retranslate）——选语言即切全局语言、重放自身文案、并经
//           languageApplied() 信号令宿主主窗原地重放（不关面板、不销毁窗口）。所有面对人的文案一律
//           走 util/retranslate.h 的 bind 助手（设初值 + 登记重放闭包），与面板 retranslate() 同一机制；
//           故本类自持一份 Retranslator。下拉项数据（语言码）不可译，只重放 itemText（保 userData/currentIndex）。
// 说明：「恢复默认」回退配色与交互两单例（含吸附；语言/输出目录/确认偏好不回退）。
// ============================================================================
#include "app/appearance_dialog.h"

#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include "app/canvas_prefs.h"      // CanvasPrefs（「恢复默认」回退交互单例）
#include "util/scrollable_dialog.h"   // limitDialogToScreen：按可用几何钳对话框最大宽 / 高（小屏 / 横屏护栏）
#include "i18n/gui_texts.h"
#include "i18n/gui_i18n.h"
#include "i18n/retranslate.h"

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

    // 页序：配色 → （快捷键，仅桌面）→ 其他（用户要求「其他放快捷键和配色后面」）。
    // 三页构建器按页拆 TU：配色 buildColorsTab 在 appearance_dialog_colors.cpp、其他 buildOtherTab 在
    // _other、快捷键 buildShortcutsTab（自行 addTab）在 _shortcuts；本构造只做 tab 组装与
    // 「恢复默认/关闭」常驻区装配。
    addTab(buildColorsTab(), IDC_GT(label, kTabColors));
    if (shortcutsEnabled_) buildShortcutsTab();    addTab(buildOtherTab(), IDC_GT(label, kTabOther));
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
