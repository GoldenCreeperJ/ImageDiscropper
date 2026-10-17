// ============================================================================
// 文件：panels/left_panel.cpp
// 作用：实现左侧面板的构建与 Document 双向同步（见同名头文件说明）。
// 分块依据：build 中用 QGroupBox 分区（模式 / 极性）；
//           槽函数只把用户选择写回 Document，不触碰任何引擎逻辑（CONTRIBUTING.md「分层纪律」）。
// ============================================================================
#include "panels/left_panel.h"

#include <algorithm>

#include <QButtonGroup>
#include <QGroupBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "engine/engine.h"
#include "model/document.h"
#include "util/gui_nums.h"
#include "i18n/gui_texts.h"
#include "i18n/gui_i18n.h"

namespace idc::gui {
namespace {

// 模式按钮的强调样式：未选态底/字/边框走 palette(...) 令牌（随深/浅主题自动给正确字色）；
// 选中态品牌强调色（蓝 #3b7ddd / 橙 #e67e22）原样保留（均配白字，两主题皆可读）。
// 注：QSS palette() 角色名按 QPalette 枚举名大小写不敏感匹配、**不带连字符**（buttontext/windowtext/midtext）。
auto kModeStyle =
    "QPushButton{padding:6px;border:1px solid palette(mid);border-radius:4px;background:palette(button);color:palette(buttontext);}"
    "QPushButton:checked{background:#3b7ddd;color:#fff;border-color:#3b7ddd;}";
// L2（核心特色）：刻意保留浅橙弱化底 + 橙描边 + 深字作品牌识别（底恒为浅，故深字两主题皆可读，不随 palette 走）。
auto kL2Style =
    "QPushButton{padding:6px;border:1px solid #d9a05b;border-radius:4px;background:#fdeede;color:#1b1b1b;}"
    "QPushButton:checked{background:#e67e22;color:#fff;border-color:#e67e22;}";
// 极性按钮：未选态不覆底/字（交默认 button palette 随主题），仅边框走 palette(mid)；选中态填充对应品牌色。
auto kKeepStyle =
    "QPushButton{padding:6px;border:1px solid palette(mid);border-radius:4px;}"
    "QPushButton:checked{background:#2ecc71;color:#fff;border-color:#2ecc71;}";
auto kRemoveStyle =
    "QPushButton{padding:6px;border:1px solid palette(mid);border-radius:4px;}"
    "QPushButton:checked{background:#e74c3c;color:#fff;border-color:#e74c3c;}";

} // namespace

// 构建面板布局。
LeftPanel::LeftPanel(QWidget* parent) : QWidget(parent) {
    setMinimumWidth(nums::panels::kLeftPanelMinWidthPx); // 左侧面板下限（实际左栏最小宽由主窗 leftScroll 控制，需容纳标注工具 3 列网格）。
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(nums::panels::kPanelMarginPx, nums::panels::kPanelMarginPx,
                             nums::panels::kPanelMarginPx, nums::panels::kPanelMarginPx);
    root->setSpacing(nums::panels::kPanelGroupSpacingPx); // 分组间距 12px（本目录 README「画布视觉规范」）。

    // ---- 模式切换分组 ----
    auto* modeBox = new QGroupBox(this);
    i18n::bindTitle(retranslator_, modeBox, IDC_GT(label, kModeSection));
    auto* modeLay = new QHBoxLayout(modeBox);
    modeLay->setSpacing(nums::panels::kPanelSpacingPx); // 控件间距 6px。
    modeGroup_ = new QButtonGroup(this);
    modeGroup_->setExclusive(true);

    auto* l1Btn = new QPushButton(modeBox);
    i18n::bindButton(retranslator_, l1Btn, IDC_GT(menu, kModeL1));
    l1Btn->setCheckable(true);
    l1Btn->setStyleSheet(kModeStyle);
    i18n::bindToolTip(retranslator_, l1Btn, IDC_GT(tooltip, kModeL1Btn));
    modeGroup_->addButton(l1Btn, 1);
    modeLay->addWidget(l1Btn);

    auto* l2Btn = new QPushButton(modeBox);
    i18n::bindButton(retranslator_, l2Btn, IDC_GT(menu, kModeL2));
    l2Btn->setCheckable(true);
    l2Btn->setStyleSheet(kL2Style); // 强调色区分核心特色。
    i18n::bindToolTip(retranslator_, l2Btn, IDC_GT(tooltip, kModeL2Btn));
    modeGroup_->addButton(l2Btn, 2);
    modeLay->addWidget(l2Btn);

    auto* l3Btn = new QPushButton(modeBox);
    i18n::bindButton(retranslator_, l3Btn, IDC_GT(menu, kModeL3));
    l3Btn->setCheckable(true);
    l3Btn->setStyleSheet(kModeStyle);
    i18n::bindToolTip(retranslator_, l3Btn, IDC_GT(tooltip, kModeL3Btn));
    modeGroup_->addButton(l3Btn, 3);
    modeLay->addWidget(l3Btn);

    connect(modeGroup_, &QButtonGroup::idToggled, this, &LeftPanel::onModeToggled);
    root->addWidget(modeBox);

    // ---- 极性开关分组 ----
    auto* polBox = new QGroupBox(this);
    i18n::bindTitle(retranslator_, polBox, IDC_GT(label, kPolarityGroupBox));
    auto* polLay = new QHBoxLayout(polBox);
    polLay->setSpacing(nums::panels::kPanelSpacingPx);
    polarityGroup_ = new QButtonGroup(this);
    polarityGroup_->setExclusive(true);

    auto* keepBtn = new QPushButton(polBox);
    i18n::bindButton(retranslator_, keepBtn, IDC_GT(label, kPolarityKeepButton));
    keepBtn->setCheckable(true);
    keepBtn->setStyleSheet(kKeepStyle);
    i18n::bindToolTip(retranslator_, keepBtn, IDC_GT(tooltip, kPolarityKeepBtn));
    polarityGroup_->addButton(keepBtn, 0);
    polLay->addWidget(keepBtn);

    auto* removeBtn = new QPushButton(polBox);
    i18n::bindButton(retranslator_, removeBtn, IDC_GT(label, kPolarityRemoveButton));
    removeBtn->setCheckable(true);
    removeBtn->setStyleSheet(kRemoveStyle);
    i18n::bindToolTip(retranslator_, removeBtn, IDC_GT(tooltip, kPolarityRemoveBtn));
    polarityGroup_->addButton(removeBtn, 1);
    polLay->addWidget(removeBtn);

    connect(polarityGroup_, &QButtonGroup::idToggled, this, &LeftPanel::onPolarityToggled);
    root->addWidget(polBox);

    // 工具 / 图层分组分属独立的 ModeToolPanel / LayerPanel，由主窗口左侧容器统一装配；
    // 本面板只放模式切换与保留/删除开关。

    root->addStretch(1); // 底部弹性，使分组靠上。

    // i18n 自适应宽度：220 下限按中文标定；英文模式/极性按钮（如「Keep Inside /
    // Remove Inside」）更长，构造完成后以本面板实测宽抬高下限（取 max），避免截断。
    setMinimumWidth(std::max(nums::panels::kLeftPanelMinWidthPx, root->sizeHint().width()));
}

// 语言热切换重放：重跑本面板登记的模式/极性标题与按钮文案闭包，再按新文本重测最小宽。
void LeftPanel::retranslate() {
    retranslator_.run();
    if (const auto* lay = layout())
        setMinimumWidth(std::max(nums::panels::kLeftPanelMinWidthPx, lay->sizeHint().width()));
}

// 绑定 Document 并同步一次。
void LeftPanel::setDocument(Document* doc) {
    doc_ = doc;
    syncFromDocument();
}

// 从 Document 反向同步按钮选中态。
void LeftPanel::syncFromDocument() const {
    if (!doc_) return;

    // 模式：Tier → 按钮 id（L1=1, L2=2, L3=3）。
    int modeId = 1;
    switch (doc_->mode()) {
        case engine::Tier::Standard: modeId = 1; break;
        case engine::Tier::Erase: modeId = 2; break;
        case engine::Tier::Grid: modeId = 3; break;
    }
    modeGroup_->blockSignals(true);
    if (auto* b = modeGroup_->button(modeId)) b->setChecked(true);
    modeGroup_->blockSignals(false);

    // 极性：Keep=0, Remove=1。
    const int polId = doc_->polarity() == engine::Polarity::Remove ? 1 : 0;
    polarityGroup_->blockSignals(true);
    if (auto* b = polarityGroup_->button(polId)) b->setChecked(true);
    polarityGroup_->blockSignals(false);
}

// 模式按钮切换 → 写回 Document（Document 会发 changed 触发刷新与面板再同步）。
void LeftPanel::onModeToggled(const int id, const bool checked) const {
    if (!checked || !doc_) return;
    switch (id) {
        case 1: doc_->setMode(engine::Tier::Standard); break;
        case 2: doc_->setMode(engine::Tier::Erase); break;
        case 3: doc_->setMode(engine::Tier::Grid); break;
        default: break;
    }
}

// 极性按钮切换 → 写回 Document，实时刷新遮罩（NFR-6）。
void LeftPanel::onPolarityToggled(const int id, const bool checked) const {
    if (!checked || !doc_) return;
    if (id == 1) doc_->setPolarity(engine::Polarity::Remove);
    else if (id == 0) doc_->setPolarity(engine::Polarity::Keep);
}

} // namespace idc::gui
