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
#include "i18n/gui_texts.h"
#include "i18n/gui_i18n.h"

namespace idc::gui {


// L1 页：形状 + 坐标。
QWidget* ParamPanel::buildL1Page() {
    auto* page = new QWidget(this);
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);

    auto* box = new QGroupBox(page);
    i18n::bindTitle(retranslator_, box, IDC_GT(label, kParamL1Box));
    auto* form = new QFormLayout(box);
    l1Shape_ = new QComboBox(box);
    i18n::addItemsI18n(retranslator_, l1Shape_, {IDC_GT(menu, kL1Rect), IDC_GT(menu, kL1HBand),
                                                 IDC_GT(menu, kL1VBand)});
    i18n::bindToolTip(retranslator_, l1Shape_, IDC_GT(tooltip, kParamL1ShapeCombo));
    i18n::bindFormRow(retranslator_, form, l1Shape_, IDC_GT(label, kShapeFieldLabel));

    l1x1_ = makeCoordSpin(box); l1y1_ = makeCoordSpin(box);
    l1x2_ = makeCoordSpin(box); l1y2_ = makeCoordSpin(box);
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

    auto* box = new QGroupBox(page);
    i18n::bindTitle(retranslator_, box, IDC_GT(label, kParamL2Box));
    auto* form = new QFormLayout(box);
    l2Sub_ = new QComboBox(box);
    i18n::addItemsI18n(retranslator_, l2Sub_, {IDC_GT(menu, kL2Cross), IDC_GT(menu, kL2HLine),
                                               IDC_GT(menu, kL2VLine), IDC_GT(menu, kL2MultiRect)});
    i18n::bindToolTip(retranslator_, l2Sub_, IDC_GT(tooltip, kParamL2SubCombo));
    i18n::bindFormRow(retranslator_, form, l2Sub_, IDC_GT(label, kSubFeatureFieldLabel));

    l2x1_ = makeCoordSpin(box); l2y1_ = makeCoordSpin(box);
    l2x2_ = makeCoordSpin(box); l2y2_ = makeCoordSpin(box);
    form->addRow(QStringLiteral("x1"), l2x1_);
    form->addRow(QStringLiteral("y1"), l2y1_);
    form->addRow(QStringLiteral("x2"), l2x2_);
    form->addRow(QStringLiteral("y2"), l2y2_);
    v->addWidget(box);

    // ---- 多矩形并集列表（仅 l2Sub_==MultiRect 显示）----
    // 追加矩形的主入口是画布框选（CanvasCoordinator::onRubberSelect）；此处提供列表可视化 +
    // 选中项坐标微调（复用上方 x1/y1/x2/y2）+ 删除/清空。面板只搬运，不做并集计算（CONTRIBUTING.md「分层纪律」）。
    l2MultiBox_ = new QGroupBox(page);
    i18n::bindTitle(retranslator_, dynamic_cast<QGroupBox*>(l2MultiBox_), IDC_GT(label, kMultiRectBox));
    auto* mv = new QVBoxLayout(l2MultiBox_);
    l2RectList_ = new QListWidget(l2MultiBox_);
    i18n::bindToolTip(retranslator_, l2RectList_, IDC_GT(tooltip, kParamRectList));
    mv->addWidget(l2RectList_);
    auto* mbtnRow = new QHBoxLayout();
    l2RectDelBtn_ = new QPushButton(l2MultiBox_);
    i18n::bindButton(retranslator_, l2RectDelBtn_, IDC_GT(label, kDeleteSelectedButton));
    l2RectClearBtn_ = new QPushButton(l2MultiBox_);
    i18n::bindButton(retranslator_, l2RectClearBtn_, IDC_GT(label, kClearButton));
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
    l2ToGridBtn_ = new QPushButton(page);
    i18n::bindButton(retranslator_, l2ToGridBtn_, IDC_GT(label, kToGridButton));
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

// L3 页：网格定义（基准点 + 单元尺寸 + 余量策略）+ 选择集（全选/反选/清空）+ 排序。
// 说明：行列数由 Core 依图像边界自动推导（只读回显），面板不输入行列数（CONTRIBUTING.md「分层纪律」）；
//       画布上可单击单元切换选中、拖拽框选批量选中、Custom 下拖拽调序（见 canvas/cell_picker_item）；本页提供按钮式选择集与排序策略。
QWidget* ParamPanel::buildL3Page() {
    auto* page = new QWidget(this);
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);

    // ---- 网格定义（FR-L3.1 / FR-L3.2）----
    auto* defBox = new QGroupBox(page);
    i18n::bindTitle(retranslator_, defBox, IDC_GT(label, kParamL3Box));
    auto* defForm = new QFormLayout(defBox);
    gx0_ = makeCoordSpin(defBox); gy0_ = makeCoordSpin(defBox);
    gcw_ = makeCoordSpin(defBox); gch_ = makeCoordSpin(defBox);
    gcw_->setMinimum(1); gch_->setMinimum(1); // 单元尺寸必须为正（Core Grid::build 要求）。
    i18n::bindToolTip(retranslator_, gx0_, IDC_GT(tooltip, kGridOriginXSpin));
    i18n::bindToolTip(retranslator_, gy0_, IDC_GT(tooltip, kGridOriginYSpin));
    i18n::bindToolTip(retranslator_, gcw_, IDC_GT(tooltip, kCellWidthSpin));
    i18n::bindToolTip(retranslator_, gch_, IDC_GT(tooltip, kCellHeightSpin));
    i18n::bindFormRow(retranslator_, defForm, gx0_, IDC_GT(label, kGridOriginXLabel));
    i18n::bindFormRow(retranslator_, defForm, gy0_, IDC_GT(label, kGridOriginYLabel));
    i18n::bindFormRow(retranslator_, defForm, gcw_, IDC_GT(label, kCellWidthLabel));
    i18n::bindFormRow(retranslator_, defForm, gch_, IDC_GT(label, kCellHeightLabel));

    gRemainder_ = new QComboBox(defBox);
    // 索引与 Core RemainderPolicy 一致：0=Discard, 1=KeepPartial, 2=Pad。
    i18n::addItemsI18n(retranslator_, gRemainder_, {IDC_GT(menu, kRemainderDiscard),
                                                    IDC_GT(menu, kRemainderKeep),
                                                    IDC_GT(menu, kRemainderPad)});
    i18n::bindToolTip(retranslator_, gRemainder_, IDC_GT(tooltip, kRemainderCombo));
    i18n::bindFormRow(retranslator_, defForm, gRemainder_, IDC_GT(label, kRemainderFieldLabel));

    gGridInfo_ = new QLabel(IDC_GTR(status, kGridPendingInfo), defBox);   // 动态回显：syncFromDocument 重派生
    gGridInfo_->setWordWrap(true);
    i18n::bindFormRow(retranslator_, defForm, gGridInfo_, IDC_GT(label, kDerivedScaleLabel));
    v->addWidget(defBox);

    // ---- 选择集（FR-L3.3）----
    auto* selBox = new QGroupBox(page);
    i18n::bindTitle(retranslator_, selBox, IDC_GT(label, kSelectSetBox));
    auto* selV = new QVBoxLayout(selBox);
    auto* btnRow = new QHBoxLayout();
    auto* allBtn = new QPushButton(selBox);
    i18n::bindButton(retranslator_, allBtn, IDC_GT(label, kSelectAllButton));
    auto* invBtn = new QPushButton(selBox);
    i18n::bindButton(retranslator_, invBtn, IDC_GT(label, kInvertButton));
    auto* clrBtn = new QPushButton(selBox);
    i18n::bindButton(retranslator_, clrBtn, IDC_GT(label, kClearButton));
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
    auto* sortBox = new QGroupBox(page);
    i18n::bindTitle(retranslator_, sortBox, IDC_GT(label, kSortSetBox));
    // 1×3 横排：策略下拉 / 反向 / 蛇形 同占一行（策略下拉占余宽，两个复选框按文字取宽）。
    auto* sortRow = new QHBoxLayout(sortBox);
    gSort_ = new QComboBox(sortBox);
    // 索引与 Core SortStrategy 一致：0=RowMajor, 1=ColumnMajor, 2=Custom。
    i18n::addItemsI18n(retranslator_, gSort_, {IDC_GT(menu, kSortRowMajor),
                                               IDC_GT(menu, kSortColMajor),
                                               IDC_GT(menu, kSortCustom)});
    i18n::bindToolTip(retranslator_, gSort_, IDC_GT(tooltip, kSortCombo));
    gReverse_ = new QCheckBox(sortBox);
    i18n::bindButton(retranslator_, gReverse_, IDC_GT(menu, kSortReverse));
    gSnake_ = new QCheckBox(sortBox);
    i18n::bindButton(retranslator_, gSnake_, IDC_GT(label, kSnakeCheck));
    sortRow->addWidget(gSort_, 1);
    sortRow->addWidget(gReverse_);
    sortRow->addWidget(gSnake_);
    v->addWidget(sortBox);
    v->addStretch(1);

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
