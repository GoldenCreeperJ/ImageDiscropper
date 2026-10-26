// ============================================================================
// 文件：app/menus/main_window_menus_view.cpp
// 作用：实现 MainWindowUi::buildMenus 的「视图 / 帮助」域——缩放/适应/重置、图层显隐开关、
//       外观对话框入口与使用说明/关于正文。
// 分块依据：见 main_window_menus.cpp 头注（按菜单域四 TU 拆分）；本 TU 只依赖菜单绑定助手
//       （main_window_menus_internal.h）与各面板/控制器的既有写回路径，域内控件/连接自持。
// ============================================================================
#include "app/main_window_ui.h"

#include <QAction>
#include <QMenu>
#include <QMenuBar>

#include "app/appearance_dialog.h"
#include "app/keybindings.h"
#include "app/app_prefs.h"
#include "app/main_window.h"
#include "canvas/canvas_view.h"
#include "panels/layer_panel.h"
#include "util/scrollable_dialog.h"   // showScrollableMessage：使用说明 / 关于长文本改可滚动信息框
#include "i18n/gui_texts.h"
#include "i18n/gui_i18n.h"
#include "i18n/retranslate.h"

#include "main_window_menus_internal.h" // bindAddAction / bindAddMenu（四个菜单 TU 共用）

// 版本号经 CMake 编译期宏注入；独立配置本目录时回退占位版本（与构建说明一致）。
#ifndef IDC_GUI_VERSION
#define IDC_GUI_VERSION "dev"
#endif
#ifndef IDC_CORE_VERSION
#define IDC_CORE_VERSION "dev"
#endif

namespace idc::gui {

void MainWindowUi::buildViewMenu(MainWindow* w, QMenuBar* bar) {
    // ---- 视图 ----
    QMenu* mView = bindAddMenu(w->retranslator_, bar, IDC_GT(menu, kMenuView));
    QAction* aZoomIn = bindAddAction(w->retranslator_, mView, IDC_GT(menu, kZoomIn));
    KeyBindings::instance().registerAction(CommandId::ZoomIn, aZoomIn);
    QObject::connect(aZoomIn, &QAction::triggered, w, [w] { w->view_->zoomIn(); });
    QAction* aZoomOut = bindAddAction(w->retranslator_, mView, IDC_GT(menu, kZoomOut));
    KeyBindings::instance().registerAction(CommandId::ZoomOut, aZoomOut);
    QObject::connect(aZoomOut, &QAction::triggered, w, [w] { w->view_->zoomOut(); });
    QAction* aFit = bindAddAction(w->retranslator_, mView, IDC_GT(menu, kFitToWindow));
    KeyBindings::instance().registerAction(CommandId::Fit, aFit);
    QObject::connect(aFit, &QAction::triggered, w, [w] { w->view_->fitToWindow(); });
    const QAction* aReset = bindAddAction(w->retranslator_, mView, IDC_GT(menu, kResetView));
    QObject::connect(aReset, &QAction::triggered, w, [w] { w->view_->resetZoom(); });
    mView->addSeparator();
    // 图层显隐（与左侧「图层」面板逐行双向镜像）：面板复选框是唯一真相源——菜单经 setXVisible 前向驱动复选框，
    // 复选框 toggled 自然发面板意图信号，走与用户直接勾选同一条落地链（面板信号 → AnnotationCoordinator /
    // SessionController → 场景/模型）；面板/右键改动反向回同步勾选态（setChecked 不触发 triggered，无回环）。
    // 默认勾选与面板初始态一致（全部可见）。
    const auto addViewToggle = [w, mView](const i18n::TextRef& ref,
                                          void (LayerPanel::*setter)(bool) const,
                                          void (LayerPanel::*signal)(bool)) {
        QAction* a = bindAddAction(w->retranslator_, mView, ref);
        a->setCheckable(true);
        a->setChecked(true);
        QObject::connect(a, &QAction::triggered, w, [w, setter](const bool on) { (w->layerPanel_->*setter)(on); });
        QObject::connect(w->layerPanel_, signal, a, [a](const bool on) { a->setChecked(on); });
    };
    addViewToggle(IDC_GT(menu, kShowBase), &LayerPanel::setBaseVisible, &LayerPanel::baseVisibilityChanged);
    addViewToggle(IDC_GT(menu, kShowImageLayer), &LayerPanel::setImageVisible, &LayerPanel::imageLayerVisibilityChanged);
    addViewToggle(IDC_GT(menu, kShowAnnotation), &LayerPanel::setAnnotationVisible, &LayerPanel::annotationVisibilityChanged);
    addViewToggle(IDC_GT(menu, kShowMask), &LayerPanel::setMaskVisible, &LayerPanel::maskVisibilityChanged);
    // 网格线与切割线共用一项（同一开关，按模式各司其职：L3 网格线 / L1-L2 贯穿切割线）。
    addViewToggle(IDC_GT(menu, kShowGrid), &LayerPanel::setGridVisible, &LayerPanel::gridVisibilityChanged);
    addViewToggle(IDC_GT(menu, kShowSelection), &LayerPanel::setSelectionVisible, &LayerPanel::selectionVisibilityChanged);
    addViewToggle(IDC_GT(menu, kShowCellNumber), &LayerPanel::setNumberVisible, &LayerPanel::numberVisibilityChanged);

    mView->addSeparator();
    // 外观/交互：QTabWidget 多页采参——配色页（六色 + 三 alpha 写 Palette，含深浅色主题写 UiPrefs）、其他页
    // （画布交互写 CanvasPrefs + 应用语言 + 默认输出文件夹写 UiPrefs + 「确认与吸附」组：选区吸附写 CanvasPrefs、两项导出前确认写 UiPrefs）、
    // 快捷键页（KeyBindings，桌面方开）；均 QSettings 应用级持久化，改动即时生效（选区吸附已并入本对话框）。
    QAction* aAppearance = bindAddAction(w->retranslator_, mView, IDC_GT(menu, kAppearance));
    i18n::bindToolTip(w->retranslator_, aAppearance, IDC_GT(tooltip, kAppearance));
    QObject::connect(aAppearance, &QAction::triggered, w, [w] {
        AppearanceDialog dlg(w);
        // 对话框内选语言即时热切换：本窗换文由对话框自完，主窗重放经 languageApplied 回接（不关面板、不销毁窗口）。
        QObject::connect(&dlg, &AppearanceDialog::languageApplied, w, [w] { w->retranslateUi(); });
        // 选主题即时应用：theme::apply 已全量 re-polish chrome，本回接仅作兜底（触发主窗重绘自定义绘制）。
        QObject::connect(&dlg, &AppearanceDialog::themeApplied, w, [w] { w->update(); });
        dlg.exec();
        // 关闭后兜底：若尚未应用的语言变更（理论上对话框已即时应用）再原地重放一次，保证一致。
        if (i18n::needsRetranslate(UiPrefs::instance().language()))
            w->retranslateUi();
    });

}

void MainWindowUi::buildHelpMenu(MainWindow* w, QMenuBar* bar) {
    // ---- 帮助 ----
    QMenu* mHelp = bindAddMenu(w->retranslator_, bar, IDC_GT(menu, kMenuHelp));
    const QAction* aUsage = bindAddAction(w->retranslator_, mHelp, IDC_GT(menu, kUsageGuide));
    QObject::connect(aUsage, &QAction::triggered, w, [w] {
        showScrollableMessage(w, IDC_GTR(menu, kUsageGuide),
            IDC_GTR(msg, kHelpUsageBody), IDC_GTR(label, kDialogOkButton));
    });
    const QAction* aAbout = bindAddAction(w->retranslator_, mHelp, IDC_GT(menu, kAbout));
    QObject::connect(aAbout, &QAction::triggered, w, [w] {
        showScrollableMessage(w, IDC_GTR(menu, kAbout),
            IDC_GTR(msg, kAboutBody)
                .arg(QStringLiteral(IDC_GUI_VERSION), QStringLiteral(IDC_CORE_VERSION)),
            IDC_GTR(label, kDialogOkButton));
    });
}

} // namespace idc::gui
