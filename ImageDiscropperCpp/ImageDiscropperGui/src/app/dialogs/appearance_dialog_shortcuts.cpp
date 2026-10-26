// ============================================================================
// 文件：app/dialogs/appearance_dialog_shortcuts.cpp
// 作用：AppearanceDialog「快捷键」页（仅桌面 shortcutsEnabled_）——依 KeyBindings 注册表逐行建表
//       （命令名 / QKeySequenceEdit / 行内恢复按钮），改绑经缺修饰校验与冲突改绑询问后提交单例
//       即时生效，重置（单行/全部）后延后统一扫描冲突对并一次性提示。
// 分块依据：见 appearance_dialog.cpp 头注（按页四 TU 拆分）；本 TU 只构建页面部件并自行 addTab
//       （页序由装配壳按「配色 → 快捷键 → 其他」统一决定），页内控件/连接自持。
// ============================================================================
#include "app/appearance_dialog.h"

#include <QAbstractItemView>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyCombination>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>

#include "app/keybindings.h"      // KeyBindings / CommandDef / CommandId / KeyKind（行建表与改绑校验）
#include "util/gui_nums.h"        // nums::dialog（行内恢复按钮定宽）
#include "util/widget_factory.h"  // makeButton
#include "i18n/gui_texts.h"
#include "i18n/gui_i18n.h"
#include "i18n/retranslate.h"

namespace idc::gui {

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
    auto* resetAllBtn = makeButton(retranslator_, page, IDC_GT(label, kShortcutResetAllButton));
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

} // namespace idc::gui
