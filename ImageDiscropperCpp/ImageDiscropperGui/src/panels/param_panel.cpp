// ============================================================================
// 文件：panels/param_panel.cpp
// 作用：实现右侧参数面板的三页构建与 Document 同步（见同名头文件说明）。
// 分块依据：buildL1Page/buildL2Page/buildL3Page 各自独立，互不干扰；坐标写回统一走
//           applyCoordsToDocument（依当前模式选取对应页的 spinbox）。
// ============================================================================
#include "panels/param_panel.h"

#include <algorithm>
#include <ranges>

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QResizeEvent>
#include <QSpinBox>
#include <QStackedLayout>
#include <QStackedWidget>

#include "model/document.h"
#include "util/gui_nums.h"
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

} // namespace

// 创建坐标输入框：范围 [0, nums::panels::kCoordLimitPx]，关闭逐键跟踪（提交才触发），回车即生效（NFR-5）。
QSpinBox* makeCoordSpin(QWidget* parent) {
    auto* s = new QSpinBox(parent);
    s->setRange(0, nums::panels::kCoordLimitPx);
    s->setKeyboardTracking(false);
    s->setFixedWidth(nums::panels::kCoordSpinWidthPx);
    return s;
}

// 构建面板：一个 QStackedWidget 承载 L1/L2/L3 三页。
ParamPanel::ParamPanel(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(nums::panels::kPanelMarginPx, nums::panels::kPanelMarginPx,
                             nums::panels::kPanelMarginPx, nums::panels::kPanelMarginPx);
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

    // L3 网格参数回填（blockSignals 防回环）。
    const auto&[originX, originY, cellWidth, cellHeight, remainder] = doc_->gridParams();
    for (QSpinBox* s : {gx0_, gy0_, gcw_, gch_}) s->blockSignals(true);
    gx0_->setValue(originX);
    gy0_->setValue(originY);
    gcw_->setValue(cellWidth);
    gch_->setValue(cellHeight);
    for (QSpinBox* s : {gx0_, gy0_, gcw_, gch_}) s->blockSignals(false);
    // 有图时把基准点/单元尺寸上限收紧到图像尺寸。
    if (doc_->hasImage()) {
        gx0_->setMaximum(doc_->width());
        gy0_->setMaximum(doc_->height());
    }
    gRemainder_->blockSignals(true);
    gRemainder_->setCurrentIndex(static_cast<int>(remainder));
    gRemainder_->blockSignals(false);

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

// 单元尺寸 cw/ch 变更（Document 侧拒绝非正值）。
void ParamPanel::onCellSizeEdited() const {
    if (!doc_) return;
    doc_->setCellSize(gcw_->value(), gch_->value());
}

// 余量策略变更（combo 索引与 RemainderPolicy 一致）。
void ParamPanel::onRemainderChanged(const int index) const {
    if (!doc_ || index < 0 || index > 2) return;
    doc_->setRemainder(static_cast<engine::RemainderPolicy>(index));
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
