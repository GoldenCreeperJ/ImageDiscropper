// ============================================================================
// 文件：app/forms/pattern_form.cpp
// 作用：实现 PatternFormWidget 的图案参数表单（见同名头文件说明）——
//       承载控件构建 / 采参汇编 / 图案相关显隐与文案 / 取色，参数变化发 paramsChanged() 交上游刷新预览。
// ============================================================================
#include "app/forms/pattern_form.h"

#include <array>
#include <utility>
#include <vector>

#include <QColorDialog>
#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>

#include "core/color.h"
#include "util/gui_nums.h"
#include "i18n/gui_texts.h"
#include "i18n/gui_i18n.h"

namespace idc::gui {

PatternFormWidget::PatternFormWidget(QWidget* parent) : QWidget(parent) {
    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(0, 0, 0, 0);

    int row = 0;
    // ---- 图案类型 ----（图片不属于图案类型，经上游「附加图片」独立入口提供）
    kindCombo_ = new QComboBox(this);
    for (const auto k : {generate::PatternKind::Solid, generate::PatternKind::LinearGradient,
                         generate::PatternKind::RadialGradient, generate::PatternKind::Checkerboard,
                         generate::PatternKind::Stripes, generate::PatternKind::GridLines,
                         generate::PatternKind::DotGrid, generate::PatternKind::Concentric,
                         generate::PatternKind::RadialRays, generate::PatternKind::HueGradient,
                         generate::PatternKind::Noise}) {
        // Core 返回的是按当前语言现取的显示名（std::string）；此处经 c_str() 走 fromUtf8 的 const char* 重载。
        kindCombo_->addItem(QString::fromUtf8(generate::patternKindName(k).c_str()));
        // itemData 存枚举值，避免依赖显示序与枚举数值一致。
        kindCombo_->setItemData(kindCombo_->count() - 1, static_cast<int>(k));
    }
    grid->addWidget(new QLabel(IDC_GTR(label, kPatternFieldLabel), this), row, 0);
    grid->addWidget(kindCombo_, row, 1, 1, 3);
    ++row;

    // ---- 方向（图案切换时由 rebuildOrientationCombo 依类型动态重建）----
    orientationLabel_ = new QLabel(IDC_GTR(label, kOrientationColonLabel), this);
    orientationCombo_ = new QComboBox(this);
    grid->addWidget(orientationLabel_, row, 0);
    grid->addWidget(orientationCombo_, row, 1, 1, 3);
    ++row;

    // ---- 底色（点击取色对话框选 RGBA，含 alpha）----
    bgSwatch_ = new QPushButton(this);
    bgSwatch_->setFixedSize(nums::dialog::kColorSwatchWidthPx, nums::dialog::kColorSwatchHeightPx);
    bgSwatch_->setToolTip(IDC_GTR(tooltip, kPatternBgSwatch));
    grid->addWidget(new QLabel(IDC_GTR(label, kBgColorFieldLabel), this), row, 0);
    grid->addWidget(bgSwatch_, row, 1);
    ++row;

    // ---- 前景色（仅非纯色图案，点击取色对话框选 RGBA）----
    fgSwatch_ = new QPushButton(this);
    fgSwatch_->setFixedSize(nums::dialog::kColorSwatchWidthPx, nums::dialog::kColorSwatchHeightPx);
    fgSwatch_->setToolTip(IDC_GTR(tooltip, kPatternFgSwatch));
    fgLabel_ = new QLabel(IDC_GTR(label, kFgColorFieldLabel), this);
    grid->addWidget(fgLabel_, row, 0);
    grid->addWidget(fgSwatch_, row, 1);
    ++row;

    // ---- 图案参数 ----
    periodLabel_ = new QLabel(IDC_GTR(label, kPeriodFieldLabel), this);
    periodSpin_ = new QSpinBox(this);
    periodSpin_->setRange(core::nums::generate::kMinPeriod, nums::dialog::kPatternPeriodMaxPx);
    periodSpin_->setValue(core::nums::generate::kDefaultPeriod);
    periodSpin_->setSuffix(QStringLiteral(" px"));
    lineWidthLabel_ = new QLabel(IDC_GTR(label, kLineWidthColonLabel), this);
    lineWidthSpin_ = new QSpinBox(this);
    lineWidthSpin_->setRange(core::nums::generate::kMinLineWidth, nums::dialog::kPatternParamMaxPx);
    lineWidthSpin_->setValue(core::nums::generate::kDefaultLineWidth);
    dotRadiusLabel_ = new QLabel(IDC_GTR(label, kDotRadiusColonLabel), this);
    dotRadiusSpin_ = new QSpinBox(this);
    dotRadiusSpin_->setRange(core::nums::generate::kMinDotRadius, nums::dialog::kPatternParamMaxPx);
    dotRadiusSpin_->setValue(core::nums::generate::kDefaultDotRadius);
    grid->addWidget(periodLabel_, row, 0);
    grid->addWidget(periodSpin_, row, 1);
    grid->addWidget(lineWidthLabel_, row, 2);
    grid->addWidget(lineWidthSpin_, row, 3);
    ++row;
    grid->addWidget(dotRadiusLabel_, row, 0);
    grid->addWidget(dotRadiusSpin_, row, 1);
    ++row;

    // ---- 噪声专属参数（仅 Noise 图案显隐）：种子 + 配色模式 ----
    seedLabel_ = new QLabel(IDC_GTR(label, kSeedFieldLabel), this);
    seedSpin_ = new QSpinBox(this);
    seedSpin_->setRange(0, 2147483647);
    seedSpin_->setValue(0);
    seedSpin_->setToolTip(IDC_GTR(tooltip, kPatternSeedSpin));
    noiseModeLabel_ = new QLabel(IDC_GTR(label, kNoiseModeFieldLabel), this);
    noiseModeCombo_ = new QComboBox(this);
    for (const auto&[mode, name] : std::array<std::pair<generate::NoiseMode, i18n::TextRef>, 3>{{
             {generate::NoiseMode::FullColor, IDC_GT(label, kNoiseFullColor)},
             {generate::NoiseMode::Grayscale, IDC_GT(label, kNoiseGrayscale)},
             {generate::NoiseMode::TwoColor, IDC_GT(label, kNoiseTwoColor)}}}) {
        noiseModeCombo_->addItem(i18n::qs(name));
        noiseModeCombo_->setItemData(noiseModeCombo_->count() - 1, static_cast<int>(mode));
    }
    grid->addWidget(seedLabel_, row, 0);
    grid->addWidget(seedSpin_, row, 1);
    grid->addWidget(noiseModeLabel_, row, 2);
    grid->addWidget(noiseModeCombo_, row, 3);
    ++row;

    // ---- 连接：任一图案参数控件变化 → paramsChanged（上游据此刷新预览）----
    connect(bgSwatch_, &QPushButton::clicked, this, &PatternFormWidget::onPickBackground);
    connect(fgSwatch_, &QPushButton::clicked, this, &PatternFormWidget::onPickForeground);
    for (const QSpinBox* s : {periodSpin_, lineWidthSpin_, dotRadiusSpin_, seedSpin_})
        connect(s, qOverload<int>(&QSpinBox::valueChanged), this, &PatternFormWidget::paramsChanged);
    connect(kindCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, &PatternFormWidget::paramsChanged);
    connect(orientationCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, &PatternFormWidget::paramsChanged);
    connect(noiseModeCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, &PatternFormWidget::paramsChanged);

    updateSwatches();
    updateAvailability();
}

// 从控件汇编全尺寸语义的 PatternParams（background/foreground 由本组件底色/前景色控件供给）。
void PatternFormWidget::params(generate::PatternParams& p) const {
    p.kind = static_cast<generate::PatternKind>(kindCombo_->currentData().toInt());
    if (orientationCombo_->count() > 0 && orientationCombo_->currentData().isValid())
        p.orientation = static_cast<generate::Orientation>(orientationCombo_->currentData().toInt());
    p.background = core::Color(static_cast<std::uint8_t>(bgColor_.red()),
                               static_cast<std::uint8_t>(bgColor_.green()),
                               static_cast<std::uint8_t>(bgColor_.blue()),
                               static_cast<std::uint8_t>(bgColor_.alpha()));
    p.foreground = core::Color(static_cast<std::uint8_t>(fgColor_.red()),
                               static_cast<std::uint8_t>(fgColor_.green()),
                               static_cast<std::uint8_t>(fgColor_.blue()),
                               static_cast<std::uint8_t>(fgColor_.alpha()));
    p.period = periodSpin_->value();
    p.lineWidth = lineWidthSpin_->value();
    p.dotRadius = dotRadiusSpin_->value();
    p.seed = seedSpin_->value();
    p.noiseMode = static_cast<generate::NoiseMode>(noiseModeCombo_->currentData().toInt());
}

// 依当前图案类型显隐/启停相关控件（未用的行整行收起，避免空白）。
//   图案切换时（kind != shownKind_）重建方向条目，并在进入色相渐变时把色相波长初值置 128。
void PatternFormWidget::updateAvailability() {
    const auto kind = static_cast<generate::PatternKind>(kindCombo_->currentData().toInt());
    // 图案切换：重建方向条目；色相渐变进入时把色相波长初值置 128。
    if (kind != shownKind_) {
        shownKind_ = kind;
        rebuildOrientationCombo(kind);
        if (kind == generate::PatternKind::HueGradient) periodSpin_->setValue(nums::dialog::kPatternHuePeriodPx);
    }
    const bool solid = kind == generate::PatternKind::Solid;
    const bool gradient = kind == generate::PatternKind::LinearGradient || kind == generate::PatternKind::RadialGradient;
    const bool noise = kind == generate::PatternKind::Noise;
    // 前景色：除纯色、色相渐变（不吃 bg/fg）外皆适用；噪声保留（two-color 用到 fg）。
    const bool hasFg = !solid && kind != generate::PatternKind::HueGradient;
    // 方向：线性渐变 / 条纹（含斜纹）/ 同心（圆环·方环）/ 色相渐变扫描轴。
    const bool hasOrientation = kind == generate::PatternKind::LinearGradient || kind == generate::PatternKind::Stripes
                                || kind == generate::PatternKind::Concentric || kind == generate::PatternKind::HueGradient;
    // 周期：除纯色、两种渐变、噪声外皆适用（噪声 orientation/period/lineWidth/dotRadius 均无效）。
    const bool hasPeriod = !solid && !gradient && !noise;
    const bool hasLineWidth = kind == generate::PatternKind::GridLines || kind == generate::PatternKind::Concentric;
    const bool hasDotRadius = kind == generate::PatternKind::DotGrid;

    bgSwatch_->setEnabled(true);
    fgLabel_->setVisible(hasFg);
    fgSwatch_->setVisible(hasFg);
    orientationLabel_->setVisible(hasOrientation);
    orientationCombo_->setVisible(hasOrientation);
    periodLabel_->setVisible(hasPeriod);
    periodSpin_->setVisible(hasPeriod);
    lineWidthLabel_->setVisible(hasLineWidth);
    lineWidthSpin_->setVisible(hasLineWidth);
    dotRadiusLabel_->setVisible(hasDotRadius);
    dotRadiusSpin_->setVisible(hasDotRadius);
    seedLabel_->setVisible(noise);
    seedSpin_->setVisible(noise);
    noiseModeLabel_->setVisible(noise);
    noiseModeCombo_->setVisible(noise);
    // 周期标签随类型微调文案。
    if (kind == generate::PatternKind::Checkerboard)
        periodLabel_->setText(IDC_GTR(label, kPeriodTagCell));
    else if (kind == generate::PatternKind::Concentric)
        periodLabel_->setText(IDC_GTR(label, kPeriodTagRing));
    else if (kind == generate::PatternKind::RadialRays)
        periodLabel_->setText(IDC_GTR(label, kPeriodTagDensity));
    else if (kind == generate::PatternKind::HueGradient)
        periodLabel_->setText(IDC_GTR(label, kPeriodTagHue));
    else
        periodLabel_->setText(IDC_GTR(label, kPeriodTagPlain));
}

// 依当前图案类型重建方向条目（存 Orientation 枚举于 itemData）。
void PatternFormWidget::rebuildOrientationCombo(const generate::PatternKind kind) const {
    const QSignalBlocker blocker(orientationCombo_);   // 重建条目不触发联动（避免递归）。
    const QVariant prev = orientationCombo_->currentData();   // 尽量保留已选方向。
    orientationCombo_->clear();

    std::vector<std::pair<generate::Orientation, i18n::TextRef>> items;
    if (kind == generate::PatternKind::Stripes) {
        items = {{generate::Orientation::Horizontal, IDC_GT(label, kDirStripeH)},
                 {generate::Orientation::Vertical, IDC_GT(label, kDirStripeV)},
                 {generate::Orientation::DiagonalDown, IDC_GT(label, kDirDiagDown)},
                 {generate::Orientation::DiagonalUp, IDC_GT(label, kDirDiagUp)}};
    } else if (kind == generate::PatternKind::Concentric) {
        items = {{generate::Orientation::Horizontal, IDC_GT(label, kDirRingItem)},
                 {generate::Orientation::Vertical, IDC_GT(label, kDirSquareItem)}};
    } else if (kind == generate::PatternKind::LinearGradient || kind == generate::PatternKind::HueGradient) {
        items = {{generate::Orientation::Horizontal, IDC_GT(label, kDirHorizontal)},
                 {generate::Orientation::Vertical, IDC_GT(label, kDirVertical)}};
    }
    for (const auto&[o, name] : items) {
        orientationCombo_->addItem(i18n::qs(name));
        orientationCombo_->setItemData(orientationCombo_->count() - 1, static_cast<int>(o));
    }
    // 恢复先前选择（若新列表仍含该方向），否则回第一项。
    int idx = 0;
    if (prev.isValid()) {
        for (int i = 0; i < orientationCombo_->count(); ++i)
            if (orientationCombo_->itemData(i) == prev) { idx = i; break; }
    }
    orientationCombo_->setCurrentIndex(idx);
}

// 重绘两个色块按钮的半透明底（rgba 直接反映 alpha，透出按钮默认底以示意透明）。
void PatternFormWidget::updateSwatches() const {
    const auto style = [](const QColor& c) {
        return QStringLiteral("background-color: rgba(%1,%2,%3,%4); border:1px solid #888;")
            .arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
    };
    bgSwatch_->setStyleSheet(style(bgColor_));
    fgSwatch_->setStyleSheet(style(fgColor_));
}

void PatternFormWidget::onPickBackground() {
    const QColor c = QColorDialog::getColor(bgColor_, this, IDC_GTR(label, kDlgTitlePickBgColor),
                                            QColorDialog::ShowAlphaChannel);
    if (c.isValid()) {
        bgColor_ = c;    // 取 RGBA，alpha 由取色对话框设定。
        updateSwatches();
        emit paramsChanged();
    }
}

void PatternFormWidget::onPickForeground() {
    const QColor c = QColorDialog::getColor(fgColor_, this, IDC_GTR(label, kDlgTitlePickFgColor),
                                            QColorDialog::ShowAlphaChannel);
    if (c.isValid()) {
        fgColor_ = c;    // 取 RGBA，alpha 由取色对话框设定。
        updateSwatches();
        emit paramsChanged();
    }
}

} // namespace idc::gui
