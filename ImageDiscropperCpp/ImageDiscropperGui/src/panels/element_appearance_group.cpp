// ============================================================================
// 文件：panels/element_appearance_group.cpp
// 作用：实现 ElementAppearanceGroup（见同名头文件说明）——几何/色彩两个独立区（各带类型行 +
//       步骤列表 + 移/删行；色彩区另含作用通道行）+ 不透明度采集。
// ============================================================================
#include "panels/element_appearance_group.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include <variant>

#include "preprocess/preprocess_config.h"
#include "util/gui_nums.h"
#include "util/widget_factory.h"
#include "i18n/gui_texts.h"
#include "i18n/gui_i18n.h"

namespace idc::gui {

namespace {
// 几何步骤类型 → 构造（简表口径：无 resize——元素外观不改节点区域）。
preprocess::PreprocessConfig makeGeoStep(const QString& kind) {
    using namespace preprocess;
    if (kind == u"rotate90") return RotateOp{90};
    if (kind == u"rotate180") return RotateOp{180};
    if (kind == u"rotate270") return RotateOp{270};
    if (kind == u"flipH") return FlipOp{true};
    return FlipOp{false};   // flipV
}

// 色彩步骤类型 → 构造（遮码来自作用通道行 R/G/B 三勾；gray 忽略遮码）。
preprocess::PreprocessConfig makeColorStep(const QString& kind, const std::string& mask) {
    using namespace preprocess;
    if (kind == u"gray") return GrayOp{};
    if (kind == u"invert") return InvertOp{mask};
    return SplitOp{mask};   // split
}

// 步骤 → 行文本（与预处理面板行文本同源；未知类型回退 kStepUnknown）。
QString stepText(const preprocess::PreprocessConfig& cfg) {
    using namespace preprocess;
    if (std::holds_alternative<GrayOp>(cfg)) return i18n::qs(IDC_GT(label, kColorTypeGray));
    if (const auto* s = std::get_if<InvertOp>(&cfg)) return IDC_GTR(status, kStepInvertDesc).arg(QString::fromStdString(s->mask));
    if (const auto* s = std::get_if<SplitOp>(&cfg)) return IDC_GTR(status, kStepSplitDesc).arg(QString::fromStdString(s->mask));
    if (const auto* s = std::get_if<RotateOp>(&cfg)) return IDC_GTR(status, kStepRotateDesc).arg(s->angle);
    if (const auto* s = std::get_if<FlipOp>(&cfg))
        return i18n::qs(s->horizontal ? IDC_GT(menu, kFlipHorizontal) : IDC_GT(menu, kFlipVertical));
    return IDC_GTR(status, kStepUnknown);
}

// pipeline 前段几何块长度（几何恒前，与预处理面板 geo→color 不变式同口径）。
std::size_t geoBlockSize(const preprocess::PreprocessPipeline& pipeline) {
    std::size_t n = 0;
    for (const preprocess::PreprocessConfig& cfg : pipeline.items()) {
        if (preprocess::isColorStep(cfg)) break;
        ++n;
    }
    return n;
}
} // namespace

ElementAppearanceGroup::ElementAppearanceGroup(QWidget* parent, const bool geometricAllowed, const bool showOpacity)
    : QGroupBox(parent), geometricAllowed_(geometricAllowed), showOpacity_(showOpacity) {
    setTitle(IDC_GTR(label, kElementAppearanceBox));
    auto* root = new QVBoxLayout(this);
    root->setSpacing(nums::panels::kPanelTightSpacingPx);

    // 窄屏判据（与对话框同口径）：操作行（更新/上移/下移/删除）改 2×2 两行排，压最小宽防竖屏撑爆。
    const QScreen* sc = screen();
    if (!sc) sc = QGuiApplication::primaryScreen();
    const bool compact = !sc || sc->availableGeometry().width() < nums::mobile::kStackColumnsMaxWidthPx;

    // ---- 几何区（类型行 + 列表 + 移/删行）：仅 geometricAllowed 显示。条目 itemData 存构造 kind
    //      （makeGeoStep 键），文案重放用 setItemText 原地改字（保 currentIndex/userData，retranslate.h 纪律）。----
    geoSection_ = new QWidget(this);
    {
        auto* lay = new QVBoxLayout(geoSection_);
        lay->setContentsMargins(0, 0, 0, 0);
        geoCombo_ = new QComboBox(geoSection_);
        const QStringList kinds{QStringLiteral("rotate90"), QStringLiteral("rotate180"), QStringLiteral("rotate270"),
                                QStringLiteral("flipH"), QStringLiteral("flipV")};
        for (const QString& k : kinds) geoCombo_->addItem(QString(), k);
        applyGeoKindNames();
        retranslator_.add([this] { applyGeoKindNames(); });
        geoAddBtn_ = makeButton(retranslator_, geoSection_, IDC_GT(label, kAppearanceAddStep));
        auto* typeRow = new QHBoxLayout();
        typeRow->addWidget(new QLabel(IDC_GTR(label, kGeoSectionBox), geoSection_));
        typeRow->addWidget(geoCombo_, 1);
        typeRow->addWidget(geoAddBtn_);
        lay->addLayout(typeRow);
        geoList_ = new QListWidget(geoSection_);
        geoList_->setFixedHeight(nums::dialog::kTouchFieldMinHeightPx * 3);
        lay->addWidget(geoList_);
        geoUpdateBtn_ = makeButton(retranslator_, geoSection_, IDC_GT(label, kUpdateSelectedButton));
        geoMoveUpBtn_ = makeButton(retranslator_, geoSection_, IDC_GT(label, kMoveUpButton));
        geoMoveDownBtn_ = makeButton(retranslator_, geoSection_, IDC_GT(label, kMoveDownButton));
        geoRemoveBtn_ = makeButton(retranslator_, geoSection_, IDC_GT(label, kRemoveButton));
        geoOpGrid_ = new QGridLayout();   // 操作行形态（单行 4 列 / 2×2）由 rebuildOpRows 按 compact_ 重摆。
        lay->addLayout(geoOpGrid_);
    }
    geoSection_->setVisible(geometricAllowed_);
    root->addWidget(geoSection_);

    // ---- 色彩区（类型行 + 作用通道行 + 列表 + 移/删行）。----
    {
        colorCombo_ = new QComboBox(this);
        const QStringList kinds{QStringLiteral("gray"), QStringLiteral("invert"), QStringLiteral("split")};
        for (const QString& k : kinds) colorCombo_->addItem(QString(), k);
        applyColorKindNames();
        retranslator_.add([this] { applyColorKindNames(); });
        colorAddBtn_ = makeButton(retranslator_, this, IDC_GT(label, kAppearanceAddStep));
        auto* typeRow = new QHBoxLayout();
        typeRow->addWidget(new QLabel(IDC_GTR(label, kColorSectionBox), this));
        typeRow->addWidget(colorCombo_, 1);
        typeRow->addWidget(colorAddBtn_);
        root->addLayout(typeRow);

        // 「作用通道」行（仅反色/分离显示；与预处理面板同款 R/G/B 三勾，缺省全选）。
        chanRow_ = new QWidget(this);
        {
            auto* h = new QHBoxLayout(chanRow_);
            h->setContentsMargins(0, 0, 0, 0);
            h->addWidget(new QLabel(IDC_GTR(label, kChannelFieldLabel), chanRow_));
            chR_ = new QCheckBox(QStringLiteral("R"), chanRow_);
            chG_ = new QCheckBox(QStringLiteral("G"), chanRow_);
            chB_ = new QCheckBox(QStringLiteral("B"), chanRow_);
            for (QCheckBox* c : {chR_, chG_, chB_}) c->setChecked(true);
            h->addWidget(chR_);
            h->addWidget(chG_);
            h->addWidget(chB_);
            h->addStretch(1);
        }
        root->addWidget(chanRow_);
        updateChanRow();

        colorList_ = new QListWidget(this);
        colorList_->setFixedHeight(nums::dialog::kTouchFieldMinHeightPx * 3);
        root->addWidget(colorList_);
        colorUpdateBtn_ = makeButton(retranslator_, this, IDC_GT(label, kUpdateSelectedButton));
        colorMoveUpBtn_ = makeButton(retranslator_, this, IDC_GT(label, kMoveUpButton));
        colorMoveDownBtn_ = makeButton(retranslator_, this, IDC_GT(label, kMoveDownButton));
        colorRemoveBtn_ = makeButton(retranslator_, this, IDC_GT(label, kRemoveButton));
        colorOpGrid_ = new QGridLayout();
        root->addLayout(colorOpGrid_);
    }

    compact_ = compact;
    rebuildOpRows();   // 依 compact 形态摆好两区操作行按钮。

    // 不透明度（元素外观·不透明度，0..255）；节点外观实例不显示（由对话框整体不透明度行独家采集）。
    if (showOpacity_) {
        opacitySpin_ = makeSpin(this, 0, 255);
        opacitySpin_->setValue(255);
        opacitySpin_->setToolTip(IDC_GTR(tooltip, kLayerOpacitySpin));
        auto* opRow2 = new QHBoxLayout();
        opRow2->addWidget(new QLabel(IDC_GTR(label, kOpacityFieldLabel), this));
        opRow2->addWidget(opacitySpin_, 1);
        root->addLayout(opRow2);
        connect(opacitySpin_, qOverload<int>(&QSpinBox::valueChanged), this, &ElementAppearanceGroup::changed);
    }

    connect(geoAddBtn_, &QPushButton::clicked, this, &ElementAppearanceGroup::onAddGeoStep);
    connect(colorAddBtn_, &QPushButton::clicked, this, &ElementAppearanceGroup::onAddColorStep);
    connect(geoUpdateBtn_, &QPushButton::clicked, this, &ElementAppearanceGroup::onUpdateGeoStep);
    connect(colorUpdateBtn_, &QPushButton::clicked, this, &ElementAppearanceGroup::onUpdateColorStep);
    connect(geoRemoveBtn_, &QPushButton::clicked, this, &ElementAppearanceGroup::onRemoveGeoStep);
    connect(colorRemoveBtn_, &QPushButton::clicked, this, &ElementAppearanceGroup::onRemoveColorStep);
    connect(geoMoveUpBtn_, &QPushButton::clicked, this, [this] { onMoveGeoStep(false); });
    connect(geoMoveDownBtn_, &QPushButton::clicked, this, [this] { onMoveGeoStep(true); });
    connect(colorMoveUpBtn_, &QPushButton::clicked, this, [this] { onMoveColorStep(false); });
    connect(colorMoveDownBtn_, &QPushButton::clicked, this, [this] { onMoveColorStep(true); });
    connect(colorCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { updateChanRow(); });

    rebuildLists();
}

preprocess::PreprocessPipeline ElementAppearanceGroup::pipeline() const { return pipeline_; }

int ElementAppearanceGroup::opacity() const { return opacitySpin_ ? opacitySpin_->value() : 255; }

void ElementAppearanceGroup::setPipeline(const preprocess::PreprocessPipeline& pipeline) {
    // 入参归一化：几何恒前、色彩按原序在后（不变式对任意入参成立——防手工 JSON 等非规范序管线灌入
    //   破坏块结构，致上/下移静默违背「几何恒前」）。
    preprocess::PreprocessPipeline geo;
    preprocess::PreprocessPipeline color;
    for (const preprocess::PreprocessConfig& cfg : pipeline.items())
        (preprocess::isColorStep(cfg) ? color : geo).add(cfg);
    pipeline_ = std::move(geo);
    for (const preprocess::PreprocessConfig& cfg : color.items()) pipeline_.add(cfg);
    rebuildLists();
}

void ElementAppearanceGroup::setOpacity(const int opacity) const {
    if (!opacitySpin_) return;
    const QSignalBlocker b(opacitySpin_);
    opacitySpin_->setValue(opacity);
}

void ElementAppearanceGroup::setGeometricAllowed(const bool allowed) {
    if (geometricAllowed_ == allowed) return;
    geometricAllowed_ = allowed;
    geoSection_->setVisible(allowed);
    if (!allowed) {
        // 剔除几何步：图案元素外观不改区域尺寸，仅留色彩步（与 Core 写出/还原过滤同口径）。
        preprocess::PreprocessPipeline kept;
        for (const preprocess::PreprocessConfig& cfg : pipeline_.items())
            if (preprocess::isColorStep(cfg)) kept.add(cfg);
        pipeline_ = std::move(kept);
    }
    rebuildLists();
}

void ElementAppearanceGroup::setCompactLayout(const bool compact) {
    if (compact_ == compact) return;
    compact_ = compact;
    rebuildOpRows();
}

void ElementAppearanceGroup::rebuildOpRows() const {
    const auto fill = [](QGridLayout* g, QPushButton* u, QPushButton* mU, QPushButton* mD, QPushButton* rm,
                         const bool compact) {
        while (const QLayoutItem* item = g->takeAt(0)) delete item;   // 清空布局项（spacer 等）；按钮为宿主子件不随项删除。
        for (int c = 0; c < g->columnCount(); ++c) g->setColumnStretch(c, 0);
        if (compact) {   // 窄屏：2×2 两行排（触控目标不减、行宽减半）。
            g->addWidget(u, 0, 0);
            g->addWidget(mU, 0, 1);
            g->addWidget(mD, 1, 0);
            g->addWidget(rm, 1, 1);
            g->setColumnStretch(2, 1);
        } else {         // 宽屏：单行 4 列。
            g->addWidget(u, 0, 0);
            g->addWidget(mU, 0, 1);
            g->addWidget(mD, 0, 2);
            g->addWidget(rm, 0, 3);
            g->setColumnStretch(4, 1);
        }
    };
    fill(geoOpGrid_, geoUpdateBtn_, geoMoveUpBtn_, geoMoveDownBtn_, geoRemoveBtn_, compact_);
    fill(colorOpGrid_, colorUpdateBtn_, colorMoveUpBtn_, colorMoveDownBtn_, colorRemoveBtn_, compact_);
}

void ElementAppearanceGroup::onAddGeoStep() {
    if (const QString kind = geoCombo_->currentData().toString(); !kind.isEmpty()) {
        // 几何块恒在前：新几何步插到首个色彩步之前（无色彩步则追加尾部）。
        preprocess::PreprocessPipeline rebuilt;
        bool inserted = false;
        for (const preprocess::PreprocessConfig& cfg : pipeline_.items()) {
            if (!inserted && preprocess::isColorStep(cfg)) {
                rebuilt.add(makeGeoStep(kind));
                inserted = true;
            }
            rebuilt.add(cfg);
        }
        if (!inserted) rebuilt.add(makeGeoStep(kind));
        pipeline_ = std::move(rebuilt);
        rebuildLists();
        emit changed();
    }
}

void ElementAppearanceGroup::onAddColorStep() {
    if (const QString kind = colorCombo_->currentData().toString(); !kind.isEmpty()) {
        const std::string mask = std::string(chR_->isChecked() ? "1" : "0") +
                                 (chG_->isChecked() ? "1" : "0") +
                                 (chB_->isChecked() ? "1" : "0");
        pipeline_.add(makeColorStep(kind, mask));   // 色彩步按添加序追加（几何恒在前的块结构保持）。
        rebuildLists();
        emit changed();
    }
}

void ElementAppearanceGroup::onUpdateGeoStep() {
    const int row = geoList_->currentRow();
    if (row < 0) return;
    const QString kind = geoCombo_->currentData().toString();
    if (kind.isEmpty()) return;
    // 就地替换选中几何步（类型更新、位置不变）。
    preprocess::PreprocessPipeline rebuilt;
    std::size_t i = 0;
    for (const preprocess::PreprocessConfig& cfg : pipeline_.items()) {
        rebuilt.add(i == static_cast<std::size_t>(row) ? makeGeoStep(kind) : cfg);
        ++i;
    }
    pipeline_ = std::move(rebuilt);
    rebuildLists();
    emit changed();
}

void ElementAppearanceGroup::onUpdateColorStep() {
    const int row = colorList_->currentRow();
    if (row < 0) return;
    const QString kind = colorCombo_->currentData().toString();
    if (kind.isEmpty()) return;
    const std::string mask = std::string(chR_->isChecked() ? "1" : "0") +
                             (chG_->isChecked() ? "1" : "0") +
                             (chB_->isChecked() ? "1" : "0");
    // 就地替换选中色彩步（类型/遮码更新、位置不变）。
    const std::size_t base = geoBlockSize(pipeline_);
    preprocess::PreprocessPipeline rebuilt;
    std::size_t i = 0;
    for (const preprocess::PreprocessConfig& cfg : pipeline_.items()) {
        rebuilt.add(i == base + static_cast<std::size_t>(row) ? makeColorStep(kind, mask) : cfg);
        ++i;
    }
    pipeline_ = std::move(rebuilt);
    rebuildLists();
    emit changed();
}

void ElementAppearanceGroup::onRemoveGeoStep() {
    const int row = geoList_->currentRow();
    if (row < 0 || row >= static_cast<int>(geoBlockSize(pipeline_))) return;
    pipeline_.remove(static_cast<std::size_t>(row));
    rebuildLists();
    emit changed();
}

void ElementAppearanceGroup::onRemoveColorStep() {
    const int row = colorList_->currentRow();
    const std::size_t base = geoBlockSize(pipeline_);
    if (row < 0 || base + static_cast<std::size_t>(row) >= pipeline_.items().size()) return;
    pipeline_.remove(base + static_cast<std::size_t>(row));
    rebuildLists();
    emit changed();
}

void ElementAppearanceGroup::onMoveGeoStep(const bool down) {
    const int row = geoList_->currentRow();
    if (row < 0) return;
    const std::size_t geoN = geoBlockSize(pipeline_);
    if (down) {
        if (row + 1 >= static_cast<int>(geoN)) return;
        pipeline_.moveDown(static_cast<std::size_t>(row));
    } else {
        if (row == 0) return;
        pipeline_.moveUp(static_cast<std::size_t>(row));
    }
    rebuildLists();
    emit changed();
}

void ElementAppearanceGroup::onMoveColorStep(const bool down) {
    const int row = colorList_->currentRow();
    if (row < 0) return;
    const std::size_t base = geoBlockSize(pipeline_);
    const std::size_t idx = base + static_cast<std::size_t>(row);
    if (down) {
        if (idx + 1 >= pipeline_.items().size()) return;
        pipeline_.moveDown(idx);
    } else {
        if (idx == base) return;   // 块首：不可再上（跨块限幅由结构保证）。
        pipeline_.moveUp(idx);
    }
    rebuildLists();
    emit changed();
}

void ElementAppearanceGroup::applyGeoKindNames() const {
    geoCombo_->setItemText(0, IDC_GTR(status, kStepRotateDesc).arg(90));
    geoCombo_->setItemText(1, IDC_GTR(status, kStepRotateDesc).arg(180));
    geoCombo_->setItemText(2, IDC_GTR(status, kStepRotateDesc).arg(270));
    geoCombo_->setItemText(3, i18n::qs(IDC_GT(menu, kFlipHorizontal)));
    geoCombo_->setItemText(4, i18n::qs(IDC_GT(menu, kFlipVertical)));
}

void ElementAppearanceGroup::applyColorKindNames() const {
    colorCombo_->setItemText(0, i18n::qs(IDC_GT(label, kColorTypeGray)));
    colorCombo_->setItemText(1, i18n::qs(IDC_GT(label, kColorTypeInvert)));
    colorCombo_->setItemText(2, i18n::qs(IDC_GT(label, kColorTypeSplit)));
}

void ElementAppearanceGroup::updateChanRow() const {
    if (!chanRow_) return;   // 构造期先于通道行创建被调用：空指针即跳过，建行后再显式刷一次。
    const QString kind = colorCombo_->currentData().toString();
    chanRow_->setVisible(kind == u"invert" || kind == u"split");
}

void ElementAppearanceGroup::rebuildLists() const {
    const std::size_t geoN = geoBlockSize(pipeline_);
    {
        const QSignalBlocker b(geoList_);
        const int selected = geoList_->currentRow();   // 重建保选中行（清表后 currentRow 归 −1）。
        geoList_->clear();
        for (std::size_t i = 0; i < geoN; ++i) geoList_->addItem(stepText(pipeline_.items()[i]));
        if (selected >= 0 && selected < geoList_->count()) geoList_->setCurrentRow(selected);
    }
    {
        const QSignalBlocker b(colorList_);
        const int selected = colorList_->currentRow();
        colorList_->clear();
        for (std::size_t i = geoN; i < pipeline_.items().size(); ++i)
            colorList_->addItem(stepText(pipeline_.items()[i]));
        if (selected >= 0 && selected < colorList_->count()) colorList_->setCurrentRow(selected);
    }
    geoUpdateBtn_->setEnabled(geoN > 0);
    geoMoveUpBtn_->setEnabled(geoN > 0);
    geoMoveDownBtn_->setEnabled(geoN > 0);
    geoRemoveBtn_->setEnabled(geoN > 0);
    const bool hasColor = pipeline_.items().size() > geoN;
    colorUpdateBtn_->setEnabled(hasColor);
    colorMoveUpBtn_->setEnabled(hasColor);
    colorMoveDownBtn_->setEnabled(hasColor);
    colorRemoveBtn_->setEnabled(hasColor);
}

} // namespace idc::gui
