// ============================================================================
// 文件：panels/export_panel_output.cpp
// 作用：ExportPanel 的输出方式 / 格式 / 质量 / 命名 / 路径簇实现——外部选择入口
//       （selectExportMode/selectFormat，文件菜单「导出设置」与用户点 combo 同一路径）、
//       槽写回 Document、字段显隐与保存过滤器、模式项可用性刷新。
// 分块依据：见 export_panel.cpp 头注（合并重排簇在 export_panel_merge.cpp、输出方式簇在本 TU、
//       装配与同步在主文件）；本 TU 只写回 Document 真相源与刷新既有控件，不建任何控件。
// ============================================================================
#include "panels/export_panel.h"

#include <QFileDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QStandardItemModel>

#include "model/document.h"   // Document（ExportMode / MergeLayout / ExportFormat 写回真相源）
#include "i18n/gui_texts.h"
#include "i18n/gui_i18n.h"    // IDC_GTR（目录/文件对话框标题与过滤器文案）

namespace idc::gui {

// 输出模式变更 → 写回 Document + 切换字段可见性。
// 外部（文件菜单「导出设置」）选输出方式：与用户点 combo 同一路径（含重排 auto-grid 与警告刷新）。
void ExportPanel::selectExportMode(const int index) const {
    if (index < 0 || index >= modeCombo_->count()) return;
    if (modeCombo_->currentIndex() != index) modeCombo_->setCurrentIndex(index); // 触发 onModeChanged
}

// 外部（文件菜单「导出设置」）选格式：与用户点 combo 同一路径。
void ExportPanel::selectFormat(const int index) const {
    if (index < 0 || index >= formatCombo_->count()) return;
    if (formatCombo_->currentIndex() != index) formatCombo_->setCurrentIndex(index); // 触发 onFormatChanged
}

void ExportPanel::onModeChanged(const int index) {
    if (!doc_) return;
    switch (index) {
        case 0: doc_->setExport(engine::ExportMode::Separate, engine::MergeLayout::Collapse); break;
        case 1: doc_->setExport(engine::ExportMode::Merged, engine::MergeLayout::Collapse); break;
        case 2: doc_->setExport(engine::ExportMode::Merged, engine::MergeLayout::Rearrange); break;
        default: break;
    }
    // 进入重排：若用户未手动指定 cols/rows，依保留块数开方自动填入具体值。
    if (index == 2 && !mergeGridTouched_) applyAutoGrid();
    updateFieldVisibility();
    updateRearrangeWarning();
}

// 格式变更 → 写回 Document + 质量字段显隐。
void ExportPanel::onFormatChanged(const int index) const {
    if (!doc_) return;
    auto f = engine::ExportFormat::Png;
    switch (index) {
        case 0: f = engine::ExportFormat::Png; break;
        case 1: f = engine::ExportFormat::Jpeg; break;
        case 2: f = engine::ExportFormat::Webp; break;
        case 3: f = engine::ExportFormat::Bmp; break;
        default: break;
    }
    doc_->setFormat(f);
    const bool lossy = f == engine::ExportFormat::Jpeg || f == engine::ExportFormat::Webp;
    qualityLabel_->setVisible(lossy);
    quality_->setVisible(lossy);
}

// 质量变更 → 写回 Document。
void ExportPanel::onQualityChanged(const int value) const {
    if (doc_) doc_->setQuality(value);
}

// 命名模板提交 → 写回 Document。
void ExportPanel::onNamingEdited() const {
    if (doc_) doc_->setNaming(naming_->text());
}

// 目录手动输入提交 → 写回 Document（与「浏览…」等价，允许直接键入路径）。
void ExportPanel::onDirEdited() const {
    if (doc_) doc_->setOutputDir(dirEdit_->text().trimmed());
}

// 文件手动输入提交 → 写回 Document（与「浏览…」等价，允许直接键入路径）。
void ExportPanel::onFileEdited() const {
    if (doc_) doc_->setOutputFile(fileEdit_->text().trimmed());
}

// 选择输出目录（分离导出）。
void ExportPanel::onBrowseDir() {
    const QString d = QFileDialog::getExistingDirectory(this, IDC_GTR(label, kDlgTitleChooseOutDir),
                                                        dirEdit_->text());
    if (!d.isEmpty()) {
        dirEdit_->setText(d);
        if (doc_) doc_->setOutputDir(d);
    }
}

// 选择输出文件（合并导出）。
void ExportPanel::onBrowseFile() {
    const QString f = QFileDialog::getSaveFileName(this, IDC_GTR(label, kDlgTitleExportAs),
                                                   fileEdit_->text(), saveFilter());
    if (!f.isEmpty()) {
        fileEdit_->setText(f);
        if (doc_) doc_->setOutputFile(f);
    }
}

// 依当前输出模式切换目录/文件/命名/重排行显隐。
void ExportPanel::updateFieldVisibility() const {
    const int idx = modeCombo_->currentIndex();
    const bool separate = idx == 0;
#ifdef Q_OS_ANDROID
    // 移动端目录固定为相册公共子目录（「浏览…」已禁用），目录行无意义：隐藏，
    // 分离模式只留命名模板行（SPEC §8.3 形态差异）。
    dirRow_->setVisible(false);
#else
    dirRow_->setVisible(separate);
#endif
    namingRow_->setVisible(separate);
    fileRow_->setVisible(!separate);
    // 填充色行三模式恒可见：合并画布底色 / Pad 补白 / 有损压平共用该值；分离导出仅在没有
    // Pad 残缺单元时无从体现（完整单元无空位），但隐藏整行会让人误以为参数丢失。
    rearrangeRow_->setVisible(idx == 2); // 仅「合并重排」显示重排参数。
}

// 依当前格式返回保存对话框过滤器。
QString ExportPanel::saveFilter() const {
    if (!doc_) return IDC_GTR(label, kFilterAll);
    switch (doc_->format()) {
        case engine::ExportFormat::Png:  return IDC_GTR(label, kFilterSavePng);
        case engine::ExportFormat::Jpeg: return IDC_GTR(label, kFilterSaveJpeg);
        case engine::ExportFormat::Webp: return IDC_GTR(label, kFilterSaveWebp);
        case engine::ExportFormat::Bmp:  return IDC_GTR(label, kFilterSaveBmp);
    }
    return IDC_GTR(label, kFilterAll);
}

// 依 Core 坍缩可行性与 Document 模式，刷新「合并坍缩」「合并重排」两项的可用性：
// 坍缩项依 collapsible_；重排项依 rearrangeAvailable（L3 且保留块长宽全等，Core runEngine 硬约束）。
void ExportPanel::refreshModeItemStates() const {
    const auto* m = qobject_cast<QStandardItemModel*>(modeCombo_->model());
    if (!m) return;
    if (QStandardItem* it = m->item(1)) it->setEnabled(collapsible_);
    if (QStandardItem* it = m->item(2)) it->setEnabled(rearrangeAvailable());
}

// 重排项可用性：仅 L3 且保留块长宽全等。面板置灰与菜单「导出设置·重排」共用同一判据。
bool ExportPanel::rearrangeAvailable() const {
    return doc_ && doc_->mode() == engine::Tier::Grid && uniformBlocks_;
}

} // namespace idc::gui
