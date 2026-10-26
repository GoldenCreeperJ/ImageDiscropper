// ============================================================================
// 文件：panels/export_panel.cpp
// 作用：实现导出面板的构建、Document 同步、坍缩可行性联动与烧录开关入口（见头文件说明）。
// 分块依据：按簇拆三个 TU——本文件（构建/同步/坍缩联动/烧录开关）、export_panel_merge.cpp
//           （合并重排参数与警告）、export_panel_output.cpp（输出方式/格式/质量/命名/路径簇）；
//           参数写回与导出动作（exportRequested 交上层）分离；目录/文件/命名行随输出模式显隐，
//           避免无关字段干扰。
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
#include <QPixmap>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QStringList>

#include "engine/engine.h"
#include "model/document.h"
#include "util/coord_spin.h"
#include "util/gui_nums.h"
#include "util/widget_factory.h"
#include "i18n/gui_texts.h"
#include "i18n/gui_i18n.h"

namespace idc::gui {

// 构建导出面板布局。
ExportPanel::ExportPanel(QWidget* parent) : QWidget(parent) {
    auto* root = makePanelLayout(this);
    root->setSpacing(nums::panels::kPanelSpacingPx);

    auto* box = makeGroupBox(retranslator_, this, IDC_GT(label, kExportBoxTitle));
    auto* form = new QVBoxLayout(box);
    form->setSpacing(nums::panels::kPanelSpacingPx);

    // 输出模式。
    auto* outModeLbl = new QLabel(box);
    i18n::bindText(retranslator_, outModeLbl, IDC_GT(label, kOutModeLabel));
    form->addWidget(outModeLbl);
    modeCombo_ = new QComboBox(box);
    i18n::addItemsI18n(retranslator_, modeCombo_, {IDC_GT(label, kExportSeparateItem), IDC_GT(label, kExportCollapseItem),
                                                   IDC_GT(label, kExportRearrangeItem)});
    i18n::bindToolTip(retranslator_, modeCombo_, IDC_GT(tooltip, kExportModeCombo));
    form->addWidget(modeCombo_);

    // 目录行（分离导出）。
    dirRow_ = new QWidget(box);
    auto* dh = new QHBoxLayout(dirRow_);
    dh->setContentsMargins(0, 0, 0, 0);
    dirEdit_ = new QLineEdit(dirRow_);
    i18n::bindPlaceholder(retranslator_, dirEdit_, IDC_GT(label, kPhOutDir));
    dirBtn_ = makeButton(retranslator_, dirRow_, IDC_GT(label, kBrowseButton));
    auto* outDirLbl = new QLabel(dirRow_);
    i18n::bindText(retranslator_, outDirLbl, IDC_GT(label, kOutDirLabel));
    dh->addWidget(outDirLbl);
    dh->addWidget(dirEdit_, 1);
    dh->addWidget(dirBtn_);
    form->addWidget(dirRow_);

    // 命名行（分离导出）。
    namingRow_ = new QWidget(box);
    auto* nh = new QHBoxLayout(namingRow_);
    nh->setContentsMargins(0, 0, 0, 0);
    namingLabel_ = new QLabel(namingRow_);
    i18n::bindText(retranslator_, namingLabel_, IDC_GT(label, kNamingLabel));
    naming_ = new QLineEdit(QString::fromUtf8(texts::ident::kNamingDefaultTemplate), namingRow_);
    i18n::bindToolTip(retranslator_, naming_, IDC_GT(tooltip, kNamingTemplate));
    nh->addWidget(namingLabel_);
    nh->addWidget(naming_, 1);
    form->addWidget(namingRow_);

    // 文件行（合并导出）。
    fileRow_ = new QWidget(box);
    auto* fh = new QHBoxLayout(fileRow_);
    fh->setContentsMargins(0, 0, 0, 0);
    fileEdit_ = new QLineEdit(fileRow_);
    fileBtn_ = makeButton(retranslator_, fileRow_, IDC_GT(label, kBrowseButton));
    auto* outFileLbl = new QLabel(fileRow_);
    i18n::bindText(retranslator_, outFileLbl, IDC_GT(label, kOutFileLabel));
    fh->addWidget(outFileLbl);
    fh->addWidget(fileEdit_, 1);
    fh->addWidget(fileBtn_);
    form->addWidget(fileRow_);

    // 填充色行：合并画布底色（重排空位、坍缩下的越界部分）、Pad 余量策略下的越界单元补白
    // （分离与合并导出同一语义，见 Core exportSeparate）、有损格式（JPEG/WebP/BMP）压平透明
    // 像素的背景色。三模式恒可见——切换模式时整行消失会让人误以为参数丢失。
    padRow_ = new QWidget(box);
    auto* ph = new QHBoxLayout(padRow_);
    ph->setContentsMargins(0, 0, 0, 0);
    auto* padLbl = new QLabel(padRow_);
    i18n::bindText(retranslator_, padLbl, IDC_GT(label, kPadColorLabel));
    padColorBtn_ = new QPushButton(padRow_);
    i18n::bindToolTip(retranslator_, padColorBtn_, IDC_GT(tooltip, kPadColor));
    ph->addWidget(padLbl);
    ph->addWidget(padColorBtn_, 1);
    form->addWidget(padRow_);

    // 移动端禁用「浏览…」（SPEC §8.3 形态差异）：SAF 自定义目录真机不可靠——文件管理器
    // 建的目录 other 无权限、华为 Downloads 提供者拒绝 open/delete/查询，自定义路径无法
    // 保证写入（详见 mobile/android_media_store 与 src/mobile README「移动端实现状态与
    // 遗留事项」）；移动端统一导出到相册公共目录（ExportController::onExport 编排），留空即可。
    // 目录选择无 SAF 适配（打开图像的 content:// 入缓存分支不受影响）。
    // 输入框仍可手输绝对路径（应用可写目录）作为高级用法。
#ifdef Q_OS_ANDROID
    dirBtn_->setEnabled(false);
    fileBtn_->setEnabled(false);
    i18n::bindPlaceholder(retranslator_, fileEdit_, IDC_GT(label, kPhOutFileAndroid));
#else
    i18n::bindPlaceholder(retranslator_, fileEdit_, IDC_GT(label, kPhOutFile));
#endif

    // 合并重排参数行（FR-L3.7）：仅「合并重排」模式显示。
    // 列/行/单元尺寸以 0 表示「自动」（setSpecialValueText 显示为“自动”），交 Core compose 推导。
    rearrangeRow_ = new QWidget(box);
    auto* rf = new QFormLayout(rearrangeRow_);
    rf->setContentsMargins(0, 0, 0, 0);
    rf->setSpacing(nums::panels::kPanelTightSpacingPx);
    mergeCols_ = new QSpinBox(rearrangeRow_);
    mergeRows_ = new QSpinBox(rearrangeRow_);
    mergeCellW_ = new CoordSpinBox(rearrangeRow_);
    mergeCellH_ = new CoordSpinBox(rearrangeRow_);
    for (QSpinBox* s : {mergeCols_, mergeRows_, static_cast<QSpinBox*>(mergeCellW_),
                        static_cast<QSpinBox*>(mergeCellH_)}) {
        s->setRange(0, nums::panels::kMergeValueLimitPx);
        s->setValue(0);
        s->setKeyboardTracking(false);          // 提交才触发，避免逐键刷新。
        i18n::bindSpecialValue(retranslator_, s, IDC_GT(label, kAutoSpecialValue)); // 最小值 0 显示为「自动」。
    }
    i18n::bindToolTip(retranslator_, mergeCols_, IDC_GT(tooltip, kMergeCols));
    i18n::bindToolTip(retranslator_, mergeRows_, IDC_GT(tooltip, kMergeRows));
    i18n::bindToolTip(retranslator_, mergeCellW_, IDC_GT(tooltip, kMergeCellW));
    i18n::bindToolTip(retranslator_, mergeCellH_, IDC_GT(tooltip, kMergeCellH));
    // 单元宽/高另接受百分比串（SPEC §2.1）：基准＝画布宽/高（§7 export.merge 以画布为基准）；
    // 尚无画布时退回工作图尺寸，均无图则基准 <= 0、只认整数像素。“0%”换算为 0，即“自动”。
    const auto mergeCellBasis = [this](const bool horizontal) {
        if (!doc_) return 0;
        if (doc_->canvasWidth() > 0 && doc_->canvasHeight() > 0)
            return horizontal ? doc_->canvasWidth() : doc_->canvasHeight();
        return horizontal ? doc_->width() : doc_->height();
    };
    mergeCellW_->setBasisProvider([mergeCellBasis] { return mergeCellBasis(true); });
    mergeCellH_->setBasisProvider([mergeCellBasis] { return mergeCellBasis(false); });
    i18n::bindFormRow(retranslator_, rf, mergeCols_, IDC_GT(label, kMergeColsLabel));
    i18n::bindFormRow(retranslator_, rf, mergeRows_, IDC_GT(label, kMergeRowsLabel));
    autoGridBtn_ = makeButton(retranslator_, rearrangeRow_, IDC_GT(label, kAutoGridButton));
    i18n::bindToolTip(retranslator_, autoGridBtn_, IDC_GT(tooltip, kAutoGrid));
    rf->addRow(QString(), autoGridBtn_);
    i18n::bindFormRow(retranslator_, rf, mergeCellW_, IDC_GT(label, kMergeCellWLabel));
    i18n::bindFormRow(retranslator_, rf, mergeCellH_, IDC_GT(label, kMergeCellHLabel));
    // 重排填充顺序（MergeOrder）：与 L3 选择排序正交——决定块列表以何种路径铺进 cols×rows 画布。
    mergeSortCombo_ = new QComboBox(rearrangeRow_);
    i18n::addItemsI18n(retranslator_, mergeSortCombo_, {IDC_GT(label, kMergeRowMajor), IDC_GT(label, kMergeColMajor)});
    i18n::bindToolTip(retranslator_, mergeSortCombo_, IDC_GT(tooltip, kMergeOrder));
    mergeSnake_ = makeCheckBox(retranslator_, rearrangeRow_, IDC_GT(label, kMergeSnakeLabel));
    i18n::bindToolTip(retranslator_, mergeSnake_, IDC_GT(tooltip, kMergeSnake));
    mergeReverse_ = makeCheckBox(retranslator_, rearrangeRow_, IDC_GT(label, kMergeReverseLabel));
    i18n::bindToolTip(retranslator_, mergeReverse_, IDC_GT(tooltip, kMergeReverse));
    auto* orderRow = new QWidget(rearrangeRow_);
    auto* oh = new QHBoxLayout(orderRow);
    oh->setContentsMargins(0, 0, 0, 0);
    oh->addWidget(mergeSortCombo_);
    oh->addWidget(mergeSnake_);
    oh->addWidget(mergeReverse_);
    oh->addStretch(1);
    i18n::bindFormRow(retranslator_, rf, orderRow, IDC_GT(label, kMergeOrderLabel));
    // 内联警告（红字）：cols*rows < 保留块数 或 cw/ch < 网格单元尺寸时提示（导出前另弹窗确认）。
    rearrangeWarn_ = new QLabel(rearrangeRow_);
    rearrangeWarn_->setWordWrap(true);
    rearrangeWarn_->setStyleSheet(QStringLiteral("color:#c0392b;"));
    rearrangeWarn_->setVisible(false);
    rf->addRow(rearrangeWarn_);
    form->addWidget(rearrangeRow_);

    // 格式。
    auto* formatLbl = new QLabel(box);
    i18n::bindText(retranslator_, formatLbl, IDC_GT(label, kFormatLabel));
    form->addWidget(formatLbl);
    formatCombo_ = new QComboBox(box);
    formatCombo_->addItems({QStringLiteral("PNG"), QStringLiteral("JPEG"),
                            QStringLiteral("WebP"), QStringLiteral("BMP")});
    form->addWidget(formatCombo_);

    // 质量（仅有损格式）。
    qualityLabel_ = new QLabel(box);
    i18n::bindText(retranslator_, qualityLabel_, IDC_GT(label, kQualityLabel));
    form->addWidget(qualityLabel_);
    quality_ = makeSpin(box, nums::panels::kQualityMinPercent, nums::panels::kQualityMaxPercent);
    quality_->setValue(nums::panels::kQualityDefaultPercent);
    form->addWidget(quality_);

    root->addWidget(box);

    // 输出图像预览（导出前预览）：固定尺寸的缩略图标签，由 PreviewController 按 Core Composition 回灌。
    // 固定尺寸使 setPreviewPixmap 能依确定的框内缩放（不随布局时序变化），且不撑宽右侧面板。
    auto* outPreviewLbl = new QLabel(this);
    i18n::bindText(retranslator_, outPreviewLbl, IDC_GT(label, kOutputPreviewLabel));
    root->addWidget(outPreviewLbl);
    previewLabel_ = new QLabel(IDC_GTR(label, kNoPreviewThumb), this);   // 占位文案：有有图时由 setPreviewPixmap 回灌，不绑
    previewLabel_->setFixedSize(nums::panels::kExportPreviewWidthPx, nums::panels::kExportPreviewHeightPx);
    previewLabel_->setAlignment(Qt::AlignCenter);
    // 预览框：作为有意为之的深色「图片卡纸」，底固定 #2b2b2b、占位文字固定浅色 #bbb（两主题下均靠此
    // 浅字 / 深底对比保证可读，不随主题走——若改 palette 会在深色下变成深字配深底不可读）；仅边框随主题。
    previewLabel_->setStyleSheet(QStringLiteral(
        "QLabel{background:#2b2b2b;border:1px solid palette(mid);border-radius:4px;color:#bbb;}"));
    i18n::bindToolTip(retranslator_, previewLabel_, IDC_GT(tooltip, kPreviewThumb));
    root->addWidget(previewLabel_);

    // 导出前文字信息（与缩略图并存：画布尺寸/保留块数）。
    infoLabel_ = new QLabel(IDC_GTR(label, kNoPreviewInfo), this);   // 动态：setPreviewInfo 回灌，不绑
    infoLabel_->setWordWrap(true);
    root->addWidget(infoLabel_);

    // 导出时烧录标注：勾选后导出把标注合成进像素（随像素一起被切割）。
    burnIn_ = makeCheckBox(retranslator_, this, IDC_GT(label, kBurnInCheck));
    burnIn_->setChecked(false);
    i18n::bindToolTip(retranslator_, burnIn_, IDC_GT(tooltip, kBurnInHint));

    // 导出时烧录图片图层（对标「导出时烧录标注」）：勾选后导出把叠加图片图层并入像素（随图像一起被切割）。
    // 与图层面板的「显示」开关正交——后者仅控画布预览，本开关只影响导出结果。默认勾选（保持“导出含图层”的现有行为）。
    layersBurnIn_ = makeCheckBox(retranslator_, this, IDC_GT(label, kLayersBurnInCheck));
    layersBurnIn_->setChecked(true);
    i18n::bindToolTip(retranslator_, layersBurnIn_, IDC_GT(tooltip, kLayersBurnInHint));
    // 两个烧录开关并排 1×2（各自信号绑定不动）。
    auto* burnRow = new QHBoxLayout();
    burnRow->setSpacing(nums::panels::kPanelSpacingPx);
    burnRow->addWidget(burnIn_);
    burnRow->addWidget(layersBurnIn_);
    root->addLayout(burnRow);

    // 导出按钮。
    exportBtn_ = makeButton(retranslator_, this, IDC_GT(label, kExportBoxTitle));
    i18n::bindToolTip(retranslator_, exportBtn_, IDC_GT(tooltip, kExportButton));
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
    connect(burnIn_, &QCheckBox::toggled, this, &ExportPanel::burnInChanged);
    connect(layersBurnIn_, &QCheckBox::toggled, this, &ExportPanel::layersBurnInChanged);
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

    // 初次布局按默认模式收敛显隐（构造时 combo 停在“分离导出”，绑定 Document 前也保持一致）。
    updateFieldVisibility();
}

// 绑定 Document 并同步一次。
void ExportPanel::setDocument(Document* doc) {
    doc_ = doc;
    syncFromDocument();
}

// 从 Document 反向同步控件与字段可见性。
void ExportPanel::syncFromDocument() {
    if (!doc_) return;

    // 输出模式：Separate→0；Merged+Collapse→1；Merged+Rearrange→2。
    int idx = 0;
    if (doc_->exportMode() == engine::ExportMode::Merged) {
        idx = doc_->layout() == engine::MergeLayout::Collapse ? 1 : 2;
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
    for (QSpinBox* s : {mergeCols_, mergeRows_, static_cast<QSpinBox*>(mergeCellW_),
                        static_cast<QSpinBox*>(mergeCellH_)})
        s->blockSignals(true);
    mergeCols_->setValue(doc_->mergeCols());
    mergeRows_->setValue(doc_->mergeRows());
    mergeCellW_->setValue(doc_->mergeCellW());
    mergeCellH_->setValue(doc_->mergeCellH());
    for (QSpinBox* s : {mergeCols_, mergeRows_, static_cast<QSpinBox*>(mergeCellW_),
                        static_cast<QSpinBox*>(mergeCellH_)})
        s->blockSignals(false);
    updatePadColorSwatch();

    // 重排填充顺序回填（MergeOrder；blockSignals 防回环）。
    mergeSortCombo_->blockSignals(true);
    mergeSortCombo_->setCurrentIndex(doc_->mergeOrder().strategy == engine::SortStrategy::ColumnMajor ? 1 : 0);
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
    const bool lossy = doc_->format() == engine::ExportFormat::Jpeg ||
                       doc_->format() == engine::ExportFormat::Webp;
    qualityLabel_->setVisible(lossy);
    quality_->setVisible(lossy);
}

// 依 Core 坍缩可行性与 Document 模式刷新模式项：不可行且当前选中坍缩时回退。
void ExportPanel::setCollapsible(const bool collapsible) {
    if (collapsible_ == collapsible) { refreshModeItemStates(); return; }
    collapsible_ = collapsible;
    emit collapsibleChanged(collapsible);
    refreshModeItemStates();
    // 当前选中「坍缩」但不可坍缩：回退到重排（仅 L3 且块尺寸全等时可用），否则退回分离导出。
    if (!collapsible && modeCombo_->currentIndex() == 1) {
        const bool isL3 = doc_ && doc_->mode() == engine::Tier::Grid;
        modeCombo_->setCurrentIndex(isL3 && uniformBlocks_ ? 2 : 0); // 触发 onModeChanged → 写回 Document。
    }
}

// 更新导出前信息文字。
void ExportPanel::setPreviewInfo(const QString& text) const {
    if (infoLabel_) infoLabel_->setText(text);
}

// 显示输出图像预览缩略图（导出前预览）：等比缩到标签框内（仅缩小、不放大，避免小图被拉伸模糊）。
// 传入空 pixmap（无有效输出/无图像）时清空并回退到占位文案。
void ExportPanel::setPreviewPixmap(const QPixmap& pm) const {
    if (!previewLabel_) return;
    if (pm.isNull()) {
        previewLabel_->setPixmap(QPixmap());
        previewLabel_->setText(IDC_GTR(label, kNoPreviewThumb));
        return;
    }
    // 预留 4px 内边距，避免缩略图压到边框。
    const int boxW = previewLabel_->width() - 4;
    const int boxH = previewLabel_->height() - 4;
    QPixmap shown = pm;
    if (pm.width() > boxW || pm.height() > boxH) {
        shown = pm.scaled(boxW, boxH, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    previewLabel_->setText(QString());
    previewLabel_->setPixmap(shown);
}

// 反向同步「导出时烧录标注」复选框（AnnotationCoordinator 依 AnnotationBridge::burnInEnabled 回灌）。
// blockSignals 防止回灌时再触发 burnInChanged 造成回环。
void ExportPanel::setBurnInChecked(const bool on) const {
    if (!burnIn_) return;
    burnIn_->blockSignals(true);
    burnIn_->setChecked(on);
    burnIn_->blockSignals(false);
}

// 反向同步「导出时烧录图片图层」复选框（MainWindow 依 Document::layersBurnIn 回灌）。
// blockSignals 防止回灌时再触发 layersBurnInChanged 造成回环。
void ExportPanel::setLayersBurnInChecked(const bool on) const {
    if (!layersBurnIn_) return;
    layersBurnIn_->blockSignals(true);
    layersBurnIn_->setChecked(on);
    layersBurnIn_->blockSignals(false);
}

// 视同用户勾选「导出时烧录标注」：不 blockSignals，仅在未勾时 setChecked(true) 触发
// toggled→burnInChanged，复用既有信号链写回真相源并同步菜单/预览；已勾则不发冗余信号。
void ExportPanel::enableBurnIn() const {
    if (burnIn_ && !burnIn_->isChecked()) burnIn_->setChecked(true);
}

// 视同用户勾选「导出时烧录图片图层」：语义同 enableBurnIn，走 layersBurnInChanged 链。
void ExportPanel::enableLayersBurnIn() const {
    if (layersBurnIn_ && !layersBurnIn_->isChecked()) layersBurnIn_->setChecked(true);
}
} // namespace idc::gui
