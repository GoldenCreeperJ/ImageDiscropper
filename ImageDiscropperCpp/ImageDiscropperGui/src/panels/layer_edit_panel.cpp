// ============================================================================
// 文件：panels/layer_edit_panel.cpp
// 作用：实现「图层编辑」面板的构建与模型反向同步（见同名头文件说明）。
// 分块依据：两块分组——① 图层增删序按钮；② 落位变换区（锚点/枢轴/偏移/宽高/不透明度）。
//           控件回调只发意图信号，无任何图层 / 合成 / 像素逻辑（CONTRIBUTING.md「分层纪律」）。
// ============================================================================
#include "panels/layer_edit_panel.h"

#include <QComboBox>
#include <QFont>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
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

namespace {
// 锚点 / 枢轴 9 向下拉条目（顺序严格对齐 generate::ImageAnchor 枚举声明序，索引即枚举值）。
// 九向名称与菜单「图片→锚点/枢轴」子菜单同源（texts::label::kDir*）。
void fillAnchorCombo(i18n::Retranslator& r, QComboBox* combo) {
    i18n::addItemsI18n(r, combo, {IDC_GT(label, kDirTopLeft), IDC_GT(label, kDirTopCenter), IDC_GT(label, kDirTopRight),
                             IDC_GT(label, kDirMiddleLeft), IDC_GT(label, kDirCenter), IDC_GT(label, kDirMiddleRight),
                             IDC_GT(label, kDirBottomLeft), IDC_GT(label, kDirBottomCenter), IDC_GT(label, kDirBottomRight)});
}
} // namespace

LayerEditPanel::LayerEditPanel(QWidget* parent) : QWidget(parent) {
    auto* root = makePanelLayout(this);
    root->setSpacing(nums::panels::kPanelSpacingPx);

    // ---- ① 图层增删序区（作用于画布当前拾取层；无图层列表，选中由画布承担）----
    auto* listBox = makeGroupBox(retranslator_, this, IDC_GT(label, kImageLayerName));
    auto* listLay = new QVBoxLayout(listBox);
    listLay->setSpacing(nums::panels::kPanelTightSpacingPx);

    // 1-2-2-2 网格：「添加图片」独占首行（跨两列），上移/下移、移至顶层/底层、删除/清空图层各占一行。
    auto* gridBtns = new QGridLayout();
    gridBtns->setSpacing(nums::panels::kPanelTightSpacingPx);
    addImageBtn_ = makeButton(retranslator_, listBox, IDC_GT(label, kAddImageButton));
    i18n::bindToolTip(retranslator_, addImageBtn_, IDC_GT(tooltip, kLayerAddImage));
    moveUpBtn_ = makeButton(retranslator_, listBox, IDC_GT(label, kMoveUpButton));
    i18n::bindToolTip(retranslator_, moveUpBtn_, IDC_GT(tooltip, kLayerMoveUp));
    moveDownBtn_ = makeButton(retranslator_, listBox, IDC_GT(label, kMoveDownButton));
    i18n::bindToolTip(retranslator_, moveDownBtn_, IDC_GT(tooltip, kLayerMoveDown));
    moveTopBtn_ = makeButton(retranslator_, listBox, IDC_GT(label, kMoveTopButton));
    i18n::bindToolTip(retranslator_, moveTopBtn_, IDC_GT(tooltip, kLayerMoveTop));
    moveBottomBtn_ = makeButton(retranslator_, listBox, IDC_GT(label, kMoveBottomButton));
    i18n::bindToolTip(retranslator_, moveBottomBtn_, IDC_GT(tooltip, kLayerMoveBottom));
    removeBtn_ = makeButton(retranslator_, listBox, IDC_GT(label, kRemoveButton));
    i18n::bindToolTip(retranslator_, removeBtn_, IDC_GT(tooltip, kLayerEditRemove));
    clearLayersBtn_ = makeButton(retranslator_, listBox, IDC_GT(label, kClearLayersButton));
    i18n::bindToolTip(retranslator_, clearLayersBtn_, IDC_GT(tooltip, kClearAllImageLayers));
    gridBtns->addWidget(addImageBtn_, 0, 0, 1, 2);   // 首行跨两列
    gridBtns->addWidget(moveUpBtn_, 1, 0);
    gridBtns->addWidget(moveDownBtn_, 1, 1);
    gridBtns->addWidget(moveTopBtn_, 2, 0);
    gridBtns->addWidget(moveBottomBtn_, 2, 1);
    gridBtns->addWidget(removeBtn_, 3, 0);
    gridBtns->addWidget(clearLayersBtn_, 3, 1);
    listLay->addLayout(gridBtns);
    root->addWidget(listBox);
    auto* tfBox = makeGroupBox(retranslator_, this, IDC_GT(label, kLayerPlaceBox));
    auto* tfForm = new QFormLayout(tfBox);
    tfForm->setSpacing(nums::panels::kPanelTightSpacingPx);
    anchorCombo_ = new QComboBox(tfBox);
    fillAnchorCombo(retranslator_, anchorCombo_);
    i18n::bindToolTip(retranslator_, anchorCombo_, IDC_GT(tooltip, kLayerAnchorCombo));
    pivotCombo_ = new QComboBox(tfBox);
    fillAnchorCombo(retranslator_, pivotCombo_);
    i18n::bindToolTip(retranslator_, pivotCombo_, IDC_GT(tooltip, kLayerPivotCombo));
    posXSpin_ = makeCoordSpin(tfBox, -nums::model::kTransformCoordLimitPx, nums::model::kTransformCoordLimitPx);
    posXSpin_->setSuffix(QStringLiteral(" px"));
    i18n::bindToolTip(retranslator_, posXSpin_, IDC_GT(tooltip, kLayerOffsetXSpin));
    posYSpin_ = makeCoordSpin(tfBox, -nums::model::kTransformCoordLimitPx, nums::model::kTransformCoordLimitPx);
    posYSpin_->setSuffix(QStringLiteral(" px"));
    i18n::bindToolTip(retranslator_, posYSpin_, IDC_GT(tooltip, kLayerOffsetYSpin));
    widthSpin_ = makeCoordSpin(tfBox, 0, nums::model::kTransformCoordLimitPx);
    widthSpin_->setSuffix(QStringLiteral(" px"));
    i18n::bindSpecialValue(retranslator_, widthSpin_, IDC_GT(label, kNativeSpecialValue));
    i18n::bindToolTip(retranslator_, widthSpin_, IDC_GT(tooltip, kLayerWidthSpin));
    heightSpin_ = makeCoordSpin(tfBox, 0, nums::model::kTransformCoordLimitPx);
    heightSpin_->setSuffix(QStringLiteral(" px"));
    i18n::bindSpecialValue(retranslator_, heightSpin_, IDC_GT(label, kNativeSpecialValue));
    i18n::bindToolTip(retranslator_, heightSpin_, IDC_GT(tooltip, kLayerHeightSpin));
    opacitySpin_ = makeSpin(tfBox, 0, 255);
    opacitySpin_->setValue(255);
    i18n::bindToolTip(retranslator_, opacitySpin_, IDC_GT(tooltip, kLayerOpacitySpin));
    i18n::bindFormRow(retranslator_, tfForm, anchorCombo_, IDC_GT(label, kAnchorFieldLabel));
    i18n::bindFormRow(retranslator_, tfForm, pivotCombo_, IDC_GT(label, kPivotFieldLabel));
    i18n::bindFormRow(retranslator_, tfForm, [&] {
        auto* w = new QWidget(tfBox);
        auto* h = new QHBoxLayout(w);
        h->setContentsMargins(0, 0, 0, 0);
        h->addWidget(posXSpin_);
        h->addWidget(posYSpin_);
        return w;
    }(), IDC_GT(label, kOffsetFieldLabel));
    i18n::bindFormRow(retranslator_, tfForm, [&] {
        auto* w = new QWidget(tfBox);
        auto* h = new QHBoxLayout(w);
        h->setContentsMargins(0, 0, 0, 0);
        h->addWidget(widthSpin_);
        h->addWidget(heightSpin_);
        return w;
    }(), IDC_GT(label, kTargetSizeFieldLabel));
    i18n::bindFormRow(retranslator_, tfForm, opacitySpin_, IDC_GT(label, kOpacityFieldLabel));
    applyTransformBtn_ = makeButton(retranslator_, tfBox, IDC_GT(label, kApplyPlaceButton));
    tfForm->addRow(applyTransformBtn_);
    root->addWidget(tfBox);

    root->addStretch(1);

    // ---- 回调 → 意图信号 ----
    connect(moveUpBtn_, &QPushButton::clicked, this, &LayerEditPanel::onMoveUp);
    connect(moveDownBtn_, &QPushButton::clicked, this, &LayerEditPanel::onMoveDown);
    connect(moveTopBtn_, &QPushButton::clicked, this, &LayerEditPanel::onMoveToTop);
    connect(moveBottomBtn_, &QPushButton::clicked, this, &LayerEditPanel::onMoveToBottom);
    connect(removeBtn_, &QPushButton::clicked, this, &LayerEditPanel::onRemove);
    connect(addImageBtn_, &QPushButton::clicked, this, &LayerEditPanel::addImageRequested);
    connect(clearLayersBtn_, &QPushButton::clicked, this, &LayerEditPanel::clearAllLayersRequested);
    connect(applyTransformBtn_, &QPushButton::clicked, this, &LayerEditPanel::onApplyTransform);
    // 落位编辑即时预览：锚点 / 枢轴 / 偏移 / 目标尺寸任一改变即提交（syncing_ 已回环护栏）。
    connect(anchorCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, &LayerEditPanel::onApplyTransform);
    connect(pivotCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, &LayerEditPanel::onApplyTransform);
    connect(posXSpin_, qOverload<int>(&QSpinBox::valueChanged), this, &LayerEditPanel::onApplyTransform);
    connect(posYSpin_, qOverload<int>(&QSpinBox::valueChanged), this, &LayerEditPanel::onApplyTransform);
    connect(widthSpin_, qOverload<int>(&QSpinBox::valueChanged), this, &LayerEditPanel::onApplyTransform);
    connect(heightSpin_, qOverload<int>(&QSpinBox::valueChanged), this, &LayerEditPanel::onApplyTransform);
    connect(opacitySpin_, qOverload<int>(&QSpinBox::valueChanged), this, &LayerEditPanel::onApplyTransform);

    // 落位坐标另接受百分比串（SPEC §2.1）：基准＝画布宽/高（§7 layers[].transform 以画布为基准），
    // x/宽 用宽、y/高 用高；无画布（_canvasWidth/Height 为 0）时只认整数像素。“0%”在宽/高上即“原生”。
    const auto canvasBasis = [this](const bool horizontal) {
        if (!doc_) return 0;
        return horizontal ? doc_->canvasWidth() : doc_->canvasHeight();
    };
    posXSpin_->setBasisProvider([canvasBasis] { return canvasBasis(true); });
    widthSpin_->setBasisProvider([canvasBasis] { return canvasBasis(true); });
    posYSpin_->setBasisProvider([canvasBasis] { return canvasBasis(false); });
    heightSpin_->setBasisProvider([canvasBasis] { return canvasBasis(false); });
}

void LayerEditPanel::setDocument(Document* doc) {
    doc_ = doc;
    syncFromDocument();
}

// 从 Document 反向同步：回填选中层变换与画布尺寸、启停控件。
void LayerEditPanel::syncFromDocument() {
    if (!doc_) return;
    syncing_ = true;

    const bool hasCanvas = doc_->hasLayers();

    // 回填选中层变换。
    const auto sel = doc_->selectedLayerId();
    const canvas::ImageLayer* layer = sel.has_value() ? doc_->layerById(*sel) : nullptr;
    if (layer != nullptr) loadTransformIntoControls(layer->transform);
    // 无选中图片图层（图层画布）：不透明度控件按当前预处理目标回填——Base→底图整体透明度、Canvas→画布级透明度。
    else if (hasCanvas) opacitySpin_->setValue(doc_->preprocessTarget() == PreprocessTarget::Base
                                                   ? doc_->baseOpacity() : doc_->canvasOpacity());

    setControlsEnabled(hasCanvas, layer != nullptr);
    syncing_ = false;
}

void LayerEditPanel::loadTransformIntoControls(const canvas::ImageTransform& t) const {
    anchorCombo_->blockSignals(true);
    pivotCombo_->blockSignals(true);
    anchorCombo_->setCurrentIndex(static_cast<int>(t.inCanvasAnchor));
    pivotCombo_->setCurrentIndex(static_cast<int>(t.layerPivot));
    anchorCombo_->blockSignals(false);
    pivotCombo_->blockSignals(false);
    posXSpin_->setValue(t.x);
    posYSpin_->setValue(t.y);
    widthSpin_->setValue(t.width);
    heightSpin_->setValue(t.height);
    opacitySpin_->setValue(t.opacity);
}

void LayerEditPanel::setControlsEnabled(const bool hasLayerCanvas, const bool hasSelection) const {
    // 「添加图片」入口：只要有图即可用（有图即图层画布；空文档经 ensureLayerCanvas 兜底起画布）。
    addImageBtn_->setEnabled(doc_ != nullptr && doc_->hasImage());
    clearLayersBtn_->setEnabled(hasLayerCanvas);   // 仅图层画布态可清空全部图层。
    moveUpBtn_->setEnabled(hasSelection);
    moveDownBtn_->setEnabled(hasSelection);
    moveTopBtn_->setEnabled(hasSelection);
    moveBottomBtn_->setEnabled(hasSelection);
    removeBtn_->setEnabled(hasSelection);
    for (QWidget* w : {static_cast<QWidget*>(anchorCombo_), static_cast<QWidget*>(pivotCombo_),
                       static_cast<QWidget*>(posXSpin_), static_cast<QWidget*>(posYSpin_),
                       static_cast<QWidget*>(widthSpin_), static_cast<QWidget*>(heightSpin_),
                       static_cast<QWidget*>(applyTransformBtn_)})
        w->setEnabled(hasSelection);
    // 不透明度控件特殊：按当前预处理目标三选一——选中层→该层 transform.opacity；Base→底图整体透明度；
    //   Canvas→画布级整体透明度。故只要有图层画布即可用（其余落位控件仍需选中层）。
    opacitySpin_->setEnabled(hasLayerCanvas);
}

// 上/下移/删除的作用对象取画布当前拾取层（selectedLayerId）；未拾取时无操作。
void LayerEditPanel::onMoveUp() {
    if (syncing_) return;
    if (const auto id = doc_ ? doc_->selectedLayerId() : std::nullopt; id) emit layerMoveRequested(*id, true);
}

void LayerEditPanel::onMoveDown() {
    if (syncing_) return;
    if (const auto id = doc_ ? doc_->selectedLayerId() : std::nullopt; id) emit layerMoveRequested(*id, false);
}

void LayerEditPanel::onMoveToTop() {
    if (syncing_) return;
    if (const auto id = doc_ ? doc_->selectedLayerId() : std::nullopt; id) emit layerMoveEdgeRequested(*id, true);
}

void LayerEditPanel::onMoveToBottom() {
    if (syncing_) return;
    if (const auto id = doc_ ? doc_->selectedLayerId() : std::nullopt; id) emit layerMoveEdgeRequested(*id, false);
}

void LayerEditPanel::onRemove() {
    if (syncing_) return;
    if (const auto id = doc_ ? doc_->selectedLayerId() : std::nullopt; id) emit layerRemoveRequested(*id);
}

void LayerEditPanel::onApplyTransform() {
    if (syncing_) return;
    const auto id = doc_ ? doc_->selectedLayerId() : std::nullopt;
    if (!id.has_value()) {
        // 无选中图片图层：按当前预处理目标三选一分派不透明度（Base→底图整体、Canvas→画布级）。
        if (!doc_ || !doc_->hasLayers()) return;
        if (doc_->preprocessTarget() == PreprocessTarget::Base)
            emit baseOpacityRequested(opacitySpin_->value());
        else
            emit canvasOpacityRequested(opacitySpin_->value());
        return;
    }
    canvas::ImageTransform t;
    t.inCanvasAnchor = static_cast<generate::ImageAnchor>(anchorCombo_->currentIndex());
    t.layerPivot = static_cast<generate::ImageAnchor>(pivotCombo_->currentIndex());
    t.x = posXSpin_->value();
    t.y = posYSpin_->value();
    t.width = widthSpin_->value();
    t.height = heightSpin_->value();
    t.opacity = opacitySpin_->value();
    emit layerTransformRequested(*id, t);
}

} // namespace idc::gui
