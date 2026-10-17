// ============================================================================
// 文件：panels/layer_panel.cpp
// 作用：实现图层面板的构建与模型反向同步（见同名头文件说明）。
// 分块依据：一个「图层」分组内按画布 z 序排列各图层显示开关（底图 / 图片图层 / 标注 / 遮罩 /
//           网格线 / 切割线 / 选取边框 / 单元编号），一行一开关；复选框 toggled 直接转发为面板信号，
//           槽内无任何图层 / 合成逻辑（CONTRIBUTING.md「分层纪律」）。
// ============================================================================
#include "panels/layer_panel.h"

#include <QCheckBox>
#include <QGroupBox>
#include <QVBoxLayout>

#include "model/document.h"
#include "util/gui_nums.h"
#include "i18n/gui_texts.h"
#include "i18n/gui_i18n.h"

namespace idc::gui {

// 构建面板：底图 / 图片（单开关）/ 标注 / 遮罩 / 网格线 / 切割线 / 选取边框 / 单元编号各行显示开关。
LayerPanel::LayerPanel(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(nums::panels::kPanelMarginPx, nums::panels::kPanelMarginPx,
                             nums::panels::kPanelMarginPx, nums::panels::kPanelMarginPx);
    root->setSpacing(nums::panels::kPanelSpacingPx);

    auto* box = new QGroupBox(this);
    i18n::bindTitle(retranslator_, box, IDC_GT(label, kLayerPanelBox));
    auto* lay = new QVBoxLayout(box);
    lay->setSpacing(nums::panels::kPanelTightSpacingPx);

    // 一行一开关：建复选框（绑定文字+tooltip 入重放表、默认勾选）并归入本分组。
    const auto addToggle = [this, box, lay](const i18n::TextRef& text, const i18n::TextRef& tip) {
        auto* cb = new QCheckBox(box);
        i18n::bindButton(retranslator_, cb, text);
        cb->setChecked(true);
        i18n::bindToolTip(retranslator_, cb, tip);
        lay->addWidget(cb);
        return cb;
    };

    // 底图层：仅显示/隐藏（只切换画布 base 图元可见性）。
    baseVisible_ = addToggle(IDC_GT(label, kShowBaseCheck), IDC_GT(tooltip, kLayerShowBase));

    // 图片图层：所有图片合成的单一层，一个「显示」开关整体控制其显隐；行位于底图与标注之间（z 序：底图 < 图片图层 < 标注）。
    // 常驻显示：无论有无图层该行总在面板中且始终可用（不依 hasLayers 置灰；无图层时切换无副作用）。
    imageVisible_ = addToggle(IDC_GT(label, kShowImageCheck), IDC_GT(tooltip, kLayerShowImage));

    // 标注层：显示/隐藏（隐藏后标注不可拖动/选中，与选取边框一致）。
    annoVisible_ = addToggle(IDC_GT(label, kShowAnnoCheck), IDC_GT(tooltip, kLayerShowAnno));

    // 遮罩层：保留(绿)/删除(红)预览遮罩（正式图层项）。
    maskVisible_ = addToggle(IDC_GT(label, kShowMaskCheck), IDC_GT(tooltip, kLayerShowMask));

    // 网格线层：L3 / L2 多矩形的 Core 诱导网格灰色虚线。
    gridVisible_ = addToggle(IDC_GT(label, kShowGridCheck), IDC_GT(tooltip, kLayerShowGrid));

    // 切割线层：选区标记边向全图贯穿延伸的橙色切割线。
    cutLineVisible_ = addToggle(IDC_GT(label, kShowCutLineCheck), IDC_GT(tooltip, kLayerShowCutLine));

    // 选取边框层：橙色选区矩形描边/填充/手柄（含 L2 多矩形轮廓）与 L3 单元选择高亮。
    selectionVisible_ = addToggle(IDC_GT(label, kShowSelectionCheck), IDC_GT(tooltip, kLayerShowSelection));

    // 单元编号层：L3 Custom 序下每个已选单元左上角的序号角标（CellPickerItem 的角标子图层）。
    numberVisible_ = addToggle(IDC_GT(label, kShowNumberCheck), IDC_GT(tooltip, kLayerShowNumber));

    root->addWidget(box);

    root->addStretch(1);

    // 复选框状态直接转发为面板意图信号（AnnotationCoordinator / SessionController 分派到 Scene / Model）。
    connect(baseVisible_, &QCheckBox::toggled, this, &LayerPanel::baseVisibilityChanged);
    connect(imageVisible_, &QCheckBox::toggled, this, &LayerPanel::imageLayerVisibilityChanged);
    connect(annoVisible_, &QCheckBox::toggled, this, &LayerPanel::annotationVisibilityChanged);
    connect(maskVisible_, &QCheckBox::toggled, this, &LayerPanel::maskVisibilityChanged);
    connect(gridVisible_, &QCheckBox::toggled, this, &LayerPanel::gridVisibilityChanged);
    connect(cutLineVisible_, &QCheckBox::toggled, this, &LayerPanel::cutLineVisibilityChanged);
    connect(selectionVisible_, &QCheckBox::toggled, this, &LayerPanel::selectionVisibilityChanged);
    connect(numberVisible_, &QCheckBox::toggled, this, &LayerPanel::numberVisibilityChanged);
}

void LayerPanel::setModel(AnnotationBridge* model) {
    model_ = model;
    syncFromModel();
}

void LayerPanel::setDocument(Document* doc) {
    doc_ = doc;
    syncFromDocument();
}

// 从 Document 反向同步「图片图层」整体开关：仅同步勾选态（取整体可见态，blockSignals 防回环）；行常驻可用，不切换 enabled。
void LayerPanel::syncFromDocument() const {
    if (!imageVisible_) return;
    const bool visible = !doc_ || doc_->imageLayersVisible();
    imageVisible_->blockSignals(true);
    imageVisible_->setChecked(visible);
    imageVisible_->blockSignals(false);
}

// 从模型反向同步标注显示态（底图/遮罩/网格线/切割线/选取边框可见性非模型状态，保持控件当前值）。
void LayerPanel::syncFromModel() const {
    if (!model_) return;
    annoVisible_->blockSignals(true);
    annoVisible_->setChecked(model_->layerVisible());
    annoVisible_->blockSignals(false);
}

// 外部（视图菜单 / 右键）切换遮罩后反向同步复选框（blockSignals 防回环）。
void LayerPanel::setMaskVisible(const bool visible) const {
    if (!maskVisible_) return;
    maskVisible_->blockSignals(true);
    maskVisible_->setChecked(visible);
    maskVisible_->blockSignals(false);
}

// 视图菜单显隐项的外部反向同步（同 setMaskVisible：复选框为唯一真相源，仅翻转勾选态）。
void LayerPanel::setBaseVisible(const bool visible) const {
    if (!baseVisible_) return;
    baseVisible_->blockSignals(true);
    baseVisible_->setChecked(visible);
    baseVisible_->blockSignals(false);
}

void LayerPanel::setImageVisible(const bool visible) const {
    if (!imageVisible_) return;
    imageVisible_->blockSignals(true);
    imageVisible_->setChecked(visible);
    imageVisible_->blockSignals(false);
}

void LayerPanel::setGridVisible(const bool visible) const {
    if (!gridVisible_) return;
    gridVisible_->blockSignals(true);
    gridVisible_->setChecked(visible);
    gridVisible_->blockSignals(false);
}

void LayerPanel::setCutLineVisible(const bool visible) const {
    if (!cutLineVisible_) return;
    cutLineVisible_->blockSignals(true);
    cutLineVisible_->setChecked(visible);
    cutLineVisible_->blockSignals(false);
}

void LayerPanel::setSelectionVisible(const bool visible) const {
    if (!selectionVisible_) return;
    selectionVisible_->blockSignals(true);
    selectionVisible_->setChecked(visible);
    selectionVisible_->blockSignals(false);
}

void LayerPanel::setNumberVisible(const bool visible) const {
    if (!numberVisible_) return;
    numberVisible_->blockSignals(true);
    numberVisible_->setChecked(visible);
    numberVisible_->blockSignals(false);
}

void LayerPanel::setAnnotationVisible(const bool visible) const {
    if (!annoVisible_) return;
    annoVisible_->blockSignals(true);
    annoVisible_->setChecked(visible);
    annoVisible_->blockSignals(false);
}

} // namespace idc::gui
