// ============================================================================
// 文件：panels/param_panel.cpp
// 作用：实现右侧参数面板的三页构建与 Document 同步（见同名头文件说明）。
// 分块依据：buildL1Page/buildL2Page/buildL3Page 各自独立，互不干扰；坐标写回统一走
//           applyCoordsToDocument（依当前模式选取对应页的 spinbox）。
// ============================================================================
#include "panels/param_panel.h"

#include <algorithm>

#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardItemModel>
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
    // 多矩形并集剔除在第二阶段接入，先禁用该项。
    if (auto* m = qobject_cast<QStandardItemModel*>(l2Sub_->model())) {
        if (QStandardItem* it = m->item(3)) it->setEnabled(false);
    }
    form->addRow(QStringLiteral("子功能"), l2Sub_);

    l2x1_ = makeCoordSpin(box); l2y1_ = makeCoordSpin(box);
    l2x2_ = makeCoordSpin(box); l2y2_ = makeCoordSpin(box);
    form->addRow(QStringLiteral("x1"), l2x1_);
    form->addRow(QStringLiteral("y1"), l2y1_);
    form->addRow(QStringLiteral("x2"), l2x2_);
    form->addRow(QStringLiteral("y2"), l2y2_);
    v->addWidget(box);

    // 坍缩可行性提示：由 MainWindow 依 Core isCollapsible 结果回灌。
    collapseHint_ = new QLabel(QStringLiteral("坍缩可行性：待选区确定后评估。"), page);
    collapseHint_->setWordWrap(true);
    v->addWidget(collapseHint_);

    auto* outNote = new QLabel(QStringLiteral("输出模式（分离 / 坍缩 / 重排）见右侧「导出」页。"), page);
    outNote->setWordWrap(true);
    v->addWidget(outNote);
    v->addStretch(1);

    connect(l2Sub_, &QComboBox::currentIndexChanged, this, &ParamPanel::onL2SubChanged);
    for (QSpinBox* s : {l2x1_, l2y1_, l2x2_, l2y2_})
        connect(s, &QSpinBox::valueChanged, this, &ParamPanel::onCoordEdited);
    return page;
}

// L3 页：占位（第二阶段接入网格参数与排序）。
QWidget* ParamPanel::buildL3Page() {
    auto* page = new QWidget(this);
    auto* v = new QVBoxLayout(page);
    auto* label = new QLabel(QStringLiteral(
        "L3 网格分割参数（第二阶段接入）：\n基准点 (x0,y0)、单元尺寸 (cellWidth,cellHeight)、"
        "余量策略、选择集、排序策略与自定义序列。"), page);
    label->setWordWrap(true);
    label->setEnabled(false);
    v->addWidget(label);
    v->addStretch(1);
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

// L2 子功能切换 → 写回 Document（多矩形并集项已禁用，index 仅 0..2）。
void ParamPanel::onL2SubChanged(int index) {
    if (!doc_ || index < 0 || index > 2) return;
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
    doc_->setRect(r); // 触发预览刷新。
}

} // namespace idc::gui
