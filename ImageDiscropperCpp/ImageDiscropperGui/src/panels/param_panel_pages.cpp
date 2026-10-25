// ============================================================================
// 文件：panels/param_panel_pages.cpp
// 作用：ParamPanel 的 L1/L2/L3 三页构建（buildL1Page/buildL2Page/buildL3Page，TU 拆分）。
//       本 TU 仅按页分拆编译，类不拆。
// ============================================================================
#include "panels/param_panel.h"

#include "param_panel_internal.h"

#include <algorithm>

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QResizeEvent>
#include <QSpinBox>
#include <QStackedLayout>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "engine/engine.h"
#include "model/document.h"
#include "util/widget_factory.h"
#include "i18n/gui_texts.h"
#include "i18n/gui_i18n.h"

namespace idc::gui {


// L1 页：形状 + 坐标。
QWidget* ParamPanel::buildL1Page() {
    auto* page = new QWidget(this);
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);

    auto* box = makeGroupBox(retranslator_, page, IDC_GT(label, kParamL1Box));
    auto* form = new QFormLayout(box);
    l1Shape_ = new QComboBox(box);
    i18n::addItemsI18n(retranslator_, l1Shape_, {IDC_GT(menu, kL1Rect), IDC_GT(menu, kL1HBand),
                                                 IDC_GT(menu, kL1VBand)});
    i18n::bindToolTip(retranslator_, l1Shape_, IDC_GT(tooltip, kParamL1ShapeCombo));
    i18n::bindFormRow(retranslator_, form, l1Shape_, IDC_GT(label, kShapeFieldLabel));

    l1x1_ = makeCoordSpin(box); l1y1_ = makeCoordSpin(box);
    l1x2_ = makeCoordSpin(box); l1y2_ = makeCoordSpin(box);
    // 百分比输入基准：横向坐标取画布宽、纵向取画布高（SPEC §2.1）。
    bindCoordBasis(l1x1_, true);
    bindCoordBasis(l1x2_, true);
    bindCoordBasis(l1y1_, false);
    bindCoordBasis(l1y2_, false);
    form->addRow(QStringLiteral("x1"), l1x1_);
    form->addRow(QStringLiteral("y1"), l1y1_);
    form->addRow(QStringLiteral("x2"), l1x2_);
    form->addRow(QStringLiteral("y2"), l1y2_);
    v->addWidget(box);

    auto* note = new QLabel(page);
    i18n::bindText(retranslator_, note, IDC_GT(label, kL1PageNote));
    note->setWordWrap(true);
    v->addWidget(note);
    v->addStretch(1);

    connect(l1Shape_, &QComboBox::currentIndexChanged, this, &ParamPanel::onL1ShapeChanged);
    for (const QSpinBox* s : {l1x1_, l1y1_, l1x2_, l1y2_})
        connect(s, &QSpinBox::valueChanged, this, &ParamPanel::onCoordEdited);
    return page;
}

// L2 页：子功能 + 坐标 + 坍缩可行性提示。
QWidget* ParamPanel::buildL2Page() {
    auto* page = new QWidget(this);
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);

    auto* box = makeGroupBox(retranslator_, page, IDC_GT(label, kParamL2Box));
    auto* form = new QFormLayout(box);
    l2Sub_ = new QComboBox(box);
    i18n::addItemsI18n(retranslator_, l2Sub_, {IDC_GT(menu, kL2Cross), IDC_GT(menu, kL2HLine),
                                               IDC_GT(menu, kL2VLine), IDC_GT(menu, kL2MultiRect)});
    i18n::bindToolTip(retranslator_, l2Sub_, IDC_GT(tooltip, kParamL2SubCombo));
    i18n::bindFormRow(retranslator_, form, l2Sub_, IDC_GT(label, kSubFeatureFieldLabel));

    l2x1_ = makeCoordSpin(box); l2y1_ = makeCoordSpin(box);
    l2x2_ = makeCoordSpin(box); l2y2_ = makeCoordSpin(box);
    // 百分比输入基准：横向坐标取画布宽、纵向取画布高（SPEC §2.1）。
    bindCoordBasis(l2x1_, true);
    bindCoordBasis(l2x2_, true);
    bindCoordBasis(l2y1_, false);
    bindCoordBasis(l2y2_, false);
    form->addRow(QStringLiteral("x1"), l2x1_);
    form->addRow(QStringLiteral("y1"), l2y1_);
    form->addRow(QStringLiteral("x2"), l2x2_);
    form->addRow(QStringLiteral("y2"), l2y2_);
    v->addWidget(box);

    // ---- 多矩形并集列表（仅 l2Sub_==MultiRect 显示）----
    // 追加矩形的主入口是画布框选（CanvasCoordinator::onRubberSelect）；此处提供列表可视化 +
    // 选中项坐标微调（复用上方 x1/y1/x2/y2）+ 删除/清空。面板只搬运，不做并集计算（CONTRIBUTING.md「分层纪律」）。
    l2MultiBox_ = makeGroupBox(retranslator_, page, IDC_GT(label, kMultiRectBox));
    auto* mv = new QVBoxLayout(l2MultiBox_);
    l2RectList_ = new QListWidget(l2MultiBox_);
    i18n::bindToolTip(retranslator_, l2RectList_, IDC_GT(tooltip, kParamRectList));
    mv->addWidget(l2RectList_);
    auto* mbtnRow = new QHBoxLayout();
    l2RectDelBtn_ = makeButton(retranslator_, l2MultiBox_, IDC_GT(label, kDeleteSelectedButton));
    l2RectClearBtn_ = makeButton(retranslator_, l2MultiBox_, IDC_GT(label, kClearButton));
    mbtnRow->addWidget(l2RectDelBtn_);
    mbtnRow->addWidget(l2RectClearBtn_);
    mv->addLayout(mbtnRow);
    auto* mnote = new QLabel(l2MultiBox_);
    i18n::bindText(retranslator_, mnote, IDC_GT(label, kMultiRectNote));
    mnote->setWordWrap(true);
    mv->addWidget(mnote);
    l2MultiBox_->setVisible(false); // 默认隐藏，切到 MultiRect 才显示（见 syncFromDocument）。
    v->addWidget(l2MultiBox_);
    
    // 坍缩可行性提示：由 PreviewController 依 Core isCollapsible 结果回灌（初值绑定，动态值由 syncPanels 重派生）。
    collapseHint_ = new QLabel(page);
    i18n::bindText(retranslator_, collapseHint_, IDC_GT(status, kCollapseHintInit));
    collapseHint_->setWordWrap(true);
    v->addWidget(collapseHint_);
    
    auto* outNote = new QLabel(page);
    i18n::bindText(retranslator_, outNote, IDC_GT(label, kL2OutModeNote));
    outNote->setWordWrap(true);
    v->addWidget(outNote);
    
    // 「转为网格模式编辑」入口：把当前矩形选区送入 L3 逐单元精修。
    l2ToGridBtn_ = makeButton(retranslator_, page, IDC_GT(label, kToGridButton));
    i18n::bindToolTip(retranslator_, l2ToGridBtn_, IDC_GT(tooltip, kParamToGrid));
    v->addWidget(l2ToGridBtn_);
    v->addStretch(1);

    connect(l2Sub_, &QComboBox::currentIndexChanged, this, &ParamPanel::onL2SubChanged);
    for (const QSpinBox* s : {l2x1_, l2y1_, l2x2_, l2y2_})
        connect(s, &QSpinBox::valueChanged, this, &ParamPanel::onCoordEdited);
    connect(l2ToGridBtn_, &QPushButton::clicked, this, &ParamPanel::onConvertToGrid);
    connect(l2RectList_, &QListWidget::currentRowChanged, this, &ParamPanel::onRectListSelectionChanged);
    connect(l2RectDelBtn_, &QPushButton::clicked, this, &ParamPanel::onRectDelClicked);
    connect(l2RectClearBtn_, &QPushButton::clicked, this, &ParamPanel::onRectClearClicked);
    return page;
}

// L3 页：网格定义（每轴可选周期 / 分割数 / 比例）+ 选择集（全选/反选/清空）+ 排序。
// 说明：行列数由 Core 依图像边界自动推导（只读回显）；三形态的语义与展开规则全在 Core（GridAxisSpec），
//       面板只搬运参数（CONTRIBUTING.md「分层纪律」）；
//       画布上可单击单元切换选中、拖拽框选批量选中、Custom 下拖拽调序（见 canvas/cell_picker_item）；本页提供按钮式选择集与排序策略。
QWidget* ParamPanel::buildL3Page() {
    auto* page = new QWidget(this);
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);

    // ---- 网格定义（FR-L3.1 / FR-L3.2）----
    // 按轴各选一种定义方式（两轴各用各的：如「横向等分 + 纵向固定高度」），与 config/CLI 的 x/y 嵌套定义同一口径。
    auto* defBox = makeGroupBox(retranslator_, page, IDC_GT(label, kParamL3Box));
    auto* defForm = new QFormLayout(defBox);

    // 单轴三形态页：0 周期（基准点 + 单元尺寸）/ 1 分割数（份数）/ 2 比例（权重），索引与 GridAxisMode 一致。
    const auto makeAxisPages = [this, defBox](QSpinBox*& origin, QSpinBox*& cell, QSpinBox*& count,
                                             QLineEdit*& weights,
                                             const i18n::TextRef& originLabel, const i18n::TextRef& cellLabel,
                                             const i18n::TextRef& originTip, const i18n::TextRef& cellTip,
                                             const bool horizontal) {
        auto* stack = new QStackedWidget(defBox);

        auto* pitch = new QWidget(stack);
        auto* pf = new QFormLayout(pitch);
        pf->setContentsMargins(0, 0, 0, 0);
        origin = makeCoordSpin(pitch);
        cell = makeCoordSpin(pitch);
        cell->setMinimum(1);   // 单元尺寸必须为正（Core Grid::build 要求）。
        // 百分比输入基准：同轴同基准（横轴取画布宽、纵轴取画布高，SPEC §2.1）——基准点与单元尺寸共用。
        bindCoordBasis(origin, horizontal);
        bindCoordBasis(cell, horizontal);
        i18n::bindToolTip(retranslator_, origin, originTip);
        i18n::bindToolTip(retranslator_, cell, cellTip);
        i18n::bindFormRow(retranslator_, pf, origin, originLabel);
        i18n::bindFormRow(retranslator_, pf, cell, cellLabel);
        stack->addWidget(pitch);

        auto* countPage = new QWidget(stack);
        auto* cf = new QFormLayout(countPage);
        cf->setContentsMargins(0, 0, 0, 0);
        count = makeSpin(countPage, 1, nums::panels::kCoordLimitPx);
        count->setKeyboardTracking(false);
        count->setFixedWidth(nums::panels::kCoordSpinWidthPx);
        i18n::bindToolTip(retranslator_, count, IDC_GT(tooltip, kGridCountSpin));
        i18n::bindFormRow(retranslator_, cf, count, IDC_GT(label, kGridCountLabel));
        stack->addWidget(countPage);

        auto* weightPage = new QWidget(stack);
        auto* wf = new QFormLayout(weightPage);
        wf->setContentsMargins(0, 0, 0, 0);
        weights = new QLineEdit(weightPage);
        i18n::bindToolTip(retranslator_, weights, IDC_GT(tooltip, kGridWeightsEdit));
        i18n::bindPlaceholder(retranslator_, weights, IDC_GT(label, kGridWeightsPlaceholder));
        i18n::bindFormRow(retranslator_, wf, weights, IDC_GT(label, kGridWeightsLabel));
        stack->addWidget(weightPage);
        return stack;
    };

    gModeX_ = new QComboBox(defBox);
    i18n::addItemsI18n(retranslator_, gModeX_, {IDC_GT(menu, kGridModePitch), IDC_GT(menu, kGridModeCount),
                                                IDC_GT(menu, kGridModeWeights)});
    i18n::bindToolTip(retranslator_, gModeX_, IDC_GT(tooltip, kGridModeCombo));
    i18n::bindFormRow(retranslator_, defForm, gModeX_, IDC_GT(label, kGridDefineXLabel));
    gStackX_ = makeAxisPages(gx0_, gcw_, gCountX_, gWeightsX_,
                             IDC_GT(label, kGridOriginXLabel), IDC_GT(label, kCellWidthLabel),
                             IDC_GT(tooltip, kGridOriginXSpin), IDC_GT(tooltip, kCellWidthSpin), true);
    defForm->addRow(gStackX_);   // 无行标签：三形态页内各自带行标签。

    gModeY_ = new QComboBox(defBox);
    i18n::addItemsI18n(retranslator_, gModeY_, {IDC_GT(menu, kGridModePitch), IDC_GT(menu, kGridModeCount),
                                                IDC_GT(menu, kGridModeWeights)});
    i18n::bindToolTip(retranslator_, gModeY_, IDC_GT(tooltip, kGridModeCombo));
    i18n::bindFormRow(retranslator_, defForm, gModeY_, IDC_GT(label, kGridDefineYLabel));
    gStackY_ = makeAxisPages(gy0_, gch_, gCountY_, gWeightsY_,
                             IDC_GT(label, kGridOriginYLabel), IDC_GT(label, kCellHeightLabel),
                             IDC_GT(tooltip, kGridOriginYSpin), IDC_GT(tooltip, kCellHeightSpin), false);
    defForm->addRow(gStackY_);

    gRemainder_ = new QComboBox(defBox);
    // 索引与 Core RemainderPolicy 一致：0=Discard, 1=KeepPartial, 2=Pad。
    i18n::addItemsI18n(retranslator_, gRemainder_, {IDC_GT(menu, kRemainderDiscard),
                                                    IDC_GT(menu, kRemainderKeep),
                                                    IDC_GT(menu, kRemainderPad)});
    i18n::bindToolTip(retranslator_, gRemainder_, IDC_GT(tooltip, kRemainderCombo));
    i18n::bindFormRow(retranslator_, defForm, gRemainder_, IDC_GT(label, kRemainderFieldLabel));

    // 两轴均非周期（分割数 / 比例恰好铺满，不存在残缺单元）时的同屏说明：与余量组合框置灰一起出现。
    gNoPitchHint_ = new QLabel(defBox);
    i18n::bindText(retranslator_, gNoPitchHint_, IDC_GT(status, kGridNoPitchHint));
    gNoPitchHint_->setWordWrap(true);
    gNoPitchHint_->setVisible(false);   // 默认两轴周期；由 syncFromDocument 依轴形态切换。
    defForm->addRow(gNoPitchHint_);

    gGridInfo_ = new QLabel(IDC_GTR(status, kGridPendingInfo), defBox);   // 动态回显：syncFromDocument 重派生
    gGridInfo_->setWordWrap(true);
    i18n::bindFormRow(retranslator_, defForm, gGridInfo_, IDC_GT(label, kDerivedScaleLabel));
    v->addWidget(defBox);

    // ---- 选择集（FR-L3.3）----
    auto* selBox = makeGroupBox(retranslator_, page, IDC_GT(label, kSelectSetBox));
    auto* selV = new QVBoxLayout(selBox);
    auto* btnRow = new QHBoxLayout();
    auto* allBtn = makeButton(retranslator_, selBox, IDC_GT(label, kSelectAllButton));
    auto* invBtn = makeButton(retranslator_, selBox, IDC_GT(label, kInvertButton));
    auto* clrBtn = makeButton(retranslator_, selBox, IDC_GT(label, kClearButton));
    btnRow->addWidget(allBtn);
    btnRow->addWidget(invBtn);
    btnRow->addWidget(clrBtn);
    selV->addLayout(btnRow);
    gSelInfo_ = new QLabel(IDC_GTR(status, kGridSelectedInfo).arg(0), selBox);   // 动态回显
    gSelInfo_->setWordWrap(true);
    selV->addWidget(gSelInfo_);
    auto* selNote = new QLabel(selBox);
    i18n::bindText(retranslator_, selNote, IDC_GT(label, kCellSelectNote));
    selNote->setWordWrap(true);
    selV->addWidget(selNote);
    v->addWidget(selBox);

    // ---- 排序（FR-L3.5）----
    auto* sortBox = makeGroupBox(retranslator_, page, IDC_GT(label, kSortSetBox));
    // 1×3 横排：策略下拉 / 反向 / 蛇形 同占一行（策略下拉占余宽，两个复选框按文字取宽）。
    auto* sortRow = new QHBoxLayout(sortBox);
    gSort_ = new QComboBox(sortBox);
    // 索引与 Core SortStrategy 一致：0=RowMajor, 1=ColumnMajor, 2=Custom。
    i18n::addItemsI18n(retranslator_, gSort_, {IDC_GT(menu, kSortRowMajor),
                                               IDC_GT(menu, kSortColMajor),
                                               IDC_GT(menu, kSortCustom)});
    i18n::bindToolTip(retranslator_, gSort_, IDC_GT(tooltip, kSortCombo));
    gReverse_ = makeCheckBox(retranslator_, sortBox, IDC_GT(menu, kSortReverse));
    gSnake_ = makeCheckBox(retranslator_, sortBox, IDC_GT(label, kSnakeCheck));
    sortRow->addWidget(gSort_, 1);
    sortRow->addWidget(gReverse_);
    sortRow->addWidget(gSnake_);
    v->addWidget(sortBox);
    v->addStretch(1);

    connect(gModeX_, &QComboBox::currentIndexChanged, this,
            [this](const int index) { onGridModeChanged(true, index); });
    connect(gModeY_, &QComboBox::currentIndexChanged, this,
            [this](const int index) { onGridModeChanged(false, index); });
    connect(gCountX_, &QSpinBox::valueChanged, this, [this](int) { onGridCountEdited(true); });
    connect(gCountY_, &QSpinBox::valueChanged, this, [this](int) { onGridCountEdited(false); });
    connect(gWeightsX_, &QLineEdit::editingFinished, this, [this] { onGridWeightsEdited(true); });
    connect(gWeightsY_, &QLineEdit::editingFinished, this, [this] { onGridWeightsEdited(false); });
    connect(gx0_, &QSpinBox::valueChanged, this, &ParamPanel::onGridOriginEdited);
    connect(gy0_, &QSpinBox::valueChanged, this, &ParamPanel::onGridOriginEdited);
    connect(gcw_, &QSpinBox::valueChanged, this, &ParamPanel::onCellSizeEdited);
    connect(gch_, &QSpinBox::valueChanged, this, &ParamPanel::onCellSizeEdited);
    connect(gRemainder_, &QComboBox::currentIndexChanged, this, &ParamPanel::onRemainderChanged);
    connect(gSort_, &QComboBox::currentIndexChanged, this, &ParamPanel::onSortStrategyChanged);
    connect(gReverse_, &QCheckBox::toggled, this, &ParamPanel::onSortReverseToggled);
    connect(gSnake_, &QCheckBox::toggled, this, &ParamPanel::onSortSnakeToggled);
    connect(allBtn, &QPushButton::clicked, this, &ParamPanel::onSelectAllCells);
    connect(invBtn, &QPushButton::clicked, this, &ParamPanel::onInvertCells);
    connect(clrBtn, &QPushButton::clicked, this, &ParamPanel::onClearCells);
    return page;
}

} // namespace idc::gui
