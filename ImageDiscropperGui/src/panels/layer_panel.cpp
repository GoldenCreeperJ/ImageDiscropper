// ============================================================================
// 文件：src/panels/layer_panel.cpp
// 作用：实现图层面板的构建与模型反向同步（见同名头文件说明）。
// 分块依据：一个「图层」分组内按行排列各图层显示开关（底图 / 遮罩 / 网格线 / 切割线 /
//           选取边框 / 标注）；复选框 toggled 直接转发为面板信号，
//           槽内无任何图层 / 合成逻辑（A-0.1）。
// ============================================================================
#include "panels/layer_panel.h"

#include <QCheckBox>
#include <QGroupBox>
#include <QVBoxLayout>

namespace idc::gui {

// 构建面板：底图 / 遮罩 / 网格线 / 切割线 / 选取边框 / 标注各行显示开关。
LayerPanel::LayerPanel(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    auto* box = new QGroupBox(QStringLiteral("图层"), this);
    auto* lay = new QVBoxLayout(box);
    lay->setSpacing(4);

    // 底图层：仅显示/隐藏（本轮只切换画布 base 图元可见性）。
    baseVisible_ = new QCheckBox(QStringLiteral("底图  显示"), box);
    baseVisible_->setChecked(true);
    baseVisible_->setToolTip(QStringLiteral("显示 / 隐藏底图（仅影响画布预览，不影响导出）"));
    lay->addWidget(baseVisible_);

    // 遮罩层：保留(绿)/删除(红)预览遮罩（从工具栏/视图菜单迁入，成为正式图层项）。
    maskVisible_ = new QCheckBox(QStringLiteral("遮罩  显示"), box);
    maskVisible_->setChecked(true);
    maskVisible_->setToolTip(QStringLiteral("显示 / 隐藏保留(绿)/删除(红)预览遮罩（仅影响预览，不影响导出）"));
    lay->addWidget(maskVisible_);

    // 网格线层：L3 / L2 多矩形的 Core 诱导网格灰色虚线。
    gridVisible_ = new QCheckBox(QStringLiteral("网格线  显示"), box);
    gridVisible_->setChecked(true);
    gridVisible_->setToolTip(QStringLiteral("显示 / 隐藏网格分割(L3)与多矩形(L2)的诱导网格线"));
    lay->addWidget(gridVisible_);

    // 切割线层：选区标记边向全图贯穿延伸的橙色切割线。
    cutLineVisible_ = new QCheckBox(QStringLiteral("切割线  显示"), box);
    cutLineVisible_->setChecked(true);
    cutLineVisible_->setToolTip(QStringLiteral("显示 / 隐藏贯穿全图的橙色切割线（即选区标记边的延伸）"));
    lay->addWidget(cutLineVisible_);

    // 选取边框层：橙色选区矩形描边/填充/手柄（含 L2 多矩形轮廓）与 L3 单元选择高亮。
    selectionVisible_ = new QCheckBox(QStringLiteral("选取边框  显示"), box);
    selectionVisible_->setChecked(true);
    selectionVisible_->setToolTip(QStringLiteral("显示 / 隐藏橙色选区边框与四角手柄（含 L3 单元选择高亮）；隐藏后选区不可编辑，需重新勾选才能再拖动"));
    lay->addWidget(selectionVisible_);

    // 标注层：显示/隐藏（隐藏后标注不可拖动/选中，与选取边框一致）。
    annoVisible_ = new QCheckBox(QStringLiteral("标注  显示"), box);
    annoVisible_->setChecked(true);
    annoVisible_->setToolTip(QStringLiteral("显示 / 隐藏标注图层（矢量叠加，不改底图像素；隐藏后标注不可交互）"));
    lay->addWidget(annoVisible_);

    root->addWidget(box);
    root->addStretch(1);

    // 复选框状态直接转发为面板意图信号（MainWindow 分派到 Scene / Model）。
    connect(baseVisible_, &QCheckBox::toggled, this, &LayerPanel::baseVisibilityChanged);
    connect(maskVisible_, &QCheckBox::toggled, this, &LayerPanel::maskVisibilityChanged);
    connect(gridVisible_, &QCheckBox::toggled, this, &LayerPanel::gridVisibilityChanged);
    connect(cutLineVisible_, &QCheckBox::toggled, this, &LayerPanel::cutLineVisibilityChanged);
    connect(selectionVisible_, &QCheckBox::toggled, this, &LayerPanel::selectionVisibilityChanged);
    connect(annoVisible_, &QCheckBox::toggled, this, &LayerPanel::annotationVisibilityChanged);
}

void LayerPanel::setModel(AnnotationBridge* model) {
    model_ = model;
    syncFromModel();
}

// 从模型反向同步标注显示态（底图/遮罩/网格线/切割线/选取边框可见性非模型状态，保持控件当前值）。
void LayerPanel::syncFromModel() {
    if (!model_) return;
    annoVisible_->blockSignals(true);
    annoVisible_->setChecked(model_->layerVisible());
    annoVisible_->blockSignals(false);
}

// 外部（视图菜单 / 右键）切换遮罩后反向同步复选框（blockSignals 防回环）。
void LayerPanel::setMaskVisible(const bool visible) {
    if (!maskVisible_) return;
    maskVisible_->blockSignals(true);
    maskVisible_->setChecked(visible);
    maskVisible_->blockSignals(false);
}

} // namespace idc::gui
