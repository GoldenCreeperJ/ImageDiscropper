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
#include <utility>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QDockWidget>
#include <QGridLayout>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QScrollBar>
#include <QScroller>
#include <QSpinBox>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QTouchEvent>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <QWheelEvent>

#include "app/annotation_coordinator.h"
#include "app/main_window.h"
#include "app/preprocess_controller.h"
#include "app/status_bar.h"
#include "canvas/canvas_scene.h"
#include "canvas/canvas_view.h"
#include "mobile/mobile_step_pad.h"
#include "panels/annotation_prop_panel.h"
#include "panels/export_panel.h"
#include "panels/image_panel.h"
#include "panels/layer_panel.h"
#include "panels/left_panel.h"
#include "panels/param_panel.h"
#include "panels/tool_panel.h"

namespace idc::gui {
namespace {

/// 底部工具条滚轮重定向：QScrollArea 默认滚轮处理驱动的是**垂直**滚动条——横向条带里
/// 会出现「上下滚动」的错乱。拦截视口链（含悬停在按钮上时）的 Wheel 事件，
/// 换算成水平位移；无溢出/非滚轮事件不消费（交回默认链）。
class WheelToHScrollFilter : public QObject {
public:
    explicit WheelToHScrollFilter(QScrollBar* target, QObject* parent = nullptr)
        : QObject(parent), target_(target) {}

protected:
    bool eventFilter(QObject* /*obj*/, QEvent* ev) override {
        if (ev->type() != QEvent::Wheel) return false;
        const auto* we = dynamic_cast<QWheelEvent*>(ev);
        const int dx = we->angleDelta().x();
        const int dy = we->angleDelta().y();
        const int step = dx != 0 ? dx : dy;   // 横向滚轮（触控板）优先，否则竖轮横用。
        if (step == 0) return true;           // 纯高精度横抖：吞掉即可。
        // 无水平溢出（含横屏单列形态）：交回默认链——横屏单列时默认竖滚正是想要的。
        if (target_->minimum() == target_->maximum()) return false;
        target_->setValue(target_->value() - step);
        return true;
    }

private:
    QScrollBar* target_;
};

// 触控滚动接管（QScroller 替代）：QScroller 在 Android 上的属性通道不完整——
// 过冲属性经 QPA 不生效（回弹关不干净），且其 click-through 合成会在拖拽释放时
// 向条带按钮投递点击（「滑动时按下按钮」的元凶——MaxClickThroughVelocity 只挡
// 快速甩动，慢速收尾照样投递）。自定义过滤器替代：全程消费触摸事件（合成鼠标
// 无从干扰），累计位移超阈值（kTapThreshold）即按增量滚动——滚动条自身钳制，
// 无过冲无回弹；未超阈值（真轻点）在释放时人工合成一次 press-release 触发子部件点击。
class TouchScrollFilter : public QObject {
public:
    explicit TouchScrollFilter(QScrollArea* area, QObject* parent = nullptr)
        : QObject(parent), area_(area) {
        area_->viewport()->installEventFilter(this);
        area_->viewport()->setAttribute(Qt::WA_AcceptTouchEvents);
    }

protected:
    bool eventFilter(QObject* /*watched*/, QEvent* event) override {
        switch (event->type()) {
            case QEvent::TouchBegin:
            case QEvent::TouchUpdate:
            case QEvent::TouchEnd: {
                const auto* te = dynamic_cast<QTouchEvent*>(event);
                if (event->type() == QEvent::TouchBegin && !te->points().isEmpty()) {
                    // 落在文本输入类控件（输入框/数字框/下拉框）上的触摸序列放行：
                    // 文字选择、拖动删除、光标定位需要 Qt 原生的合成鼠标事件，
                    // 被本过滤器消费则「删除都删不了」（真机反馈）。
                    passThrough_ = isTextEntryWidgetAt(te->points().first().position());
                    if (passThrough_) return false;
                }
                if (passThrough_) {
                    if (event->type() == QEvent::TouchEnd) passThrough_ = false;
                    return false; // 放行整条序列，交 Qt 合成鼠标处理。
                }
                handleTouch(te);
                return true; // 全程消费：点击只由本过滤器合成，绝不误投。
            }
            default:
                return false;
        }
    }

    // 命中点（及其祖先）是否文本输入类控件——这类控件保留原生触摸语义。
    bool isTextEntryWidgetAt(const QPointF& viewportPos) const {
        const QPoint gp = area_->viewport()->mapToGlobal(viewportPos.toPoint());
        QWidget* w = QApplication::widgetAt(gp);
        while (w) {
            if (qobject_cast<QLineEdit*>(w) || qobject_cast<QTextEdit*>(w)
                || qobject_cast<QPlainTextEdit*>(w) || qobject_cast<QAbstractSpinBox*>(w)
                || qobject_cast<QComboBox*>(w))
                return true;
            w = w->parentWidget();
        }
        return false;
    }

private:
    void handleTouch(const QTouchEvent* event) {
        if (event->points().isEmpty()) return;
        const QPointF pos = event->points().first().position();
        switch (event->type()) {
            case QEvent::TouchBegin:
                tracking_ = true;
                moved_ = false;
                lastPos_ = pos;
                total_ = QPointF();
                break;
            case QEvent::TouchUpdate: {
                if (!tracking_) break;
                const QPointF delta = pos - lastPos_;
                lastPos_ = pos;
                total_ += delta;
                if (!moved_ && total_.manhattanLength() < kTapThreshold) break; // 轻点抖动容忍区内。
                moved_ = true;
                // 内容跟手：手指位移反作用于滚动条值（上/左滑 → 值增大）。
                area_->horizontalScrollBar()->setValue(
                    area_->horizontalScrollBar()->value() + qRound(-delta.x()));
                area_->verticalScrollBar()->setValue(
                    area_->verticalScrollBar()->value() + qRound(-delta.y()));
                break;
            }
            case QEvent::TouchEnd:
                if (tracking_ && !moved_) synthesizeTap(pos);
                tracking_ = false;
                break;
            default:
                break;
        }
    }

    // 人工点击合成：widgetAt 深查命中部件（透过条带容器直达按钮），发一对鼠标
    // press-release；只处理本滚动区子树内的目标。
    void synthesizeTap(const QPointF& viewportPos) const {
        const QWidget* vp = area_->viewport();
        const auto global = QPointF(vp->mapToGlobal(viewportPos.toPoint()));
        QWidget* child = QApplication::widgetAt(global.toPoint());
        if (!child || !vp->isAncestorOf(child)) return;
        const auto local = QPointF(child->mapFromGlobal(global.toPoint()));
        QMouseEvent press(QEvent::MouseButtonPress, local, global,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, local, global,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(child, &press);
        QApplication::sendEvent(child, &release);
    }

    static constexpr qreal kTapThreshold = 6.0; // 轻点抖动容忍（px）；超过即判为滑动。
    QScrollArea* area_;
    bool passThrough_ = false; // 本序列落在文本输入控件上：放行交 Qt 原生处理。
    bool tracking_ = false;
    bool moved_ = false;
    QPointF lastPos_;
    QPointF total_;
};

// 移动端全部滚动区（抽屉页/分组/底部条带）统一换装上述过滤器；
// 桌面预览保持 QScroller 鼠标协调器模式（无触摸事件，不受影响）。
void installTouchScroll(QScrollArea* area) {
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    new TouchScrollFilter(area, area); // 父对象持有生命周期，随滚动区销毁。
#else
    QScroller::grabGesture(area->viewport(), QScroller::LeftMouseButtonGesture);
#endif
}
} // namespace

// 中央区＝画布本体（「画布为主」）；面板创建与注入逐行对齐桌面 buildCentral（同一套类，零改动）。
void MobileShellUi::buildCentral(MainWindow* w) {
    w->scene_ = new CanvasScene(w);
    w->view_ = new CanvasView(w->scene_, w);

    w->left_ = new LeftPanel(w);
    w->param_ = new ParamPanel(w);
    w->exportPanel_ = new ExportPanel(w);
    w->imagePanel_ = new ImagePanel(w);
    w->toolPanel_ = new ToolPanel(w);
    w->layerPanel_ = new LayerPanel(w);
    w->annoPropPanel_ = new AnnotationPropPanel(w);
    w->left_->setDocument(&w->doc_);
    w->param_->setDocument(&w->doc_);
    w->exportPanel_->setDocument(&w->doc_);
    w->imagePanel_->setDocument(&w->doc_);
    w->toolPanel_->setModel(&w->annoBridge_);
    w->layerPanel_->setModel(&w->annoBridge_);
    w->annoPropPanel_->setModel(&w->annoBridge_);

    w->setCentralWidget(w->view_);   // 桌面是 QSplitter 三分；移动端画布独占中央区。
}

// 抽屉：桌面三栏折叠为五页选项卡，停靠底部、初始隐藏，由工具条「面板」按钮开合。
// rightTabs_ 回填同一成员 → PreviewController 的「错误定位到某页」逻辑自动沿用
//（页面经滚动包裹后的兼容比较已做在 preview_controller 的 showingExportPage）。
void MobileShellUi::buildDrawer(MainWindow* w) {
    auto* tabs = new QTabWidget(w);   // 移动端保持选择式（rightTabs_ 成员为双型共用 QWidget*）。
    // 抽屉最小尺寸＝设计下限（320×240，真机横/竖屏均放得下；无界回压的实测修复
    // 与取值依据见 src/mobile/README.md「触控精度与方向」）。
    tabs->setMinimumSize(320, 240);

    // 页 1「模式/图层」：桌面左侧一组（模式极性 + 标注工具 + 图层）竖排可滚动。
    auto* groupContainer = new QWidget(w);
    auto* groupLayout = new QVBoxLayout(groupContainer);
    groupLayout->setContentsMargins(0, 0, 0, 0);
    groupLayout->setSpacing(6);
    groupLayout->addWidget(w->left_);
    groupLayout->addWidget(w->toolPanel_);
    groupLayout->addWidget(w->layerPanel_);
    groupLayout->addStretch(1);
    auto* groupScroll = new QScrollArea(w);
    groupScroll->setWidgetResizable(true);
    groupScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    groupScroll->setWidget(groupContainer);
    installTouchScroll(groupScroll);
    tabs->addTab(groupScroll, QStringLiteral("模式"));

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

    tabs->addTab(scrollPage(w->param_), QStringLiteral("参数"));
    tabs->addTab(scrollPage(w->exportPanel_), QStringLiteral("导出"));
    tabs->addTab(scrollPage(w->imagePanel_), QStringLiteral("图像"));
    tabs->addTab(scrollPage(w->annoPropPanel_), QStringLiteral("标注"));
    w->rightTabs_ = tabs;

    auto* drawer = new QDockWidget(QStringLiteral("面板"), w);
    drawer->setObjectName(QLatin1String(kDrawerName));
    drawer->setFeatures(QDockWidget::NoDockWidgetFeatures);   // 不可拖出/关闭（形态固定）。
    drawer->setTitleBarWidget(new QWidget(drawer));           // 空标题栏 = 隐藏标题。
    drawer->setWidget(tabs);
    w->addDockWidget(Qt::BottomDockWidgetArea, drawer);
    drawer->hide();   // 画布为主：默认收起，按需展开。
}

// 状态栏精简（SPEC §8.3）：只留模式/保留块数/缩放，详情入抽屉；提示仍限时弹出可见。
void MobileShellUi::buildStatus(MainWindow* w) {
    w->status_ = new StatusBar(w);
    w->status_->setModeTier(w->doc_.mode());
    w->status_->setCompactMode(true);
    w->setStatusBar(w->status_);
}

// 工具条（方向自适应）：高频能力常驻 + 「更多」低频菜单 + 「面板」开合抽屉。
// 按钮全部接既有私有槽 / 视图 gesture* 入口（与桌面同链路，行为同构）。
// 窄屏可达性：条带住 QScrollArea（触控由 TouchScrollFilter 接管、桌面预览用 QScroller，
// 另配滚轮横滚；细节见 src/mobile/README.md「触控精度与方向」）；初始态＝竖屏单行，方向
// 翻转由 applyOrientationLayout 零重建重排。
void MobileShellUi::buildBottomBar(MainWindow* w) {
    auto* tb = new QToolBar(QStringLiteral("触控工具条"), w);
    tb->setObjectName(QLatin1String(kToolBarName));
    w->addToolBar(Qt::BottomToolBarArea, tb);
    tb->setMovable(false);

    auto* scroller = new QScrollArea(tb);
    scroller->setObjectName(QLatin1String(kStripName));
    scroller->setFrameShape(QFrame::NoFrame);
    scroller->setWidgetResizable(true);   // 内容尺寸由布局最小尺寸（按钮 44pt 之和）撑开。
    scroller->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);   // 不出滚动条，只靠拖拽/滚轮。
    scroller->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroller->setFixedHeight(48);
    scroller->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* host = new QWidget(scroller);
    // 单 grid 双向复用：竖屏全在行 0（横向单行），横屏全在列 0（竖向单列）；
    // 方向切换由 applyOrientationLayout 重加同一批 widget，不重建、状态无损。
    auto* lay = new QGridLayout(host);
    lay->setContentsMargins(2, 2, 2, 2);
    lay->setSpacing(4);
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
    auto addButton = [host, lay, &stripIndex](QAction* a) {
        auto* b = new QToolButton(host);
        b->setDefaultAction(a);
        b->setToolButtonStyle(Qt::ToolButtonTextOnly);
        b->setAutoRaise(true);
        lay->addWidget(b, 0, stripIndex++);   // 初始竖屏：全入行 0。
        return b;
    };
    // 便捷：把一个动作接到主窗口槽上并加入条带（context 统一为 w，随窗口销毁）。
    auto addAct = [w, addButton](const QString& text, void (MainWindow::*slot)()) {
        const auto a = new QAction(text, w);
        QObject::connect(a, &QAction::triggered, w, slot);
        addButton(a);
        return a;
    };
    auto addLambda = [w, addButton](const QString& text, std::function<void()> fn) {
        const auto a = new QAction(text, w);
        QObject::connect(a, &QAction::triggered, w, [fn = std::move(fn)] { fn(); });
        addButton(a);
        return a;
    };

    addAct(QStringLiteral("打开"), &MainWindow::onOpen);
    addAct(QStringLiteral("导出"), &MainWindow::onExport);

    // 撤销/重做常驻（SPEC §8.3 无键盘补偿）：无菜单，故在本建造者创建动作并回填成员；
    // 不绑快捷键（触控无键盘；启用态仍由 onHistoryAvailabilityChanged 回灌同一成员）。
    w->aUndo_ = new QAction(QStringLiteral("撤销"), w);
    QObject::connect(w->aUndo_, &QAction::triggered, w, &MainWindow::onUndo);
    addButton(w->aUndo_);
    w->aRedo_ = new QAction(QStringLiteral("重做"), w);
    QObject::connect(w->aRedo_, &QAction::triggered, w, &MainWindow::onRedo);
    addButton(w->aRedo_);

    addLambda(QStringLiteral("缩小"), [w] { w->view_->zoomOut(); });
    addLambda(QStringLiteral("放大"), [w] { w->view_->zoomIn(); });
    addLambda(QStringLiteral("适应"), [w] { w->view_->fitToWindow(); });   // 与双击适应同源。

    // 模式切换（互斥组回填 modeAction* 成员）：syncPanels 的勾选回灌与 onModeAction 与桌面同源。
    auto* group = new QActionGroup(w);
    group->setExclusive(true);
    w->modeActionL1_ = group->addAction(QStringLiteral("L1"));
    w->modeActionL2_ = group->addAction(QStringLiteral("L2"));
    w->modeActionL3_ = group->addAction(QStringLiteral("L3"));
    for (QAction* a : {w->modeActionL1_, w->modeActionL2_, w->modeActionL3_}) {
        a->setCheckable(true);
        addButton(a);
    }
    w->modeActionL1_->setChecked(true);
    QObject::connect(w->modeActionL1_, &QAction::triggered, w, [w] { w->onModeAction(1); });
    QObject::connect(w->modeActionL2_, &QAction::triggered, w, [w] { w->onModeAction(2); });
    QObject::connect(w->modeActionL3_, &QAction::triggered, w, [w] { w->onModeAction(3); });

    // 极性按钮（替代 K/R 键；勾选态回显由抽屉「模式」页的桌面面板承担，同一 Document）。
    addLambda(QStringLiteral("保留"), [w] { w->onPolarityShortcut(false); });
    addLambda(QStringLiteral("删除"), [w] { w->onPolarityShortcut(true); });

    // 「删除」标注（替代 Delete 键）：与菜单/键盘同走 AnnotationCoordinator 一个槽。
    // （annotation_ 在 initCore 里创建，lambda 延迟到点击时才取——构造顺序无碍。）
    addLambda(QStringLiteral("删标注"), [w] { w->annotation_->onAnnoDeleteSelected(); });

    // 「取消/完成」（替代 Esc）：绘制态 → annoEscape（收笔/取消预览，与键盘 Esc 同信号）；
    // 否则 → 清除选区（与桌面 Esc 快捷键的菜单动作同槽）。
    addLambda(QStringLiteral("取消"), [w] {
        if (w->view_->annotationDrawActive()) w->view_->requestAnnoEscape();
        else w->onClearCut();
    });

    // 8 向步进盘（替代方向键）：QMenu + QWidgetAction 弹出，nudge → requestNudge 同链路。
    auto* padButton = new QToolButton(host);
    padButton->setText(QStringLiteral("步进"));
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

    // 「更多」：桌面菜单里低频项的触控等价物（配置存取 FR-L3.8、清除全部标注二次确认）。
    auto* moreButton = new QToolButton(host);
    moreButton->setText(QStringLiteral("更多"));
    moreButton->setPopupMode(QToolButton::InstantPopup);
    auto* moreMenu = new QMenu(moreButton);
    const QAction* aLoad = moreMenu->addAction(QStringLiteral("加载配置…"));
    QObject::connect(aLoad, &QAction::triggered, w, &MainWindow::onLoadConfig);
    const QAction* aSave = moreMenu->addAction(QStringLiteral("保存配置…"));
    QObject::connect(aSave, &QAction::triggered, w, &MainWindow::onSaveConfig);
    const QAction* aAnnoClear = moreMenu->addAction(QStringLiteral("清除全部标注"));
    QObject::connect(aAnnoClear, &QAction::triggered, w, [w] { w->annotation_->onAnnoClearAll(); });
    moreButton->setMenu(moreMenu);
    lay->addWidget(moreButton, 0, stripIndex++);

    // 「面板」：开合抽屉（尺寸按当前方向：竖屏底部占高 3/5 / 横屏右侧占宽 2/5，画布仍为主）。
    addLambda(QStringLiteral("面板"), [w] {
        auto* drawer = w->findChild<QDockWidget*>(QLatin1String(kDrawerName));
        if (!drawer) return;
        const bool show = !drawer->isVisible();
        drawer->setVisible(show);
        if (show) {
            if (w->width() > w->height())   // 横屏：抽屉停靠右侧，按宽度取值。
                w->resizeDocks({drawer}, {w->width() * 2 / 5}, Qt::Horizontal);
            else                            // 竖屏：底部抽屉，按高度取值。
                w->resizeDocks({drawer}, {w->height() * 3 / 5}, Qt::Vertical);
        }
    });
}

// 忙碌全屏遮罩：预处理耗时操作的桌面居中小对话框→移动端铺满屏幕（SPEC §8.2）；
// preprocess_ 在 initCore 才创建，故本方法由 MobileShell 构造在 initCore 后调用。
void MobileShellUi::enableTouchErgonomics(const MainWindow* w) {
    w->preprocess_->setBusyOverlayMode(true);
}

} // namespace idc::gui
