// ============================================================================
// 文件：panels/preprocess_panel.cpp
// 作用：实现图像处理面板——顶部独立「画布尺寸（Resize）」区 + 「几何操作」区/列表 + 「色彩操作」区/列表。
// 分块依据：构造函数只做「排版 + 串联信号」；两区各自的操作类型下拉 + 参数控件按类型启停；
//           尺寸区控件在有图时常开；槽函数只把控件值翻译成轻操作 PreprocessConfig 或 resize 意图发信号，绝不触碰像素运算。
//           信号携带扁平队列下标（色彩项 = 几何项数 + 色彩行）；「几何块在前、色彩块在后」的不变式由控制器保证。
// 视觉规范（本目录 README「画布视觉规范」）：面板内边距 8px、控件间距 6px、分组间距 12px。
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

#include <algorithm>
#include <cmath>
#include <string>
#include <variant>
#include <vector>

#include <QStandardItem>
#include <QStandardItemModel>

#include "model/document.h"
#include "util/gui_nums.h"
#include "i18n/gui_texts.h"
#include "i18n/gui_i18n.h"

namespace idc::gui {

namespace {
// 几何类型下拉索引（与 buildGeoConfig / loadGeoIntoControls 一一对应）。
enum GeoType { kRotate = 0, kFlip, kGeoTypeCount };
// 色彩类型下拉索引（与 buildColorConfig / loadColorIntoControls 一一对应）。
enum ColorType { kGray = 0, kInvert, kSplit, kColorTypeCount };

// 轻操作分类：几何步判定统一委托 Core preprocess::isGeometryStep（与控制器 / 折叠同源），保证扁平下标映射口径一致。
bool isGeo(const preprocess::PreprocessConfig& cfg) {
    return preprocess::isGeometryStep(cfg);
}

// 把长度 3 的掩码（'1'/'0'）回灌到 R/G/B 勾选。
void applyMaskTo(const std::string& mask, QCheckBox* r, QCheckBox* g, QCheckBox* b) {
    r->setChecked(!mask.empty() && mask[0] == '1');
    g->setChecked(mask.size() > 1 && mask[1] == '1');
    b->setChecked(mask.size() > 2 && mask[2] == '1');
}
} // namespace

// 构造：上「画布尺寸（Resize）」+ 中「几何操作」区/列表 + 「色彩操作」区/列表 + 下「重置」。
PreprocessPanel::PreprocessPanel(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(nums::panels::kPanelMarginPx, nums::panels::kPanelMarginPx,
                             nums::panels::kPanelMarginPx, nums::panels::kPanelMarginPx);   // 面板内边距。
    root->setSpacing(nums::panels::kPanelGroupSpacingPx);                                    // 分组间距。

    // ---- 顶部当前预处理目标标注（未选图层＝画布；选中图片图层＝该层）----
    targetLabel_ = new QLabel(this);
    i18n::bindText(retranslator_, targetLabel_, IDC_GT(status, kTargetLabelInit));   // 初值绑定；有图时 syncFromDocument 回灌三元目标
    i18n::bindToolTip(retranslator_, targetLabel_, IDC_GT(tooltip, kPreprocessTarget));
    root->addWidget(targetLabel_);

    // 分区构建（画布尺寸 / 几何操作 / 色彩操作；实现见 preprocess_panel_sections.cpp）。
    buildResizeSection(root);
    buildGeoSection(root);
    buildColorSection(root);

    // ---- 重置 ----
    resetBtn_ = new QPushButton(this);
    i18n::bindButton(retranslator_, resetBtn_, IDC_GT(label, kResetPreprocessButton));
    i18n::bindToolTip(retranslator_, resetBtn_, IDC_GT(tooltip, kResetPreprocessBtn));
    root->addWidget(resetBtn_);

    // ---- 串联信号 ----
    connect(geoTypeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PreprocessPanel::onGeoTypeChanged);
    connect(geoAddBtn_, &QPushButton::clicked, this, &PreprocessPanel::onGeoAdd);
    connect(geoReplaceBtn_, &QPushButton::clicked, this, &PreprocessPanel::onGeoReplace);
    connect(geoDelBtn_, &QPushButton::clicked, this, &PreprocessPanel::onGeoDelete);
    connect(geoUpBtn_, &QPushButton::clicked, this, &PreprocessPanel::onGeoMoveUp);
    connect(geoDownBtn_, &QPushButton::clicked, this, &PreprocessPanel::onGeoMoveDown);
    connect(geoList_, &QListWidget::currentRowChanged, this, [this](int) { onGeoSelectionChanged(); });

    connect(colorTypeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PreprocessPanel::onColorTypeChanged);
    connect(colorAddBtn_, &QPushButton::clicked, this, &PreprocessPanel::onColorAdd);
    connect(colorReplaceBtn_, &QPushButton::clicked, this, &PreprocessPanel::onColorReplace);
    connect(colorDelBtn_, &QPushButton::clicked, this, &PreprocessPanel::onColorDelete);
    connect(colorUpBtn_, &QPushButton::clicked, this, &PreprocessPanel::onColorMoveUp);
    connect(colorDownBtn_, &QPushButton::clicked, this, &PreprocessPanel::onColorMoveDown);
    connect(colorList_, &QListWidget::currentRowChanged, this, [this](int) { onColorSelectionChanged(); });

    connect(resetBtn_, &QPushButton::clicked, this, &PreprocessPanel::resetRequested);
    connect(applySizeBtn_, &QPushButton::clicked, this, &PreprocessPanel::onApplyResize);
    connect(scaleSizeBtn_, &QPushButton::clicked, this, &PreprocessPanel::onScaleResizeClicked);
    connect(clearSizeBtn_, &QPushButton::clicked, this, &PreprocessPanel::onClearResizeClicked);
    connect(resizeW_, QOverload<int>::of(&QSpinBox::valueChanged), this, &PreprocessPanel::onResizeWidthEdited);

    onGeoTypeChanged(geoTypeCombo_->currentIndex());
    onColorTypeChanged(colorTypeCombo_->currentIndex());
}

// ---- 几何区：类型启停（旋转→角度，翻转→方向）----
void PreprocessPanel::onGeoTypeChanged(const int index) const {
    const bool has = doc_ && doc_->hasImage();
    rotAngle_->setEnabled(has && index == kRotate);
    flipDir_->setEnabled(has && index == kFlip);
    geoAddBtn_->setEnabled(has);
    geoReplaceBtn_->setEnabled(has && geoList_->currentRow() >= 0);
}

// ---- 色彩区：类型启停（反色 / 分离→通道；黑白→通道隐藏），叠加灰度产出智能禁用 ----
void PreprocessPanel::onColorTypeChanged(const int index) const {
    const bool has = doc_ && doc_->hasImage();
    const bool chan = index == kInvert || index == kSplit;
    if (colorChanRow_) colorChanRow_->setVisible(chan);
    chR_->setEnabled(has && chan);
    chG_->setEnabled(has && chan);
    chB_->setEnabled(has && chan);
    // 队列已产出灰度时，「黑白」(冗余) 与「色道分离」(对灰度无操作) 不可再入队 / 替换。
    const bool blocked = has && doc_->activeSession().producesGray() && (index == kGray || index == kSplit);
    colorAddBtn_->setEnabled(has && !blocked);
    colorAddBtn_->setToolTip(blocked ? IDC_GTR(tooltip, kColorAddBlocked)
                                     : IDC_GTR(tooltip, kColorAddBtn));
    colorReplaceBtn_->setEnabled(has && colorList_->currentRow() >= 0 && !blocked);
}

// 依几何类型 + 参数构造 PreprocessConfig（RotateOp / FlipOp）。
preprocess::PreprocessConfig PreprocessPanel::buildGeoConfig() const {
    switch (geoTypeCombo_->currentIndex()) {
        case kRotate: return preprocess::RotateOp{rotAngle_->currentData().toInt()};
        case kFlip:   return preprocess::FlipOp{flipDir_->currentData().toInt() != 0};
        default:      return preprocess::RotateOp{90};
    }
}

// 依色彩类型 + 参数构造 PreprocessConfig（GrayOp / InvertOp / SplitOp）。
preprocess::PreprocessConfig PreprocessPanel::buildColorConfig() const {
    switch (colorTypeCombo_->currentIndex()) {
        case kGray:   return preprocess::GrayOp{};
        case kInvert: {
            const std::string mask = std::string(chR_->isChecked() ? "1" : "0") +
                                     (chG_->isChecked() ? "1" : "0") + (chB_->isChecked() ? "1" : "0");
            return preprocess::InvertOp{mask};
        }
        case kSplit: {
            const std::string mask = std::string(chR_->isChecked() ? "1" : "0") +
                                     (chG_->isChecked() ? "1" : "0") + (chB_->isChecked() ? "1" : "0");
            return preprocess::SplitOp{mask};
        }
        default:      return preprocess::GrayOp{};
    }
}

// ---- 几何区动作 ----
void PreprocessPanel::onGeoAdd() { emit stepAppendRequested(buildGeoConfig()); }

void PreprocessPanel::onGeoReplace() {
    const int idx = geoFlatIndex();
    if (idx < 0) return;
    emit stepReplaceRequested(idx, buildGeoConfig());
}

void PreprocessPanel::onGeoDelete() {
    const int idx = geoFlatIndex();
    if (idx < 0) return;
    emit stepRemoveRequested(idx);
}

void PreprocessPanel::onGeoMoveUp() {
    const int row = geoList_->currentRow();
    if (row <= 0) return;
    geoPendingRow_ = row - 1;
    emit stepMoveRequested(geoFlatIndex(), false);
}

void PreprocessPanel::onGeoMoveDown() {
    const int row = geoList_->currentRow();
    if (row < 0 || row + 1 >= geoList_->count()) return;
    geoPendingRow_ = row + 1;
    emit stepMoveRequested(geoFlatIndex(), true);
}

// ---- 色彩区动作 ----
void PreprocessPanel::onColorAdd() { emit stepAppendRequested(buildColorConfig()); }

void PreprocessPanel::onColorReplace() {
    const int idx = colorFlatIndex();
    if (idx < 0) return;
    emit stepReplaceRequested(idx, buildColorConfig());
}

void PreprocessPanel::onColorDelete() {
    const int idx = colorFlatIndex();
    if (idx < 0) return;
    emit stepRemoveRequested(idx);
}

void PreprocessPanel::onColorMoveUp() {
    const int row = colorList_->currentRow();
    if (row <= 0) return;   // 首色上移会跨入几何块（破坏 geo→color 不变式），故禁止
    colorPendingRow_ = row - 1;
    emit stepMoveRequested(colorFlatIndex(), false);
}

void PreprocessPanel::onColorMoveDown() {
    const int row = colorList_->currentRow();
    if (row < 0 || row + 1 >= colorList_->count()) return;
    colorPendingRow_ = row + 1;
    emit stepMoveRequested(colorFlatIndex(), true);
}

// 「画布尺寸」区动作：把控件值翻译成 resize 意图发信号（真正的重放物化在控制器）。
void PreprocessPanel::onApplyResize() { emit resizeAppliedRequested(resizeW_->value(), resizeH_->value()); }

void PreprocessPanel::onScaleResizeClicked() {
    const double factor = static_cast<double>(resizeScalePct_->value()) / 100.0;
    emit resizeScaleRequested(factor);
}

void PreprocessPanel::onClearResizeClicked() { emit resizeClearedRequested(); }

// ---- 扁平下标映射 ----
int PreprocessPanel::geoRowCount() const {
    if (!doc_) return 0;
    int n = 0;
    for (const auto& cfg : doc_->activeSession().steps().items()) {
        if (!isGeo(cfg)) break;   // 几何块在前，遇首个色彩即止
        ++n;
    }
    return n;
}

int PreprocessPanel::geoFlatIndex() const {
    const int row = geoList_ ? geoList_->currentRow() : -1;
    return row;   // 几何行 = 扁平下标（几何块恒在前）
}

int PreprocessPanel::colorFlatIndex() const {
    const int row = colorList_ ? colorList_->currentRow() : -1;
    if (row < 0) return -1;
    return geoRowCount() + row;   // 色彩行需加几何项数偏移
}

// 几何列表选中：把该步参数回灌几何配置区，刷新按钮启停。
void PreprocessPanel::onGeoSelectionChanged() {
    const int row = geoList_->currentRow();
    if (row >= 0 && doc_) {
        if (const auto& steps = doc_->activeSession().steps().items(); static_cast<std::size_t>(row) < steps.size()) loadGeoIntoControls(steps[static_cast<std::size_t>(row)]);
    }
    const bool has = doc_ && doc_->hasImage();
    geoReplaceBtn_->setEnabled(has && row >= 0);
    geoDelBtn_->setEnabled(has && row >= 0);
    geoUpBtn_->setEnabled(has && row > 0);
    geoDownBtn_->setEnabled(has && row >= 0 && row + 1 < geoList_->count());
    onGeoTypeChanged(geoTypeCombo_->currentIndex());
}

// 色彩列表选中：把该步参数回灌色彩配置区，刷新按钮启停。
void PreprocessPanel::onColorSelectionChanged() {
    const int row = colorList_->currentRow();
    if (row >= 0 && doc_) {
        const auto& steps = doc_->activeSession().steps().items();
        if (const auto flat = static_cast<std::size_t>(geoRowCount() + row); flat < steps.size()) loadColorIntoControls(steps[flat]);
    }
    const bool has = doc_ && doc_->hasImage();
    colorReplaceBtn_->setEnabled(has && row >= 0);
    colorDelBtn_->setEnabled(has && row >= 0);
    colorUpBtn_->setEnabled(has && row > 0);
    colorDownBtn_->setEnabled(has && row >= 0 && row + 1 < colorList_->count());
    onColorTypeChanged(colorTypeCombo_->currentIndex());
}

// 把某几何步参数回灌几何控件（类型下拉 + 角度 / 方向）。
void PreprocessPanel::loadGeoIntoControls(const preprocess::PreprocessConfig& cfg) {
    std::visit([this]<typename T0>(const T0& op) {
        using T = std::decay_t<T0>;
        if constexpr (std::is_same_v<T, preprocess::RotateOp>) {
            geoTypeCombo_->setCurrentIndex(kRotate);
            for (int i = 0; i < rotAngle_->count(); ++i)
                if (rotAngle_->itemData(i).toInt() == op.angle) { rotAngle_->setCurrentIndex(i); break; }
        } else if constexpr (std::is_same_v<T, preprocess::FlipOp>) {
            geoTypeCombo_->setCurrentIndex(kFlip);
            flipDir_->setCurrentIndex(op.horizontal ? 0 : 1);
        }
    }, cfg);
}

// 把某色彩步参数回灌色彩控件（类型下拉 + 通道勾选）。
void PreprocessPanel::loadColorIntoControls(const preprocess::PreprocessConfig& cfg) {
    std::visit([this]<typename T0>(const T0& op) {
        using T = std::decay_t<T0>;
        if constexpr (std::is_same_v<T, preprocess::GrayOp>) {
            colorTypeCombo_->setCurrentIndex(kGray);
        } else if constexpr (std::is_same_v<T, preprocess::InvertOp>) {
            colorTypeCombo_->setCurrentIndex(kInvert);
            applyMaskTo(op.mask, chR_, chG_, chB_);
        } else if constexpr (std::is_same_v<T, preprocess::SplitOp>) {
            colorTypeCombo_->setCurrentIndex(kSplit);
            applyMaskTo(op.mask, chR_, chG_, chB_);
        }
    }, cfg);
}

// 某步的中文描述（列表文本）。
QString PreprocessPanel::describeStep(const preprocess::PreprocessConfig& cfg) {
    return std::visit([]<typename T0>(const T0& op) -> QString {
        using T = std::decay_t<T0>;
        if constexpr (std::is_same_v<T, preprocess::GrayOp>) {
            return IDC_GTR(label, kColorTypeGray);
        } else if constexpr (std::is_same_v<T, preprocess::InvertOp>) {
            return IDC_GTR(status, kStepInvertDesc).arg(QString::fromStdString(op.mask));
        } else if constexpr (std::is_same_v<T, preprocess::SplitOp>) {
            return IDC_GTR(status, kStepSplitDesc).arg(QString::fromStdString(op.mask));
        } else if constexpr (std::is_same_v<T, preprocess::RotateOp>) {
            return IDC_GTR(status, kStepRotateDesc).arg(op.angle);
        } else if constexpr (std::is_same_v<T, preprocess::FlipOp>) {
            return op.horizontal ? IDC_GTR(menu, kFlipHorizontal) : IDC_GTR(menu, kFlipVertical);
        } else {
            return IDC_GTR(status, kStepUnknown);
        }
    }, cfg);
}

// 绑定 Document。
void PreprocessPanel::setDocument(Document* doc) {
    doc_ = doc;
    syncFromDocument();
}

// 从 Document 反向同步：整板启停、刷新尺寸区、按类型重建几何 / 色彩两列表、重置按钮启停。
void PreprocessPanel::syncFromDocument() {
    const bool has = doc_ && doc_->hasImage();
    setEnabled(has);
    if (!has) return;

    // ---- 顶部目标标注：三元（图片图层 / 底图 / 画布）----
    if (targetLabel_) {
        QString t;
        switch (doc_->preprocessTarget()) {
            case PreprocessTarget::Layer:  t = IDC_GTR(label, kImageLayerName); break;
            case PreprocessTarget::Base:   t = IDC_GTR(menu, kShowBase); break;
            case PreprocessTarget::Canvas: t = IDC_GTR(label, kTargetCanvasName); break;
        }
        targetLabel_->setText(IDC_GTR(status, kTargetLabelFmt).arg(t));
    }

    // 底图（Base）目标：仅开放色彩预处理——禁用「几何操作」区与「画布尺寸（Resize）」区
    //（几何/resize 会改画布尺寸、破坏图层/标注坐标基准）；Layer/Canvas 目标维持全部可用。
    const bool baseTarget = doc_->preprocessTarget() == PreprocessTarget::Base;
    if (geoBox_) geoBox_->setEnabled(!baseTarget);
    if (sizeBox_) sizeBox_->setEnabled(!baseTarget);

    // ---- 尺寸区：只读回显当前输出尺寸；尺寸框默认取当前输出宽高（阻断联动回调避免抖动）----
    if (currentSizeLabel_)
        currentSizeLabel_->setText(IDC_GTR(status, kCurrentSizeFmt).arg(doc_->width()).arg(doc_->height()));
    for (QSpinBox* s : {resizeW_, resizeH_}) s->blockSignals(true);
    resizeW_->setValue(doc_->width());
    resizeH_->setValue(doc_->height());
    for (QSpinBox* s : {resizeW_, resizeH_}) s->blockSignals(false);

    // ---- 按类型拆分队列到两个列表（几何块 / 色彩块）----
    std::vector<preprocess::PreprocessConfig> geoSteps, colorSteps;
    for (const auto& cfg : doc_->activeSession().steps().items())
        (isGeo(cfg) ? geoSteps : colorSteps).push_back(cfg);

    // 重建列表：阻断选中信号避免回灌抖动；记住各列选中行。若本次源于「上/下移」（pending 有效），
    // 高亮跟随被移动项到新行，否则保持原行；行号越界钳到末行。
    const int geoKeep = geoList_->currentRow();
    geoList_->blockSignals(true);
    geoList_->clear();
    for (const auto& cfg : geoSteps) geoList_->addItem(describeStep(cfg));
    int geoTarget = geoPendingRow_ >= 0 ? geoPendingRow_ : geoKeep;
    if (geoTarget >= geoList_->count()) geoTarget = geoList_->count() - 1;
    if (geoTarget >= 0) geoList_->setCurrentRow(geoTarget);
    geoList_->blockSignals(false);
    geoPendingRow_ = -1;

    const int colorKeep = colorList_->currentRow();
    colorList_->blockSignals(true);
    colorList_->clear();
    for (const auto& cfg : colorSteps) colorList_->addItem(describeStep(cfg));
    int colorTarget = colorPendingRow_ >= 0 ? colorPendingRow_ : colorKeep;
    if (colorTarget >= colorList_->count()) colorTarget = colorList_->count() - 1;
    if (colorTarget >= 0) colorList_->setCurrentRow(colorTarget);
    colorList_->blockSignals(false);
    colorPendingRow_ = -1;

    resetBtn_->setEnabled(doc_->activeSession().hasPreprocess());

    // 灰度启停智能：队列产出灰度后禁用色彩下拉中「黑白」「色道分离」两项（冗余 / 对灰度无操作）；
    // 若当前停在被禁项且无步骤选中，回退到首个始终可用的「色道反色」，避免停留在禁用态。
    if (const auto* model = qobject_cast<QStandardItemModel*>(colorTypeCombo_->model())) {
        const bool grayOut = doc_->activeSession().producesGray();
        for (const int idx : {kGray, kSplit}) {
            if (QStandardItem* it = model->item(idx)) {
                it->setEnabled(!grayOut);
                it->setToolTip(grayOut ? IDC_GTR(tooltip, kGrayOutItem) : QString());
            }
        }
        if (grayOut && colorList_->currentRow() < 0 &&
            (colorTypeCombo_->currentIndex() == kGray || colorTypeCombo_->currentIndex() == kSplit))
            colorTypeCombo_->setCurrentIndex(kInvert);
    }

    onGeoSelectionChanged();
    onColorSelectionChanged();
}

// 保持宽高比时，改宽联动算高（比例取当前输出尺寸；纯 UI 便利，不涉及切割几何）。
void PreprocessPanel::onResizeWidthEdited(const int value) const {
    if (!resizeKeepRatio_->isChecked() || !doc_ || !doc_->hasImage()) return;
    const int cw = doc_->width();
    const int ch = doc_->height();
    if (cw <= 0) return;
    const int h = static_cast<int>(std::lround(static_cast<double>(value) *
                                               static_cast<double>(ch) / static_cast<double>(cw)));
    resizeH_->blockSignals(true);
    resizeH_->setValue(std::max(1, h));
    resizeH_->blockSignals(false);
}

} // namespace idc::gui
