// ============================================================================
// 文件：app/keybindings.cpp
// 作用：KeyBindings 单例实现——命令注册表成文、QSettings（shortcuts/ 组）载入/落盘、
//       生效键查询、双应用路径（Action 类 setShortcut / Route 类供 KeyRouter 匹配）、冲突检测与恢复默认。
// 分块依据：持久化组名取 texts::ident（常量层），键名取本件 idKey（与注册表同源单点）；
//           注册表默认键与显示 TextRef 仅此一处成文，界面与分派各自读取，杜绝两处漂移。
// 说明：QAction 句柄表按进程内唯一实例存于函数级 static（生命周期随单例，注册即回填）；
//       keyCombo 与 QKeySequence[i] 均为 Qt 原生「key|modifiers」整型组合，逐位比较即匹配。
// ============================================================================
#include "app/keybindings.h"

#include <QAction>
#include <QHash>
#include <QKeyCombination>
#include <QSettings>

#include "i18n/gui_texts.h"

namespace idc::gui {

namespace {

// 组合键类命令的 QAction 句柄表（(int)CommandId → QAction*）：注册于菜单装配期，改键即时回填 shortcut。
QHash<int, QAction*>& actionHandles() {
    static QHash<int, QAction*> s;
    return s;
}

int idxOf(const CommandId id) { return static_cast<int>(id); }

} // namespace

// ---------------------------------------------------------------------------
// 命令注册表：id + 应用类 + 默认键 + 可译显示名（复用 menu 组，命令名不再新增译文）。
// 默认键以 QKeySequence::StandardKey 构造以保平台原生；DeleteAnnotation 携 Del/Backspace 两 alternate。
// ---------------------------------------------------------------------------
const std::vector<CommandDef>& KeyBindings::registry() {
    static const std::vector<CommandDef> s = {
        // —— 组合键类（Action：QAction::setShortcut）——
        {CommandId::New, KeyKind::Action, QKeySequence(QKeySequence::New), IDC_GT(menu, kFileNewCanvas)},
        {CommandId::Open, KeyKind::Action, QKeySequence(QKeySequence::Open), IDC_GT(menu, kFileOpenImage)},
        {CommandId::Export, KeyKind::Action, QKeySequence(QKeySequence::Save), IDC_GT(menu, kFileExport)},
        {CommandId::Undo, KeyKind::Action, QKeySequence(QKeySequence::Undo), IDC_GT(menu, kEditUndo)},
        {CommandId::Redo, KeyKind::Action, QKeySequence(QKeySequence::Redo), IDC_GT(menu, kEditRedo)},
        {CommandId::ZoomIn, KeyKind::Action, QKeySequence(QKeySequence::ZoomIn), IDC_GT(menu, kZoomIn)},
        {CommandId::ZoomOut, KeyKind::Action, QKeySequence(QKeySequence::ZoomOut), IDC_GT(menu, kZoomOut)},
        {CommandId::Fit, KeyKind::Action, QKeySequence(Qt::CTRL | Qt::Key_0), IDC_GT(menu, kFitToWindow)},
        // —— 单键类（Route：KeyRouter 表驱动分派，不走 QAction/QShortcut）——
        {CommandId::ModeL1, KeyKind::Route, QKeySequence(Qt::Key_1), IDC_GT(menu, kModeL1)},
        {CommandId::ModeL2, KeyKind::Route, QKeySequence(Qt::Key_2), IDC_GT(menu, kModeL2)},
        {CommandId::ModeL3, KeyKind::Route, QKeySequence(Qt::Key_3), IDC_GT(menu, kModeL3)},
        {CommandId::PolarityKeep, KeyKind::Route, QKeySequence(Qt::Key_K), IDC_GT(menu, kPolarityKeep)},
        {CommandId::PolarityRemove, KeyKind::Route, QKeySequence(Qt::Key_R), IDC_GT(menu, kPolarityRemove)},
        {CommandId::ClearSelection, KeyKind::Route, QKeySequence(Qt::Key_Escape), IDC_GT(menu, kEditClearSel)},
        {CommandId::DeleteAnnotation, KeyKind::Route,
         QKeySequence{static_cast<int>(Qt::Key_Delete), static_cast<int>(Qt::Key_Backspace)},
         IDC_GT(menu, kAnnoDeleteSelected)},
    };
    return s;
}

KeyBindings& KeyBindings::instance() {
    // Meyers 单例：C++11 起线程安全；生命周期随进程（QSettings 写入在变更点即时完成）。
    static KeyBindings s;
    return s;
}

KeyBindings::KeyBindings(QObject* parent) : QObject(parent) {
    override_.resize(idxOf(CommandId::Count));
    customized_.assign(idxOf(CommandId::Count), false);
    load();
}

void KeyBindings::load() {
    QSettings st;
    st.beginGroup(QString::fromUtf8(texts::ident::kGroupShortcuts));
    for (const CommandDef& def : registry()) {
        const QString key = idKey(def.id);
        if (!st.contains(key)) continue;   // 缺席 = 未覆盖，回落默认。
        override_[idxOf(def.id)] = QKeySequence(st.value(key).toString(), QKeySequence::PortableText);
        customized_[idxOf(def.id)] = true;
    }
    st.endGroup();
}

QString KeyBindings::idKey(const CommandId id) {
    switch (id) {
        case CommandId::New: return QStringLiteral("new");
        case CommandId::Open: return QStringLiteral("open");
        case CommandId::Export: return QStringLiteral("export");
        case CommandId::Undo: return QStringLiteral("undo");
        case CommandId::Redo: return QStringLiteral("redo");
        case CommandId::ZoomIn: return QStringLiteral("zoomIn");
        case CommandId::ZoomOut: return QStringLiteral("zoomOut");
        case CommandId::Fit: return QStringLiteral("fit");
        case CommandId::ModeL1: return QStringLiteral("mode1");
        case CommandId::ModeL2: return QStringLiteral("mode2");
        case CommandId::ModeL3: return QStringLiteral("mode3");
        case CommandId::PolarityKeep: return QStringLiteral("polKeep");
        case CommandId::PolarityRemove: return QStringLiteral("polRemove");
        case CommandId::ClearSelection: return QStringLiteral("clearSel");
        case CommandId::DeleteAnnotation: return QStringLiteral("delAnno");
        case CommandId::Count: break;
    }
    return {};
}

QKeySequence KeyBindings::defaultSequence(const CommandId id) {
    return registry()[idxOf(id)].defaultSeq;
}

KeyKind KeyBindings::kindOf(const CommandId id) {
    return registry()[idxOf(id)].kind;
}

i18n::TextRef KeyBindings::displayOf(const CommandId id) {
    return registry()[idxOf(id)].display;
}

bool KeyBindings::isCustomized(const CommandId id) const {
    return customized_[idxOf(id)];
}

QKeySequence KeyBindings::effective(const CommandId id) const {
    return customized_[idxOf(id)] ? override_[idxOf(id)] : registry()[idxOf(id)].defaultSeq;
}

void KeyBindings::registerAction(const CommandId id, QAction* action) const {
    if (kindOf(id) != KeyKind::Action || !action) return;
    actionHandles()[idxOf(id)] = action;
    applyToAction(id);   // 立即按 effective（含已存覆盖）应用，菜单装配期一次到位。
}

void KeyBindings::applyToAction(const CommandId id) const {
    if (kindOf(id) != KeyKind::Action) return;
    if (QAction* a = actionHandles().value(idxOf(id), nullptr)) a->setShortcut(effective(id));
}

void KeyBindings::setSequence(const CommandId id, const QKeySequence& seq) {
    override_[idxOf(id)] = seq;
    customized_[idxOf(id)] = true;
    saveOne(id);
    applyToAction(id);
    emit changed(id);
}

void KeyBindings::resetOne(const CommandId id) {
    // 回落默认可能与「他命令已被改绑到本命令默认键」静默撞键：本层只负责数据回落，不做冲突抢占，
    // 由调用方（外观对话框重置按钮）在其后扫描并提示，用户自行处置。
    customized_[idxOf(id)] = false;
    override_[idxOf(id)] = QKeySequence();
    removeOne(id);
    applyToAction(id);
    emit changed(id);
}

void KeyBindings::resetAll() {
    for (const CommandDef& def : registry()) resetOne(def.id);
}

void KeyBindings::saveOne(const CommandId id) const {
    QSettings st;
    st.beginGroup(QString::fromUtf8(texts::ident::kGroupShortcuts));
    // 存 PortableText；空序列落成空串（仍算「已覆盖」= 解绑）。
    st.setValue(idKey(id), override_[idxOf(id)].toString(QKeySequence::PortableText));
    st.endGroup();
    st.sync();
}

void KeyBindings::removeOne(const CommandId id) {
    QSettings st;
    st.beginGroup(QString::fromUtf8(texts::ident::kGroupShortcuts));
    st.remove(idKey(id));
    st.endGroup();
    st.sync();
}

std::optional<CommandId> KeyBindings::matchRoute(const int key, const Qt::KeyboardModifiers mods) const {
    if (key == 0 || key == Qt::Key_unknown) return std::nullopt;
    for (const CommandDef& def : registry()) {
        if (def.kind != KeyKind::Route) continue;
        const QKeySequence seq = effective(def.id);
        for (int i = 0; i < seq.count(); ++i) {
            // QKeySequence[i] 在 Qt6 返回 QKeyCombination：拆成 key/modifiers 逐位比对，不走弃用的 int 合成。
            const QKeyCombination kc = seq[i];
            if (static_cast<int>(kc.key()) == key && kc.keyboardModifiers() == mods) return def.id;
        }
    }
    return std::nullopt;
}

std::optional<CommandId> KeyBindings::conflictOf(const QKeySequence& seq, const CommandId exclude) const {
    if (seq.count() == 0) return std::nullopt;   // 空绑定不与他人冲突。
    const QKeyCombination candidate = seq[0];     // 直接与 QKeyCombination 比对（避开弃用的 →int）。
    for (const CommandDef& def : registry()) {
        if (def.id == exclude) continue;
        const QKeySequence other = effective(def.id);
        for (int i = 0; i < other.count(); ++i) {
            if (other[i] == candidate) return def.id;
        }
    }
    return std::nullopt;
}

} // namespace idc::gui
