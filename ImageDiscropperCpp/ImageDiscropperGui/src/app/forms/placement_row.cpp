// ============================================================================
// 文件：app/forms/placement_row.cpp
// 作用：实现 PlacementRow 落位行组件（见同名头文件说明）——把两处重复的落位装配
//       统一为一个组件（含 9 向锚点/枢轴条目）。
// ============================================================================
#include "app/forms/placement_row.h"

#include <array>
#include <utility>

#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QSpinBox>

#include "util/gui_nums.h"
#include "i18n/gui_texts.h"
#include "i18n/gui_i18n.h"

namespace idc::gui {

namespace {
// 9 向锚点条目（图片锚点/枢轴、图层锚点/枢轴共用；默认「居中」）。
QComboBox* makeAnchorCombo(QWidget* parent) {
    auto* combo = new QComboBox(parent);
    for (const auto&[anchor, name] : std::array<std::pair<generate::ImageAnchor, i18n::TextRef>, 9>{{
             {generate::ImageAnchor::TopLeft, IDC_GT(label, kDirTopLeft)},
             {generate::ImageAnchor::TopCenter, IDC_GT(label, kDirTopCenter)},
             {generate::ImageAnchor::TopRight, IDC_GT(label, kDirTopRight)},
             {generate::ImageAnchor::MiddleLeft, IDC_GT(label, kDirMiddleLeft)},
             {generate::ImageAnchor::Center, IDC_GT(label, kDirCenter)},
             {generate::ImageAnchor::MiddleRight, IDC_GT(label, kDirMiddleRight)},
             {generate::ImageAnchor::BottomLeft, IDC_GT(label, kDirBottomLeft)},
             {generate::ImageAnchor::BottomCenter, IDC_GT(label, kDirBottomCenter)},
             {generate::ImageAnchor::BottomRight, IDC_GT(label, kDirBottomRight)}}}) {
        combo->addItem(i18n::qs(name));
        combo->setItemData(combo->count() - 1, static_cast<int>(anchor));
    }
    combo->setCurrentIndex(4);   // 默认「居中」。
    return combo;
}
} // namespace

PlacementRow::PlacementRow(const QString& groupTitle, QWidget* parent) : QWidget(parent) {
    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(0, 0, 0, 0);

    int row = 0;
    groupLabel_ = new QLabel(groupTitle, this);
    groupLabel_->setStyleSheet(QStringLiteral("font-weight:bold; padding-top:4px;"));
    grid->addWidget(groupLabel_, row, 0, 1, 4);
    ++row;

    auto* anchorLabel = new QLabel(IDC_GTR(label, kAnchorColonLabel), this);
    anchorCombo_ = makeAnchorCombo(this);
    auto* pivotLabel = new QLabel(IDC_GTR(label, kPivotColonLabel), this);
    pivotCombo_ = makeAnchorCombo(this);
    grid->addWidget(anchorLabel, row, 0);
    grid->addWidget(anchorCombo_, row, 1);
    grid->addWidget(pivotLabel, row, 2);
    grid->addWidget(pivotCombo_, row, 3);
    ++row;

    auto* posLabel = new QLabel(IDC_GTR(label, kPositionColonLabel), this);
    posXSpin_ = new QSpinBox(this);
    posXSpin_->setRange(-nums::model::kTransformCoordLimitPx, nums::model::kTransformCoordLimitPx);
    posXSpin_->setSuffix(QStringLiteral(" ,"));
    posXSpin_->setToolTip(IDC_GTR(tooltip, kPlaceOffsetXTip));
    posYSpin_ = new QSpinBox(this);
    posYSpin_->setRange(-nums::model::kTransformCoordLimitPx, nums::model::kTransformCoordLimitPx);
    posYSpin_->setSuffix(QStringLiteral(" px"));
    posYSpin_->setToolTip(IDC_GTR(tooltip, kPlaceOffsetYTip));
    grid->addWidget(posLabel, row, 0);
    grid->addWidget(posXSpin_, row, 1);
    grid->addWidget(posYSpin_, row, 2, 1, 2);
    ++row;

    // 任一控件变化 → changed（供上游刷新预览）。
    connect(anchorCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, &PlacementRow::changed);
    connect(pivotCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, &PlacementRow::changed);
    connect(posXSpin_, qOverload<int>(&QSpinBox::valueChanged), this, &PlacementRow::changed);
    connect(posYSpin_, qOverload<int>(&QSpinBox::valueChanged), this, &PlacementRow::changed);
}

generate::ImageAnchor PlacementRow::anchor() const {
    return static_cast<generate::ImageAnchor>(anchorCombo_->currentData().toInt());
}

generate::ImageAnchor PlacementRow::pivot() const {
    return static_cast<generate::ImageAnchor>(pivotCombo_->currentData().toInt());
}

int PlacementRow::offsetX() const { return posXSpin_->value(); }
int PlacementRow::offsetY() const { return posYSpin_->value(); }

} // namespace idc::gui
