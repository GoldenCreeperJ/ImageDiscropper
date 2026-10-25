// ============================================================================
// 文件：panels/param_panel.cpp
// 作用：实现右侧参数面板的三页构建与 Document 同步（见同名头文件说明）。
// 分块依据：buildL1Page/buildL2Page/buildL3Page 各自独立，互不干扰；坐标写回统一走
//           applyCoordsToDocument（依当前模式选取对应页的 spinbox）。
// ============================================================================
#include "panels/param_panel.h"

#include "param_panel_internal.h"

#include <algorithm>
#include <optional>
#include <ranges>
#include <vector>

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QResizeEvent>
#include <QSpinBox>
#include <QStackedLayout>
#include <QStackedWidget>
#include <QStringList>

#include "model/document.h"
#include "util/gui_nums.h"
#include "util/widget_factory.h"
#include "i18n/gui_texts.h"
#include "i18n/gui_i18n.h"

namespace idc::gui {
namespace {

// 高度由 ParamPanel::reflowStack 显式钳制的堆叠容器。QStackedLayout 在 SizeAll 下会按
// 所有页取最大定高（widget 级 sizeHint/heightForWidth 覆写会被父布局的 totalHeightForWidth
// 直接绕过 QStackedLayout 而失效），故这里只关掉对外的 heightForWidth 声明，使父面板
// 按 widget 的 sizeHint（=当前页高度，已由 min/max 钳定）而非隐藏页高度排版。
class PageSizedStack : public QStackedWidget {
public:
    using QStackedWidget::QStackedWidget;
    bool hasHeightForWidth() const override { return false; }   // 高度由 min/max 显控制，不向外报 HFW。
};

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

// 创建坐标输入框：范围 [0, nums::panels::kCoordLimitPx]，关闭逐键跟踪（提交才触发），回车即生效（NFR-5）。
// 除整数像素外另接受 "50%" 形式的百分比输入（按当前基准换算成像素，见 CoordSpinBox）。
CoordSpinBox* makeCoordSpin(QWidget* parent) {
    auto* s = new CoordSpinBox(parent);
    s->setRange(0, nums::panels::kCoordLimitPx);
    s->setKeyboardTracking(false);
    s->setFixedWidth(nums::panels::kCoordSpinWidthPx);
    return s;
}

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

// 构建面板：一个 QStackedWidget 承载 L1/L2/L3 三页。
ParamPanel::ParamPanel(QWidget* parent) : QWidget(parent) {
    auto* root = makePanelLayout(this);
    stack_ = new PageSizedStack(this);
    pages_[0] = buildL1Page();
    pages_[1] = buildL2Page();
    pages_[2] = buildL3Page();
    for (QWidget* page : { pages_[0].data(), pages_[1].data(), pages_[2].data() }) {
        stack_->addWidget(page);   // 加入顺序即 L1/L2/L3（index 0/1/2）。
    }
    // 切页时重钳当前页高度（reflowStack 为公有槽，供 currentChanged 直连）。
    connect(qobject_cast<QStackedLayout*>(stack_->layout()), &QStackedLayout::currentChanged,
            this, &ParamPanel::reflowStack);
    // 垂直保持 Preferred：高度由 reflowStack 显式钳 min=max 定住（见 PageSizedStack）。
    stack_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    root->addWidget(stack_);
    // 尾部 stretch：移动端抽屉页（面板外套 QScrollArea、置于 QStackedWidget）比钳定后的 stack 高时，QWidgetItem 会把
    // max 受限的 widget 在格子里**居中**；加 stretch 吸收剩余空间保证顶部对齐。
    root->addStretch(1);
    reflowStack();   // 初始按当前页钳一次高（宽度未定时用 sizeHint 宽，后续 resizeEvent 会校准）。
}

// 把堆叠容器高度硬钳到「当前页在当前宽度下所需高度」。因 hasHeightForWidth 已关，
// 父布局会按 widget 的 sizeHint 排版；而 sizeHint 仍为最高页——故直接设 min=max 高度绕开。
// 高度取自 heightForWidth(当前宽)，保证 wordWrap 换行后不裁切。宽度变化（分栏拖拽）时重算。
void ParamPanel::reflowStack() const {
    if (!stack_) return;
    if (const QWidget* cur = stack_->currentWidget()) {
        const int w = stack_->width() > 0 ? stack_->width() : cur->sizeHint().width();
        const int h = cur->hasHeightForWidth() ? cur->heightForWidth(w) : cur->sizeHint().height();
        stack_->setMinimumHeight(h);
        stack_->setMaximumHeight(h);
    } else {
        stack_->setMinimumHeight(0);
        stack_->setMaximumHeight(QWIDGETSIZE_MAX);
    }
    stack_->updateGeometry();
}

// 面板宽度变化（分隔条拖拽）时，当前页 wordWrap 换行高度会变 → 重钳 stack 高度。
void ParamPanel::resizeEvent(QResizeEvent* ev) {
    QWidget::resizeEvent(ev);
    reflowStack();
}

void ParamPanel::setDocument(Document* doc) {
    doc_ = doc;
    syncFromDocument();
}

// 从 Document 反向同步控件（形状/子功能/坐标/页），并依图像尺寸收紧坐标上限。
void ParamPanel::syncFromDocument() {
    if (!doc_) return;

    // 形状 / 子功能：枚举值与 combo 顺序一致。
    l1Shape_->blockSignals(true);
    l1Shape_->setCurrentIndex(static_cast<int>(doc_->l1Shape()));
    l1Shape_->blockSignals(false);
    l2Sub_->blockSignals(true);
    l2Sub_->setCurrentIndex(static_cast<int>(doc_->l2Sub()));
    l2Sub_->blockSignals(false);

    // 坐标上限收紧到图像尺寸（若有图）。
    if (doc_->hasImage()) {
        const int maxX = doc_->width(), maxY = doc_->height();
        for (QSpinBox* s : {l1x1_, l1x2_, l2x1_, l2x2_}) s->setMaximum(maxX);
        for (QSpinBox* s : {l1y1_, l1y2_, l2y1_, l2y2_}) s->setMaximum(maxY);
    }

    // 坐标：把 Document 的矩形回填到当前页的 spinbox（两页共用同一 rect）。
    const auto& r = doc_->rect();
    const bool has = doc_->hasRect();
    const int x1 = has ? r.left : 0, y1 = has ? r.top : 0;
    const int x2 = has ? r.right : 0, y2 = has ? r.bottom : 0;
    for (QSpinBox* s : {l1x1_, l2x1_}) { s->blockSignals(true); s->setValue(x1); s->blockSignals(false); }
    for (QSpinBox* s : {l1y1_, l2y1_}) { s->blockSignals(true); s->setValue(y1); s->blockSignals(false); }
    for (QSpinBox* s : {l1x2_, l2x2_}) { s->blockSignals(true); s->setValue(x2); s->blockSignals(false); }
    for (QSpinBox* s : {l1y2_, l2y2_}) { s->blockSignals(true); s->setValue(y2); s->blockSignals(false); }

    // L3 网格参数回填（按轴三形态；blockSignals 防回环）。
    syncGridAxisToUi(true);
    syncGridAxisToUi(false);
    gRemainder_->blockSignals(true);
    gRemainder_->setCurrentIndex(static_cast<int>(doc_->gridParams().remainder));
    gRemainder_->blockSignals(false);
    // 余量策略仅对周期轴生效：两轴都是分割数/比例时置灰并给出同屏说明。
    const bool anyPitch = doc_->gridAxisIsPitch(true) || doc_->gridAxisIsPitch(false);
    gRemainder_->setEnabled(anyPitch);
    if (gNoPitchHint_) gNoPitchHint_->setVisible(!anyPitch);

    // L3 排序回填。
    const auto&[strategy, reverse, snake] = doc_->order();
    gSort_->blockSignals(true);
    gSort_->setCurrentIndex(static_cast<int>(strategy));
    gSort_->blockSignals(false);
    gReverse_->blockSignals(true);
    gReverse_->setChecked(reverse);
    gReverse_->blockSignals(false);
    gSnake_->blockSignals(true);
    gSnake_->setChecked(snake);
    gSnake_->blockSignals(false);

    // L3 只读回显：派生行列数与已选单元数。
    const int rows = doc_->gridRows(), cols = doc_->gridCols();
    gGridInfo_->setText(IDC_GTR(status, kGridDerivedInfo)
                            .arg(rows).arg(cols).arg(rows * cols));
    gSelInfo_->setText(IDC_GTR(status, kGridSelectedInfo)
                           .arg(doc_->selectedCells().size()));

    // 「转为网格」按钮：有可转换的选区几何时可用——单选区 rect_ 有效，
    // 或多矩形并集（MultiRect）列表非空（转换时取其包围盒，见 Document::convertRectToGrid）。
    if (l2ToGridBtn_) {
        const bool singleOk = doc_->hasRect() && doc_->rect().width() > 0 && doc_->rect().height() > 0;
        const bool multiOk = doc_->l2Sub() == L2Sub::MultiRect && !doc_->rects().empty();
        l2ToGridBtn_->setEnabled(singleOk || multiOk);
    }

    // L2 多矩形列表：仅 MultiRect 显示；依 doc_->rects() 重建，并把选中项坐标回填 spinbox。
    const bool multi = doc_->l2Sub() == L2Sub::MultiRect;
    if (l2MultiBox_) l2MultiBox_->setVisible(multi);
    if (multi && l2RectList_) {
        syncingRectList_ = true;               // 抑制重建期间的 currentRowChanged 回环。
        const int prevRow = l2RectList_->currentRow();
        l2RectList_->clear();
        const auto& rs = doc_->rects();
        // 下标循环即可；std::views::enumerate 对 Apple/NDK libc++<19 不可用。
        for (std::size_t i = 0; i < rs.size(); ++i) {
            const auto& r = rs[i];
            l2RectList_->addItem(IDC_GTR(status, kRectListItem)
                                     .arg(i + 1).arg(r.left).arg(r.top)
                                     .arg(r.right).arg(r.bottom));
        }
        if (!rs.empty()) {
            const int last = static_cast<int>(rs.size()) - 1;
            l2RectList_->setCurrentRow(prevRow >= 0 && prevRow <= last ? prevRow : last);
        }
        syncingRectList_ = false;
        // 选中矩形坐标覆盖到 spinbox（多矩形下单 rect_ 的回填值无意义，须按当前行矩形回填）。
        const int row = l2RectList_->currentRow();
        if (row >= 0 && row < static_cast<int>(rs.size())) {
            const auto& rr = rs[static_cast<std::size_t>(row)];
            l2x1_->blockSignals(true); l2x1_->setValue(rr.left);   l2x1_->blockSignals(false);
            l2y1_->blockSignals(true); l2y1_->setValue(rr.top);    l2y1_->blockSignals(false);
            l2x2_->blockSignals(true); l2x2_->setValue(rr.right);  l2x2_->blockSignals(false);
            l2y2_->blockSignals(true); l2y2_->setValue(rr.bottom); l2y2_->blockSignals(false);
        }
        // 重建列表时 syncingRectList_ 抑制了选中信号，此处补发一次以同步画布高亮（-1 清除）。
        emit rectSelected(row);
    }

    setMode(doc_->mode());
    // 显隐 l2MultiBox_ 可能在未切页时改变当前页高度（currentChanged 不触发），补重报一次。
    reflowStack();
}

// 切换到某模式对应的页。
void ParamPanel::setMode(const engine::Tier tier) const {
    switch (tier) {
        case engine::Tier::Standard: stack_->setCurrentIndex(0); break;
        case engine::Tier::Erase: stack_->setCurrentIndex(1); break;
        case engine::Tier::Grid: stack_->setCurrentIndex(2); break;
    }
}

// 回灌坍缩可行性提示（L2 页）。
void ParamPanel::setCollapseHint(const bool collapsible, const QString& reason) const {
    if (!collapseHint_) return;
    if (collapsible) {
        collapseHint_->setText(IDC_GTR(status, kCollapseHintOk));
    } else {
        collapseHint_->setText(IDC_GTR(status, kCollapseHintFail).arg(reason));
    }
}

// L1 形状切换 → 写回 Document。
void ParamPanel::onL1ShapeChanged(int index) const {
    if (!doc_) return;
    doc_->setL1Shape(static_cast<L1Shape>(index));
}

// L2 子功能切换 → 写回 Document（index 0..3，3 = 多矩形并集）。
void ParamPanel::onL2SubChanged(int index) const {
    if (!doc_ || index < 0 || index > 3) return;
    doc_->setL2Sub(static_cast<L2Sub>(index));
}

// 坐标变更统一入口：依当前模式取对应页 spinbox，组装规范化矩形写回 Document。
void ParamPanel::onCoordEdited() const {
    applyCoordsToDocument();
}

// 组装规范化矩形（左上/右下）写回 Document。
void ParamPanel::applyCoordsToDocument() const {
    if (!doc_) return;
    const bool l1 = doc_->mode() == engine::Tier::Standard;
    const QSpinBox* x1 = l1 ? l1x1_ : l2x1_;
    const QSpinBox* y1 = l1 ? l1y1_ : l2y1_;
    const QSpinBox* x2 = l1 ? l1x2_ : l2x2_;
    const QSpinBox* y2 = l1 ? l1y2_ : l2y2_;

    const int a = x1->value(), b = y1->value(), c = x2->value(), d = y2->value();
    const engine::RectRegion r(std::min(a, c), std::min(b, d), std::max(a, c), std::max(b, d));
    // 校验：x1==x2 或 y1==y2 会得到零宽/零高的退化矩形，直接送入 Core 会崩溃；
    // 此处拒绝退化输入（保持上一次有效选区），Document::setRect 另有兜底（CONTRIBUTING.md「分层纪律」）。
    if (r.width() <= 0 || r.height() <= 0) return;
    // L2 多矩形：坐标框编辑的是列表中当前选中的矩形（updateRect），而非单选区 rect_。
    if (!l1 && doc_->l2Sub() == L2Sub::MultiRect) {
        if (l2RectList_) {
            if (const int row = l2RectList_->currentRow(); row >= 0) doc_->updateRect(static_cast<std::size_t>(row), r);
        }
        return;
    }
    doc_->setRect(r); // 触发预览刷新。
}

// 「转为网格模式编辑」：把当前矩形选区送入 L3（Document 负责参数映射与模式切换）。
void ParamPanel::onConvertToGrid() const {
    if (!doc_) return;
    doc_->convertRectToGrid(); // 触发 changed → 刷新预览 + 面板切到 L3 页 + 工具栏/左面板同步。
}

// ---- L2 多矩形并集槽 ----

// 列表选中项变化：高亮画布对应矩形 + 把该矩形坐标载入 spinbox（重建期间由 syncingRectList_ 抑制）。
void ParamPanel::onRectListSelectionChanged() {
    if (syncingRectList_ || !doc_ || !l2RectList_) return;
    const int row = l2RectList_->currentRow();
    emit rectSelected(row);   // 通知画布高亮对应选区框（-1 清除高亮）。
    const auto& rs = doc_->rects();
    if (row < 0 || row >= static_cast<int>(rs.size())) return;
    const auto& rr = rs[static_cast<std::size_t>(row)];
    l2x1_->blockSignals(true); l2x1_->setValue(rr.left);   l2x1_->blockSignals(false);
    l2y1_->blockSignals(true); l2y1_->setValue(rr.top);    l2y1_->blockSignals(false);
    l2x2_->blockSignals(true); l2x2_->setValue(rr.right);  l2x2_->blockSignals(false);
    l2y2_->blockSignals(true); l2y2_->setValue(rr.bottom); l2y2_->blockSignals(false);
}

// 仅刷新列表中第 index 行的坐标文本（若为选中行则同步 spinbox），不重建整个列表。
// 供画布拖拽期间实时回显矩形尺寸（此时 syncPanels 被跳过，列表不会重建）。
void ParamPanel::updateRectListItem(const int index) const {
    if (!doc_ || !l2RectList_) return;
    const auto& rs = doc_->rects();
    if (index < 0 || index >= static_cast<int>(rs.size())) return;
    const auto& rr = rs[static_cast<std::size_t>(index)];
    if (QListWidgetItem* item = l2RectList_->item(index)) {
        item->setText(IDC_GTR(status, kRectListItem)
                          .arg(index + 1).arg(rr.left).arg(rr.top).arg(rr.right).arg(rr.bottom));
    }
    if (l2RectList_->currentRow() == index) {   // 选中行同步回 spinbox（实时微调回显）。
        l2x1_->blockSignals(true); l2x1_->setValue(rr.left);   l2x1_->blockSignals(false);
        l2y1_->blockSignals(true); l2y1_->setValue(rr.top);    l2y1_->blockSignals(false);
        l2x2_->blockSignals(true); l2x2_->setValue(rr.right);  l2x2_->blockSignals(false);
        l2y2_->blockSignals(true); l2y2_->setValue(rr.bottom); l2y2_->blockSignals(false);
    }
}

// 把列表选中行置为 index（已是则免打扰）；setCurrentRow 会触发 onRectListSelectionChanged
// 从而 emit rectSelected + 回填 spinbox，故无需在此重复。
void ParamPanel::selectRectRow(const int index) const {
    if (!doc_ || !l2RectList_) return;
    if (l2RectList_->currentRow() == index) return;
    if (index < 0 || index >= static_cast<int>(doc_->rects().size())) return;
    l2RectList_->setCurrentRow(index);
}

// 删除选中矩形。
void ParamPanel::onRectDelClicked() const {
    if (!doc_ || !l2RectList_) return;
    const int row = l2RectList_->currentRow();
    if (row < 0) return;
    doc_->removeRect(static_cast<std::size_t>(row));
}

// 清空多矩形列表。
void ParamPanel::onRectClearClicked() const {
    if (!doc_) return;
    doc_->clearRects();
}

// ---- L3 槽：网格参数 / 选择集 / 排序 → 写回 Document ----

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

// 排序策略变更（combo 索引与 SortStrategy 一致）。
void ParamPanel::onSortStrategyChanged(const int index) const {
    if (!doc_ || index < 0 || index > 2) return;
    doc_->setSortStrategy(static_cast<engine::SortStrategy>(index));
}

// 整体逆序开关。
void ParamPanel::onSortReverseToggled(const bool on) const {
    if (!doc_) return;
    doc_->setSortReverse(on);
}

// 蛇形排序开关。
void ParamPanel::onSortSnakeToggled(const bool on) const {
    if (!doc_) return;
    doc_->setSortSnake(on);
}

// 全选（依派生行列数生成全序号）。
void ParamPanel::onSelectAllCells() const {
    if (!doc_) return;
    doc_->selectAllCells();
}

// 反选（依派生行列数取补集）。
void ParamPanel::onInvertCells() const {
    if (!doc_) return;
    doc_->invertCells();
}

// 清空选择集。
void ParamPanel::onClearCells() const {
    if (!doc_) return;
    doc_->clearCells();
}

} // namespace idc::gui
