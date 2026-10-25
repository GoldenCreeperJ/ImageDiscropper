// ============================================================================
// 文件：panels/export_panel_merge.cpp
// 作用：ExportPanel 的「合并重排」域成员实现（TU 拆分）——重排参数输入区（列/行/单元尺寸/
//       填充色）与重排编排区（上下文回灌/自动行列/警告判定/排序与蛇形逆序）。
//       本 TU 仅按域分拆编译，类不拆。
// ============================================================================
#include "panels/export_panel.h"

#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QStringList>

#include "engine/engine.h"
#include "model/document.h"
#include "util/color_picker.h"
#include "util/coord_spin.h"
#include "i18n/gui_texts.h"
#include "i18n/gui_i18n.h"

namespace idc::gui {

void ExportPanel::onMergeGridEdited() {
    mergeGridTouched_ = true;   // 用户手动改过 → 停止自动填充。
    if (doc_) doc_->setMergeGrid(mergeCols_->value(), mergeRows_->value());
    updateRearrangeWarning();
}

// 重排单元宽/高变更 → 写回 Document（0=用保留块原尺寸）+ 重算警告。
void ExportPanel::onMergeCellEdited() const {
    if (doc_) doc_->setMergeCellSize(mergeCellW_->value(), mergeCellH_->value());
    updateRearrangeWarning();
}

// 点击填充色按钮 → 弹带 Alpha 的取色对话框（非原生框装配与 setGeometry 噪声规避见 util/color_picker.h），
// 写回 Document 并刷新色块。
void ExportPanel::onPadColorClicked() const {
    if (!doc_) return;
    const core::Color cur = doc_->padColor();
    QColor picked(cur.r, cur.g, cur.b, cur.a);   // 当前色作初值传入，确认时就地取回。
    if (!pickColorWithAlpha(window(), picked, IDC_GTR(label, kDlgTitlePickPadColor))) return;   // 取消 / 无效色。
    doc_->setPadColor(core::Color(static_cast<std::uint8_t>(picked.red()),
                                       static_cast<std::uint8_t>(picked.green()),
                                       static_cast<std::uint8_t>(picked.blue()),
                                       static_cast<std::uint8_t>(picked.alpha())));
    updatePadColorSwatch();
}

// 依 Document 的 padColor 更新填充色按钮背景色块（含 Alpha 预览文案）。
void ExportPanel::updatePadColorSwatch() const {
    if (!padColorBtn_ || !doc_) return;
    const core::Color c = doc_->padColor();
    padColorBtn_->setStyleSheet(QStringLiteral(
        "QPushButton{background:rgba(%1,%2,%3,%4);border:1px solid palette(mid);border-radius:4px;min-height:20px;}")
        .arg(c.r).arg(c.g).arg(c.b).arg(c.a));
    padColorBtn_->setText(c.a == 0 ? IDC_GTR(label, kPadColorTransparent)
                                   : IDC_GTR(status, kSwatchColorText).arg(c.r).arg(c.g).arg(c.b).arg(c.a));
}


// 回灌重排上下文（PreviewController 依 Core 引擎结果调用）：保留块数 + 网格单元尺寸 + 块尺寸是否全等。
// 重排准入门限（SPEC §4.3，与 Core runEngine 同一口径）：块尺寸不全等不可重排，当前选中重排则回退到
// 坍缩（可行时）或分离。调用顺序要求：须在 setCollapsible 之后调用，两个判据都取本次 Core 结果。
void ExportPanel::setRearrangeContext(const int keptCount, const int cellW, const int cellH,
                                     const bool uniformBlocks) {
    keptCount_ = keptCount < 0 ? 0 : keptCount;
    cellW_ = cellW < 0 ? 0 : cellW;
    cellH_ = cellH < 0 ? 0 : cellH;
    const bool wasAvailable = rearrangeAvailable();   // 判据含 uniformBlocks_，赋值前先取旧值。
    uniformBlocks_ = uniformBlocks;
    // 块尺寸不全等 → 重排不可用：回退到坍缩（可坍缩）或分离（也不可坍缩）。
    if (!uniformBlocks_ && modeCombo_->currentIndex() == 2)
        modeCombo_->setCurrentIndex(collapsible_ ? 1 : 0);
    // 处于重排模式且用户未手动指定 cols/rows 时，依新的保留块数自动开方填入。
    if (modeCombo_->currentIndex() == 2 && !mergeGridTouched_) applyAutoGrid();
    refreshModeItemStates();
    updateRearrangeWarning();
    if (rearrangeAvailable() != wasAvailable) emit rearrangeAvailableChanged(rearrangeAvailable());
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
void ExportPanel::onMergeSortChanged(const int index) const {
    if (doc_) doc_->setMergeOrderStrategy(index == 1 ? engine::SortStrategy::ColumnMajor
                                                     : engine::SortStrategy::RowMajor);
}

// 重排填充：蛇形。
void ExportPanel::onMergeSnakeToggled(const bool on) const {
    if (doc_) doc_->setMergeOrderSnake(on);
}

// 重排填充：倒序。
void ExportPanel::onMergeReverseToggled(const bool on) const {
    if (doc_) doc_->setMergeOrderReverse(on);
}

// 生成当前重排参数下的警告文本（无警告返回空串）：
//   ① cols*rows < 保留块数：多出的块无处安放（Core 会扩行，但与用户显式设定不符）；
//   ② cw/ch > 0 且 < 网格单元尺寸：块按自身尺寸落位会溢出格子、相互覆盖或越界被裁剪。
QString ExportPanel::rearrangeWarning() const {
    if (modeCombo_->currentIndex() != 2) return {};  // 非重排模式无警告。
    QStringList msgs;
    const int cols = mergeCols_->value();
    if (const int rows = mergeRows_->value(); keptCount_ > 0 && cols > 0 && rows > 0 && cols * rows < keptCount_) {
        msgs << IDC_GTR(status, kRearrangeColsShortFmt)
                    .arg(cols).arg(rows).arg(cols * rows).arg(keptCount_);
    }
    const int cw = mergeCellW_->value();
    const int ch = mergeCellH_->value();
    if (cw > 0 && cellW_ > 0 && cw < cellW_) {
        msgs << IDC_GTR(status, kRearrangeCellWShortFmt)
                    .arg(cw).arg(cellW_);
    }
    if (ch > 0 && cellH_ > 0 && ch < cellH_) {
        msgs << IDC_GTR(status, kRearrangeCellHShortFmt)
                    .arg(ch).arg(cellH_);
    }
    return msgs.join(QStringLiteral("\n"));
}

// 重算并显示内联警告（红字）。
void ExportPanel::updateRearrangeWarning() const {
    if (!rearrangeWarn_) return;
    const QString w = rearrangeWarning();
    rearrangeWarn_->setText(w.isEmpty() ? QString() : IDC_GTR(status, kWarnPrefix) + w);
    rearrangeWarn_->setVisible(!w.isEmpty());
}


} // namespace idc::gui
