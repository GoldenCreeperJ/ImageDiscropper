// ============================================================================
// 文件：mobile/mobile_shell.cpp
// 作用：MobileShell 实现——移动端顶层壳本体。骨架装配（部件创建/回填与连接）全在
//       建造者 MobileShellUi（mobile_shell_ui.cpp，与桌面 main_window_ui.cpp 同纪律）；
//       本文件只含壳自身的两件事：装配顺序（构造）与方向切换（resizeEvent）。
// 分块依据：
//   - 构造 = 骨架装配 → initCore()（基类共用接线，序列承重不可打乱）；
//   - 竖/横屏翻转仅重排抽屉停靠区与工具条（不重建任何部件 → 状态无损，SPEC §8.3）。
// 说明：样式表与部件 objectName 常量（kTouchChromeStyle / kDrawerName 等）随
//       mobile_shell_ui.h 共驻（壳与建造者共用）。桌面构建中本文件只属 idc_gui_mobile
//       目标（IDC_GUI_MOBILE=ON，默认）；Android 单目标必含。
// ============================================================================
#include "mobile/mobile_shell.h"

#include <QDockWidget>
#include <QGridLayout>
#include <QMainWindow>
#include <QResizeEvent>
#include <QScrollArea>
#include <QToolBar>
#include <QToolButton>
#include <QWidget>

#include "mobile/mobile_shell_ui.h"

// 版本号经 CMake 编译期宏注入；独立配置本目录时回退占位版本（与构建说明一致）。
#ifndef IDC_GUI_VERSION
#define IDC_GUI_VERSION "dev"
#endif

namespace idc::gui {

// 构造：装配顺序＝部件 → 抽屉 → 状态栏 → 工具条 → initCore（基类接线，顺序承重）。
MobileShell::MobileShell(QWidget* parent) : MainWindow(MobileShellTag{}, parent) {
    setWindowTitle(QStringLiteral("ImageDiscropper GUI %1").arg(QStringLiteral(IDC_GUI_VERSION)));
    resize(480, 960);                      // 竖屏典型比例；横屏同为合法形态（§8.3 双布局）。
    setStyleSheet(QLatin1String(kTouchChromeStyle));

    MobileShellUi::buildCentral(this);
    MobileShellUi::buildDrawer(this);
    MobileShellUi::buildStatus(this);
    MobileShellUi::buildBottomBar(this);

    initCore();                            // 与桌面逐行共用的唯一接线序列。
    MobileShellUi::enableTouchErgonomics(this);   // 控制器就绪后再开移动形态开关。
}

// 旋转处理（SPEC §8.3）：Android 方向变化表现为窗口尺寸变化（先旋转后 resize），
// 桌面 idc_gui_mobile 预览拖窗口同路径；只重排抽屉停靠区，不重建任何部件。
void MobileShell::resizeEvent(QResizeEvent* event) {
    MainWindow::resizeEvent(event);
    applyOrientationLayout();
}

// 双布局唯一切换点：仅当宽高比跨越 1:1（方向真正翻转）时动作——普通 resize 直接返回，
// 天然防抖；面板/按钮状态（勾选/参数/列表选中）全在既有 widget 内，重排零触碰 → 状态无损。
// 两件事：① 抽屉停靠区翻转（竖＝底部高 3/5 / 横＝右侧宽 2/5）；
// ② 工具条换边 + 条带重排（竖＝底部单行 / 横＝右侧单列：横屏底部纵向空间珍贵，
//    改右侧边栏后工具条宽自适应最宽按钮，高度不够靠既有的拖动/默认竖滚触达）。
void MobileShell::applyOrientationLayout() {
    auto* drawer = findChild<QDockWidget*>(QLatin1String(kDrawerName));
    if (!drawer) return;   // 首次 show 前 buildDrawer 已按竖屏底部停靠，此处只响应翻转。
    const Orientation next = width() > height() ? Orientation::Landscape : Orientation::Portrait;
    if (next == orientation_) return;
    orientation_ = next;
    const bool landscape = next == Orientation::Landscape;
    if (landscape) {
        // 横屏：抽屉改停右侧（与画布左右分屏），宽占 2/5；展开/收起可见态不变。
        // 布局自外向内＝[画布 | 抽屉 | 工具条]：工具条最右、抽屉居右、画布仍为主。
        addDockWidget(Qt::RightDockWidgetArea, drawer);
        resizeDocks({drawer}, {width() * 2 / 5}, Qt::Horizontal);
    } else {
        // 竖屏：回到底部，高占 3/5。
        addDockWidget(Qt::BottomDockWidgetArea, drawer);
        resizeDocks({drawer}, {height() * 3 / 5}, Qt::Vertical);
    }

    // 工具条换停靠区（QMainWindow::addToolBar 对已在窗口内的工具条即迁移）。
    auto* tb = findChild<QToolBar*>(QLatin1String(kToolBarName));
    auto* scroller = findChild<QScrollArea*>(QLatin1String(kStripName));
    if (!tb || !scroller) return;

    // 先重排内容、后迁移停靠区（顺序承重：先迁移则网格仍是旧形态单行 → 侧栏宽度爆炸）。
    // 条带重排：同一批 QToolButton 改网格坐标：行 0 ↔ 列 0。
    const auto* host = scroller->widget();
    auto* grid = host ? qobject_cast<QGridLayout*>(host->layout()) : nullptr;
    if (grid) {
        // 仅直接子级：步进盘/菜单里若有嵌套按钮不得入重排（child 列表序＝添加序）。
        const auto buttons = host->findChildren<QToolButton*>(QString(), Qt::FindDirectChildrenOnly);
        for (int i = 0; i < buttons.size(); ++i)
            grid->addWidget(buttons[i], landscape ? i : 0, landscape ? 0 : i);
        grid->setAlignment(landscape ? Qt::AlignTop : Qt::AlignLeft);   // 不拉伸剩余空间。
    }
    // 条带尺寸策略随形态：竖屏定高 48 横向铺展；横屏定宽＝单列内容宽、纵向填充侧栏。
    scroller->setMinimumHeight(0);
    scroller->setMaximumHeight(QWIDGETSIZE_MAX);
    scroller->setMinimumWidth(0);
    scroller->setMaximumWidth(QWIDGETSIZE_MAX);
    if (landscape) {
        // 定宽到单列内容（宽扁条实测修复，详见 src/mobile/README.md「触控精度与方向」）。
        if (grid) grid->activate();
        scroller->setFixedWidth(host ? host->sizeHint().width() : 96);
        scroller->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    } else {
        scroller->setFixedHeight(48);
        scroller->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    addToolBar(landscape ? Qt::RightToolBarArea : Qt::BottomToolBarArea, tb);
}

} // namespace idc::gui
