// ============================================================================
// 文件：mobile/mobile_shell_ui.cpp
// 作用：MobileShellUi 实现——移动端骨架建造（部件创建/回填与连接全在此，见同名
//       头文件说明）。逻辑自移动壳拆分独立成文（与桌面 main_window_ui.cpp 同纪律）。
// 分块依据：
//   - 部件创建/回填与连接全在 MobileShellUi；MobileShell 构造只规定装配顺序；
//   - 底部工具条按钮 → MainWindow 既有私有槽 / CanvasView request*/gesture* 入口，
//     与菜单/快捷键/右键同一信号链路（行为同构）；「更多」菜单收纳低频的桌面菜单能力。
// 说明：触控细节：抽屉内桌面面板统一触控字号/间距（样式表限定 #mobileDrawer 子树，
//       不波及画布）；竖/横屏翻转仅重排抽屉停靠区（不重建部件 → 状态无损）；
//       预处理忙碌改全屏遮罩（SPEC §8.2）。
//       桌面构建中本文件只属 idc_gui_mobile 目标（IDC_GUI_MOBILE=ON，默认）；Android 单目标必含。
// ============================================================================
#include "mobile/mobile_shell_ui.h"

#include <functional>
#include <initializer_list>
#include <utility>
#include <algorithm>

#include <QAction>
#include <QDockWidget>
#include <QGridLayout>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>

#include "app/controllers/config_controller.h"
#include "app/controllers/export_controller.h"
#include "app/appearance_dialog.h"
#include "app/canvas_prefs.h"
#include "app/app_prefs.h"
#include "app/main_window.h"
#include "app/controllers/preprocess_controller.h"
#include "app/controllers/session_controller.h"
#include "app/status_bar.h"
#include "canvas/canvas_scene.h"
#include "canvas/canvas_view.h"
#include "mobile/mobile_step_pad.h"
#include "mobile/touch_scroll.h"
#include "panels/annotation_prop_panel.h"
#include "panels/export_panel.h"
#include "panels/image_panel.h"
#include "panels/layer_edit_panel.h"
#include "panels/layer_panel.h"
#include "panels/left_panel.h"
#include "panels/param_panel.h"
#include "panels/tool_panel.h"
#include "util/gui_nums.h"
#include "i18n/gui_texts.h"
#include "i18n/gui_i18n.h"
#include "i18n/retranslate.h"

// 版本号经 CMake 编译期宏注入；独立配置本目录时回退占位版本（与构建说明一致）。
#ifndef IDC_GUI_VERSION
#define IDC_GUI_VERSION "dev"
#endif
#ifndef IDC_CORE_VERSION
#define IDC_CORE_VERSION "dev"
#endif

namespace idc::gui {
void MobileShellUi::buildCentral(MainWindow* w) {
    w->scene_ = new CanvasScene(w);
    w->view_ = new CanvasView(w->scene_, w);

    w->left_ = new LeftPanel(w);
    w->param_ = new ParamPanel(w);
    w->exportPanel_ = new ExportPanel(w);
    w->imagePanel_ = new ImagePanel(w);
    w->toolPanel_ = new ToolPanel(w);
    w->layerPanel_ = new LayerPanel(w);
    w->layerEditPanel_ = new LayerEditPanel(w);
    w->annoPropPanel_ = new AnnotationPropPanel(w);
    w->left_->setDocument(&w->doc_);
    w->param_->setDocument(&w->doc_);
    w->exportPanel_->setDocument(&w->doc_);
    w->imagePanel_->setDocument(&w->doc_);
    w->layerEditPanel_->setDocument(&w->doc_);
    w->toolPanel_->setModel(&w->annoBridge_);
    w->layerPanel_->setModel(&w->annoBridge_);
    w->annoPropPanel_->setModel(&w->annoBridge_);

    w->setCentralWidget(w->view_);   // 桌面是 QSplitter 三分；移动端画布独占中央区。
}

// 抽屉：桌面八分区折叠为六页选项卡（标注工具并入「模式」、图片/图层编辑并入「图层」，
//       与桌面左栏 + 右栏折叠节的分区归属一一对齐），停靠底部、初始隐藏，由工具条「面板」按钮开合。
// rightTabs_ 回填同一成员 → PreviewController 的「错误定位到某页」逻辑自动沿用
//（页面经滚动包裹后的兼容比较已做在 preview_controller 的 showingExportPage）。
void MobileShellUi::buildDrawer(MainWindow* w) {
    auto* tabs = new QTabWidget(w);   // 移动端保持选择式（rightTabs_ 成员为双型共用 QWidget*）。
    // 抽屉最小尺寸＝设计下限（nums::mobile::kDrawerMinWidthPx × kDrawerMinHeightPx，
    // 真机横/竖屏均放得下；无界回压的实测修复与取值依据见 src/mobile/README.md「触控精度与方向」）。
    tabs->setMinimumSize(nums::mobile::kDrawerMinWidthPx, nums::mobile::kDrawerMinHeightPx);

    // 页签标题：建页即设当前值 + 登记按新语言重放 tabText 的闭包（就地改字、不重建）。
    auto addI18nTab = [tabs, w](QWidget* content, const i18n::TextRef& ref) {
        const int idx = tabs->addTab(content, i18n::qs(ref));
        i18n::bindTabText(w->retranslator_, tabs, idx, ref);
    };

    // 页 1「模式/图层」：桌面左侧一组（模式极性 + 标注工具 + 图层）竖排可滚动。
    auto* groupContainer = new QWidget(w);
    auto* groupLayout = new QVBoxLayout(groupContainer);
    groupLayout->setContentsMargins(0, 0, 0, 0);
    groupLayout->setSpacing(nums::panels::kPanelSpacingPx);
    groupLayout->addWidget(w->left_);
    groupLayout->addWidget(w->toolPanel_);
    groupLayout->addWidget(w->layerPanel_);
    groupLayout->addStretch(1);
    auto* groupScroll = new QScrollArea(w);
    groupScroll->setWidgetResizable(true);
    groupScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    groupScroll->setWidget(groupContainer);
    installTouchScroll(groupScroll);
    addI18nTab(groupScroll, IDC_GT(label, kModeSection));

    // 每页套纵向 QScrollArea：高度不够时页内上下滚动（参数/导出页天然比抽屉高）；
    // 横向 AlwaysOff——内容随抽屉宽自适应（minimumSizeHint 无界回压的实测修复
    // 详见 src/mobile/README.md「触控精度与方向」）。
    auto scrollPage = [w](QWidget* content) {
        auto* sa = new QScrollArea(w);
        sa->setWidgetResizable(true);
        sa->setFrameShape(QFrame::NoFrame);
        sa->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        sa->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        sa->setWidget(content);
        installTouchScroll(sa);
        return sa;
    };

    addI18nTab(scrollPage(w->layerEditPanel_), IDC_GT(label, kSecImage));
    addI18nTab(scrollPage(w->imagePanel_), IDC_GT(label, kSecPreprocess));
    addI18nTab(scrollPage(w->annoPropPanel_), IDC_GT(label, kSecAnnotation));
    addI18nTab(scrollPage(w->param_), IDC_GT(label, kSecParam));
    addI18nTab(scrollPage(w->exportPanel_), IDC_GT(label, kExportBoxTitle));
    w->rightTabs_ = tabs;

    auto* drawer = new QDockWidget(IDC_GTR(label, kPanelToggle), w);
    drawer->setObjectName(QLatin1String(kDrawerName));
    drawer->setFeatures(QDockWidget::NoDockWidgetFeatures);   // 不可拖出/关闭（形态固定）。
    drawer->setTitleBarWidget(new QWidget(drawer));           // 空标题栏 = 隐藏标题。
    drawer->setWidget(tabs);
    w->addDockWidget(Qt::BottomDockWidgetArea, drawer);
    drawer->hide();   // 画布为主：默认收起，按需展开。
}

// 状态栏精简（SPEC §8.3）：紧凑模式隐藏坐标/RGB/提示标签，只留缩放；模式/保留块数
// 已随状态栏瘦身并入提示线（CanvasCoordinator「交互模式：…」hint），提示仍经 showMessage 限时弹出。
void MobileShellUi::buildStatus(MainWindow* w) {
    w->status_ = new StatusBar(w);
    w->status_->setCompactMode(true);
    w->setStatusBar(w->status_);
}

// 工具条（方向自适应）：高频能力常驻 + 「更多」低频菜单 + 「面板」开合抽屉。
// 按钮全部接既有私有槽 / 视图 gesture* 入口（与桌面同链路，行为同构）。
// 窄屏可达性：条带住 QScrollArea（触控由 TouchScrollFilter 接管、桌面预览用 QScroller，
// 另配滚轮横滚；细节见 src/mobile/README.md「触控精度与方向」）；初始态＝竖屏单行，方向
// 翻转由 applyOrientationLayout 零重建重排。
void MobileShellUi::buildBottomBar(MainWindow* w) {
    auto* tb = new QToolBar(IDC_GTR(label, kMobileToolBarTitle), w);
    tb->setObjectName(QLatin1String(kToolBarName));
    w->addToolBar(Qt::BottomToolBarArea, tb);
    tb->setMovable(false);

    auto* scroller = new QScrollArea(tb);
    scroller->setObjectName(QLatin1String(kStripName));
    scroller->setFrameShape(QFrame::NoFrame);
    scroller->setWidgetResizable(true);   // 内容尺寸由布局最小尺寸（按钮 44pt 之和）撑开。
    scroller->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);   // 不出滚动条，只靠拖拽/滚轮。
    scroller->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroller->setFixedHeight(nums::mobile::kStripHeightPx);
    scroller->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* host = new QWidget(scroller);
    // 单 grid 双向复用：竖屏全在行 0（横向单行），横屏全在列 0（竖向单列）；
    // 方向切换由 applyOrientationLayout 重加同一批 widget，不重建、状态无损。
    auto* lay = new QGridLayout(host);
    lay->setContentsMargins(nums::mobile::kStripMarginPx, nums::mobile::kStripMarginPx,
                           nums::mobile::kStripMarginPx, nums::mobile::kStripMarginPx);
    lay->setSpacing(nums::mobile::kStripSpacingPx);
    lay->setAlignment(Qt::AlignLeft);     // 内容窄于视口时按钮不拉伸。
    int stripIndex = 0;                   // 条带槽位序（添加顺序即重排顺序）。
    scroller->setWidget(host);
    tb->addWidget(scroller);

    // 拖拽接管：touch 序列落在按钮上时，按钮不接受 QEvent::Touch*（它们只吃合成鼠标序列），
    // touch 事件冒泡到 viewport 由 TouchScrollFilter 全程消费——位移超阈值滚动、未超阈值
    // 轻点人工合成 press-release 触发 clicked（点击与滑动互斥共存，见 installTouchScroll）。
    installTouchScroll(scroller);
    // 滚轮→横滚（桌面预览补齐拖拽之外的第二条滚动路径，也消除默认竖滚错乱）。
    scroller->viewport()->installEventFilter(
        new WheelToHScrollFilter(scroller->horizontalScrollBar(), scroller));

    // 把一个 QAction 做成 44pt 文本按钮入条带（启用态/勾选态经 defaultAction 自动同步）。
    // 宽度按文字实测强制下限：Fixed 策略下布局取宽 = max(sizeHint, minimumWidth)，而纯文本
    // autoRaise 按钮的 sizeHint 会低估（英文 Zoom In/Out 因此被裁），故显式用字体度量算宽，
    // 并在语言切换后随新文字重算（登记到 retranslator_）。
    auto fitButtonWidth = [](QToolButton* b, const QString& text) {
        const int need = b->fontMetrics().horizontalAdvance(text)
                       + nums::mobile::kStripButtonHPadPx;
        b->setMinimumWidth(std::max(nums::mobile::kMinButtonSizePt, need));
    };
    auto addButton = [host, lay, &stripIndex, w, fitButtonWidth](QAction* a) {
        auto* b = new QToolButton(host);
        b->setDefaultAction(a);
        b->setToolButtonStyle(Qt::ToolButtonTextOnly);
        b->setAutoRaise(true);
        b->setSizePolicy(QSizePolicy::Fixed, b->sizePolicy().verticalPolicy());
        fitButtonWidth(b, a->text());
        w->retranslator_.add([b, a, fitButtonWidth] { fitButtonWidth(b, a->text()); });
        lay->addWidget(b, 0, stripIndex++);   // 初始竖屏：全入行 0。
        return b;
    };
    // 便捷：把一个动作接到 lambda 上并加入条带（context 统一为 w，随窗口销毁）。
    auto addLambda = [w, addButton](const i18n::TextRef& ref, std::function<void()> fn) {
        const auto a = new QAction(i18n::qs(ref), w);
        i18n::bindAction(w->retranslator_, a, ref);
        QObject::connect(a, &QAction::triggered, w, [fn = std::move(fn)] { fn(); });
        addButton(a);
        return a;
    };

    // 新建/打开/导出与桌面菜单同源：转发各域控制器（会话/导出）。
    addLambda(IDC_GT(label, kTbNew), [w] { w->session_->onNew(); });
    addLambda(IDC_GT(label, kTbOpen), [w] { w->session_->onOpen(); });
    addLambda(IDC_GT(label, kExportBoxTitle), [w] { w->export_->onExport(); });

    // 撤销/重做常驻（SPEC §8.3 无键盘补偿）：无菜单，故在本建造者创建动作并回填成员；
    // 不绑快捷键（触控无键盘；启用态仍由 onHistoryAvailabilityChanged 回灌同一成员）。
    w->aUndo_ = new QAction(i18n::qs(IDC_GT(label, kTbUndo)), w);
    i18n::bindAction(w->retranslator_, w->aUndo_, IDC_GT(label, kTbUndo));
    QObject::connect(w->aUndo_, &QAction::triggered, w, &MainWindow::onUndo);
    addButton(w->aUndo_);
    w->aRedo_ = new QAction(i18n::qs(IDC_GT(label, kTbRedo)), w);
    i18n::bindAction(w->retranslator_, w->aRedo_, IDC_GT(label, kTbRedo));
    QObject::connect(w->aRedo_, &QAction::triggered, w, &MainWindow::onRedo);
    addButton(w->aRedo_);

    addLambda(IDC_GT(menu, kZoomOut), [w] { w->view_->zoomOut(); });
    addLambda(IDC_GT(menu, kZoomIn), [w] { w->view_->zoomIn(); });
    addLambda(IDC_GT(label, kTbFit), [w] { w->view_->fitToWindow(); });   // 与双击适应同源。

    // 模式切换与极性不设工具条按钮（移动端「一个功能一个地方」纪律，见 src/mobile/README）：
    // 常驻抽屉「模式」页（LeftPanel 模式/极性开关，与桌面左栏同面板同链路、勾选回显同源）。

    // 删标注/取消不设工具条按钮（移动端「一个功能一个地方」纪律）：删除选中标注在抽屉
    // 「标注」页「删除选中标注」按钮（与桌面 Delete 键/标注菜单同槽）；取消（绘制态收笔/
    // 清选区）由长按手势覆盖——绘制态长按＝收笔，普通态长按菜单「清除切割线」（等价右键）。

    // 8 向步进盘（替代方向键）：QMenu + QWidgetAction 弹出，nudge → requestNudge 同链路。
    auto* padButton = new QToolButton(host);
    i18n::bindButton(w->retranslator_, padButton, IDC_GT(label, kTbStepPad));
    // 与 addButton 同源：Fixed + 按文字实测宽（语言切换后重算）。
    padButton->setSizePolicy(QSizePolicy::Fixed, padButton->sizePolicy().verticalPolicy());
    fitButtonWidth(padButton, padButton->text());
    w->retranslator_.add([padButton, fitButtonWidth] { fitButtonWidth(padButton, padButton->text()); });
    padButton->setPopupMode(QToolButton::InstantPopup);
    auto* padMenu = new QMenu(padButton);
    auto* padAction = new QWidgetAction(padMenu);
    auto* pad = new StepPadWidget(padMenu);
    QObject::connect(pad, &StepPadWidget::nudgeRequested, w,
                     [w](const int dx, const int dy) { w->view_->requestNudge(dx, dy); });
    padAction->setDefaultWidget(pad);
    padMenu->addAction(padAction);
    padButton->setMenu(padMenu);
    lay->addWidget(padButton, 0, stripIndex++);

    // 「更多」：桌面菜单里低频项的触控等价物（配置存取 FR-L3.8、重置全部、重置视图、
    // 使用说明/关于/退出）；清除全部标注不在此——已在抽屉「标注」页按钮，一个功能一个地方。
    auto* moreButton = new QToolButton(host);
    i18n::bindButton(w->retranslator_, moreButton, IDC_GT(label, kTbMore));
    // 与 addButton 同源：Fixed + 按文字实测宽（语言切换后重算）。
    moreButton->setSizePolicy(QSizePolicy::Fixed, moreButton->sizePolicy().verticalPolicy());
    fitButtonWidth(moreButton, moreButton->text());
    w->retranslator_.add([moreButton, fitButtonWidth] { fitButtonWidth(moreButton, moreButton->text()); });
    moreButton->setPopupMode(QToolButton::InstantPopup);
    auto* moreMenu = new QMenu(moreButton);
    // 「更多」菜单项：建项即设当前值 + 登记按新语言重放 actionText 的闭包。
    auto addI18nAction = [moreMenu, w](const i18n::TextRef& ref) {
        auto* a = moreMenu->addAction(i18n::qs(ref));
        i18n::bindAction(w->retranslator_, a, ref);
        return a;
    };
    const QAction* aLoad = addI18nAction(IDC_GT(label, kTbLoadConfigEllipsis));
    QObject::connect(aLoad, &QAction::triggered, w, [w] { w->config_->onLoadConfig(); });
    const QAction* aSave = addI18nAction(IDC_GT(label, kTbSaveConfigEllipsis));
    QObject::connect(aSave, &QAction::triggered, w, [w] { w->config_->onSaveConfig(); });
    // 选区吸附（运行时开关）：与桌面「视图→选区吸附」同一 CanvasPrefs 应用级态；勾选态切换即时联动重设选区图元。
    QAction* aSnap = addI18nAction(IDC_GT(menu, kSnapToggle));
    aSnap->setCheckable(true);
    aSnap->setChecked(CanvasPrefs::instance().snapEnabled());
    QObject::connect(aSnap, &QAction::toggled, w, [](const bool on) { CanvasPrefs::instance().setSnapEnabled(on); });
    // 外观/交互：与桌面「视图 → 外观/交互…」同一模态弹窗（Palette + CanvasPrefs 单例共享，
    // 呈现/交互偏好走 QSettings 应用级；移动端同样可用，弹窗控件已在 AppearanceDialog 内取触控下限）。
    // 键盘属桌面能力：传入 shortcutsEnabled=false，移动端不建「快捷键」Tab（其余两页照常）。
    QAction* aAppearance = addI18nAction(IDC_GT(menu, kAppearance));
    i18n::bindToolTip(w->retranslator_, aAppearance, IDC_GT(tooltip, kAppearance));
    QObject::connect(aAppearance, &QAction::triggered, w, [w] {
        AppearanceDialog dlg(w, /*shortcutsEnabled=*/false);
        // 同桌面：对话框内选语言即时换文，主窗重放经 languageApplied 回接（不关面板、不销毁窗口）。
        QObject::connect(&dlg, &AppearanceDialog::languageApplied, w, [w] { w->retranslateUi(); });
        // 选主题即时应用：theme::apply 已全量 re-polish chrome（Android 本就跟随系统，resolveDark(System) 直接受益），本回接仅作兜底。
        QObject::connect(&dlg, &AppearanceDialog::themeApplied, w, [w] { w->update(); });
        dlg.exec();
        // 关闭后兜底：若尚存未应用的语言差异再原地重放一次（同桌面触发）。
        if (i18n::resolveForPreference(UiPrefs::instance().language())
                != core::i18n::language())
            w->retranslateUi();
    });
    // 重排前二次确认（与桌面「导出设置→重排前二次确认」同一 UiPrefs 应用级态）：行为同构。
    QAction* aConfirmRearrange = addI18nAction(IDC_GT(menu, kConfirmRearrange));
    aConfirmRearrange->setCheckable(true);
    aConfirmRearrange->setChecked(UiPrefs::instance().confirmRearrangeRisk());
    QObject::connect(aConfirmRearrange, &QAction::toggled, w, [](const bool on) { UiPrefs::instance().setConfirmRearrangeRisk(on); });
    const QAction* aDestroyAll = addI18nAction(IDC_GT(label, kTbResetAll));
    QObject::connect(aDestroyAll, &QAction::triggered, w, [w] { w->session_->onDestroyAll(); });
    moreMenu->addSeparator();
    // 使用说明（触控版流程，与桌面帮助菜单正文同构但把手势替代快捷键）+ 关于（同桌面正文）。
    const QAction* aUsage = addI18nAction(IDC_GT(menu, kUsageGuide));
    QObject::connect(aUsage, &QAction::triggered, w, [w] {
        QMessageBox::information(w, IDC_GTR(menu, kUsageGuide),
            IDC_GTR(msg, kMobileHelpBody));
    });
    const QAction* aAbout = addI18nAction(IDC_GT(menu, kAbout));
    QObject::connect(aAbout, &QAction::triggered, w, [w] {
        QMessageBox::about(w, IDC_GTR(menu, kAbout),
            IDC_GTR(msg, kAboutBody)
                .arg(QStringLiteral(IDC_GUI_VERSION), QStringLiteral(IDC_CORE_VERSION)));
    });
    moreMenu->addSeparator();
    const QAction* aExit = addI18nAction(IDC_GT(label, kTbExit));
    QObject::connect(aExit, &QAction::triggered, w, &QWidget::close);
    moreButton->setMenu(moreMenu);
    lay->addWidget(moreButton, 0, stripIndex++);

    // 「面板」：开合抽屉（尺寸按当前方向：竖屏取高、横屏取宽，比例见
    // nums::mobile::kDrawerPortraitHeightNum / kDrawerLandscapeWidthNum / kDrawerRatioDen；画布仍为主）。
    addLambda(IDC_GT(label, kPanelToggle), [w] {
        auto* drawer = w->findChild<QDockWidget*>(QLatin1String(kDrawerName));
        if (!drawer) return;
        const bool show = !drawer->isVisible();
        drawer->setVisible(show);
        if (show) {
            if (w->width() > w->height())   // 横屏：抽屉停靠右侧，按宽度取值。
                w->resizeDocks({drawer},
                               {w->width() * nums::mobile::kDrawerLandscapeWidthNum / nums::mobile::kDrawerRatioDen},
                               Qt::Horizontal);
            else                            // 竖屏：底部抽屉，按高度取值。
                w->resizeDocks({drawer},
                               {w->height() * nums::mobile::kDrawerPortraitHeightNum / nums::mobile::kDrawerRatioDen},
                               Qt::Vertical);
        }
    });
}

// 忙碌全屏遮罩：预处理耗时操作的桌面居中小对话框→移动端铺满屏幕（SPEC §8.2）；
// preprocess_ 在 initCore 才创建，故本方法由 MobileShell 构造在 initCore 后调用。
void MobileShellUi::enableTouchErgonomics(const MainWindow* w) {
    w->preprocess_->setBusyOverlayMode(true);
}

} // namespace idc::gui
