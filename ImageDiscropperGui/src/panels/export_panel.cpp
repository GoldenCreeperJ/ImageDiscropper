// ============================================================================
// 文件：panels/export_panel.cpp
// 作用：实现导出面板的构建、Document 同步、坍缩可行性联动与文件对话框（见头文件说明）。
// 分块依据：参数写回（onModeChanged/onFormatChanged/...）与导出动作（exportRequested 交上层）
//           分离；目录/文件/命名行随输出模式显隐，避免无关字段干扰。
// ============================================================================
#include "panels/export_panel.h"

#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QStringList>
#include <QVBoxLayout>

#include <cmath>

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

    // 合并重排参数行（FR-L3.7 / §4.6）：仅「合并重排」模式显示。
    // 列/行/单元尺寸以 0 表示「自动」（setSpecialValueText 显示为“自动”），交 Core compose 推导。
    rearrangeRow_ = new QWidget(box);
    auto* rf = new QFormLayout(rearrangeRow_);
    rf->setContentsMargins(0, 0, 0, 0);
    rf->setSpacing(4);
    mergeCols_ = new QSpinBox(rearrangeRow_);
    mergeRows_ = new QSpinBox(rearrangeRow_);
    mergeCellW_ = new QSpinBox(rearrangeRow_);
    mergeCellH_ = new QSpinBox(rearrangeRow_);
    for (QSpinBox* s : {mergeCols_, mergeRows_, mergeCellW_, mergeCellH_}) {
        s->setRange(0, 100000);
        s->setValue(0);
        s->setKeyboardTracking(false);          // 提交才触发，避免逐键刷新。
        s->setSpecialValueText(QStringLiteral("自动")); // 最小值 0 显示为「自动」。
    }
    mergeCols_->setToolTip(QStringLiteral("重排画布列数；自动=依保留块数推导。"));
    mergeRows_->setToolTip(QStringLiteral("重排画布行数；自动=依保留块数推导。"));
    mergeCellW_->setToolTip(QStringLiteral("重排单元宽；自动=用保留块原尺寸。"));
    mergeCellH_->setToolTip(QStringLiteral("重排单元高；自动=用保留块原尺寸。"));
    rf->addRow(QStringLiteral("列数"), mergeCols_);
    rf->addRow(QStringLiteral("行数"), mergeRows_);
    autoGridBtn_ = new QPushButton(QStringLiteral("按格数自动"), rearrangeRow_);
    autoGridBtn_->setToolTip(QStringLiteral("依保留块数 n 计算 cols=ceil(sqrt(n))、rows=ceil(n/cols) 并填入。"));
    rf->addRow(QString(), autoGridBtn_);
    rf->addRow(QStringLiteral("单元宽"), mergeCellW_);
    rf->addRow(QStringLiteral("单元高"), mergeCellH_);
    // 重排填充顺序（MergeOrder）：与 L3 选择排序正交——决定块列表以何种路径铺进 cols×rows 画布。
    mergeSortCombo_ = new QComboBox(rearrangeRow_);
    mergeSortCombo_->addItems({QStringLiteral("行优先"), QStringLiteral("列优先")});
    mergeSortCombo_->setToolTip(QStringLiteral("重排填充顺序：块列表按行优先/列优先铺进画布（与选择排序正交）。"));
    mergeSnake_ = new QCheckBox(QStringLiteral("蛇形"), rearrangeRow_);
    mergeSnake_->setToolTip(QStringLiteral("隔行（行优先）/隔列（列优先）反向填充。"));
    mergeReverse_ = new QCheckBox(QStringLiteral("倒序"), rearrangeRow_);
    mergeReverse_->setToolTip(QStringLiteral("填充路径整体逆序。"));
    auto* orderRow = new QWidget(rearrangeRow_);
    auto* oh = new QHBoxLayout(orderRow);
    oh->setContentsMargins(0, 0, 0, 0);
    oh->addWidget(mergeSortCombo_);
    oh->addWidget(mergeSnake_);
    oh->addWidget(mergeReverse_);
    oh->addStretch(1);
    rf->addRow(QStringLiteral("填充顺序"), orderRow);
    padColorBtn_ = new QPushButton(rearrangeRow_);
    padColorBtn_->setToolTip(QStringLiteral("空位/余量填充色（默认透明）。点击选择。"));
    rf->addRow(QStringLiteral("填充色"), padColorBtn_);
    // 内联警告（红字）：cols*rows < 保留块数 或 cw/ch < 网格单元尺寸时提示（导出前另弹窗确认）。
    rearrangeWarn_ = new QLabel(rearrangeRow_);
    rearrangeWarn_->setWordWrap(true);
    rearrangeWarn_->setStyleSheet(QStringLiteral("color:#c0392b;"));
    rearrangeWarn_->setVisible(false);
    rf->addRow(rearrangeWarn_);
    form->addWidget(rearrangeRow_);

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
    // 重排参数：列/行共用一个槽、单元宽/高共用一个槽（信号多出的 int 参数自动丢弃）。
    connect(mergeCols_, &QSpinBox::valueChanged, this, &ExportPanel::onMergeGridEdited);
    connect(mergeRows_, &QSpinBox::valueChanged, this, &ExportPanel::onMergeGridEdited);
    connect(mergeCellW_, &QSpinBox::valueChanged, this, &ExportPanel::onMergeCellEdited);
    connect(mergeCellH_, &QSpinBox::valueChanged, this, &ExportPanel::onMergeCellEdited);
    connect(padColorBtn_, &QPushButton::clicked, this, &ExportPanel::onPadColorClicked);
    connect(autoGridBtn_, &QPushButton::clicked, this, &ExportPanel::onAutoGridClicked);
    connect(mergeSortCombo_, &QComboBox::currentIndexChanged, this, &ExportPanel::onMergeSortChanged);
    connect(mergeSnake_, &QCheckBox::toggled, this, &ExportPanel::onMergeSnakeToggled);
    connect(mergeReverse_, &QCheckBox::toggled, this, &ExportPanel::onMergeReverseToggled);
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

    // 重排参数回填（blockSignals 防回环）。
    for (QSpinBox* s : {mergeCols_, mergeRows_, mergeCellW_, mergeCellH_}) s->blockSignals(true);
    mergeCols_->setValue(doc_->mergeCols());
    mergeRows_->setValue(doc_->mergeRows());
    mergeCellW_->setValue(doc_->mergeCellW());
    mergeCellH_->setValue(doc_->mergeCellH());
    for (QSpinBox* s : {mergeCols_, mergeRows_, mergeCellW_, mergeCellH_}) s->blockSignals(false);
    updatePadColorSwatch();

    // 重排填充顺序回填（MergeOrder；blockSignals 防回环）。
    mergeSortCombo_->blockSignals(true);
    mergeSortCombo_->setCurrentIndex(doc_->mergeOrder().strategy == idc::engine::SortStrategy::COLUMN_MAJOR ? 1 : 0);
    mergeSortCombo_->blockSignals(false);
    mergeSnake_->blockSignals(true);
    mergeSnake_->setChecked(doc_->mergeOrder().snake);
    mergeSnake_->blockSignals(false);
    mergeReverse_->blockSignals(true);
    mergeReverse_->setChecked(doc_->mergeOrder().reverse);
    mergeReverse_->blockSignals(false);

    updateFieldVisibility();
    refreshModeItemStates();   // 依模式(L3?)/坍缩可行性刷新各输出模式项可用性。
    updateRearrangeWarning();  // 重算内联警告。
    const bool lossy = (doc_->format() == idc::engine::ExportFormat::JPEG ||
                        doc_->format() == idc::engine::ExportFormat::WEBP);
    qualityLabel_->setVisible(lossy);
    quality_->setVisible(lossy);
}

// 依 Core 坍缩可行性与 Document 模式刷新模式项：不可行且当前选中坍缩时回退。
void ExportPanel::setCollapsible(const bool collapsible) {
    collapsible_ = collapsible;
    refreshModeItemStates();
    // 当前选中「坍缩」但不可坍缩：回退到重排（仅 L3 可用），否则退回分离导出。
    if (!collapsible && modeCombo_->currentIndex() == 1) {
        const bool isL3 = doc_ && doc_->mode() == idc::engine::Tier::L3;
        modeCombo_->setCurrentIndex(isL3 ? 2 : 0); // 触发 onModeChanged → 写回 Document。
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
    // 进入重排：若用户未手动指定 cols/rows，依保留块数开方自动填入具体值。
    if (index == 2 && !mergeGridTouched_) applyAutoGrid();
    updateFieldVisibility();
    updateRearrangeWarning();
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

// 重排列数/行数变更 → 标记手动 + 写回 Document（0=自动，交 Core 推导）+ 重算警告。
void ExportPanel::onMergeGridEdited() {
    mergeGridTouched_ = true;   // 用户手动改过 → 停止自动填充。
    if (doc_) doc_->setMergeGrid(mergeCols_->value(), mergeRows_->value());
    updateRearrangeWarning();
}

// 重排单元宽/高变更 → 写回 Document（0=用保留块原尺寸）+ 重算警告。
void ExportPanel::onMergeCellEdited() {
    if (doc_) doc_->setMergeCellSize(mergeCellW_->value(), mergeCellH_->value());
    updateRearrangeWarning();
}

// 点击填充色按钮 → 弹带 Alpha 的取色对话框，写回 Document 并刷新色块。
// 说明：因需 Alpha 通道，Qt 无法使用原生取色框而回退到自带对话框（固定尺寸）；
//       若以这个很窄的右侧面板为父，Windows 下会以面板尺寸推导初始 geometry
//       再被强制夹到固定最小尺寸，刷出大量 "QWindowsWindow::setGeometry: Unable to
//       set geometry" 噪声告警。改以顶层窗口为父 + 显式 DontUseNativeDialog 消除。
void ExportPanel::onPadColorClicked() {
    if (!doc_) return;
    const idc::core::Color cur = doc_->padColor();
    const QColor init(cur.r, cur.g, cur.b, cur.a);
    QColorDialog dlg(init, window());                 // 以顶层窗口为父，避免以窄面板推导初始尺寸。
    dlg.setWindowTitle(QStringLiteral("选择填充色"));
    dlg.setOption(QColorDialog::ShowAlphaChannel, true);
    dlg.setOption(QColorDialog::DontUseNativeDialog, true); // Alpha 需求下本就走非原生，显式声明意图。
    if (dlg.exec() != QDialog::Accepted) return;      // 用户取消。
    const QColor picked = dlg.currentColor();
    doc_->setPadColor(idc::core::Color(static_cast<std::uint8_t>(picked.red()),
                                       static_cast<std::uint8_t>(picked.green()),
                                       static_cast<std::uint8_t>(picked.blue()),
                                       static_cast<std::uint8_t>(picked.alpha())));
    updatePadColorSwatch();
}

// 依 Document 的 padColor 更新填充色按钮背景色块（含 Alpha 预览文案）。
void ExportPanel::updatePadColorSwatch() {
    if (!padColorBtn_ || !doc_) return;
    const idc::core::Color c = doc_->padColor();
    padColorBtn_->setStyleSheet(QStringLiteral(
        "QPushButton{background:rgba(%1,%2,%3,%4);border:1px solid #999;border-radius:4px;min-height:20px;}")
        .arg(c.r).arg(c.g).arg(c.b).arg(c.a));
    padColorBtn_->setText(c.a == 0 ? QStringLiteral("透明")
                                   : QStringLiteral("RGBA(%1,%2,%3,%4)").arg(c.r).arg(c.g).arg(c.b).arg(c.a));
}

// 依当前输出模式切换目录/文件/命名/重排行显隐。
void ExportPanel::updateFieldVisibility() {
    const int idx = modeCombo_->currentIndex();
    const bool separate = (idx == 0);
    dirRow_->setVisible(separate);
    namingRow_->setVisible(separate);
    fileRow_->setVisible(!separate);
    rearrangeRow_->setVisible(idx == 2); // 仅「合并重排」显示重排参数。
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

// 依 Core 坍缩可行性与 Document 模式，刷新「合并坍缩」「合并重排」两项的可用性：
// 坍缩项依 collapsible_；重排项仅 L3 可用（Core runEngine 硬约束，非 L3 重排会报错）。
void ExportPanel::refreshModeItemStates() {
    auto* m = qobject_cast<QStandardItemModel*>(modeCombo_->model());
    if (!m) return;
    const bool isL3 = doc_ && doc_->mode() == idc::engine::Tier::L3;
    if (QStandardItem* it = m->item(1)) it->setEnabled(collapsible_);
    if (QStandardItem* it = m->item(2)) it->setEnabled(isL3);
}

// 回灌重排上下文（MainWindow 依 Core 引擎结果调用）：保留块数 + 网格单元尺寸。
void ExportPanel::setRearrangeContext(const int keptCount, const int cellW, const int cellH) {
    keptCount_ = keptCount < 0 ? 0 : keptCount;
    cellW_ = cellW < 0 ? 0 : cellW;
    cellH_ = cellH < 0 ? 0 : cellH;
    // 处于重排模式且用户未手动指定 cols/rows 时，依新的保留块数自动开方填入。
    if (modeCombo_->currentIndex() == 2 && !mergeGridTouched_) applyAutoGrid();
    updateRearrangeWarning();
}

// 「按格数自动」按钮：清除手动标记并按格数开方重算 cols/rows。
void ExportPanel::onAutoGridClicked() {
    mergeGridTouched_ = false;
    applyAutoGrid();
    updateRearrangeWarning();
}

// 依保留块数 n 计算 cols=ceil(sqrt(n))、rows=ceil(n/cols) 并填入 spinbox（写回 Document）。
void ExportPanel::applyAutoGrid() {
    const int n = keptCount_;
    if (n <= 0) return;                       // 无保留块：不覆盖用户设置。
    int cols = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(n))));
    if (cols < 1) cols = 1;
    int rows = (n + cols - 1) / cols;         // ceil(n/cols)
    if (rows < 1) rows = 1;
    for (QSpinBox* s : {mergeCols_, mergeRows_}) s->blockSignals(true);
    mergeCols_->setValue(cols);
    mergeRows_->setValue(rows);
    for (QSpinBox* s : {mergeCols_, mergeRows_}) s->blockSignals(false);
    if (doc_) doc_->setMergeGrid(cols, rows); // 写回（值不变时 setMergeGrid 早退，不会无限回环）。
}

// 重排填充顺序：行/列优先（与 L3 选择排序正交）。
void ExportPanel::onMergeSortChanged(const int index) {
    if (doc_) doc_->setMergeOrderStrategy(index == 1 ? idc::engine::SortStrategy::COLUMN_MAJOR
                                                     : idc::engine::SortStrategy::ROW_MAJOR);
}

// 重排填充：蛇形。
void ExportPanel::onMergeSnakeToggled(const bool on) {
    if (doc_) doc_->setMergeOrderSnake(on);
}

// 重排填充：倒序。
void ExportPanel::onMergeReverseToggled(const bool on) {
    if (doc_) doc_->setMergeOrderReverse(on);
}

// 生成当前重排参数下的警告文本（无警告返回空串）：
//   ① cols*rows < 保留块数：多出的块无处安放（Core 会扩行，但与用户显式设定不符）；
//   ② cw/ch > 0 且 < 网格单元尺寸：块按自身尺寸落位会溢出格子、相互覆盖或越界被裁剪。
QString ExportPanel::rearrangeWarning() const {
    if (modeCombo_->currentIndex() != 2) return QString();  // 非重排模式无警告。
    QStringList msgs;
    const int cols = mergeCols_->value();
    const int rows = mergeRows_->value();
    if (keptCount_ > 0 && cols > 0 && rows > 0 && cols * rows < keptCount_) {
        msgs << QStringLiteral("画布 %1×%2=%3 格 < 保留块数 %4，多出的块将被丢弃或覆盖。")
                    .arg(cols).arg(rows).arg(cols * rows).arg(keptCount_);
    }
    const int cw = mergeCellW_->value();
    const int ch = mergeCellH_->value();
    if (cw > 0 && cellW_ > 0 && cw < cellW_) {
        msgs << QStringLiteral("单元宽 %1 < 网格单元宽 %2，块会溢出格子并被相邻块覆盖/裁剪。")
                    .arg(cw).arg(cellW_);
    }
    if (ch > 0 && cellH_ > 0 && ch < cellH_) {
        msgs << QStringLiteral("单元高 %1 < 网格单元高 %2，块会溢出格子并被相邻块覆盖/裁剪。")
                    .arg(ch).arg(cellH_);
    }
    return msgs.join(QStringLiteral("\n"));
}

// 重算并显示内联警告（红字）。
void ExportPanel::updateRearrangeWarning() {
    if (!rearrangeWarn_) return;
    const QString w = rearrangeWarning();
    rearrangeWarn_->setText(w.isEmpty() ? QString() : QStringLiteral("警告：") + w);
    rearrangeWarn_->setVisible(!w.isEmpty());
}

} // namespace idc::gui
