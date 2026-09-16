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
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "model/document.h"

namespace idc::gui {
namespace {

// 创建坐标输入框：范围 [0,100000]，关闭逐键跟踪（提交才触发），回车即生效（NFR-5）。
QSpinBox* makeCoordSpin(QWidget* parent) {
    auto* s = new QSpinBox(parent);
    s->setRange(0, 100000);
    s->setKeyboardTracking(false);
    s->setFixedWidth(90);
    return s;
}

} // namespace

// 构建面板：一个 QStackedWidget 承载 L1/L2/L3 三页。
ParamPanel::ParamPanel(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    stack_ = new QStackedWidget(this);
    stack_->addWidget(buildL1Page()); // index 0
    stack_->addWidget(buildL2Page()); // index 1
    stack_->addWidget(buildL3Page()); // index 2
    root->addWidget(stack_);
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

    auto* note = new QLabel(QStringLiteral("保留选区形状内的区域。切割线是贯穿全图的直线，"
                                          "而非线段；极性可在左侧切换。"), page);
    note->setWordWrap(true);
    v->addWidget(note);
    v->addStretch(1);

    connect(l1Shape_, &QComboBox::currentIndexChanged, this, &ParamPanel::onL1ShapeChanged);
    for (QSpinBox* s : {l1x1_, l1y1_, l1x2_, l1y2_})
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
    l2Sub_->setToolTip(QStringLiteral("每个矩形诱导贯穿全图的十字带，删除区域为所有十字带的并集。"));
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
    // 选中项坐标微调（复用上方 x1/y1/x2/y2）+ 删除/清空。面板只搬运，不做并集计算（A-0.1）。
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
    auto* mnote = new QLabel(QStringLiteral("在画布空白处拖拽框选可追加矩形；矩形可直接拖动/四角缩放/拖边调整。删除区域为各矩形十字带的并集。"), l2MultiBox_);
    mnote->setWordWrap(true);
    mv->addWidget(mnote);
    l2MultiBox_->setVisible(false); // 默认隐藏，切到 MULTI_RECT 才显示（见 syncFromDocument）。
    v->addWidget(l2MultiBox_);

    // 坍缩可行性提示：由 MainWindow 依 Core isCollapsible 结果回灌。
    collapseHint_ = new QLabel(QStringLiteral("坍缩可行性：待选区确定后评估。"), page);
    collapseHint_->setWordWrap(true);
    v->addWidget(collapseHint_);

    auto* outNote = new QLabel(QStringLiteral("输出模式（分离 / 坍缩 / 重排）见右侧「导出」页。"), page);
    outNote->setWordWrap(true);
    v->addWidget(outNote);

    // 「转为网格模式编辑」入口（FR §4.4.3 / G-15）：把当前矩形选区送入 L3 逐单元精修。
    l2ToGridBtn_ = new QPushButton(QStringLiteral("转为网格模式编辑…"), page);
    l2ToGridBtn_->setToolTip(QStringLiteral("以当前矩形选区的左上为基准点、宽高为单元尺寸，"
                                            "切换到 L3 网格模式，继续逐单元点选与排序精修。"));
    v->addWidget(l2ToGridBtn_);
    v->addStretch(1);

    connect(l2Sub_, &QComboBox::currentIndexChanged, this, &ParamPanel::onL2SubChanged);
    for (QSpinBox* s : {l2x1_, l2y1_, l2x2_, l2y2_})
        connect(s, &QSpinBox::valueChanged, this, &ParamPanel::onCoordEdited);
    connect(l2ToGridBtn_, &QPushButton::clicked, this, &ParamPanel::onConvertToGrid);
    connect(l2RectList_, &QListWidget::currentRowChanged, this, &ParamPanel::onRectListSelectionChanged);
    connect(l2RectDelBtn_, &QPushButton::clicked, this, &ParamPanel::onRectDelClicked);
    connect(l2RectClearBtn_, &QPushButton::clicked, this, &ParamPanel::onRectClearClicked);
    return page;
}

// L3 页：网格定义（基准点 + 单元尺寸 + 余量策略）+ 选择集（全选/反选/清空）+ 排序。
// 说明：行列数由 Core 依图像边界自动推导（只读回显），面板不输入行列数（A-0.1）；
//       画布单元点选/自定义拖拽调序由后续增量接入，本页先提供按钮式选择集与排序策略。
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
    gx0_->setToolTip(QStringLiteral("基准点 x0：切割线相位锚，恒落在某条竖切割线上。"));
    gy0_->setToolTip(QStringLiteral("基准点 y0：切割线相位锚，恒落在某条横切割线上。"));
    gcw_->setToolTip(QStringLiteral("单元宽 cw：相邻竖切割线的周期距离（像素）。"));
    gch_->setToolTip(QStringLiteral("单元高 ch：相邻横切割线的周期距离（像素）。"));
    defForm->addRow(QStringLiteral("基准点 x0"), gx0_);
    defForm->addRow(QStringLiteral("基准点 y0"), gy0_);
    defForm->addRow(QStringLiteral("单元宽 cw"), gcw_);
    defForm->addRow(QStringLiteral("单元高 ch"), gch_);

    gRemainder_ = new QComboBox(defBox);
    // 索引与 Core RemainderPolicy 一致：0=DISCARD, 1=KEEP_PARTIAL, 2=PAD。
    gRemainder_->addItems({QStringLiteral("丢弃残缺 (discard)"),
                           QStringLiteral("保留残缺 (keep-partial)"),
                           QStringLiteral("补白 (pad)")});
    gRemainder_->setToolTip(QStringLiteral("跨越图像边界的残缺单元如何处理。"));
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
    gSort_->addItems({QStringLiteral("横优先 (row-major)"),
                      QStringLiteral("竖优先 (column-major)"),
                      QStringLiteral("自定义 (custom)")});
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
    const auto& g = doc_->gridParams();
    for (QSpinBox* s : {gx0_, gy0_, gcw_, gch_}) s->blockSignals(true);
    gx0_->setValue(g.originX);
    gy0_->setValue(g.originY);
    gcw_->setValue(g.cellWidth);
    gch_->setValue(g.cellHeight);
    for (QSpinBox* s : {gx0_, gy0_, gcw_, gch_}) s->blockSignals(false);
    // 有图时把基准点/单元尺寸上限收紧到图像尺寸。
    if (doc_->hasImage()) {
        gx0_->setMaximum(doc_->width());
        gy0_->setMaximum(doc_->height());
    }
    gRemainder_->blockSignals(true);
    gRemainder_->setCurrentIndex(static_cast<int>(g.remainder));
    gRemainder_->blockSignals(false);

    // L3 排序回填。
    const auto& o = doc_->order();
    gSort_->blockSignals(true);
    gSort_->setCurrentIndex(static_cast<int>(o.strategy));
    gSort_->blockSignals(false);
    gReverse_->blockSignals(true);
    gReverse_->setChecked(o.reverse);
    gReverse_->blockSignals(false);
    gSnake_->blockSignals(true);
    gSnake_->setChecked(o.snake);
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
        const bool multiOk = (doc_->l2Sub() == L2Sub::MULTI_RECT) && !doc_->rects().empty();
        l2ToGridBtn_->setEnabled(singleOk || multiOk);
    }

    // L2 多矩形列表：仅 MULTI_RECT 显示；依 doc_->rects() 重建，并把选中项坐标回填 spinbox。
    const bool multi = (doc_->l2Sub() == L2Sub::MULTI_RECT);
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
            l2RectList_->setCurrentRow((prevRow >= 0 && prevRow <= last) ? prevRow : last);
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
}

// 切换到某模式对应的页。
void ParamPanel::setMode(const idc::engine::Tier tier) {
    switch (tier) {
        case idc::engine::Tier::L1: stack_->setCurrentIndex(0); break;
        case idc::engine::Tier::L2: stack_->setCurrentIndex(1); break;
        case idc::engine::Tier::L3: stack_->setCurrentIndex(2); break;
    }
}

// 回灌坍缩可行性提示（L2 页）。
void ParamPanel::setCollapseHint(const bool collapsible, const QString& reason) {
    if (!collapseHint_) return;
    if (collapsible) {
        collapseHint_->setText(QStringLiteral("坍缩可行：删除整行/整列后，剩余单元可紧贴拼接。"));
    } else {
        collapseHint_->setText(QStringLiteral("坍缩不可行：%1（导出将自动降级为重排）").arg(reason));
    }
}

// L1 形状切换 → 写回 Document。
void ParamPanel::onL1ShapeChanged(int index) {
    if (!doc_) return;
    doc_->setL1Shape(static_cast<L1Shape>(index));
}

// L2 子功能切换 → 写回 Document（index 0..3，3 = 多矩形并集）。
void ParamPanel::onL2SubChanged(int index) {
    if (!doc_ || index < 0 || index > 3) return;
    doc_->setL2Sub(static_cast<L2Sub>(index));
}

// 坐标变更统一入口：依当前模式取对应页 spinbox，组装规范化矩形写回 Document。
void ParamPanel::onCoordEdited() {
    applyCoordsToDocument();
}

// 组装规范化矩形（左上/右下）写回 Document。
void ParamPanel::applyCoordsToDocument() {
    if (!doc_) return;
    const bool l1 = (doc_->mode() == idc::engine::Tier::L1);
    QSpinBox* x1 = l1 ? l1x1_ : l2x1_;
    QSpinBox* y1 = l1 ? l1y1_ : l2y1_;
    QSpinBox* x2 = l1 ? l1x2_ : l2x2_;
    QSpinBox* y2 = l1 ? l1y2_ : l2y2_;

    const int a = x1->value(), b = y1->value(), c = x2->value(), d = y2->value();
    idc::engine::RectRegion r(std::min(a, c), std::min(b, d), std::max(a, c), std::max(b, d));
    // 校验：x1==x2 或 y1==y2 会得到零宽/零高的退化矩形，直接送入 Core 会崩溃；
    // 此处拒绝退化输入（保持上一次有效选区），Document::setRect 另有兜底（A-0.1）。
    if (r.width() <= 0 || r.height() <= 0) return;
    // L2 多矩形：坐标框编辑的是列表中当前选中的矩形（updateRect），而非单选区 rect_。
    if (!l1 && doc_->l2Sub() == L2Sub::MULTI_RECT) {
        const int row = l2RectList_ ? l2RectList_->currentRow() : -1;
        if (row >= 0) doc_->updateRect(static_cast<std::size_t>(row), r);
        return;
    }
    doc_->setRect(r); // 触发预览刷新。
}

// 「转为网格模式编辑」：把当前矩形选区送入 L3（Document 负责参数映射与模式切换）。
void ParamPanel::onConvertToGrid() {
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
void ParamPanel::updateRectListItem(const int index) {
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
void ParamPanel::selectRectRow(const int index) {
    if (!doc_ || !l2RectList_) return;
    if (l2RectList_->currentRow() == index) return;
    if (index < 0 || index >= static_cast<int>(doc_->rects().size())) return;
    l2RectList_->setCurrentRow(index);
}

// 删除选中矩形。
void ParamPanel::onRectDelClicked() {
    if (!doc_ || !l2RectList_) return;
    const int row = l2RectList_->currentRow();
    if (row < 0) return;
    doc_->removeRect(static_cast<std::size_t>(row));
}

// 清空多矩形列表。
void ParamPanel::onRectClearClicked() {
    if (!doc_) return;
    doc_->clearRects();
}

// ---- L3 槽：网格参数 / 选择集 / 排序 → 写回 Document ----

// 基准点 x0/y0 变更。
void ParamPanel::onGridOriginEdited() {
    if (!doc_) return;
    doc_->setGridOrigin(gx0_->value(), gy0_->value());
}

// 单元尺寸 cw/ch 变更（Document 侧拒绝非正值）。
void ParamPanel::onCellSizeEdited() {
    if (!doc_) return;
    doc_->setCellSize(gcw_->value(), gch_->value());
}

// 余量策略变更（combo 索引与 RemainderPolicy 一致）。
void ParamPanel::onRemainderChanged(const int index) {
    if (!doc_ || index < 0 || index > 2) return;
    doc_->setRemainder(static_cast<idc::engine::RemainderPolicy>(index));
}

// 排序策略变更（combo 索引与 SortStrategy 一致）。
void ParamPanel::onSortStrategyChanged(const int index) {
    if (!doc_ || index < 0 || index > 2) return;
    doc_->setSortStrategy(static_cast<idc::engine::SortStrategy>(index));
}

// 整体逆序开关。
void ParamPanel::onSortReverseToggled(const bool on) {
    if (!doc_) return;
    doc_->setSortReverse(on);
}

// 蛇形排序开关。
void ParamPanel::onSortSnakeToggled(const bool on) {
    if (!doc_) return;
    doc_->setSortSnake(on);
}

// 全选（依派生行列数生成全序号）。
void ParamPanel::onSelectAllCells() {
    if (!doc_) return;
    doc_->selectAllCells();
}

// 反选（依派生行列数取补集）。
void ParamPanel::onInvertCells() {
    if (!doc_) return;
    doc_->invertCells();
}

// 清空选择集。
void ParamPanel::onClearCells() {
    if (!doc_) return;
    doc_->clearCells();
}

} // namespace idc::gui
