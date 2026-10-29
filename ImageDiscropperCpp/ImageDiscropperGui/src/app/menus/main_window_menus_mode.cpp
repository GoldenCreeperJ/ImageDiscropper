// ============================================================================
// 文件：app/menus/main_window_menus_mode.cpp
// 作用：实现 MainWindowUi::buildMenus 的「模式 / 预处理」域——L1/L2/L3 模式与极性、画布四态、
//       切割参数（形状/方式/排序/余量/网格定义）与预处理快捷菜单。
// 分块依据：见 main_window_menus.cpp 头注（按菜单域四 TU 拆分）；本 TU 只依赖菜单绑定助手
//       （main_window_menus_internal.h）与各面板/控制器的既有写回路径，域内控件/连接自持。
// ============================================================================
#include "app/main_window_ui.h"

#include <QAction>
#include <QActionGroup>
#include <QMenu>
#include <QMenuBar>
#include <vector>

#include "app/controllers/preprocess_controller.h"
#include "app/main_window.h"
#include "canvas/canvas_mode.h"   // CanvasMode / L1Shape / L2Sub
#include "canvas/canvas_view.h"
#include "panels/mode_tool_panel.h"
#include "panels/param_panel.h"   // 「网格定义」子菜单经 ParamPanel::setGridAxisMode 走面板同一写回路径
#include "i18n/gui_texts.h"
#include "i18n/gui_i18n.h"
#include "i18n/retranslate.h"

#include "main_window_menus_internal.h" // bindAddAction / bindAddMenu（四个菜单 TU 共用）

namespace idc::gui {

void MainWindowUi::buildModeMenu(MainWindow* w, QMenuBar* bar) {
    // ---- 模式（L1/L2/L3 + 极性）：菜单为全部功能入口；动作与工具栏共用同一批 QAction
    //      （QActionGroup 互斥、syncPanels 从 Document 回同步选中态），无第二份状态。----
    QMenu* mMode = bindAddMenu(w->retranslator_, bar, IDC_GT(menu, kMenuMode));
    auto* modeGroup = new QActionGroup(w);
    modeGroup->setExclusive(true);
    w->modeActionL1_ = bindAddAction(w->retranslator_, modeGroup, IDC_GT(menu, kModeL1));
    w->modeActionL2_ = bindAddAction(w->retranslator_, modeGroup, IDC_GT(menu, kModeL2));
    w->modeActionL3_ = bindAddAction(w->retranslator_, modeGroup, IDC_GT(menu, kModeL3));
    for (QAction* a : {w->modeActionL1_, w->modeActionL2_, w->modeActionL3_}) {
        a->setCheckable(true);
        mMode->addAction(a);
    }
    w->modeActionL1_->setChecked(true);
    i18n::bindToolTip(w->retranslator_, w->modeActionL3_, IDC_GT(tooltip, kModeL3));
    QObject::connect(w->modeActionL1_, &QAction::triggered, w, [w] { w->onModeAction(1); });
    QObject::connect(w->modeActionL2_, &QAction::triggered, w, [w] { w->onModeAction(2); });
    QObject::connect(w->modeActionL3_, &QAction::triggered, w, [w] { w->onModeAction(3); });
    mMode->addSeparator();
    // 极性（保留/删除框内）：L1/L2 下与模式一一对应（Document::setPolarity 联动切模式），L3 独立。
    auto* polGroup = new QActionGroup(w);
    polGroup->setExclusive(true);
    w->aPolKeep_ = bindAddAction(w->retranslator_, polGroup, IDC_GT(menu, kPolarityKeep));
    w->aPolRemove_ = bindAddAction(w->retranslator_, polGroup, IDC_GT(menu, kPolarityRemove));
    w->aPolKeep_->setCheckable(true);
    w->aPolRemove_->setCheckable(true);
    w->aPolKeep_->setChecked(true);
    i18n::bindToolTip(w->retranslator_, w->aPolKeep_, IDC_GT(tooltip, kPolarityKeep));
    i18n::bindToolTip(w->retranslator_, w->aPolRemove_, IDC_GT(tooltip, kPolarityRemove));
    mMode->addAction(w->aPolKeep_);
    mMode->addAction(w->aPolRemove_);
    QObject::connect(w->aPolKeep_, &QAction::triggered, w, [w] { w->onPolarityShortcut(false); });
    QObject::connect(w->aPolRemove_, &QAction::triggered, w, [w] { w->onPolarityShortcut(true); });
    mMode->addSeparator();
    // 画布四态互斥模式（与工具面板顶行按钮镜像）：经工具面板 setMode 走同一 modeChanged → applyCanvasMode 路径。
    QMenu* mCanvas = bindAddMenu(w->retranslator_, mMode, IDC_GT(menu, kCanvasMode));
    auto* canvasGroup = new QActionGroup(w);
    canvasGroup->setExclusive(true);
    w->aCanvasMarquee_ = bindAddAction(w->retranslator_, canvasGroup, IDC_GT(menu, kCanvasMarquee));
    w->aCanvasImage_ = bindAddAction(w->retranslator_, canvasGroup, IDC_GT(menu, kCanvasImagePick));
    w->aCanvasAnnoSel_ = bindAddAction(w->retranslator_, canvasGroup, IDC_GT(menu, kCanvasAnnoSelect));
    w->aCanvasAnnoCreate_ = bindAddAction(w->retranslator_, canvasGroup, IDC_GT(menu, kCanvasAnnoCreate));
    for (QAction* a : {w->aCanvasMarquee_, w->aCanvasImage_, w->aCanvasAnnoSel_, w->aCanvasAnnoCreate_}) {
        a->setCheckable(true);
        mCanvas->addAction(a);
    }
    i18n::bindToolTip(w->retranslator_, w->aCanvasMarquee_, IDC_GT(tooltip, kCanvasMarquee));
    i18n::bindToolTip(w->retranslator_, w->aCanvasImage_, IDC_GT(tooltip, kCanvasImagePick));
    i18n::bindToolTip(w->retranslator_, w->aCanvasAnnoSel_, IDC_GT(tooltip, kCanvasAnnoSelect));
    i18n::bindToolTip(w->retranslator_, w->aCanvasAnnoCreate_, IDC_GT(tooltip, kCanvasAnnoCreate));
    QObject::connect(w->aCanvasMarquee_, &QAction::triggered, w, [w] { w->toolPanel_->setMode(CanvasMode::Marquee); });
    QObject::connect(w->aCanvasImage_, &QAction::triggered, w, [w] { w->toolPanel_->setMode(CanvasMode::ImagePick); });
    QObject::connect(w->aCanvasAnnoSel_, &QAction::triggered, w, [w] { w->toolPanel_->setMode(CanvasMode::AnnoSelect); });
    QObject::connect(w->aCanvasAnnoCreate_, &QAction::triggered, w, [w] { w->toolPanel_->setMode(CanvasMode::AnnoCreate); });
    mMode->addSeparator();
    // 切割参数（与右侧「参数」页镜像，仅当前切割模式启用；syncPanels 回同步勾选态）：
    // L1 形状 / L2 方式 / 转网格（L2）/ L3 排序。
    QMenu* mL1 = bindAddMenu(w->retranslator_, mMode, IDC_GT(menu, kL1ShapeSub));
    w->mL1Shape_ = mL1;
    auto* l1Group = new QActionGroup(w);
    l1Group->setExclusive(true);
    w->aL1Rect_ = bindAddAction(w->retranslator_, l1Group, IDC_GT(menu, kL1Rect));
    w->aL1HBand_ = bindAddAction(w->retranslator_, l1Group, IDC_GT(menu, kL1HBand));
    w->aL1VBand_ = bindAddAction(w->retranslator_, l1Group, IDC_GT(menu, kL1VBand));
    for (QAction* a : {w->aL1Rect_, w->aL1HBand_, w->aL1VBand_}) {
        a->setCheckable(true);
        mL1->addAction(a);
    }
    QObject::connect(w->aL1Rect_, &QAction::triggered, w, [w] { w->doc_.setL1Shape(L1Shape::Rect); });
    QObject::connect(w->aL1HBand_, &QAction::triggered, w, [w] { w->doc_.setL1Shape(L1Shape::Hband); });
    QObject::connect(w->aL1VBand_, &QAction::triggered, w, [w] { w->doc_.setL1Shape(L1Shape::Vband); });
    QMenu* mL2 = bindAddMenu(w->retranslator_, mMode, IDC_GT(menu, kL2MethodSub));
    w->mL2Sub_ = mL2;
    auto* l2Group = new QActionGroup(w);
    l2Group->setExclusive(true);
    w->aL2Cross_ = bindAddAction(w->retranslator_, l2Group, IDC_GT(menu, kL2Cross));
    w->aL2HLine_ = bindAddAction(w->retranslator_, l2Group, IDC_GT(menu, kL2HLine));
    w->aL2VLine_ = bindAddAction(w->retranslator_, l2Group, IDC_GT(menu, kL2VLine));
    w->aL2Multi_ = bindAddAction(w->retranslator_, l2Group, IDC_GT(menu, kL2MultiRect));
    for (QAction* a : {w->aL2Cross_, w->aL2HLine_, w->aL2VLine_, w->aL2Multi_}) {
        a->setCheckable(true);
        mL2->addAction(a);
    }
    QObject::connect(w->aL2Cross_, &QAction::triggered, w, [w] { w->doc_.setL2Sub(L2Sub::Cross); });
    QObject::connect(w->aL2HLine_, &QAction::triggered, w, [w] { w->doc_.setL2Sub(L2Sub::Hline); });
    QObject::connect(w->aL2VLine_, &QAction::triggered, w, [w] { w->doc_.setL2Sub(L2Sub::Vline); });
    QObject::connect(w->aL2Multi_, &QAction::triggered, w, [w] { w->doc_.setL2Sub(L2Sub::MultiRect); });
    // L2「转为网格模式编辑」：把当前选区（单选矩形或多矩形包围盒）送入 L3——与右侧「参数」页同名按钮
    // 同一入口 Document::convertRectToGrid（触发 changed → 刷新预览 + 切到 L3 + 工具栏/面板同步）。
    // 仅 L2 且有选区时启用（syncPanels 依 hasRect/rects 回同步置灰）。
    mL2->addSeparator();
    w->aToGrid_ = bindAddAction(w->retranslator_, mL2, IDC_GT(label, kToGridButton));
    QObject::connect(w->aToGrid_, &QAction::triggered, w, [w] { w->doc_.convertRectToGrid(); });
    // L3 网格定义（与「参数」页两轴「定义方式」下拉镜像，面板为真相源）：两级子菜单横向/纵向各三态。
    // 只切形态不填数值——切换走 ParamPanel::setGridAxisMode（面板以屏上现有数值起手），故不会留下空参数；
    // 基准点/单元尺寸/份数/权重仍在参数页输入。仅 L3 启用，syncPanels 回同步勾选态。
    QMenu* mGridDefine = bindAddMenu(w->retranslator_, mMode, IDC_GT(menu, kL3DefineSub));
    w->mGridDefine_ = mGridDefine;
    const struct { engine::GridAxisMode mode{}; i18n::TextRef label; } kGridModes[] = {
        { engine::GridAxisMode::Pitch, IDC_GT(menu, kGridModePitch) },
        { engine::GridAxisMode::Count, IDC_GT(menu, kGridModeCount) },
        { engine::GridAxisMode::Weights, IDC_GT(menu, kGridModeWeights) },
    };
    const auto addGridDefineAxis = [w, &kGridModes](QMenu* parent, const i18n::TextRef& title,
                                                    const bool horizontal,
                                                    std::vector<QAction*>& actions) {
        QMenu* sub = bindAddMenu(w->retranslator_, parent, title);
        auto* group = new QActionGroup(w);
        group->setExclusive(true);
        actions.resize(3, nullptr);
        for (const auto&[mode, label] : kGridModes) {
            QAction* a = bindAddAction(w->retranslator_, group, label);
            a->setCheckable(true);
            sub->addAction(a);
            actions[static_cast<int>(mode)] = a;
            QObject::connect(a, &QAction::triggered, w,
                             [w, horizontal, mode = mode] { w->param_->setGridAxisMode(horizontal, mode); });
        }
        return sub;
    };
    w->mGridDefineH_ = addGridDefineAxis(mGridDefine, IDC_GT(menu, kGridAxisHorizontal), true,
                                         w->gridModeActionsH_);
    w->mGridDefineV_ = addGridDefineAxis(mGridDefine, IDC_GT(menu, kGridAxisVertical), false,
                                         w->gridModeActionsV_);
    // L3排序策略
    QMenu* mSort = bindAddMenu(w->retranslator_, mMode, IDC_GT(menu, kL3SortSub));
    w->mSort_ = mSort;
    auto* sortGroup = new QActionGroup(w);
    sortGroup->setExclusive(true);
    w->aSortRow_ = bindAddAction(w->retranslator_, sortGroup, IDC_GT(menu, kSortRowMajor));
    w->aSortCol_ = bindAddAction(w->retranslator_, sortGroup, IDC_GT(menu, kSortColMajor));
    w->aSortCustom_ = bindAddAction(w->retranslator_, sortGroup, IDC_GT(menu, kSortCustom));
    for (QAction* a : {w->aSortRow_, w->aSortCol_, w->aSortCustom_}) {
        a->setCheckable(true);
        mSort->addAction(a);
    }
    QObject::connect(w->aSortRow_, &QAction::triggered, w, [w] { w->doc_.setSortStrategy(engine::SortStrategy::RowMajor); });
    QObject::connect(w->aSortCol_, &QAction::triggered, w, [w] { w->doc_.setSortStrategy(engine::SortStrategy::ColumnMajor); });
    QObject::connect(w->aSortCustom_, &QAction::triggered, w, [w] { w->doc_.setSortStrategy(engine::SortStrategy::Custom); });
    mSort->addSeparator();
    w->aSortReverse_ = bindAddAction(w->retranslator_, mSort, IDC_GT(menu, kSortReverse));
    w->aSortReverse_->setCheckable(true);
    i18n::bindToolTip(w->retranslator_, w->aSortReverse_, IDC_GT(tooltip, kSortReverse));
    QObject::connect(w->aSortReverse_, &QAction::triggered, w, [w](const bool on) { w->doc_.setSortReverse(on); });
    w->aSortSnake_ = bindAddAction(w->retranslator_, mSort, IDC_GT(menu, kSortSnake));
    w->aSortSnake_->setCheckable(true);
    i18n::bindToolTip(w->retranslator_, w->aSortSnake_, IDC_GT(tooltip, kSortSnake));
    QObject::connect(w->aSortSnake_, &QAction::triggered, w, [w](const bool on) { w->doc_.setSortSnake(on); });
    // L3 余量策略（与「参数」页余量下拉镜像，面板为真相源）：仅 L3 启用，syncPanels 回同步勾选态。
    QMenu* mRemainder = bindAddMenu(w->retranslator_, mMode, IDC_GT(menu, kL3RemainderSub));
    w->mRemainder_ = mRemainder;
    auto* remGroup = new QActionGroup(w);
    remGroup->setExclusive(true);
    const struct { engine::RemainderPolicy policy{}; i18n::TextRef label; } kRems[] = {
        { engine::RemainderPolicy::Discard, IDC_GT(menu, kRemainderDiscard) },
        { engine::RemainderPolicy::KeepPartial, IDC_GT(menu, kRemainderKeep) },
        { engine::RemainderPolicy::Pad, IDC_GT(menu, kRemainderPad) },
    };
    w->remainderActions_.resize(3, nullptr);
    for (const auto&[policy, label] : kRems) {
        QAction* a = bindAddAction(w->retranslator_, remGroup, label);
        a->setCheckable(true);
        mRemainder->addAction(a);
        w->remainderActions_[static_cast<int>(policy)] = a;
        QObject::connect(a, &QAction::triggered, w, [w, policy = policy] { w->doc_.setRemainder(policy); });
    }
}

void MainWindowUi::buildPreprocessMenu(MainWindow* w, QMenuBar* bar) {
    // ---- 预处理（FR-1：旋转/翻转/黑白/反色/重置；完整控件见右侧「预处理」节）----
    QMenu* mPreprocess = bindAddMenu(w->retranslator_, bar, IDC_GT(menu, kMenuPreprocess));
    QAction* aRotL = bindAddAction(w->retranslator_, mPreprocess, IDC_GT(menu, kRotLeft90));
    QObject::connect(aRotL, &QAction::triggered, w, [w] { w->preprocess_->onRotate(-90); });
    QAction* aRotR = bindAddAction(w->retranslator_, mPreprocess, IDC_GT(menu, kRotRight90));
    QObject::connect(aRotR, &QAction::triggered, w, [w] { w->preprocess_->onRotate(90); });
    QAction* aRot180 = bindAddAction(w->retranslator_, mPreprocess, IDC_GT(menu, kRot180));
    QObject::connect(aRot180, &QAction::triggered, w, [w] { w->preprocess_->onRotate(180); });
    mPreprocess->addSeparator();
    QAction* aFlipH = bindAddAction(w->retranslator_, mPreprocess, IDC_GT(menu, kFlipHorizontal));
    QObject::connect(aFlipH, &QAction::triggered, w, [w] { w->preprocess_->onFlip(true); });
    QAction* aFlipV = bindAddAction(w->retranslator_, mPreprocess, IDC_GT(menu, kFlipVertical));
    QObject::connect(aFlipV, &QAction::triggered, w, [w] { w->preprocess_->onFlip(false); });
    mPreprocess->addSeparator();
    w->aGrayMenu_ = bindAddAction(w->retranslator_, mPreprocess, IDC_GT(menu, kGray));
    QObject::connect(w->aGrayMenu_, &QAction::triggered, w, [w] { w->preprocess_->onGray(); });
    // 按通道反色（7 种常用组合入菜单，任意组合去「预处理」面板复选框）：子菜单项随 preprocessActions_
    // 按有无图像逐项置灰，入口本身另存 mInvert_ 由 syncPanels 整入口置灰（与「色道分离」同款；
    // 灰度图仍可反色，不受灰度智能约束）。
    QMenu* mInvert = bindAddMenu(w->retranslator_, mPreprocess, IDC_GT(menu, kInvert));
    w->mInvert_ = mInvert;
    struct InvertEntry { i18n::TextRef label; bool r, g, b; };
    const InvertEntry kInverts[] = {
        { IDC_GT(menu, kInvertAll), true, true, true },
        { IDC_GT(menu, kInvertRG), true, true, false },
        { IDC_GT(menu, kInvertRB), true, false, true },
        { IDC_GT(menu, kInvertGB), false, true, true },
        { IDC_GT(menu, kInvertR), true, false, false },
        { IDC_GT(menu, kInvertG), false, true, false },
        { IDC_GT(menu, kInvertB), false, false, true },
    };
    for (const InvertEntry& inv : kInverts) {
        QAction* a = bindAddAction(w->retranslator_, mInvert, inv.label);
        QObject::connect(a, &QAction::triggered, w, [w, inv] { w->preprocess_->onInvert(inv.r, inv.g, inv.b); });
        w->preprocessActions_.push_back(a);
    }
    // 色道分离（保留掩码 '1' 保留 / '0' 置零）：常用组合入菜单，任意通道组合去「预处理」面板复选框；
    // 与「黑白」同源置灰（syncPanels：当前预处理目标会话已产出灰度→灰度图无通道可分）。
    QMenu* mSplit = bindAddMenu(w->retranslator_, mPreprocess, IDC_GT(menu, kChannelSplit));
    w->mSplit_ = mSplit;
    struct SplitEntry { i18n::TextRef label; bool r, g, b; };
    const SplitEntry kSplits[] = {
        { IDC_GT(menu, kSplitKeepR), true, false, false },
        { IDC_GT(menu, kSplitKeepG), false, true, false },
        { IDC_GT(menu, kSplitKeepB), false, false, true },
        { IDC_GT(menu, kSplitKeepRG), true, true, false },
        { IDC_GT(menu, kSplitKeepRB), true, false, true },
        { IDC_GT(menu, kSplitKeepGB), false, true, true },
    };
    for (const SplitEntry& s : kSplits) {
        QAction* a = bindAddAction(w->retranslator_, mSplit, s.label);
        QObject::connect(a, &QAction::triggered, w, [w, s] { w->preprocess_->onSplit(s.r, s.g, s.b); });
        w->preprocessActions_.push_back(a);
    }
    mPreprocess->addSeparator();
    QAction* aResetPre = bindAddAction(w->retranslator_, mPreprocess, IDC_GT(menu, kResetPreprocess));
    QObject::connect(aResetPre, &QAction::triggered, w, [w] { w->preprocess_->onResetPreprocess(); });
    // 预处理动作集合：无图像时统一置灰（syncPanels 逐项同步；反色/分离子菜单项已逐项入列，
    // 两子菜单入口另由 mInvert_/mSplit_ 整入口置灰；「黑白」「色道分离」另有灰度智能置灰）。
    for (QAction* a : {aRotL, aRotR, aRot180, aFlipH, aFlipV, w->aGrayMenu_, aResetPre})
        w->preprocessActions_.push_back(a);
}

} // namespace idc::gui
