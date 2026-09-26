// ============================================================================
// 文件：panels/param_panel.cpp
// 作用：实现右侧参数面板的三页构建与 Document 同步（见同名头文件说明）。
// 分块依据：buildL1Page/buildL2Page/buildL3Page 各自独立，互不干扰；坐标写回统一走
//           applyCoordsToDocument（依当前模式选取对应页的 spinbox）。
// ============================================================================
#include "panels/param_panel.h"

#include <algorithm>

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

// 创建坐标输入框：范围 [0,100000]，关闭逐键跟踪（提交才触发），回车即生效（NFR-5）。
QSpinBox* makeCoordSpin(QWidget* parent) {
    auto* s = new QSpinBox(parent);
    s->setRange(0, 100000);
    s->setKeyboardTracking(false);
    s->setFixedWidth(90);
    return s;
}

// 构建面板：一个 QStackedWidget 承载 L1/L2/L3 三页。
ParamPanel::ParamPanel(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    stack_ = new PageSizedStack(this);
    pages_[0] = buildL1Page();
    pages_[1] = buildL2Page();
    pages_[2] = buildL3Page();
    for (QWidget* page : { pages_[0].data(), pages_[1].data(), pages_[2].data() }) {
        stack_->addWidget(page);   // 参加顺序即 L1/L2/L3（index 0/1/2）。
    }
    // 切页时重钳当前页高度（reflowStack 为公有槽，供 currentChanged 直连）。
    connect(qobject_cast<QStackedLayout*>(stack_->layout()), &QStackedLayout::currentChanged,
            this, &ParamPanel::reflowStack);
    // 垂直保持 Preferred：高度由 reflowStack 显式钳 min=max 定住（见 PageSizedStack）。
    stack_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    root->addWidget(stack_);
    // 尾部 stretch：tab 页（移动端 QTabWidget）比钳定后的 stack 高时，QWidgetItem 会把
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

// L1 页：形状 + 坐标。
QWidget* ParamPanel::buildL1Page() {
    auto* page = new QWidget(this);
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);

    auto* box = new QGroupBox(QStringLiteral("L1 标准提取"), page);
    auto* form = new QFormLayout(box);
    l1Shape_ = new QComboBox(box);
    l1Shape_->addItems({QStringLiteral("矩形"), QStringLiteral("横带"), QStringLiteral("竖带")});
    l1Shape_->setToolTip(QStringLiteral("选择保留区域的形状：矩形选框 / 整条横带 / 整条竖带。"));
    form->addRow(QStringLiteral("形状"), l1Shape_);

    l1x1_ = makeCoordSpin(box); l1y1_ = makeCoordSpin(box);
    l1x2_ = makeCoordSpin(box); l1y2_ = makeCoordSpin(box);
    form->addRow(QStringLiteral("x1"), l1x1_);
    form->addRow(QStringLiteral("y1"), l1y1_);
    form->addRow(QStringLiteral("x2"), l1x2_);
    form->addRow(QStringLiteral("y2"), l1y2_);
    v->addWidget(box);

    auto* note = new QLabel(QStringLiteral("保留选框内的区域。切割线贯穿全图（并非只有框边）；"
                                          "保留/删除可在左侧切换。"), page);
    note->setWordWrap(true);
    v->addWidget(note);
    v->addStretch(1);

    connect(l1Shape_, &QComboBox::currentIndexChanged, this, &ParamPanel::onL1ShapeChanged);
    for (const QSpinBox* s : {l1x1_, l1y1_, l1x2_, l1y2_})
        connect(s, &QSpinBox::valueChanged, this, &ParamPanel::onCoordEdited);
    return page;
}

// L2 页：子功能 + 坐标 + 坍缩可行性提示。
QWidget* ParamPanel::buildL2Page() {
    auto* page = new QWidget(this);
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);

    auto* box = new QGroupBox(QStringLiteral("L2 反向剔除"), page);
    auto* form = new QFormLayout(box);
    l2Sub_ = new QComboBox(box);
    l2Sub_->addItems({QStringLiteral("十字切割"), QStringLiteral("横线切割"),
                      QStringLiteral("竖线切割"), QStringLiteral("多矩形并集剔除")});
    l2Sub_->setToolTip(QStringLiteral("每个矩形删除的都是贯穿全图的「竖带 + 横带」（十字带）；多矩形时删除区域取并集。"));
    form->addRow(QStringLiteral("子功能"), l2Sub_);

    l2x1_ = makeCoordSpin(box); l2y1_ = makeCoordSpin(box);
    l2x2_ = makeCoordSpin(box); l2y2_ = makeCoordSpin(box);
    form->addRow(QStringLiteral("x1"), l2x1_);
    form->addRow(QStringLiteral("y1"), l2y1_);
    form->addRow(QStringLiteral("x2"), l2x2_);
    form->addRow(QStringLiteral("y2"), l2y2_);
    v->addWidget(box);

    // ---- 多矩形并集列表（仅 l2Sub_==MULTI_RECT 显示）----
    // 追加矩形的主入口是画布框选（MainWindow.onRubberSelect）；此处提供列表可视化 +
    // 选中项坐标微调（复用上方 x1/y1/x2/y2）+ 删除/清空。面板只搬运，不做并集计算（CONTRIBUTING.md「分层纪律」）。
    l2MultiBox_ = new QGroupBox(QStringLiteral("多矩形列表"), page);
    auto* mv = new QVBoxLayout(l2MultiBox_);
    l2RectList_ = new QListWidget(l2MultiBox_);
    l2RectList_->setToolTip(QStringLiteral("在画布上拖拽框选追加矩形，也可直接拖动/缩放已有矩形；选中列表项后可在上方坐标框微调或删除。"));
    mv->addWidget(l2RectList_);
    auto* mbtnRow = new QHBoxLayout();
    l2RectDelBtn_ = new QPushButton(QStringLiteral("删除选中"), l2MultiBox_);
    l2RectClearBtn_ = new QPushButton(QStringLiteral("清空"), l2MultiBox_);
    mbtnRow->addWidget(l2RectDelBtn_);
    mbtnRow->addWidget(l2RectClearBtn_);
    mv->addLayout(mbtnRow);
    auto* mnote = new QLabel(QStringLiteral("在画布空白处拖拽框选可追加矩形；矩形可直接拖动/四角缩放/拖边调整。删除区域为各矩形十字带（竖带+横带）的并集。"), l2MultiBox_);
    mnote->setWordWrap(true);
    mv->addWidget(mnote);
    l2MultiBox_->setVisible(false); // 默认隐藏，切到 MULTI_RECT 才显示（见 syncFromDocument）。
    v->addWidget(l2MultiBox_);

    // 坍缩可行性提示：由 MainWindow 依 Core isCollapsible 结果回灌。
    collapseHint_ = new QLabel(QStringLiteral("坍缩可行性：请先框选选区。"), page);
    collapseHint_->setWordWrap(true);
    v->addWidget(collapseHint_);

    auto* outNote = new QLabel(QStringLiteral("输出模式（分离 / 坍缩 / 重排）见右侧「导出」页。"), page);
    outNote->setWordWrap(true);
    v->addWidget(outNote);

    // 「转为网格模式编辑」入口：把当前矩形选区送入 L3 逐单元精修。
    l2ToGridBtn_ = new QPushButton(QStringLiteral("转为网格模式编辑…"), page);
    l2ToGridBtn_->setToolTip(QStringLiteral("以当前矩形选区的左上为基准点、宽高为单元尺寸，"
                                            "切换到 L3 网格模式，继续逐单元点选与排序精修。"));
    v->addWidget(l2ToGridBtn_);
    v->addStretch(1);

    connect(l2Sub_, &QComboBox::currentIndexChanged, this, &ParamPanel::onL2SubChanged);
    for (const QSpinBox* s : {l2x1_, l2y1_, l2x2_, l2y2_})
        connect(s, &QSpinBox::valueChanged, this, &ParamPanel::onCoordEdited);
    connect(l2ToGridBtn_, &QPushButton::clicked, this, &ParamPanel::onConvertToGrid);
    connect(l2RectList_, &QListWidget::currentRowChanged, this, &ParamPanel::onRectListSelectionChanged);
    connect(l2RectDelBtn_, &QPushButton::clicked, this, &ParamPanel::onRectDelClicked);
    connect(l2RectClearBtn_, &QPushButton::clicked, this, &ParamPanel::onRectClearClicked);
    return page;
}

// L3 页：网格定义（基准点 + 单元尺寸 + 余量策略）+ 选择集（全选/反选/清空）+ 排序。
// 说明：行列数由 Core 依图像边界自动推导（只读回显），面板不输入行列数（CONTRIBUTING.md「分层纪律」）；
//       画布上可单击单元切换选中、拖拽框选批量选中、CUSTOM 下拖拽调序（见 canvas/cell_picker_item）；本页提供按钮式选择集与排序策略。
QWidget* ParamPanel::buildL3Page() {
    auto* page = new QWidget(this);
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);

    // ---- 网格定义（FR-L3.1 / FR-L3.2）----
    auto* defBox = new QGroupBox(QStringLiteral("L3 网格定义"), page);
    auto* defForm = new QFormLayout(defBox);
    gx0_ = makeCoordSpin(defBox); gy0_ = makeCoordSpin(defBox);
    gcw_ = makeCoordSpin(defBox); gch_ = makeCoordSpin(defBox);
    gcw_->setMinimum(1); gch_->setMinimum(1); // 单元尺寸必须为正（Core Grid::build 要求）。
    gx0_->setToolTip(QStringLiteral("基准点 x0：竖切割线的起始位置。"));
    gy0_->setToolTip(QStringLiteral("基准点 y0：横切割线的起始位置。"));
    gcw_->setToolTip(QStringLiteral("单元宽 cw：相邻竖切割线的间距（像素）。"));
    gch_->setToolTip(QStringLiteral("单元高 ch：相邻横切割线的间距（像素）。"));
    defForm->addRow(QStringLiteral("基准点 x0"), gx0_);
    defForm->addRow(QStringLiteral("基准点 y0"), gy0_);
    defForm->addRow(QStringLiteral("单元宽 cw"), gcw_);
    defForm->addRow(QStringLiteral("单元高 ch"), gch_);

    gRemainder_ = new QComboBox(defBox);
    // 索引与 Core RemainderPolicy 一致：0=DISCARD, 1=KEEP_PARTIAL, 2=PAD。
    gRemainder_->addItems({QStringLiteral("丢弃残缺"),
                           QStringLiteral("保留残缺"),
                           QStringLiteral("补白")});
    gRemainder_->setToolTip(QStringLiteral("图像边缘的残缺单元如何处理：丢弃、保留、或补白到完整尺寸。"));
    defForm->addRow(QStringLiteral("余量策略"), gRemainder_);

    gGridInfo_ = new QLabel(QStringLiteral("网格：待计算"), defBox);
    gGridInfo_->setWordWrap(true);
    defForm->addRow(QStringLiteral("派生规模"), gGridInfo_);
    v->addWidget(defBox);

    // ---- 选择集（FR-L3.3）----
    auto* selBox = new QGroupBox(QStringLiteral("选择集"), page);
    auto* selV = new QVBoxLayout(selBox);
    auto* btnRow = new QHBoxLayout();
    auto* allBtn = new QPushButton(QStringLiteral("全选"), selBox);
    auto* invBtn = new QPushButton(QStringLiteral("反选"), selBox);
    auto* clrBtn = new QPushButton(QStringLiteral("清空"), selBox);
    btnRow->addWidget(allBtn);
    btnRow->addWidget(invBtn);
    btnRow->addWidget(clrBtn);
    selV->addLayout(btnRow);
    gSelInfo_ = new QLabel(QStringLiteral("已选：0 单元"), selBox);
    gSelInfo_->setWordWrap(true);
    selV->addWidget(gSelInfo_);
    auto* selNote = new QLabel(QStringLiteral("画布上：单击单元切换选中、拖拽框选批量选中。"), selBox);
    selNote->setWordWrap(true);
    selV->addWidget(selNote);
    v->addWidget(selBox);

    // ---- 排序（FR-L3.5）----
    auto* sortBox = new QGroupBox(QStringLiteral("排序"), page);
    auto* sortForm = new QFormLayout(sortBox);
    gSort_ = new QComboBox(sortBox);
    // 索引与 Core SortStrategy 一致：0=ROW_MAJOR, 1=COLUMN_MAJOR, 2=CUSTOM。
    gSort_->addItems({QStringLiteral("横优先"),
                      QStringLiteral("竖优先"),
                      QStringLiteral("自定义")});
    gSort_->setToolTip(QStringLiteral("输出顺序：从左到右逐行（横优先）、从上到下逐列（竖优先）、或按点选顺序（自定义）。"));
    sortForm->addRow(QStringLiteral("策略"), gSort_);
    gReverse_ = new QCheckBox(QStringLiteral("整体逆序"), sortBox);
    gSnake_ = new QCheckBox(QStringLiteral("蛇形（隔行/隔列反向）"), sortBox);
    sortForm->addRow(gReverse_);
    sortForm->addRow(gSnake_);
    v->addWidget(sortBox);
    v->addStretch(1);

    connect(gx0_, &QSpinBox::valueChanged, this, &ParamPanel::onGridOriginEdited);
    connect(gy0_, &QSpinBox::valueChanged, this, &ParamPanel::onGridOriginEdited);
    connect(gcw_, &QSpinBox::valueChanged, this, &ParamPanel::onCellSizeEdited);
    connect(gch_, &QSpinBox::valueChanged, this, &ParamPanel::onCellSizeEdited);
    connect(gRemainder_, &QComboBox::currentIndexChanged, this, &ParamPanel::onRemainderChanged);
    connect(gSort_, &QComboBox::currentIndexChanged, this, &ParamPanel::onSortStrategyChanged);
    connect(gReverse_, &QCheckBox::toggled, this, &ParamPanel::onSortReverseToggled);
    connect(gSnake_, &QCheckBox::toggled, this, &ParamPanel::onSortSnakeToggled);
    connect(allBtn, &QPushButton::clicked, this, &ParamPanel::onSelectAllCells);
    connect(invBtn, &QPushButton::clicked, this, &ParamPanel::onInvertCells);
    connect(clrBtn, &QPushButton::clicked, this, &ParamPanel::onClearCells);
    return page;
}

// 绑定 Document 并同步一次。
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
    gGridInfo_->setText(QStringLiteral("网格：%1 行 × %2 列 · 共 %3 单元")
                            .arg(rows).arg(cols).arg(rows * cols));
    gSelInfo_->setText(QStringLiteral("已选：%1 单元")
                           .arg(doc_->selectedCells().size()));

    // 「转为网格」按钮：有可转换的选区几何时可用——单选区 rect_ 有效，
    // 或多矩形并集（MULTI_RECT）列表非空（转换时取其包围盒，见 Document::convertRectToGrid）。
    if (l2ToGridBtn_) {
        const bool singleOk = doc_->hasRect() && doc_->rect().width() > 0 && doc_->rect().height() > 0;
        const bool multiOk = doc_->l2Sub() == L2Sub::MULTI_RECT && !doc_->rects().empty();
        l2ToGridBtn_->setEnabled(singleOk || multiOk);
    }

    // L2 多矩形列表：仅 MULTI_RECT 显示；依 doc_->rects() 重建，并把选中项坐标回填 spinbox。
    const bool multi = doc_->l2Sub() == L2Sub::MULTI_RECT;
    if (l2MultiBox_) l2MultiBox_->setVisible(multi);
    if (multi && l2RectList_) {
        syncingRectList_ = true;               // 抑制重建期间的 currentRowChanged 回环。
        const int prevRow = l2RectList_->currentRow();
        l2RectList_->clear();
        const auto& rs = doc_->rects();
        for (std::size_t i = 0; i < rs.size(); ++i) {
            l2RectList_->addItem(QStringLiteral("矩形 %1：(%2,%3)-(%4,%5)")
                                     .arg(i + 1).arg(rs[i].left).arg(rs[i].top)
                                     .arg(rs[i].right).arg(rs[i].bottom));
        }
        if (!rs.empty()) {
            const int last = static_cast<int>(rs.size()) - 1;
            l2RectList_->setCurrentRow(prevRow >= 0 && prevRow <= last ? prevRow : last);
        }
        syncingRectList_ = false;
        // 选中矩形坐标覆盖到 spinbox（此前按单 rect_ 回填的值在多矩形下无意义）。
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
        case engine::Tier::L1: stack_->setCurrentIndex(0); break;
        case engine::Tier::L2: stack_->setCurrentIndex(1); break;
        case engine::Tier::L3: stack_->setCurrentIndex(2); break;
    }
}

// 回灌坍缩可行性提示（L2 页）。
void ParamPanel::setCollapseHint(const bool collapsible, const QString& reason) const {
    if (!collapseHint_) return;
    if (collapsible) {
        collapseHint_->setText(QStringLiteral("坍缩可行：剩余块可紧贴拼成一张图。"));
    } else {
        collapseHint_->setText(QStringLiteral("坍缩不可行：%1（导出将自动改用分离导出）").arg(reason));
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
    const bool l1 = doc_->mode() == engine::Tier::L1;
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
    if (!l1 && doc_->l2Sub() == L2Sub::MULTI_RECT) {
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
        item->setText(QStringLiteral("矩形 %1：(%2,%3)-(%4,%5)")
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
