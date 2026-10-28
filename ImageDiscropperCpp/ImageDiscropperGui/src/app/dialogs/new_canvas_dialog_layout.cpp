// ============================================================================
// 文件：app/dialogs/new_canvas_dialog_layout.cpp
// 作用：实现 NewCanvasDialog 的形态编排簇——内容最小宽重算（updateFormMinWidth）、折叠/展开
//       后窗口跟随（resizeToContent）、形态候选链（applyModeChain：宽-全宽 → 宽-紧凑 →
//       宽-紧凑+预览收紧 → 窄，逐级按实际最小宽量测降档）、节内方向与外壳重挂（arrangeSections）、
//       旋转收钳（applyLayoutMode/eventFilter/resizeEvent）与宽形态下限自适应（applyWideMinimums）。
// 分块依据：见 new_canvas_dialog.cpp 头注（四 TU 拆分）；本 TU 只做几何编排（量测/重挂/收钳），
//       不建任何采参控件。
// ============================================================================
#include "app/new_canvas_dialog.h"

#include <climits>

#include <QBoxLayout>
#include <QEvent>
#include <QGuiApplication>
#include <QLabel>
#include <QMargins>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollArea>
#include <QSplitter>

#include "panels/element_appearance_group.h"   // setCompactLayout（形态编排切换外观组操作行形态）
#include "util/gui_nums.h"           // nums::mobile / nums::dialog（形态阈值与钳制比例）
#include "util/scrollable_dialog.h"  // verticalScrollBarExtent（滚动区最小宽含纵滚条宽）

namespace idc::gui {

// 仅按当前内容重算表单滚动区最小宽（不动窗口）：形态编排内部用——窗口尺寸统一由 applyLayoutMode
//   几何部分定，避免「试装增窗 → 收钳缩窗 → 本窗 Resize 重入」的事件环。
void NewCanvasDialog::updateFormMinWidth() const {
    if (!formScroll_ || !body_) return;
    const int contentMin = body_->minimumSizeHint().width() + verticalScrollBarExtent(formScroll_);
    formScroll_->setMinimumWidth(narrow_ ? contentMin : qMax(nums::dialog::kNewCanvasFormMinWidthPx, contentMin));
}

// 折叠/展开改变内容最小宽 → 重算表单滚动区最小宽，并在宽屏下让窗口跟上（只增长不收缩：
//   用户手动拖过分隔条/窗口时不打扰；护栏过滤器在超屏时钳制）。
void NewCanvasDialog::resizeToContent() {
    updateFormMinWidth();
    if (narrow_) return;   // 窄屏纵向：窗口宽由护栏管，无需调整。
    const QMargins m = layout()->contentsMargins();
    const int wantW = qMax(body_->sizeHint().width(), nums::dialog::kNewCanvasFormMinWidthPx)
                      + nums::dialog::kNewCanvasPreviewMinWidthPx + nums::dialog::kNewCanvasWideShellExtraPx
                      + m.left() + m.right();
    if (wantW > width()) resize(wantW, height());
}

// 内容最小宽变化（折叠/展开、附加图片开关、适应方式显隐、语境切换）后：先按新内容重算滚动区最小宽，
//   再重跑形态链。不重跑会把「折叠态量测判宽形态」的结论带到展开态——展开后最小宽顶超屏宽，平台旋转
//   摆位后护栏收不住、窗口卡在超宽值（985 卡死即此路径）。
void NewCanvasDialog::refreshModeChain() {
    // 构造期折叠/展开回调先于外壳装配触发（暂开取宽在 wrapScrollable 之前）：外壳未就绪即跳过，
    //   构造末的形态链首跑会以真实内容补跑。
    if (!formScroll_ || !previewCol_) return;
    if (busyOverlay_) {
        // 移动端：折叠/展开只重算滚动区最小宽，**不动窗口尺寸/位置**——内容增减走滚动，
        //   窗口保持稳定（桌面才随内容增长）。
        updateFormMinWidth();
    } else {
        resizeToContent();
    }
    const QScreen* sc = screen();
    if (!sc) sc = QGuiApplication::primaryScreen();
    applyModeChain(sc ? sc->availableGeometry() : QRect());
}

// 形态候选链：宽-全宽 → 宽-紧凑 → 宽-紧凑+预览收紧 → 窄（窄恒紧凑）。逐级按形态最小宽量测、
//   装不下退一级——不靠固定阈值赌屏宽。量测为纯算术（表单滚动区最小宽 + 预览列最小宽 + 外壳），
//   不依赖整窗 minimumSizeHint（其含 QSplitter 内部布局项，真机上不可靠）。
//   幂等：已在宽形态且现状装得下即直接返回（**不再回试升级档**——回试会重建操作行并使窗口在试装
//   与收钳之间往返，配合本窗 Resize 重入形成无限事件环，横屏打开卡死/旋转闪退）。
void NewCanvasDialog::applyModeChain(const QRect& avail) {
    if (avail.width() < nums::mobile::kStackColumnsMaxWidthPx) {
        arrangeSections(true, true);   // 窄屏直取窄形态（宽形态两级的最小宽都超出该档屏宽）。
        return;
    }
    // 宽形态整窗最小宽 = 表单最小宽（applyWideMinimums 刚按当前内容量测写入）+ 预览最小宽 + 外壳
    //   （根边距 + 分隔条把手 12px 余量）。量测须在 arrangeSections 之后（其内已重写两最小宽）。
    const int shellW = layout()->contentsMargins().left() + layout()->contentsMargins().right()
                       + nums::dialog::kNewCanvasShellFitSlackPx;
    const auto fits = [this, &avail, shellW] {
        return formScroll_->minimumWidth() + previewCol_->minimumWidth() + shellW <= avail.width();
    };
    if (!narrow_ && fits()) return;   // 已在宽形态且装得下：不动（防试装-收钳事件环）。
    // 逐级尝试：宽-全宽 → 宽-紧凑 → 宽-紧凑+预览收紧（表单一字不裁、预览让宽）→ 窄。
    arrangeSections(false, false);
    if (fits()) return;
    arrangeSections(false, true);
    if (fits()) return;
    previewCol_->setMinimumWidth(qMax(nums::dialog::kNewCanvasPreviewTightMinWidthPx,
                                      avail.width() - formScroll_->minimumWidth() - shellW));
    if (fits()) return;
    arrangeSections(true, true);
}

// 形态编排（窄/紧凑任一变化才重建；未变即跳过，多拍重跑不打扰用户分隔条）：节内方向 + 外观组
//   操作行形态 + 预览最小高 + 表单/预览外壳重挂，随后按内容重算滚动区最小宽。
void NewCanvasDialog::arrangeSections(const bool narrow, const bool compact) {
    if (narrow == narrow_ && compact == compact_) return;
    narrow_ = narrow;
    compact_ = compact;

    // 节内方向 + 外观组操作行形态 + 预览最小高。
    const QBoxLayout::Direction dir = narrow ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight;
    nodeLay_->setDirection(dir);
    backdropLay_->setDirection(dir);
    imageLay_->setDirection(dir);
    nodeAppearance_->setCompactLayout(compact);
    backdropAppearance_->setCompactLayout(compact);
    imageAppearance_->setCompactLayout(compact);
    previewLabel_->setMinimumHeight(narrow ? nums::dialog::kNewCanvasPreviewMinHeightNarrowPx : nums::dialog::kNewCanvasPreviewMinHeightPx);

    // 表单/预览外壳：窄屏纵向堆叠 ↔ 宽屏可拖分隔条（布局重挂会自动改父）。
    rootLayout_->removeWidget(formScroll_);
    rootLayout_->removeWidget(previewCol_);
    if (splitter_) rootLayout_->removeWidget(splitter_);
    if (!narrow) {
        if (!splitter_) {
            splitter_ = new QSplitter(Qt::Horizontal, this);
            splitter_->setHandleWidth(nums::dialog::kNewCanvasSplitterHandleWidthPx);   // 触控把手（与构造期同口径）。
            splitter_->setChildrenCollapsible(false);
            splitter_->setStretchFactor(0, 1);
            splitter_->setStretchFactor(1, 0);
        }
        // 宽形态下限随屏自适应（构造期与旋转切换同一口径）。
        applyWideMinimums(screen() ? screen()->availableGeometry() : QRect());
        splitter_->addWidget(formScroll_);
        splitter_->addWidget(previewCol_);
        rootLayout_->insertWidget(0, splitter_, 1);
    } else {
        if (splitter_) {
            // 子件先摘出再删分隔条（QSplitter 无 removeWidget；子件为其 QObject 子级、不摘出会随删）。
            formScroll_->setParent(nullptr);
            previewCol_->setParent(nullptr);
            delete splitter_;
            splitter_ = nullptr;
        }
        formScroll_->setMinimumHeight(0);
        rootLayout_->insertWidget(0, formScroll_, 1);   // 布局接管（重挂父）。
        rootLayout_->insertWidget(1, previewCol_);
    }
    updateFormMinWidth();   // 仅重算滚动区最小宽：窗口尺寸由 applyLayoutMode 几何部分统一定（resizeToContent 的增窗语义会与收钳形成事件环）。
}

// 屏幕几何变化（旋转）→ 形态链降档 + 按新形态的舒适尺寸重收整窗（护栏继续钳超屏）。
//   几何收钳部分每次调用都做——旋转落定多拍重钳，抵消平台在动画时序里的事后摆位。
void NewCanvasDialog::applyLayoutMode(const bool position) {
    if (!formScroll_ || !previewCol_) return;   // 外壳未就绪（构造期事件旁路）：构造末形态链首跑会补跑。
    const QScreen* sc = screen();
    if (!sc) sc = QGuiApplication::primaryScreen();
    const QRect avail = sc ? sc->availableGeometry() : QRect();
    applyModeChain(avail);
    body_->updateGeometry();
    const QSize formHint = body_->sizeHint();
    const QMargins m = layout()->contentsMargins();
    // 窄形态宽度目标 = 表单最小宽（内容恰好不裁的宽度）：不用 sizeHint——其含横屏构造烘焙的宽网格与
    //   大数值 spin 的提示宽（约 449），竖屏下该目标本身超屏、钳值未收住即露出 469 超宽。
    const int formW = narrow_ ? body_->minimumSizeHint().width() : qMax(formHint.width(), nums::dialog::kNewCanvasWideFormFloorPx);
    int wantW = formW
                + (narrow_ ? 0 : nums::dialog::kNewCanvasPreviewMinWidthPx + nums::dialog::kNewCanvasWideShellExtraPx)
                + m.left() + m.right();
    int wantH = formHint.height() + (narrow_ ? nums::dialog::kNewCanvasPreviewMinHeightNarrowPx : 0)
                + 2 * nums::dialog::kTouchFieldMinHeightPx + nums::panels::kPanelSpacingPx;
    // 显式钳到护栏口径（不依赖 Resize 事件次序）；随后按父窗口居中（无父则按屏居中）——
    //   旋转时序里屏幕几何先变、父窗口后变，护栏的父窗口处理器会随后再居中回正一次。
    if (!avail.isNull()) {
        wantW = qMin(wantW, static_cast<int>(avail.width() * nums::mobile::kDialogMaxWidthRatio));
        wantH = qMin(wantH, static_cast<int>(avail.height() * nums::mobile::kDialogMaxHeightRatio));
    }
    if (size() != QSize(wantW, wantH)) resize(wantW, wantH);   // 尺寸与目标一致即不动（每拍仅在几何不对时动作，无可见停顿）。
    // 越界才拉回（不强制居中——居中归平台；position=false 通道即移动端本窗 Resize 触发完全不摆位置）。
    if (position && !avail.isNull()) {
        QRect dlg = geometry();
        bool out = false;
        if (dlg.right() > avail.right()) { dlg.moveRight(avail.right()); out = true; }
        if (dlg.bottom() > avail.bottom()) { dlg.moveBottom(avail.bottom()); out = true; }
        if (dlg.left() < avail.left()) { dlg.moveLeft(avail.left()); out = true; }
        if (dlg.top() < avail.top()) { dlg.moveTop(avail.top()); out = true; }
        if (out) move(dlg.topLeft());
    }
}

// 父窗口尺寸/位置变化（旋转）→ 重跑形态链并收尺寸（不摆位置：动画期间平台在摆位，两方抢位即抖动；
//   位置仅由最末拍重钳与平台定）。
bool NewCanvasDialog::eventFilter(QObject* watched, QEvent* event) {
    if (watched == parentWidget() && (event->type() == QEvent::Resize || event->type() == QEvent::Move))
        applyLayoutMode(false);
    return QDialog::eventFilter(watched, event);
}

void NewCanvasDialog::resizeEvent(QResizeEvent* event) {
    QDialog::resizeEvent(event);
    if (busyOverlay_) applyLayoutMode(false);   // 移动端：平台旋转改本窗尺寸即重跑形态链、收尺寸（不摆位置——位置归平台管）。
}

// 宽形态下限随屏自适应（构造期与旋转切换共用）：固定表单/预览下限会顶过护栏钳值（可用宽×0.95）而溢出
//   ——预览下限降为屏宽/3、表单下限降为 min(kNewCanvasFormMinWidthPx, 屏宽−预览−kNewCanvasWideShellExtraPx)，
//   且恒 ≥ 内容最小宽（不裁输入）。
void NewCanvasDialog::applyWideMinimums(const QRect& avail) const {
    const int availW = avail.isNull() ? INT_MAX : avail.width();
    const int previewMinW = qMin(nums::dialog::kNewCanvasPreviewMinWidthPx, availW / 3);
    previewCol_->setMinimumWidth(previewMinW);
    const int contentMin = body_->minimumSizeHint().width() + verticalScrollBarExtent(formScroll_);
    formScroll_->setMinimumWidth(qMax(contentMin, qMin(nums::dialog::kNewCanvasFormMinWidthPx,
                                                       availW - previewMinW - nums::dialog::kNewCanvasWideShellExtraPx)));
    formScroll_->setMinimumHeight(0);
}

} // namespace idc::gui