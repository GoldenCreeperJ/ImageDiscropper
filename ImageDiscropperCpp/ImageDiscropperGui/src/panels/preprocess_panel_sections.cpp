// ============================================================================
// 文件：panels/preprocess_panel_sections.cpp
// 作用：PreprocessPanel 的三个分区构建（buildResizeSection/buildGeoSection/buildColorSection，
//       TU 拆分）——画布尺寸独立区、几何操作区、色彩操作区。
//       本 TU 仅按区分拆编译，类不拆。
// ============================================================================
#include "panels/preprocess_panel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "model/document.h"
#include "util/coord_spin.h"
#include "util/gui_nums.h"
#include "util/widget_factory.h"
#include "i18n/gui_texts.h"
#include "i18n/gui_i18n.h"

namespace idc::gui {

// 画布尺寸（Resize）独立区：常驻、恒最先重放、不入队列。
void PreprocessPanel::buildResizeSection(QVBoxLayout* root) {
    auto* sizeBox = sizeBox_ = makeGroupBox(retranslator_, this, IDC_GT(label, kCanvasSizeBox));
    auto* sizeGrid = new QGridLayout(sizeBox);
    sizeGrid->setSpacing(nums::panels::kPanelSpacingPx);

    resizeW_ = makeCoordSpin(sizeBox, 1, nums::panels::kResizeMaxEdgePx);
    resizeH_ = makeCoordSpin(sizeBox, 1, nums::panels::kResizeMaxEdgePx);
    // 上限覆盖 8000×8000 大图并留余量（范围已由 makeCoordSpin 设定）。
    for (QSpinBox* s : {resizeW_, resizeH_})
        i18n::bindToolTip(retranslator_, s, IDC_GT(tooltip, kResizeEdgeSpin));
    // 目标宽/高另接受百分比串（SPEC §2.1）：基准＝当前工作图宽/高（与「当前尺寸」回显同源，
    // 未改尺寸时即原图尺寸），故 "100%" 恒等于框内当前值、"50%" 即缩到一半；无图则只认整数像素。
    const auto resizeBasis = [this](const bool horizontal) {
        if (!doc_ || !doc_->hasImage()) return 0;
        return horizontal ? doc_->width() : doc_->height();
    };
    resizeW_->setBasisProvider([resizeBasis] { return resizeBasis(true); });
    resizeH_->setBasisProvider([resizeBasis] { return resizeBasis(false); });
    resizeKeepRatio_ = makeCheckBox(retranslator_, sizeBox, IDC_GT(label, kKeepRatioCheck));
    resizeKeepRatio_->setChecked(true);
    auto* whLabel = new QLabel(sizeBox);
    i18n::bindText(retranslator_, whLabel, IDC_GT(label, kWidthHeightLabel));
    sizeGrid->addWidget(whLabel, 0, 0);
    sizeGrid->addWidget(resizeW_, 0, 1);
    sizeGrid->addWidget(resizeH_, 0, 2);
    sizeGrid->addWidget(resizeKeepRatio_, 0, 3);

    resizeScalePct_ = makeSpin(sizeBox, 1, nums::panels::kResizeScaleMaxPercent, QStringLiteral("%"));   // 1%..1000%。
    resizeScalePct_->setValue(100);
    i18n::bindToolTip(retranslator_, resizeScalePct_, IDC_GT(tooltip, kResizeScaleSpin));
    currentSizeLabel_ = new QLabel(sizeBox);
    i18n::bindText(retranslator_, currentSizeLabel_, IDC_GT(status, kCurrentSizeUnknown));   // 初值绑定；有图时 syncFromDocument 回灌
    auto* scaleLabel = new QLabel(sizeBox);
    i18n::bindText(retranslator_, scaleLabel, IDC_GT(label, kScaleFieldLabel));
    sizeGrid->addWidget(scaleLabel, 1, 0);
    sizeGrid->addWidget(resizeScalePct_, 1, 1);
    sizeGrid->addWidget(currentSizeLabel_, 1, 2, 1, 2);

    applySizeBtn_ = makeButton(retranslator_, sizeBox, IDC_GT(label, kApplySizeButton));
    i18n::bindToolTip(retranslator_, applySizeBtn_, IDC_GT(tooltip, kApplySizeBtn));
    scaleSizeBtn_ = makeButton(retranslator_, sizeBox, IDC_GT(label, kScaleSizeButton));
    i18n::bindToolTip(retranslator_, scaleSizeBtn_, IDC_GT(tooltip, kScaleSizeBtn));
    clearSizeBtn_ = makeButton(retranslator_, sizeBox, IDC_GT(label, kClearSizeButton));
    i18n::bindToolTip(retranslator_, clearSizeBtn_, IDC_GT(tooltip, kClearSizeBtn));
    auto* sizeBtnRow = new QHBoxLayout();
    sizeBtnRow->addWidget(applySizeBtn_);
    sizeBtnRow->addWidget(scaleSizeBtn_);
    sizeBtnRow->addWidget(clearSizeBtn_);
    sizeGrid->addLayout(sizeBtnRow, 2, 0, 1, 4);
    root->addWidget(sizeBox);

}

// 几何操作区（旋转 / 翻转）。
void PreprocessPanel::buildGeoSection(QVBoxLayout* root) {
    auto* geoBox = geoBox_ = makeGroupBox(retranslator_, this, IDC_GT(label, kGeoSectionBox));
    auto* geoGrid = new QGridLayout(geoBox);
    geoGrid->setSpacing(nums::panels::kPanelSpacingPx);

    geoTypeCombo_ = new QComboBox(geoBox);
    i18n::addItemsI18n(retranslator_, geoTypeCombo_, {IDC_GT(label, kGeoTypeRotate), IDC_GT(label, kGeoTypeFlip)});
    auto* geoTypeLabel = new QLabel(geoBox);
    i18n::bindText(retranslator_, geoTypeLabel, IDC_GT(label, kOpTypeFieldLabel));
    geoGrid->addWidget(geoTypeLabel, 0, 0);
    geoGrid->addWidget(geoTypeCombo_, 0, 1, 1, 3);

    rotAngle_ = new QComboBox(geoBox);
    rotAngle_->addItem(i18n::qs(IDC_GT(menu, kRotLeft90)), -90);
    rotAngle_->addItem(i18n::qs(IDC_GT(menu, kRotRight90)), 90);
    rotAngle_->addItem(i18n::qs(IDC_GT(menu, kRot180)), 180);
    i18n::bindItemTexts(retranslator_, rotAngle_, {IDC_GT(menu, kRotLeft90), IDC_GT(menu, kRotRight90), IDC_GT(menu, kRot180)});
    auto* angleLabel = new QLabel(geoBox);
    i18n::bindText(retranslator_, angleLabel, IDC_GT(label, kAngleFieldLabel));
    geoGrid->addWidget(angleLabel, 1, 0);
    geoGrid->addWidget(rotAngle_, 1, 1, 1, 3);

    flipDir_ = new QComboBox(geoBox);
    flipDir_->addItem(i18n::qs(IDC_GT(label, kFlipHorizontalPair)), 1);
    flipDir_->addItem(i18n::qs(IDC_GT(label, kFlipVerticalPair)), 0);
    i18n::bindItemTexts(retranslator_, flipDir_, {IDC_GT(label, kFlipHorizontalPair), IDC_GT(label, kFlipVerticalPair)});
    auto* dirLabel = new QLabel(geoBox);
    i18n::bindText(retranslator_, dirLabel, IDC_GT(label, kDirectionFieldLabel));
    geoGrid->addWidget(dirLabel, 2, 0);
    geoGrid->addWidget(flipDir_, 2, 1, 1, 3);

    auto* geoBtnRow = new QHBoxLayout();
    geoAddBtn_ = makeButton(retranslator_, geoBox, IDC_GT(label, kGeoAddButton));
    i18n::bindToolTip(retranslator_, geoAddBtn_, IDC_GT(tooltip, kGeoAddBtn));
    geoReplaceBtn_ = makeButton(retranslator_, geoBox, IDC_GT(label, kUpdateSelectedButton));
    i18n::bindToolTip(retranslator_, geoReplaceBtn_, IDC_GT(tooltip, kGeoReplaceBtn));
    geoBtnRow->addWidget(geoAddBtn_);
    geoBtnRow->addWidget(geoReplaceBtn_);
    geoGrid->addLayout(geoBtnRow, 3, 0, 1, 4);

    geoList_ = new QListWidget(geoBox);
    i18n::bindToolTip(retranslator_, geoList_, IDC_GT(tooltip, kGeoList));
    geoGrid->addWidget(geoList_, 4, 0, 1, 4);

    auto* geoEditRow = new QHBoxLayout();
    geoDelBtn_ = makeButton(retranslator_, geoBox, IDC_GT(label, kRemoveButton));
    geoUpBtn_ = makeButton(retranslator_, geoBox, IDC_GT(label, kMoveUpButton));
    geoDownBtn_ = makeButton(retranslator_, geoBox, IDC_GT(label, kMoveDownButton));
    geoEditRow->addWidget(geoDelBtn_);
    geoEditRow->addWidget(geoUpBtn_);
    geoEditRow->addWidget(geoDownBtn_);
    geoGrid->addLayout(geoEditRow, 5, 0, 1, 4);
    root->addWidget(geoBox);

}

// 色彩操作区（黑白 / 反色 / 分离）。
void PreprocessPanel::buildColorSection(QVBoxLayout* root) {
    auto* colorBox = makeGroupBox(retranslator_, this, IDC_GT(label, kColorSectionBox));
    auto* colorGrid = new QGridLayout(colorBox);
    colorGrid->setSpacing(nums::panels::kPanelSpacingPx);

    colorTypeCombo_ = new QComboBox(colorBox);
    i18n::addItemsI18n(retranslator_, colorTypeCombo_,
                       {IDC_GT(label, kColorTypeGray), IDC_GT(label, kColorTypeInvert), IDC_GT(label, kColorTypeSplit)});
    auto* colorTypeLabel = new QLabel(colorBox);
    i18n::bindText(retranslator_, colorTypeLabel, IDC_GT(label, kOpTypeFieldLabel));
    colorGrid->addWidget(colorTypeLabel, 0, 0);
    colorGrid->addWidget(colorTypeCombo_, 0, 1, 1, 3);

    // 「作用通道」整行容器（仅反色 / 分离显隐）。
    colorChanRow_ = new QWidget(colorBox);
    {
        auto* h = new QHBoxLayout(colorChanRow_);
        h->setContentsMargins(0, 0, 0, 0);
        auto* chanLabel = new QLabel(colorChanRow_);
        i18n::bindText(retranslator_, chanLabel, IDC_GT(label, kChannelFieldLabel));
        h->addWidget(chanLabel);
        chR_ = new QCheckBox(QStringLiteral("R"), colorChanRow_);
        chG_ = new QCheckBox(QStringLiteral("G"), colorChanRow_);
        chB_ = new QCheckBox(QStringLiteral("B"), colorChanRow_);
        for (QCheckBox* c : {chR_, chG_, chB_}) c->setChecked(true);
        h->addWidget(chR_);
        h->addWidget(chG_);
        h->addWidget(chB_);
        h->addStretch(1);
    }
    colorGrid->addWidget(colorChanRow_, 1, 0, 1, 4);

    auto* colorBtnRow = new QHBoxLayout();
    colorAddBtn_ = makeButton(retranslator_, colorBox, IDC_GT(label, kColorAddButton));
    i18n::bindToolTip(retranslator_, colorAddBtn_, IDC_GT(tooltip, kColorAddBtn));
    colorReplaceBtn_ = makeButton(retranslator_, colorBox, IDC_GT(label, kUpdateSelectedButton));
    i18n::bindToolTip(retranslator_, colorReplaceBtn_, IDC_GT(tooltip, kColorReplaceBtn));
    colorBtnRow->addWidget(colorAddBtn_);
    colorBtnRow->addWidget(colorReplaceBtn_);
    colorGrid->addLayout(colorBtnRow, 3, 0, 1, 4);

    colorList_ = new QListWidget(colorBox);
    i18n::bindToolTip(retranslator_, colorList_, IDC_GT(tooltip, kColorList));
    colorGrid->addWidget(colorList_, 4, 0, 1, 4);

    auto* colorEditRow = new QHBoxLayout();
    colorDelBtn_ = makeButton(retranslator_, colorBox, IDC_GT(label, kRemoveButton));
    colorUpBtn_ = makeButton(retranslator_, colorBox, IDC_GT(label, kMoveUpButton));
    colorDownBtn_ = makeButton(retranslator_, colorBox, IDC_GT(label, kMoveDownButton));
    colorEditRow->addWidget(colorDelBtn_);
    colorEditRow->addWidget(colorUpBtn_);
    colorEditRow->addWidget(colorDownBtn_);
    colorGrid->addLayout(colorEditRow, 5, 0, 1, 4);
    root->addWidget(colorBox);

}

} // namespace idc::gui
