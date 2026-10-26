// ============================================================================
// 文件：app/appearance_dialog.h
// 作用：「视图 → 外观/交互…」对话框——多 Tab 布局（仅桌面附「快捷键」页）：
//       ① 配色：六项核心语义色 + 三项填充 alpha（写 Palette）+ 深浅色主题（写 UiPrefs，即时 theme::apply）；
//       ② 其他：画布交互偏好（写 CanvasPrefs）+ 应用语言（写 UiPrefs，**选语言即对话框内即时热切换、经
//         languageApplied() 令主窗原地重放**）+ 默认输出文件夹（仅桌面）；
//       ③ 快捷键（仅 shortcutsEnabled）：KeyBinding 命令表的可视化编辑器，逐行 QKeySequenceEdit 重绑，
//         即时生效 + 跨会话持久化（shortcuts/ 组），带缺修饰校验与冲突改绑询问、行内/全量恢复默认。
// 分块依据：对话框只是 Palette / CanvasPrefs / UiPrefs / KeyBindings 的采参视图，自身不持任何偏好状态（单一真相源在各单例）；
//           故无「确定/取消」回滚语义——改后即生效、即落盘。底部「恢复默认」同时回退配色与交互两单例
//           （语言/主题/输出目录/快捷键不在内：各属显式选定偏好或另有自身恢复入口）。
//           实现按页拆四个 TU 于 src/app/dialogs/：appearance_dialog.cpp（装配壳）/ _colors（配色页）/
//           _other（其他页）/ _shortcuts（快捷键页）；页构建器只回传页面部件，tab 登记与页序统一留在装配壳构造。
// 说明：属 app 编排层（与新建画布对话框并列），依赖 app 层单例与 util 的 i18n 重放基建；
//       语言区自持一份 Retranslator，选中即 setupFromPreference + retranslate()（本窗换文），并发 languageApplied() 让宿主主窗原地重放——
//       不依赖 MainWindow 类型（信号解耦），接线由组合处（menus/main_window_menus_view.cpp / mobile_shell_ui.cpp）负责。
//       shortcutsEnabled 由构造参数决定：移动端与桌面共用本类（同一编译单元），键盘属桌面能力，
//       故移动壳显式传 false 关闭「快捷键」页（不能靠逐产物宏区分）。选区吸附现收进「其他→确认与吸附」组（即时经 CanvasPrefs::changed 联动生效，不再挂菜单）。
// ============================================================================
#pragma once

#include <QDialog>
#include <QKeySequence>

#include "app/palette.h"
#include "app/keybindings.h"   // CommandId（快捷键编辑区方法签名）

#include "i18n/retranslate.h"   // Retranslator + bind 助手（本对话框自身文案原地重放）

class QPushButton;
class QCheckBox;
class QSpinBox;
class QComboBox;
class QLineEdit;
class QTabWidget;
class QTableWidget;
class QKeySequenceEdit;
class QKeyEvent;

namespace idc::gui {

// ---------------------------------------------------------------------------
// AppearanceDialog：画布外观/交互采参对话框（即时生效）。
// ---------------------------------------------------------------------------
class AppearanceDialog : public QDialog {
    Q_OBJECT
public:
    explicit AppearanceDialog(QWidget* parent = nullptr, bool shortcutsEnabled = true);

signals:
    // 用户选定新语言、本对话框已即时换文后发出；宿主接此原地重放主窗（不关面板、不销毁窗口）。
    void languageApplied();
    // 用户选定新主题、theme::apply 已完成全局 re-polish 后发出；宿主接此作兜底刷新
    //（re-polish 已覆盖 chrome，本信号仅确保缓存了 palette 派生值的自定义绘制即时 update）。
    void themeApplied();

protected:
    // 拦截「快捷键」Tab 录制态的 Esc：QDialog 默认 keyPressEvent 对 Esc 调 reject() 直接关掉整个对话框；
    // 焦点若在某个 QKeySequenceEdit（或其内部编辑子控件）上，就在此消费 Esc 仅放弃本次录制，绝不误关窗口。
    void keyPressEvent(QKeyEvent* event) override;

private:
    // 一项语义色：色块按钮 + 关联 Palette 读写。
    struct ColorRow {
        QPushButton* swatch = nullptr;
        QColor (Palette::*getter)() const = nullptr;
        void (Palette::*setter)(const QColor&) = nullptr;
    };

    void onPickColor(const ColorRow& row);   // 弹 QColorDialog，取色即写 Palette。
    void refreshSwatches();                  // 从 Palette 回读全部色块/alpha，刷新外观。
    void refreshInteraction() const;               // 从 CanvasPrefs 回读交互偏好（手柄/余量/缩放/新建），刷新数值框。
    void retranslate() const;                      // 语言热切换：逐条重放本对话框全部可译文案（值/色块与语言无关，不重跑）。

    // —— 页面构建器（按页拆 TU：_colors / _other / _shortcuts；tab 登记由装配壳构造统一负责）——
    QWidget* buildColorsTab();                // 配色页（六色+三 alpha+深浅色主题）。
    QWidget* buildOtherTab();                 // 其他页（交互 / 语言 / 输出 / 确认与吸附）。

    // —— 快捷键编辑区（仅 shortcutsEnabled_）——
    void buildShortcutsTab();                                                                // 依 KeyBindings 注册表逐行建表（命令名 / QKeySequenceEdit / 行内恢复按钮）。
    void handleShortcutEdit(CommandId id, const QKeySequence& raw);                          // 校验（Action 须含 Ctrl/Alt）→ 冲突改绑询问 → 提交；不通过则回退本行。
    void syncShortcutRow(CommandId id) const;                                                       // 把某行编辑框回显为 effective(id)（阻塞信号，供 changed 联动与回退复用）。
    void reportShortcutConflicts();                                                            // 重置（单行/全部）回落默认后统一扫描全表冲突对并一次性弹窗提示，由用户回对应行自行处置（本层不自动抢占）。
    void scheduleConflictReport();                                                            // 把冲突提示延后到当前点击事件处理完后的事件轮次，并用 pending 标志合并同一次点击派生的多次 clicked，保证只弹一次。

    ColorRow colorRows_[6] = {};
    QSpinBox* keepAlphaSpin_{nullptr};
    QSpinBox* deleteAlphaSpin_{nullptr};
    QSpinBox* selectionAlphaSpin_{nullptr};
    // 交互偏好数值框（即时写 CanvasPrefs）：手柄屏幕基准 / 边缘滚动余量 / 缩放步进 / 缩放上下限 / 新建默认宽高。
    QSpinBox* handlePxSpin_{nullptr};
    QSpinBox* scrollMarginSpin_{nullptr};
    QSpinBox* zoomStepSpin_{nullptr};
    QSpinBox* zoomMinSpin_{nullptr};
    QSpinBox* zoomMaxSpin_{nullptr};
    QSpinBox* newWidthSpin_{nullptr};
    QSpinBox* newHeightSpin_{nullptr};
    // 应用语言下拉（项数据 = 语言码串，写 UiPrefs）：选中即即时切全局语言并原地重放（见 languageApplied）。
    QComboBox* langCombo_{nullptr};
    // 外观主题下拉（项数据 = theme mode 串，写 UiPrefs）：选中即 theme::apply + 发 themeApplied。
    QComboBox* themeCombo_{nullptr};
    // 默认输出文件夹（写 UiPrefs，仅桌面）：行内可手输 + 「浏览…」，改动即时落盘。
    QLineEdit* defaultExportDirEdit_{nullptr};
    // 「确认与吸附」组复选框（即时写各自单例）：选区吸附→CanvasPrefs，两项导出前确认→UiPrefs。
    QCheckBox* snapCheck_{nullptr};
    QCheckBox* confirmBurnInCheck_{nullptr};
    QCheckBox* confirmRearrangeCheck_{nullptr};
    // —— 快捷键编辑区（仅 shortcutsEnabled_ 时构建）——
    bool shortcutsEnabled_ = true;                    // 构造参数：移动端传 false 关闭「快捷键」Tab。
    QTabWidget* tabs_{nullptr};                        // 外层多页容器（配色 / 其他 / 快捷键）。
    QTableWidget* shortcutTable_{nullptr};             // 快捷键表：行序 = 注册表序 = (int)CommandId。
    bool reportingConflicts_ = false;                  // 重入守卫：QMessageBox 模态嵌套事件循环期间若 clicked 被再投递，避免同一冲突提示连弹两次。
    bool handlingShortcut_ = false;                    // handleShortcutEdit 重入守卫：QKeySequenceEdit 一次按键可能连发两次 keySequenceChanged，模态框嵌套循环会把第二次投递进来，重入时直接忽略。
    bool conflictReportPending_ = false;               // 延迟合并标志：已有待处理的冲突提示则不重复排期（处理同一次点击的多次 clicked）。
    // 本对话框文案重放登记表：构造期各 bind 助手在此登记闭包，语言切换时 retranslate() 逐条重放。
    // 置于全部控件成员之后：成员按声明逆序析构，retranslator_ 先析构，保证其登记的闭包不会引用已销毁控件。
    i18n::Retranslator retranslator_;
};

} // namespace idc::gui
