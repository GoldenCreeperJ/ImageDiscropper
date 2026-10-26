// ============================================================================
// 文件：panels/param_panel_grid.cpp
// 作用：ParamPanel 的 L3 网格轴槽与按轴三形态（周期 / 分割数 / 比例）实现——基准点/单元尺寸/余量
//       写回、三形态页同步与切换、权重文本解析（parseWeights/weightsText）与坐标框基准绑定。
// 分块依据：见 param_panel.cpp 头注（三页构建在 param_panel_pages.cpp、L3 网格轴槽在本 TU、
//       其余同步/槽在主文件）；本 TU 只写回 Document 真相源，不建任何控件。
// ============================================================================
#include "panels/param_panel.h"

#include <algorithm>
#include <optional>
#include <vector>

#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStringList>

#include "model/document.h"   // Document（GridAxisSpec / GridParams 写回真相源）
#include "util/coord_spin.h"  // CoordSpinBox（bindCoordBasis 的百分比基准注入）
#include "util/gui_nums.h"    // nums::panels（无图时的坐标/份数上限）

namespace idc::gui {
namespace {
// 权重文本 → 权重序列：接受逗号分隔的正数或百分比（中英文逗号、空白均可），如 "1,2,1" / "25%,50%,25%"。
// 解析失败（空、非数、非正）返回 nullopt，由调用方保持旧定义（不产生半截状态）。
// 语义与 CLI --weights-* 同一口径：整数比与百分比等价，只取相对比例。
std::optional<std::vector<double>> parseWeights(const QString& text) {
    QString normalized = text;
    normalized.replace(QChar(0xFF0C), QLatin1Char(','));   // 全角逗号归一为半角
    const QStringList parts = normalized.split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (parts.isEmpty()) return std::nullopt;
    std::vector<double> out;
    out.reserve(static_cast<std::size_t>(parts.size()));
    for (QString part : parts) {
        part = part.trimmed();
        if (part.endsWith(QLatin1Char('%'))) part.chop(1);
        bool ok = false;
        const double w = part.toDouble(&ok);
        if (!ok || !(w > 0.0)) return std::nullopt;
        out.push_back(w);
    }
    return out;
}

// 权重序列 → 文本（与 parseWeights 对称；%g 风格去掉多余尾零，25.0 → "25"）。
QString weightsText(const std::vector<double>& weights) {
    QStringList parts;
    parts.reserve(static_cast<int>(weights.size()));
    for (const double w : weights) parts << QString::number(w, 'g', 6);
    return parts.join(QLatin1Char(','));
}
} // namespace

// 坐标框百分比换算基准：横向量取画布宽、纵向量取画布高（SPEC §2.1 分方向取基准）。
// 基准取当前工作图尺寸（预处理后），故预处理改尺寸后百分比输入即按新尺寸换算。
void ParamPanel::bindCoordBasis(QSpinBox* spin, const bool horizontal) const {
    auto* coord = dynamic_cast<CoordSpinBox*>(spin);   // 无 Q_OBJECT，故用 dynamic_cast 而非 qobject_cast。
    if (!coord) return;
    coord->setBasisProvider([this, horizontal] {
        if (!doc_) return 0;
        return horizontal ? doc_->width() : doc_->height();
    });
}

// 基准点 x0/y0 变更。
void ParamPanel::onGridOriginEdited() const {
    if (!doc_) return;
    doc_->setGridOrigin(gx0_->value(), gy0_->value());
}

// 单元尺寸 cw/ch 变更（Document 侧拒绝非正值）。尺寸变大时基准点须同步退回
// 「单元右/下端点仍在图内」的范围（与画布手柄同一口径），故与基准点一并写回、只发一次 changed。
void ParamPanel::onCellSizeEdited() const {
    if (!doc_) return;
    const int w = gcw_->value();
    const int h = gch_->value();
    int x0 = gx0_->value();
    int y0 = gy0_->value();
    if (doc_->hasImage()) {
        const int maxX = std::max(0, doc_->width() - w);
        const int maxY = std::max(0, doc_->height() - h);
        x0 = std::min(x0, maxX);
        y0 = std::min(y0, maxY);
        for (QSpinBox* s : {gx0_, gy0_}) s->blockSignals(true);
        gx0_->setMaximum(maxX);
        gy0_->setMaximum(maxY);
        gx0_->setValue(x0);
        gy0_->setValue(y0);
        for (QSpinBox* s : {gx0_, gy0_}) s->blockSignals(false);
    }
    doc_->setGridParams(x0, y0, w, h);
}

// 余量策略变更（combo 索引与 RemainderPolicy 一致）。
void ParamPanel::onRemainderChanged(const int index) const {
    if (!doc_ || index < 0 || index > 2) return;
    doc_->setRemainder(static_cast<engine::RemainderPolicy>(index));
}

// ---- L3 按轴三形态（周期 / 分割数 / 比例）----

// 把某轴的有效定义回填到控件：定义方式下拉、三形态页、页内数值与上限。
void ParamPanel::syncGridAxisToUi(const bool horizontal) const {
    if (!doc_) return;
    const engine::GridAxisSpec axis = doc_->gridAxis(horizontal);
    QComboBox* mode = horizontal ? gModeX_ : gModeY_;
    QStackedWidget* pages = horizontal ? gStackX_ : gStackY_;
    QSpinBox* origin = horizontal ? gx0_ : gy0_;
    QSpinBox* cell = horizontal ? gcw_ : gch_;
    QSpinBox* count = horizontal ? gCountX_ : gCountY_;
    QLineEdit* weights = horizontal ? gWeightsX_ : gWeightsY_;
    if (!mode || !pages || !origin || !cell || !count || !weights) return;

    const int index = static_cast<int>(axis.mode);
    mode->blockSignals(true);
    mode->setCurrentIndex(index);
    mode->blockSignals(false);
    pages->setCurrentIndex(index);

    // 上限先于回灌值设置（setMaximum 变小时 Qt 会夹当前值并可发 valueChanged）：
    // 单元尺寸不得宽于图像、基准点须让单元右/下端点 (x0+cw, y0+ch) 仍在图内（与画布手柄同一口径）；
    // 份数不得超过该方向像素数（Core checkAxis 同样拒绝，此处先夹住避免用户造出必定报错的输入）。
    const int limit = horizontal ? doc_->width() : doc_->height();
    origin->blockSignals(true);
    cell->blockSignals(true);
    count->blockSignals(true);
    if (limit > 0) {
        cell->setMaximum(std::max(1, limit));
        origin->setMaximum(std::max(0, limit - axis.cell));
        count->setMaximum(std::max(1, limit));
    } else {
        cell->setMaximum(nums::panels::kCoordLimitPx);
        origin->setMaximum(nums::panels::kCoordLimitPx);
        count->setMaximum(nums::panels::kCoordLimitPx);
    }
    origin->setValue(axis.origin);
    cell->setValue(std::max(1, axis.cell));
    count->setValue(std::max(1, axis.count));
    origin->blockSignals(false);
    cell->blockSignals(false);
    count->blockSignals(false);

    // 权重文本：仅比例轴回填；正在输入时不打断，其余形态保留上次输入便于来回切换。
    if (axis.mode == engine::GridAxisMode::Weights && !weights->hasFocus()) {
        weights->blockSignals(true);
        weights->setText(weightsText(axis.weights));
        weights->blockSignals(false);
    }
}

// 读该轴权重输入框的文本。
QString ParamPanel::gridWeightsText(const bool horizontal) const {
    const QLineEdit* edit = horizontal ? gWeightsX_ : gWeightsY_;
    return edit ? edit->text() : QString();
}

// 外部（模式菜单「网格定义」）切换某轴定义方式：与面板下拉 currentIndexChanged 走同一实现，
// 即「以屏上现有数值起手」，不会留下空参数；勾选态由随后的 syncPanels 从 Document 回同步。
void ParamPanel::setGridAxisMode(const bool horizontal, const engine::GridAxisMode mode) const {
    onGridModeChanged(horizontal, static_cast<int>(mode));
}

// 切换某轴的定义方式：以屏上现有数值起手，避免出现「份数=1、比例=空」这类跳变。
void ParamPanel::onGridModeChanged(const bool horizontal, const int index) const {
    if (!doc_ || index < 0 || index > 2) return;
    const auto mode = static_cast<engine::GridAxisMode>(index);
    engine::GridAxisSpec spec;
    spec.mode = mode;
    if (mode == engine::GridAxisMode::Pitch) {
        // 回到周期：沿用平铺字段（上次用过的基准点/单元尺寸），而非被显式定义盖住的默认值。
        const engine::GridParams& grid = doc_->gridParams();
        spec.origin = horizontal ? grid.originX : grid.originY;
        spec.cell = horizontal ? grid.cellWidth : grid.cellHeight;
    } else if (mode == engine::GridAxisMode::Count) {
        // 以当前派生份数起手（屏幕上正在看的列/行数），未派生（无图）时按 2 份。
        const int derived = horizontal ? doc_->gridCols() : doc_->gridRows();
        spec.count = derived > 0 ? derived : 2;
        if (const int limit = horizontal ? doc_->width() : doc_->height(); limit > 0)
            spec.count = std::min(spec.count, limit);   // 份数不得超过像素数（否则必出零宽格）。
    } else {
        // 比例：保留输入框里已有的有效文本，否则以两份等宽起手。
        const std::optional<std::vector<double>> typed = parseWeights(gridWeightsText(horizontal));
        spec.weights = typed ? *typed : std::vector{1.0, 1.0};
    }
    doc_->setGridAxis(horizontal, spec);
    // Document 值未变时不会发 changed（上面不回灌），故此处直接把三形态页切到位。
    if (QStackedWidget* pages = horizontal ? gStackX_ : gStackY_) pages->setCurrentIndex(index);
    reflowStack();
}

// 份数编辑：仅该轴当前走分割数时写回（隐藏页上的旧值不参与）。
void ParamPanel::onGridCountEdited(const bool horizontal) const {
    if (!doc_ || doc_->gridAxis(horizontal).mode != engine::GridAxisMode::Count) return;
    const QSpinBox* count = horizontal ? gCountX_ : gCountY_;
    engine::GridAxisSpec spec;
    spec.mode = engine::GridAxisMode::Count;
    spec.count = count->value();
    doc_->setGridAxis(horizontal, spec);
}

// 权重编辑（回车/失焦提交）：非法文本回退为 Document 里的旧定义，不产生半截状态。
void ParamPanel::onGridWeightsEdited(const bool horizontal) const {
    if (!doc_ || doc_->gridAxis(horizontal).mode != engine::GridAxisMode::Weights) return;
    const std::optional<std::vector<double>> parsed = parseWeights(gridWeightsText(horizontal));
    if (!parsed) {
        syncGridAxisToUi(horizontal);   // 非法：回显上次有效权重
        return;
    }
    engine::GridAxisSpec spec;
    spec.mode = engine::GridAxisMode::Weights;
    spec.weights = *parsed;
    doc_->setGridAxis(horizontal, spec);
}


} // namespace idc::gui
