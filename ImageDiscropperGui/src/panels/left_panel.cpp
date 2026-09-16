// ============================================================================
// 文件：panels/left_panel.cpp
// 作用：实现左侧面板的构建与 Document 双向同步（见同名头文件说明）。
// 分块依据：build 阶段用 QGroupBox 分区（模式 / 极性 / 工具占位 / 图层占位）；
//           槽函数只把用户选择写回 Document，不触碰任何引擎逻辑（A-0.1）。
// ============================================================================
#include "panels/left_panel.h"

#include <QButtonGroup>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "engine/engine.h"
#include "model/document.h"

namespace idc::gui {
namespace {

// 模式按钮的强调样式：L2（核心特色）用橙色强调，其余中性；选中态高亮（§4.3）。
const char* kModeStyle =
    "QPushButton{padding:6px;border:1px solid #bbb;border-radius:4px;background:#f2f2f2;}"
    "QPushButton:checked{background:#3b7ddd;color:#fff;border-color:#3b7ddd;}";
const char* kL2Style =
    "QPushButton{padding:6px;border:1px solid #d9a05b;border-radius:4px;background:#fdeede;}"
    "QPushButton:checked{background:#e67e22;color:#fff;border-color:#e67e22;}";
// 极性按钮：keep 绿、remove 红，选中态填充对应色（§4.4）。
const char* kKeepStyle =
    "QPushButton{padding:6px;border:1px solid #bbb;border-radius:4px;}"
    "QPushButton:checked{background:#2ecc71;color:#fff;border-color:#2ecc71;}";
const char* kRemoveStyle =
    "QPushButton{padding:6px;border:1px solid #bbb;border-radius:4px;}"
    "QPushButton:checked{background:#e74c3c;color:#fff;border-color:#e74c3c;}";

} // namespace

// 构建面板布局。
LeftPanel::LeftPanel(QWidget* parent) : QWidget(parent) {
    setMinimumWidth(180); // §4.1：左侧面板默认约 220px（初始宽由主窗口分隔条设定），可拖拽调宽。
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(12); // 分组间距 12px（§5.1）。

    // ---- 模式切换分组 ----
    auto* modeBox = new QGroupBox(QStringLiteral("模式"), this);
    auto* modeLay = new QVBoxLayout(modeBox);
    modeLay->setSpacing(6); // 控件间距 6px。
    modeGroup_ = new QButtonGroup(this);
    modeGroup_->setExclusive(true);

    auto* l1Btn = new QPushButton(QStringLiteral("标准提取 (L1)"), modeBox);
    l1Btn->setCheckable(true);
    l1Btn->setStyleSheet(kModeStyle);
    l1Btn->setToolTip(QStringLiteral("保留框内区域，输出裁剪结果。默认进入模式，最易上手。"));
    modeGroup_->addButton(l1Btn, 1);
    modeLay->addWidget(l1Btn);

    auto* l2Btn = new QPushButton(QStringLiteral("反向剔除 (L2)"), modeBox);
    l2Btn->setCheckable(true);
    l2Btn->setStyleSheet(kL2Style); // 强调色区分核心特色。
    l2Btn->setToolTip(QStringLiteral(
        "每个矩形诱导贯穿全图的十字带，删除区域为所有十字带的并集，保留其余部分。"));
    modeGroup_->addButton(l2Btn, 2);
    modeLay->addWidget(l2Btn);

    auto* l3Btn = new QPushButton(QStringLiteral("网格分割 (L3)"), modeBox);
    l3Btn->setCheckable(true);
    l3Btn->setStyleSheet(kModeStyle);
    l3Btn->setToolTip(QStringLiteral("以基准点 + 单元尺寸铺满全图，选择单元并排序导出。"));
    modeGroup_->addButton(l3Btn, 3);
    modeLay->addWidget(l3Btn);

    connect(modeGroup_, &QButtonGroup::idToggled, this, &LeftPanel::onModeToggled);
    root->addWidget(modeBox);

    // ---- 极性开关分组 ----
    auto* polBox = new QGroupBox(QStringLiteral("极性"), this);
    auto* polLay = new QVBoxLayout(polBox);
    polLay->setSpacing(6);
    polarityGroup_ = new QButtonGroup(this);
    polarityGroup_->setExclusive(true);

    auto* keepBtn = new QPushButton(QStringLiteral("保留框内 (keep)"), polBox);
    keepBtn->setCheckable(true);
    keepBtn->setStyleSheet(kKeepStyle);
    keepBtn->setToolTip(QStringLiteral("保留选区内的单元（绿色遮罩）。"));
    polarityGroup_->addButton(keepBtn, 0);
    polLay->addWidget(keepBtn);

    auto* removeBtn = new QPushButton(QStringLiteral("删除框内 (remove)"), polBox);
    removeBtn->setCheckable(true);
    removeBtn->setStyleSheet(kRemoveStyle);
    removeBtn->setToolTip(QStringLiteral("剔除选区内的单元（红色遮罩），保留其余部分。"));
    polarityGroup_->addButton(removeBtn, 1);
    polLay->addWidget(removeBtn);

    connect(polarityGroup_, &QButtonGroup::idToggled, this, &LeftPanel::onPolarityToggled);
    root->addWidget(polBox);

    // ---- 工具占位（标注工具，第四阶段接入）----
    auto* toolBox = new QGroupBox(QStringLiteral("工具"), this);
    auto* toolLay = new QVBoxLayout(toolBox);
    auto* toolHint = new QLabel(QStringLiteral("标注工具（第四阶段接入）"), toolBox);
    toolHint->setEnabled(false);
    toolLay->addWidget(toolHint);
    root->addWidget(toolBox);

    // ---- 图层占位（图层管理，第四阶段接入）----
    auto* layerBox = new QGroupBox(QStringLiteral("图层"), this);
    auto* layerLay = new QVBoxLayout(layerBox);
    auto* layerHint = new QLabel(QStringLiteral("底图 / 标注图层（第四阶段接入）"), layerBox);
    layerHint->setEnabled(false);
    layerLay->addWidget(layerHint);
    root->addWidget(layerBox);

    root->addStretch(1); // 底部弹性，使分组靠上。
}

// 绑定 Document 并同步一次。
void LeftPanel::setDocument(Document* doc) {
    doc_ = doc;
    syncFromDocument();
}

// 从 Document 反向同步按钮选中态。
void LeftPanel::syncFromDocument() {
    if (!doc_) return;

    // 模式：Tier → 按钮 id（L1=1, L2=2, L3=3）。
    int modeId = 1;
    switch (doc_->mode()) {
        case idc::engine::Tier::L1: modeId = 1; break;
        case idc::engine::Tier::L2: modeId = 2; break;
        case idc::engine::Tier::L3: modeId = 3; break;
    }
    modeGroup_->blockSignals(true);
    if (auto* b = modeGroup_->button(modeId)) b->setChecked(true);
    modeGroup_->blockSignals(false);

    // 极性：KEEP=0, REMOVE=1。
    const int polId = (doc_->polarity() == idc::engine::Polarity::REMOVE) ? 1 : 0;
    polarityGroup_->blockSignals(true);
    if (auto* b = polarityGroup_->button(polId)) b->setChecked(true);
    polarityGroup_->blockSignals(false);
}

// 模式按钮切换 → 写回 Document（Document 会发 changed 触发刷新与面板再同步）。
void LeftPanel::onModeToggled(const int id, const bool checked) {
    if (!checked || !doc_) return;
    switch (id) {
        case 1: doc_->setMode(idc::engine::Tier::L1); break;
        case 2: doc_->setMode(idc::engine::Tier::L2); break;
        case 3: doc_->setMode(idc::engine::Tier::L3); break;
        default: break;
    }
}

// 极性按钮切换 → 写回 Document，实时刷新遮罩（NFR-6）。
void LeftPanel::onPolarityToggled(const int id, const bool checked) {
    if (!checked || !doc_) return;
    if (id == 1) doc_->setPolarity(idc::engine::Polarity::REMOVE);
    else if (id == 0) doc_->setPolarity(idc::engine::Polarity::KEEP);
}

} // namespace idc::gui
