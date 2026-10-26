// ============================================================================
// 文件：app/menus/main_window_menus_internal.h（GUI 私有内部头，仅四个 main_window_menus*.cpp 包含）
// 作用：菜单装配绑定助手声明（bindAddAction/bindAddMenu 四种形态）——把一次 addAction/addMenu
//       变为可重放登记（包 Retranslator），收敛逐站点改动；文字入参一律用 IDC_GT(...) 产出的
//       TextRef（constexpr、key+fallback）。定义在 main_window_menus.cpp（main TU）。
// 分块依据：buildMenus 按菜单域拆为四个 TU（main_window_menus*.cpp），每个 TU 的每个站点都需
//       这组绑定助手；声明集中于此、实现单份，避免四份拷贝漂移（仿 src/panels/param_panel_internal.h
//       先例，不经 include/ 外泄）。
// ============================================================================
#pragma once

class QAction;
class QActionGroup;
class QMenu;
class QMenuBar;

namespace idc::i18n {
class Retranslator;
struct TextRef;
} // namespace idc::i18n

namespace idc::gui {

// 菜单 / 动作组动作：登记重放（setText）后返回新建动作。
QAction* bindAddAction(i18n::Retranslator& r, QMenu* m, const i18n::TextRef& ref);
QAction* bindAddAction(i18n::Retranslator& r, QActionGroup* g, const i18n::TextRef& ref);
// 子菜单（QMenu 父）与顶级菜单（QMenuBar 父）：登记重放（setTitle）后返回新建菜单。
QMenu* bindAddMenu(i18n::Retranslator& r, QMenu* parent, const i18n::TextRef& ref);
QMenu* bindAddMenu(i18n::Retranslator& r, QMenuBar* parent, const i18n::TextRef& ref);

} // namespace idc::gui
