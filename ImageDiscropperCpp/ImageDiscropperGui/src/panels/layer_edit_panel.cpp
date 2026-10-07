// ============================================================================
// 文件：src/panels/layer_edit_panel.cpp
// 作用：实现「图层编辑」面板的构建与模型反向同步（见同名头文件说明）。
// 分块依据：三块分组——① 图层列表 + 增删序按钮；② 落位变换区（锚点/枢轴/偏移/宽高）；
//           ③ 画布尺寸区。控件回调只发意图信号，无任何图层 / 合成 / 像素逻辑
//          （CONTRIBUTING.md「分层纪律」）。
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

namespace idc::gui {

namespace {
// 锚点 / 枢轴 9 向下拉条目（顺序严格对齐 generate::ImageAnchor 枚举声明序，索引即枚举值）。
void fillAnchorCombo(QComboBox* combo) {
    combo->addItem(QStringLiteral("左上"));
    combo->addItem(QStringLiteral("上中"));
    combo->addItem(QStringLiteral("右上"));
    combo->addItem(QStringLiteral("左中"));
    combo->addItem(QStringLiteral("居中"));
    combo->addItem(QStringLiteral("右中"));
    combo->addItem(QStringLiteral("左下"));
    combo->addItem(QStringLiteral("下中"));
    combo->addItem(QStringLiteral("右下"));
}
} // namespace

LayerEditPanel::LayerEditPanel(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    // ---- ① 图层增删序区（作用于画布当前拾取层；无图层列表，选中由画布承担）----
    auto* listBox = new QGroupBox(QStringLiteral("图片图层"), this);
    auto* listLay = new QVBoxLayout(listBox);
    listLay->setSpacing(4);

    // 1-2-2-2 网格：「添加图片」独占首行（跨两列），上移/下移、移至顶层/底层、删除/清空图层各占一行。
    auto* gridBtns = new QGridLayout();
    gridBtns->setSpacing(4);
    addImageBtn_ = new QPushButton(QStringLiteral("添加图片…"), listBox);
    addImageBtn_->setToolTip(QStringLiteral("添加图片图层：复用「新建画布」采参对话框——程序生成的图案作为可复现的图案图层，导入的图片作为位图层；往现有画布追加，超出部分合成时裁剪、不撑大。"));
    moveUpBtn_ = new QPushButton(QStringLiteral("上移"), listBox);
    moveUpBtn_->setToolTip(QStringLiteral("把选中图层向顶层移动一层（更靠近前面）。"));
    moveDownBtn_ = new QPushButton(QStringLiteral("下移"), listBox);
    moveDownBtn_->setToolTip(QStringLiteral("把选中图层向底层移动一层（更靠近后面）。"));
    moveTopBtn_ = new QPushButton(QStringLiteral("移至顶层"), listBox);
    moveTopBtn_->setToolTip(QStringLiteral("把选中图层移到最顶层（盖住其余所有图片图层）。"));
    moveBottomBtn_ = new QPushButton(QStringLiteral("移至底层"), listBox);
    moveBottomBtn_->setToolTip(QStringLiteral("把选中图层移到最底层（在所有图片图层之下、底图之上）。"));
    removeBtn_ = new QPushButton(QStringLiteral("删除"), listBox);
    removeBtn_->setToolTip(QStringLiteral("删除选中图层。"));
    clearLayersBtn_ = new QPushButton(QStringLiteral("清空图层"), listBox);
    clearLayersBtn_->setToolTip(QStringLiteral("清空全部图片图层，回退为单图底图态（保留底图、标注与预处理）。"));
    gridBtns->addWidget(addImageBtn_, 0, 0, 1, 2);   // 首行跨两列
    gridBtns->addWidget(moveUpBtn_, 1, 0);
    gridBtns->addWidget(moveDownBtn_, 1, 1);
    gridBtns->addWidget(moveTopBtn_, 2, 0);
    gridBtns->addWidget(moveBottomBtn_, 2, 1);
    gridBtns->addWidget(removeBtn_, 3, 0);
    gridBtns->addWidget(clearLayersBtn_, 3, 1);
    listLay->addLayout(gridBtns);
    root->addWidget(listBox);
    auto* tfBox = new QGroupBox(QStringLiteral("图层落位"), this);
    auto* tfForm = new QFormLayout(tfBox);
    tfForm->setSpacing(4);
    anchorCombo_ = new QComboBox(tfBox);
    fillAnchorCombo(anchorCombo_);
    anchorCombo_->setToolTip(QStringLiteral("画布锚点：图层对齐到画布的哪个位置。"));
    pivotCombo_ = new QComboBox(tfBox);
    fillAnchorCombo(pivotCombo_);
    pivotCombo_->setToolTip(QStringLiteral("图片枢轴：图层自身以哪个部位对齐到锚点。"));
    posXSpin_ = new QSpinBox(tfBox);
    posXSpin_->setRange(-1000000, 1000000);
    posXSpin_->setSuffix(QStringLiteral(" px"));
    posXSpin_->setToolTip(QStringLiteral("自由偏移 X（叠加在锚点/枢轴结果之上）。"));
    posYSpin_ = new QSpinBox(tfBox);
    posYSpin_->setRange(-1000000, 1000000);
    posYSpin_->setSuffix(QStringLiteral(" px"));
    posYSpin_->setToolTip(QStringLiteral("自由偏移 Y。"));
    widthSpin_ = new QSpinBox(tfBox);
    widthSpin_->setRange(0, 1000000);
    widthSpin_->setSpecialValueText(QStringLiteral("原生"));
    widthSpin_->setSuffix(QStringLiteral(" px"));
    widthSpin_->setToolTip(QStringLiteral("目标宽（0＝用原生尺寸不缩放）。"));
    heightSpin_ = new QSpinBox(tfBox);
    heightSpin_->setRange(0, 1000000);
    heightSpin_->setSpecialValueText(QStringLiteral("原生"));
    heightSpin_->setSuffix(QStringLiteral(" px"));
    heightSpin_->setToolTip(QStringLiteral("目标高（0＝用原生尺寸不缩放）。"));
    opacitySpin_ = new QSpinBox(tfBox);
    opacitySpin_->setRange(0, 255);
    opacitySpin_->setValue(255);
    opacitySpin_->setToolTip(QStringLiteral("不透明度（0–255，255＝完全不透明）：选中图片图层时整体降低该图层透明度；"
                                              "未选图层（目标＝画布/底图）时整体淡出画布底图（影响预览与导出，随配置往返）。"));
    tfForm->addRow(QStringLiteral("画布锚点"), anchorCombo_);
    tfForm->addRow(QStringLiteral("图片枢轴"), pivotCombo_);
    tfForm->addRow(QStringLiteral("偏移 X / Y"), [&] {
        auto* w = new QWidget(tfBox);
        auto* h = new QHBoxLayout(w);
        h->setContentsMargins(0, 0, 0, 0);
        h->addWidget(posXSpin_);
        h->addWidget(posYSpin_);
        return w;
    }());
    tfForm->addRow(QStringLiteral("目标宽 / 高"), [&] {
        auto* w = new QWidget(tfBox);
        auto* h = new QHBoxLayout(w);
        h->setContentsMargins(0, 0, 0, 0);
        h->addWidget(widthSpin_);
        h->addWidget(heightSpin_);
        return w;
    }());
    tfForm->addRow(QStringLiteral("不透明度"), opacitySpin_);
    applyTransformBtn_ = new QPushButton(QStringLiteral("应用落位"), tfBox);
    tfForm->addRow(applyTransformBtn_);
    root->addWidget(tfBox);

    // ---- ③ 画布尺寸区 ----
    auto* sizeBox = new QGroupBox(QStringLiteral("画布尺寸"), this);
    auto* sizeLay = new QHBoxLayout(sizeBox);
    canvasWSpin_ = new QSpinBox(sizeBox);
    canvasWSpin_->setRange(1, 65535);
    canvasWSpin_->setSuffix(QStringLiteral(" ×"));
    canvasHSpin_ = new QSpinBox(sizeBox);
    canvasHSpin_->setRange(1, 65535);
    canvasHSpin_->setSuffix(QStringLiteral(" px"));
    applyCanvasBtn_ = new QPushButton(QStringLiteral("应用"), sizeBox);
    applyCanvasBtn_->setToolTip(QStringLiteral("改画布尺寸：仅清空框选/网格选区，保留图层与标注。"));
    sizeLay->addWidget(canvasWSpin_);
    sizeLay->addWidget(canvasHSpin_);
    sizeLay->addWidget(applyCanvasBtn_);
    sizeLay->addStretch(1);
    root->addWidget(sizeBox);

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
    connect(applyCanvasBtn_, &QPushButton::clicked, this, &LayerEditPanel::onApplyCanvasSize);
    // 落位编辑即时预览：锚点 / 枢轴 / 偏移 / 目标尺寸任一改变即提交（syncing_ 已回环护栏）。
    connect(anchorCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, &LayerEditPanel::onApplyTransform);
    connect(pivotCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, &LayerEditPanel::onApplyTransform);
    connect(posXSpin_, qOverload<int>(&QSpinBox::valueChanged), this, &LayerEditPanel::onApplyTransform);
    connect(posYSpin_, qOverload<int>(&QSpinBox::valueChanged), this, &LayerEditPanel::onApplyTransform);
    connect(widthSpin_, qOverload<int>(&QSpinBox::valueChanged), this, &LayerEditPanel::onApplyTransform);
    connect(heightSpin_, qOverload<int>(&QSpinBox::valueChanged), this, &LayerEditPanel::onApplyTransform);
    connect(opacitySpin_, qOverload<int>(&QSpinBox::valueChanged), this, &LayerEditPanel::onApplyTransform);
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

    // 回填选中层变换 + 画布尺寸。
    const auto sel = doc_->selectedLayerId();
    const canvas::ImageLayer* layer = sel.has_value() ? doc_->layerById(*sel) : nullptr;
    if (layer != nullptr) loadTransformIntoControls(layer->transform);
    // 无选中图片图层（图层画布）：不透明度控件按当前预处理目标回填——Base→底图整体透明度、Canvas→画布级透明度。
    else if (hasCanvas) opacitySpin_->setValue(doc_->preprocessTarget() == PreprocessTarget::Base
                                                   ? doc_->baseOpacity() : doc_->canvasOpacity());
    if (hasCanvas) {
        canvasWSpin_->setValue(doc_->canvasWidth());
        canvasHSpin_->setValue(doc_->canvasHeight());
    }

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
    // 「添加图片」入口：只要有图即可用（单图态下经 ensureLayerCanvas 自动起图层画布），不再要求已是图层画布。
    addImageBtn_->setEnabled(doc_ != nullptr && doc_->hasImage());
    clearLayersBtn_->setEnabled(hasLayerCanvas);   // 仅图层画布态可清空全部图层。
    canvasWSpin_->setEnabled(hasLayerCanvas);
    canvasHSpin_->setEnabled(hasLayerCanvas);
    applyCanvasBtn_->setEnabled(hasLayerCanvas);
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

void LayerEditPanel::onApplyCanvasSize() {
    if (syncing_) return;
    emit canvasSizeRequested(canvasWSpin_->value(), canvasHSpin_->value());
}

} // namespace idc::gui
