// ============================================================================
// 文件：panels/export_panel.cpp
// 作用：实现导出面板的构建、Document 同步、坍缩可行性联动与文件对话框（见头文件说明）。
// 分块依据：参数写回（onModeChanged/onFormatChanged/...）与导出动作（exportRequested 交上层）
//           分离；目录/文件/命名行随输出模式显隐，避免无关字段干扰。
// ============================================================================
#include "panels/export_panel.h"

#include <QComboBox>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QVBoxLayout>

#include "engine/engine.h"
#include "model/document.h"

namespace idc::gui {

// 构建导出面板布局。
ExportPanel::ExportPanel(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    auto* box = new QGroupBox(QStringLiteral("导出"), this);
    auto* form = new QVBoxLayout(box);
    form->setSpacing(6);

    // 输出模式。
    form->addWidget(new QLabel(QStringLiteral("输出模式"), box));
    modeCombo_ = new QComboBox(box);
    modeCombo_->addItems({QStringLiteral("分离导出"), QStringLiteral("合并坍缩"),
                          QStringLiteral("合并重排")});
    modeCombo_->setToolTip(QStringLiteral("分离：每块一张图；合并坍缩：剩余块紧贴拼接为一张；"
                                          "合并重排：按序列填入新画布。"));
    form->addWidget(modeCombo_);

    // 目录行（分离导出）。
    dirRow_ = new QWidget(box);
    auto* dh = new QHBoxLayout(dirRow_);
    dh->setContentsMargins(0, 0, 0, 0);
    dirEdit_ = new QLineEdit(dirRow_);
    dirEdit_->setPlaceholderText(QStringLiteral("手动输入或点「浏览…」选择输出目录"));
    dirBtn_ = new QPushButton(QStringLiteral("浏览…"), dirRow_);
    dh->addWidget(new QLabel(QStringLiteral("目录"), dirRow_));
    dh->addWidget(dirEdit_, 1);
    dh->addWidget(dirBtn_);
    form->addWidget(dirRow_);

    // 命名行（分离导出）。
    namingRow_ = new QWidget(box);
    auto* nh = new QHBoxLayout(namingRow_);
    nh->setContentsMargins(0, 0, 0, 0);
    namingLabel_ = new QLabel(QStringLiteral("命名"), namingRow_);
    naming_ = new QLineEdit(QStringLiteral("{name}_{index:03d}"), namingRow_);
    naming_->setToolTip(QStringLiteral("分离导出命名模板，支持 {name}/{index}/{index:03d}/{row}/{col}。"));
    nh->addWidget(namingLabel_);
    nh->addWidget(naming_, 1);
    form->addWidget(namingRow_);

    // 文件行（合并导出）。
    fileRow_ = new QWidget(box);
    auto* fh = new QHBoxLayout(fileRow_);
    fh->setContentsMargins(0, 0, 0, 0);
    fileEdit_ = new QLineEdit(fileRow_);
    fileEdit_->setPlaceholderText(QStringLiteral("手动输入或点「浏览…」选择输出文件"));
    fileBtn_ = new QPushButton(QStringLiteral("浏览…"), fileRow_);
    fh->addWidget(new QLabel(QStringLiteral("文件"), fileRow_));
    fh->addWidget(fileEdit_, 1);
    fh->addWidget(fileBtn_);
    form->addWidget(fileRow_);

    // 格式。
    form->addWidget(new QLabel(QStringLiteral("格式"), box));
    formatCombo_ = new QComboBox(box);
    formatCombo_->addItems({QStringLiteral("PNG"), QStringLiteral("JPEG"),
                            QStringLiteral("WebP"), QStringLiteral("BMP")});
    form->addWidget(formatCombo_);

    // 质量（仅有损格式）。
    qualityLabel_ = new QLabel(QStringLiteral("质量"), box);
    form->addWidget(qualityLabel_);
    quality_ = new QSpinBox(box);
    quality_->setRange(1, 100);
    quality_->setValue(90);
    form->addWidget(quality_);

    root->addWidget(box);

    // 导出前信息（本阶段以文字替代缩略图）。
    infoLabel_ = new QLabel(QStringLiteral("尚未计算预览。"), this);
    infoLabel_->setWordWrap(true);
    root->addWidget(infoLabel_);

    // 导出按钮。
    exportBtn_ = new QPushButton(QStringLiteral("导出"), this);
    exportBtn_->setToolTip(QStringLiteral("按当前参数导出（Ctrl+S）。"));
    root->addWidget(exportBtn_);
    root->addStretch(1);

    // 信号连接。
    connect(modeCombo_, &QComboBox::currentIndexChanged, this, &ExportPanel::onModeChanged);
    connect(formatCombo_, &QComboBox::currentIndexChanged, this, &ExportPanel::onFormatChanged);
    connect(quality_, &QSpinBox::valueChanged, this, &ExportPanel::onQualityChanged);
    connect(naming_, &QLineEdit::editingFinished, this, &ExportPanel::onNamingEdited);
    connect(dirEdit_, &QLineEdit::editingFinished, this, &ExportPanel::onDirEdited);
    connect(fileEdit_, &QLineEdit::editingFinished, this, &ExportPanel::onFileEdited);
    connect(dirBtn_, &QPushButton::clicked, this, &ExportPanel::onBrowseDir);
    connect(fileBtn_, &QPushButton::clicked, this, &ExportPanel::onBrowseFile);
    connect(exportBtn_, &QPushButton::clicked, this, &ExportPanel::exportRequested);
}

// 绑定 Document 并同步一次。
void ExportPanel::setDocument(Document* doc) {
    doc_ = doc;
    syncFromDocument();
}

// 从 Document 反向同步控件与字段可见性。
void ExportPanel::syncFromDocument() {
    if (!doc_) return;

    // 输出模式：SEPARATE→0；MERGED+COLLAPSE→1；MERGED+REARRANGE→2。
    int idx = 0;
    if (doc_->emitMode() == idc::engine::EmitMode::MERGED) {
        idx = (doc_->layout() == idc::engine::MergeLayout::COLLAPSE) ? 1 : 2;
    }
    modeCombo_->blockSignals(true);
    modeCombo_->setCurrentIndex(idx);
    modeCombo_->blockSignals(false);

    // 格式：ExportFormat 枚举序（PNG/JPEG/WEBP/BMP）与 combo 一致。
    formatCombo_->blockSignals(true);
    formatCombo_->setCurrentIndex(static_cast<int>(doc_->format()));
    formatCombo_->blockSignals(false);

    quality_->blockSignals(true);
    quality_->setValue(doc_->quality());
    quality_->blockSignals(false);

    naming_->blockSignals(true);
    naming_->setText(doc_->naming());
    naming_->blockSignals(false);

    dirEdit_->setText(doc_->outputDir());
    fileEdit_->setText(doc_->outputFile());

    updateFieldVisibility();
    const bool lossy = (doc_->format() == idc::engine::ExportFormat::JPEG ||
                        doc_->format() == idc::engine::ExportFormat::WEBP);
    qualityLabel_->setVisible(lossy);
    quality_->setVisible(lossy);
}

// 依 Core 坍缩可行性启用/禁用「合并坍缩」；不可行且当前选中坍缩时切到重排。
void ExportPanel::setCollapsible(const bool collapsible) {
    if (auto* m = qobject_cast<QStandardItemModel*>(modeCombo_->model())) {
        if (QStandardItem* it = m->item(1)) it->setEnabled(collapsible);
    }
    if (!collapsible && modeCombo_->currentIndex() == 1) {
        modeCombo_->setCurrentIndex(2); // 触发 onModeChanged(2) → 写回重排。
    }
}

// 更新导出前信息文字。
void ExportPanel::setPreviewInfo(const QString& text) {
    if (infoLabel_) infoLabel_->setText(text);
}

// 输出模式变更 → 写回 Document + 切换字段可见性。
void ExportPanel::onModeChanged(const int index) {
    if (!doc_) return;
    switch (index) {
        case 0: doc_->setEmit(idc::engine::EmitMode::SEPARATE, idc::engine::MergeLayout::COLLAPSE); break;
        case 1: doc_->setEmit(idc::engine::EmitMode::MERGED, idc::engine::MergeLayout::COLLAPSE); break;
        case 2: doc_->setEmit(idc::engine::EmitMode::MERGED, idc::engine::MergeLayout::REARRANGE); break;
        default: break;
    }
    updateFieldVisibility();
}

// 格式变更 → 写回 Document + 质量字段显隐。
void ExportPanel::onFormatChanged(const int index) {
    if (!doc_) return;
    idc::engine::ExportFormat f = idc::engine::ExportFormat::PNG;
    switch (index) {
        case 0: f = idc::engine::ExportFormat::PNG; break;
        case 1: f = idc::engine::ExportFormat::JPEG; break;
        case 2: f = idc::engine::ExportFormat::WEBP; break;
        case 3: f = idc::engine::ExportFormat::BMP; break;
        default: break;
    }
    doc_->setFormat(f);
    const bool lossy = (f == idc::engine::ExportFormat::JPEG || f == idc::engine::ExportFormat::WEBP);
    qualityLabel_->setVisible(lossy);
    quality_->setVisible(lossy);
}

// 质量变更 → 写回 Document。
void ExportPanel::onQualityChanged(const int value) {
    if (doc_) doc_->setQuality(value);
}

// 命名模板提交 → 写回 Document。
void ExportPanel::onNamingEdited() {
    if (doc_) doc_->setNaming(naming_->text());
}

// 目录手动输入提交 → 写回 Document（与「浏览…」等价，允许直接键入路径）。
void ExportPanel::onDirEdited() {
    if (doc_) doc_->setOutputDir(dirEdit_->text().trimmed());
}

// 文件手动输入提交 → 写回 Document（与「浏览…」等价，允许直接键入路径）。
void ExportPanel::onFileEdited() {
    if (doc_) doc_->setOutputFile(fileEdit_->text().trimmed());
}

// 选择输出目录（分离导出）。
void ExportPanel::onBrowseDir() {
    const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("选择输出目录"),
                                                        dirEdit_->text());
    if (!d.isEmpty()) {
        dirEdit_->setText(d);
        if (doc_) doc_->setOutputDir(d);
    }
}

// 选择输出文件（合并导出）。
void ExportPanel::onBrowseFile() {
    const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("导出为"),
                                                   fileEdit_->text(), saveFilter());
    if (!f.isEmpty()) {
        fileEdit_->setText(f);
        if (doc_) doc_->setOutputFile(f);
    }
}

// 依当前输出模式切换目录/文件/命名行显隐。
void ExportPanel::updateFieldVisibility() {
    const bool separate = (modeCombo_->currentIndex() == 0);
    dirRow_->setVisible(separate);
    namingRow_->setVisible(separate);
    fileRow_->setVisible(!separate);
}

// 依当前格式返回保存对话框过滤器。
QString ExportPanel::saveFilter() const {
    if (!doc_) return QStringLiteral("所有文件 (*)");
    switch (doc_->format()) {
        case idc::engine::ExportFormat::PNG:  return QStringLiteral("PNG 图像 (*.png)");
        case idc::engine::ExportFormat::JPEG: return QStringLiteral("JPEG 图像 (*.jpg *.jpeg)");
        case idc::engine::ExportFormat::WEBP: return QStringLiteral("WebP 图像 (*.webp)");
        case idc::engine::ExportFormat::BMP:  return QStringLiteral("BMP 图像 (*.bmp)");
    }
    return QStringLiteral("所有文件 (*)");
}

} // namespace idc::gui
