// ============================================================================
// 文件：app/key_router.cpp
// 作用：实现 KeyRouter 的键表分派与焦点守卫（见同名头文件说明）。
// ============================================================================
#include "app/controllers/key_router.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QKeyEvent>
#include <QLineEdit>

#include "app/controllers/annotation_coordinator.h"
#include "app/controllers/canvas_coordinator.h"
#include "app/keybindings.h"
#include "app/main_window.h"

namespace idc::gui {

// 判断焦点是否在数值/文本输入控件上（用于放行单键快捷键）。
bool KeyRouter::focusInTextInput() {
    QWidget* fw = QApplication::focusWidget();
    return qobject_cast<QLineEdit*>(fw) ||
           qobject_cast<QAbstractSpinBox*>(fw) ||
           qobject_cast<QComboBox*>(fw);
}

// 单键快捷键（见 src/app/README.md「快捷键」）：1/2/3 切模式、K/R 切极性、Delete/Backspace 删除
// 选中标注、Esc 清除选区。这些命令不注册为 QAction 快捷键（曾与画布绘制态「Esc 收笔」在快捷键表互相
// 抢占——窗口级快捷键先于焦点控件 keyPressEvent 消费按键，收笔恒不生效）；改在此分派：绘制态下
// 按键先落画布视口（CanvasView::keyPressEvent 收笔并消费），普通态才冒泡到此处清选区，两条语义互不遮蔽。
// 键值不再硬编码：把事件键序（key|modifiers）交给 KeyBinding 表匹配 Route 类命令的生效序列，
// 命中回 id——用户可重绑（含改裸键/加修饰），默认下与旧 case 逐条等价；守卫与 Esc 上游收笔顺序完全不动。
bool KeyRouter::handleKeyPress(MainWindow* w, QKeyEvent* event) {
    if (focusInTextInput()) return false;   // 焦点在数值/文本输入控件：放行单键（NFR-5）。
    const auto id = KeyBindings::instance().matchRoute(event->key(), event->modifiers());
    if (!id) return false;
    switch (*id) {
        case CommandId::ModeL1:           w->onModeAction(1); break;
        case CommandId::ModeL2:           w->onModeAction(2); break;
        case CommandId::ModeL3:           w->onModeAction(3); break;
        case CommandId::PolarityKeep:     w->onPolarityShortcut(false); break;
        case CommandId::PolarityRemove:   w->onPolarityShortcut(true);  break;
        case CommandId::ClearSelection:   w->canvas_->onClearCut(); break;
        // Delete 与 Backspace 均可删除选中标注：默认序列自带两 alternate，未自定义前二者皆命中同一命令。
        case CommandId::DeleteAnnotation: w->annotation_->onAnnoDeleteSelected(); break;
        default: return false;   // Action 类命令不经此处（由 QAction 快捷键处理）。
    }
    event->accept();
    return true;
}

} // namespace idc::gui
