// ============================================================================
// 文件：panels/image_panel.cpp
// 作用：实现图像处理面板——顶部独立「画布尺寸（Resize）」区 + 「几何操作」区/列表 + 「色彩操作」区/列表。
// 分块依据：构造函数只做「排版 + 串联信号」；两区各自的操作类型下拉 + 参数控件按类型启停；
//           尺寸区控件在有图时常开；槽函数只把控件值翻译成轻操作 PreprocessConfig 或 resize 意图发信号，绝不触碰像素运算。
//           信号携带扁平队列下标（色彩项 = 几何项数 + 色彩行）；「几何块在前、色彩块在后」的不变式由控制器保证。
// 视觉规范（本目录 README「画布视觉规范」）：面板内边距 8px、控件间距 6px、分组间距 12px。
// ============================================================================
#include "panels/image_panel.h"

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
ImagePanel::ImagePanel(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);   // 面板内边距 8px。
    root->setSpacing(12);                    // 分组间距 12px。

    // ---- 画布尺寸（Resize）独立区：常驻、恒最先重放、不入队列 ----
    auto* sizeBox = new QGroupBox(QStringLiteral("画布尺寸"), this);
    auto* sizeGrid = new QGridLayout(sizeBox);
    sizeGrid->setSpacing(6);

    resizeW_ = new QSpinBox(sizeBox);
    resizeH_ = new QSpinBox(sizeBox);
    for (QSpinBox* s : {resizeW_, resizeH_}) {
        s->setRange(1, 20000);               // 上限覆盖 8000×8000 大图并留余量。
        s->setToolTip(QStringLiteral("目标边长（像素）。勾选「保持宽高比」时改宽联动算高。"));
    }
    resizeKeepRatio_ = new QCheckBox(QStringLiteral("保持宽高比"), sizeBox);
    resizeKeepRatio_->setChecked(true);
    sizeGrid->addWidget(new QLabel(QStringLiteral("宽 / 高"), sizeBox), 0, 0);
    sizeGrid->addWidget(resizeW_, 0, 1);
    sizeGrid->addWidget(resizeH_, 0, 2);
    sizeGrid->addWidget(resizeKeepRatio_, 0, 3);

    resizeScalePct_ = new QSpinBox(sizeBox);
    resizeScalePct_->setRange(1, 1000);      // 1%..1000%。
    resizeScalePct_->setValue(100);
    resizeScalePct_->setSuffix(QStringLiteral("%"));
    resizeScalePct_->setToolTip(QStringLiteral("按当前画布尺寸目标（无则原图尺寸）的百分比缩放。"));
    currentSizeLabel_ = new QLabel(QStringLiteral("当前输出尺寸: —×—"), sizeBox);
    sizeGrid->addWidget(new QLabel(QStringLiteral("比例"), sizeBox), 1, 0);
    sizeGrid->addWidget(resizeScalePct_, 1, 1);
    sizeGrid->addWidget(currentSizeLabel_, 1, 2, 1, 2);

    applySizeBtn_ = new QPushButton(QStringLiteral("应用尺寸"), sizeBox);
    applySizeBtn_->setToolTip(QStringLiteral("把工作图设为上面输入的绝对尺寸（恒从原图重采样）。"));
    scaleSizeBtn_ = new QPushButton(QStringLiteral("按比例缩放"), sizeBox);
    scaleSizeBtn_->setToolTip(QStringLiteral("按上面百分比缩放画布尺寸。"));
    clearSizeBtn_ = new QPushButton(QStringLiteral("清除尺寸"), sizeBox);
    clearSizeBtn_->setToolTip(QStringLiteral("取消画布尺寸，回到原图尺寸（轻操作队列保留）。"));
    auto* sizeBtnRow = new QHBoxLayout();
    sizeBtnRow->addWidget(applySizeBtn_);
    sizeBtnRow->addWidget(scaleSizeBtn_);
    sizeBtnRow->addWidget(clearSizeBtn_);
    sizeGrid->addLayout(sizeBtnRow, 2, 0, 1, 4);
    root->addWidget(sizeBox);

    // ================= 几何操作区（旋转 / 翻转）=================
    auto* geoBox = new QGroupBox(QStringLiteral("几何操作"), this);
    auto* geoGrid = new QGridLayout(geoBox);
    geoGrid->setSpacing(6);

    geoTypeCombo_ = new QComboBox(geoBox);
    geoTypeCombo_->addItems({QStringLiteral("旋转"), QStringLiteral("翻转")});
    geoGrid->addWidget(new QLabel(QStringLiteral("操作类型"), geoBox), 0, 0);
    geoGrid->addWidget(geoTypeCombo_, 0, 1, 1, 3);

    rotAngle_ = new QComboBox(geoBox);
    rotAngle_->addItem(QStringLiteral("左转 90°"), -90);
    rotAngle_->addItem(QStringLiteral("右转 90°"), 90);
    rotAngle_->addItem(QStringLiteral("旋转 180°"), 180);
    geoGrid->addWidget(new QLabel(QStringLiteral("角度"), geoBox), 1, 0);
    geoGrid->addWidget(rotAngle_, 1, 1, 1, 3);

    flipDir_ = new QComboBox(geoBox);
    flipDir_->addItem(QStringLiteral("水平（左右）"), 1);
    flipDir_->addItem(QStringLiteral("垂直（上下）"), 0);
    geoGrid->addWidget(new QLabel(QStringLiteral("方向"), geoBox), 2, 0);
    geoGrid->addWidget(flipDir_, 2, 1, 1, 3);

    auto* geoBtnRow = new QHBoxLayout();
    geoAddBtn_ = new QPushButton(QStringLiteral("添加几何步骤"), geoBox);
    geoAddBtn_->setToolTip(QStringLiteral("把上面配置好的几何操作追加到几何列表末尾（恒在色彩块之前）。"));
    geoReplaceBtn_ = new QPushButton(QStringLiteral("更新所选"), geoBox);
    geoReplaceBtn_->setToolTip(QStringLiteral("用当前配置就地替换几何列表中选中的步骤。"));
    geoBtnRow->addWidget(geoAddBtn_);
    geoBtnRow->addWidget(geoReplaceBtn_);
    geoGrid->addLayout(geoBtnRow, 3, 0, 1, 4);

    geoList_ = new QListWidget(geoBox);
    geoList_->setToolTip(QStringLiteral("几何操作队列（旋转 / 翻转，resize 产物上先于色彩应用）。选中后可删除 / 上下移动 / 更新。"));
    geoGrid->addWidget(geoList_, 4, 0, 1, 4);

    auto* geoEditRow = new QHBoxLayout();
    geoDelBtn_ = new QPushButton(QStringLiteral("删除"), geoBox);
    geoUpBtn_ = new QPushButton(QStringLiteral("上移"), geoBox);
    geoDownBtn_ = new QPushButton(QStringLiteral("下移"), geoBox);
    geoEditRow->addWidget(geoDelBtn_);
    geoEditRow->addWidget(geoUpBtn_);
    geoEditRow->addWidget(geoDownBtn_);
    geoGrid->addLayout(geoEditRow, 5, 0, 1, 4);
    root->addWidget(geoBox);

    // ================= 色彩操作区（黑白 / 反色 / 分离）=================
    auto* colorBox = new QGroupBox(QStringLiteral("色彩操作"), this);
    auto* colorGrid = new QGridLayout(colorBox);
    colorGrid->setSpacing(6);

    colorTypeCombo_ = new QComboBox(colorBox);
    colorTypeCombo_->addItems({QStringLiteral("黑白（灰度）"), QStringLiteral("色道反色"),
                               QStringLiteral("色道分离")});
    colorGrid->addWidget(new QLabel(QStringLiteral("操作类型"), colorBox), 0, 0);
    colorGrid->addWidget(colorTypeCombo_, 0, 1, 1, 3);

    chR_ = new QCheckBox(QStringLiteral("R"), colorBox);
    chG_ = new QCheckBox(QStringLiteral("G"), colorBox);
    chB_ = new QCheckBox(QStringLiteral("B"), colorBox);
    for (QCheckBox* c : {chR_, chG_, chB_}) c->setChecked(true);
    colorGrid->addWidget(new QLabel(QStringLiteral("作用通道"), colorBox), 1, 0);
    colorGrid->addWidget(chR_, 1, 1);
    colorGrid->addWidget(chG_, 1, 2);
    colorGrid->addWidget(chB_, 1, 3);

    auto* colorBtnRow = new QHBoxLayout();
    colorAddBtn_ = new QPushButton(QStringLiteral("添加色彩步骤"), colorBox);
    colorAddBtn_->setToolTip(QStringLiteral("把上面配置好的色彩操作追加到色彩列表末尾（恒在几何块之后）。"));
    colorReplaceBtn_ = new QPushButton(QStringLiteral("更新所选"), colorBox);
    colorReplaceBtn_->setToolTip(QStringLiteral("用当前配置就地替换色彩列表中选中的步骤。"));
    colorBtnRow->addWidget(colorAddBtn_);
    colorBtnRow->addWidget(colorReplaceBtn_);
    colorGrid->addLayout(colorBtnRow, 2, 0, 1, 4);

    colorList_ = new QListWidget(colorBox);
    colorList_->setToolTip(QStringLiteral("色彩操作队列（黑白 / 反色 / 分离，在几何产物上应用）。选中后可删除 / 上下移动 / 更新。"));
    colorGrid->addWidget(colorList_, 3, 0, 1, 4);

    auto* colorEditRow = new QHBoxLayout();
    colorDelBtn_ = new QPushButton(QStringLiteral("删除"), colorBox);
    colorUpBtn_ = new QPushButton(QStringLiteral("上移"), colorBox);
    colorDownBtn_ = new QPushButton(QStringLiteral("下移"), colorBox);
    colorEditRow->addWidget(colorDelBtn_);
    colorEditRow->addWidget(colorUpBtn_);
    colorEditRow->addWidget(colorDownBtn_);
    colorGrid->addLayout(colorEditRow, 4, 0, 1, 4);
    root->addWidget(colorBox);

    // ---- 重置 ----
    resetBtn_ = new QPushButton(QStringLiteral("重置预处理（清空尺寸与队列，恢复原图）"), this);
    resetBtn_->setToolTip(QStringLiteral("丢弃画布尺寸与全部轻操作步骤，把工作图恢复为最初打开的原图。"));
    root->addWidget(resetBtn_);

    // ---- 串联信号 ----
    connect(geoTypeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ImagePanel::onGeoTypeChanged);
    connect(geoAddBtn_, &QPushButton::clicked, this, &ImagePanel::onGeoAdd);
    connect(geoReplaceBtn_, &QPushButton::clicked, this, &ImagePanel::onGeoReplace);
    connect(geoDelBtn_, &QPushButton::clicked, this, &ImagePanel::onGeoDelete);
    connect(geoUpBtn_, &QPushButton::clicked, this, &ImagePanel::onGeoMoveUp);
    connect(geoDownBtn_, &QPushButton::clicked, this, &ImagePanel::onGeoMoveDown);
    connect(geoList_, &QListWidget::currentRowChanged, this, [this](int) { onGeoSelectionChanged(); });

    connect(colorTypeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ImagePanel::onColorTypeChanged);
    connect(colorAddBtn_, &QPushButton::clicked, this, &ImagePanel::onColorAdd);
    connect(colorReplaceBtn_, &QPushButton::clicked, this, &ImagePanel::onColorReplace);
    connect(colorDelBtn_, &QPushButton::clicked, this, &ImagePanel::onColorDelete);
    connect(colorUpBtn_, &QPushButton::clicked, this, &ImagePanel::onColorMoveUp);
    connect(colorDownBtn_, &QPushButton::clicked, this, &ImagePanel::onColorMoveDown);
    connect(colorList_, &QListWidget::currentRowChanged, this, [this](int) { onColorSelectionChanged(); });

    connect(resetBtn_, &QPushButton::clicked, this, &ImagePanel::resetRequested);
    connect(applySizeBtn_, &QPushButton::clicked, this, &ImagePanel::onApplyResize);
    connect(scaleSizeBtn_, &QPushButton::clicked, this, &ImagePanel::onScaleResizeClicked);
    connect(clearSizeBtn_, &QPushButton::clicked, this, &ImagePanel::onClearResizeClicked);
    connect(resizeW_, QOverload<int>::of(&QSpinBox::valueChanged), this, &ImagePanel::onResizeWidthEdited);

    onGeoTypeChanged(geoTypeCombo_->currentIndex());
    onColorTypeChanged(colorTypeCombo_->currentIndex());
}

// ---- 几何区：类型启停（旋转→角度，翻转→方向）----
void ImagePanel::onGeoTypeChanged(const int index) const {
    const bool has = doc_ && doc_->hasImage();
    rotAngle_->setEnabled(has && index == kRotate);
    flipDir_->setEnabled(has && index == kFlip);
    geoAddBtn_->setEnabled(has);
    geoReplaceBtn_->setEnabled(has && geoList_->currentRow() >= 0);
}

// ---- 色彩区：类型启停（反色 / 分离→通道；黑白→无通道），叠加灰度产出智能禁用 ----
void ImagePanel::onColorTypeChanged(const int index) const {
    const bool has = doc_ && doc_->hasImage();
    const bool chan = has && (index == kInvert || index == kSplit);
    chR_->setEnabled(chan);
    chG_->setEnabled(chan);
    chB_->setEnabled(chan);
    // 队列已产出灰度时，「黑白」(冗余) 与「色道分离」(对灰度无操作) 不可再入队 / 替换。
    const bool blocked = has && doc_->preprocessProducesGray() && (index == kGray || index == kSplit);
    colorAddBtn_->setEnabled(has && !blocked);
    colorAddBtn_->setToolTip(blocked ? QStringLiteral("队列已产出灰度：重复「黑白」属冗余、「色道分离」对灰度图为无操作，故不可添加。")
                                     : QStringLiteral("把上面配置好的色彩操作追加到色彩列表末尾（恒在几何块之后）。"));
    colorReplaceBtn_->setEnabled(has && colorList_->currentRow() >= 0 && !blocked);
}

// 依几何类型 + 参数构造 PreprocessConfig（RotateOp / FlipOp）。
preprocess::PreprocessConfig ImagePanel::buildGeoConfig() const {
    switch (geoTypeCombo_->currentIndex()) {
        case kRotate: return preprocess::RotateOp{rotAngle_->currentData().toInt()};
        case kFlip:   return preprocess::FlipOp{flipDir_->currentData().toInt() != 0};
        default:      return preprocess::RotateOp{90};
    }
}

// 依色彩类型 + 参数构造 PreprocessConfig（GrayOp / InvertOp / SplitOp）。
preprocess::PreprocessConfig ImagePanel::buildColorConfig() const {
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
void ImagePanel::onGeoAdd() { emit stepAppendRequested(buildGeoConfig()); }

void ImagePanel::onGeoReplace() {
    const int idx = geoFlatIndex();
    if (idx < 0) return;
    emit stepReplaceRequested(idx, buildGeoConfig());
}

void ImagePanel::onGeoDelete() {
    const int idx = geoFlatIndex();
    if (idx < 0) return;
    emit stepRemoveRequested(idx);
}

void ImagePanel::onGeoMoveUp() {
    const int row = geoList_->currentRow();
    if (row <= 0) return;
    geoPendingRow_ = row - 1;
    emit stepMoveRequested(geoFlatIndex(), false);
}

void ImagePanel::onGeoMoveDown() {
    const int row = geoList_->currentRow();
    if (row < 0 || row + 1 >= geoList_->count()) return;
    geoPendingRow_ = row + 1;
    emit stepMoveRequested(geoFlatIndex(), true);
}

// ---- 色彩区动作 ----
void ImagePanel::onColorAdd() { emit stepAppendRequested(buildColorConfig()); }

void ImagePanel::onColorReplace() {
    const int idx = colorFlatIndex();
    if (idx < 0) return;
    emit stepReplaceRequested(idx, buildColorConfig());
}

void ImagePanel::onColorDelete() {
    const int idx = colorFlatIndex();
    if (idx < 0) return;
    emit stepRemoveRequested(idx);
}

void ImagePanel::onColorMoveUp() {
    const int row = colorList_->currentRow();
    if (row <= 0) return;   // 首色上移会跨入几何块（破坏 geo→color 不变式），故禁止
    colorPendingRow_ = row - 1;
    emit stepMoveRequested(colorFlatIndex(), false);
}

void ImagePanel::onColorMoveDown() {
    const int row = colorList_->currentRow();
    if (row < 0 || row + 1 >= colorList_->count()) return;
    colorPendingRow_ = row + 1;
    emit stepMoveRequested(colorFlatIndex(), true);
}

// 「画布尺寸」区动作：把控件值翻译成 resize 意图发信号（真正的重放物化在控制器）。
void ImagePanel::onApplyResize() { emit resizeAppliedRequested(resizeW_->value(), resizeH_->value()); }

void ImagePanel::onScaleResizeClicked() {
    const double factor = static_cast<double>(resizeScalePct_->value()) / 100.0;
    emit resizeScaleRequested(factor);
}

void ImagePanel::onClearResizeClicked() { emit resizeClearedRequested(); }

// ---- 扁平下标映射 ----
int ImagePanel::geoRowCount() const {
    if (!doc_) return 0;
    int n = 0;
    for (const auto& cfg : doc_->steps().items()) {
        if (!isGeo(cfg)) break;   // 几何块在前，遇首个色彩即止
        ++n;
    }
    return n;
}

int ImagePanel::geoFlatIndex() const {
    const int row = geoList_ ? geoList_->currentRow() : -1;
    return row;   // 几何行 = 扁平下标（几何块恒在前）
}

int ImagePanel::colorFlatIndex() const {
    const int row = colorList_ ? colorList_->currentRow() : -1;
    if (row < 0) return -1;
    return geoRowCount() + row;   // 色彩行需加几何项数偏移
}

// 几何列表选中：把该步参数回灌几何配置区，刷新按钮启停。
void ImagePanel::onGeoSelectionChanged() {
    const int row = geoList_->currentRow();
    if (row >= 0 && doc_) {
        if (const auto& steps = doc_->steps().items(); static_cast<std::size_t>(row) < steps.size()) loadGeoIntoControls(steps[static_cast<std::size_t>(row)]);
    }
    const bool has = doc_ && doc_->hasImage();
    geoReplaceBtn_->setEnabled(has && row >= 0);
    geoDelBtn_->setEnabled(has && row >= 0);
    geoUpBtn_->setEnabled(has && row > 0);
    geoDownBtn_->setEnabled(has && row >= 0 && row + 1 < geoList_->count());
    onGeoTypeChanged(geoTypeCombo_->currentIndex());
}

// 色彩列表选中：把该步参数回灌色彩配置区，刷新按钮启停。
void ImagePanel::onColorSelectionChanged() {
    const int row = colorList_->currentRow();
    if (row >= 0 && doc_) {
        const auto& steps = doc_->steps().items();
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
void ImagePanel::loadGeoIntoControls(const preprocess::PreprocessConfig& cfg) {
    std::visit([this](const auto& op) {
        using T = std::decay_t<decltype(op)>;
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
void ImagePanel::loadColorIntoControls(const preprocess::PreprocessConfig& cfg) {
    std::visit([this](const auto& op) {
        using T = std::decay_t<decltype(op)>;
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
QString ImagePanel::describeStep(const preprocess::PreprocessConfig& cfg) {
    return std::visit([](const auto& op) -> QString {
        using T = std::decay_t<decltype(op)>;
        if constexpr (std::is_same_v<T, preprocess::GrayOp>) {
            return QStringLiteral("黑白（灰度）");
        } else if constexpr (std::is_same_v<T, preprocess::InvertOp>) {
            return QStringLiteral("色道反色 [%1]").arg(QString::fromStdString(op.mask));
        } else if constexpr (std::is_same_v<T, preprocess::SplitOp>) {
            return QStringLiteral("色道分离 [%1]").arg(QString::fromStdString(op.mask));
        } else if constexpr (std::is_same_v<T, preprocess::RotateOp>) {
            return QStringLiteral("旋转 %1°").arg(op.angle);
        } else if constexpr (std::is_same_v<T, preprocess::FlipOp>) {
            return op.horizontal ? QStringLiteral("水平翻转") : QStringLiteral("垂直翻转");
        } else {
            return QStringLiteral("未知步骤");
        }
    }, cfg);
}

// 绑定 Document。
void ImagePanel::setDocument(Document* doc) {
    doc_ = doc;
    syncFromDocument();
}

// 从 Document 反向同步：整板启停、刷新尺寸区、按类型重建几何 / 色彩两列表、重置按钮启停。
void ImagePanel::syncFromDocument() {
    const bool has = doc_ && doc_->hasImage();
    setEnabled(has);
    if (!has) return;

    // ---- 尺寸区：只读回显当前输出尺寸；尺寸框默认取当前输出宽高（阻断联动回调避免抖动）----
    if (currentSizeLabel_)
        currentSizeLabel_->setText(QStringLiteral("当前输出尺寸: %1×%2").arg(doc_->width()).arg(doc_->height()));
    for (QSpinBox* s : {resizeW_, resizeH_}) s->blockSignals(true);
    resizeW_->setValue(doc_->width());
    resizeH_->setValue(doc_->height());
    for (QSpinBox* s : {resizeW_, resizeH_}) s->blockSignals(false);

    // ---- 按类型拆分队列到两个列表（几何块 / 色彩块）----
    std::vector<preprocess::PreprocessConfig> geoSteps, colorSteps;
    for (const auto& cfg : doc_->steps().items())
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

    resetBtn_->setEnabled(doc_->hasPreprocess());

    // 灰度启停智能：队列产出灰度后禁用色彩下拉中「黑白」「色道分离」两项（冗余 / 对灰度无操作）；
    // 若当前停在被禁项且无步骤选中，回退到首个始终可用的「色道反色」，避免停留在禁用态。
    if (const auto* model = qobject_cast<QStandardItemModel*>(colorTypeCombo_->model())) {
        const bool grayOut = doc_->preprocessProducesGray();
        for (const int idx : {kGray, kSplit}) {
            if (QStandardItem* it = model->item(idx)) {
                it->setEnabled(!grayOut);
                it->setToolTip(grayOut ? QStringLiteral("队列已产出灰度：此操作冗余或对灰度无操作") : QString());
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
void ImagePanel::onResizeWidthEdited(const int value) const {
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
