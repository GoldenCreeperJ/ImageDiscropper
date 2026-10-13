// ============================================================================
// 文件：mobile/mobile_step_pad.cpp
// 作用：StepPadWidget 实现——8 向步进盘（方向键微调的触控等价物，见同名头文件说明）。
// ============================================================================
#include "mobile/mobile_step_pad.h"

#include <QGridLayout>
#include <QToolButton>

#include "util/gui_nums.h"
#include "i18n/gui_texts.h"
#include "i18n/gui_i18n.h"

namespace idc::gui {
namespace {
// (row, col) 网格 → 8 方向增量；中央 (1,1) 为倍率切换按钮。
constexpr struct { int r, c, dx, dy; } kDirs[8] = {
    {0, 0, -1, -1}, {0, 1, 0, -1}, {0, 2, 1, -1},
    {1, 0, -1, 0},                  {1, 2, 1, 0},
    {2, 0, -1, 1}, {2, 1, 0, 1},   {2, 2, 1, 1},
};
constexpr const char* kDirGlyphs[8] = {"↖", "↑", "↗", "←", "→", "↙", "↓", "↘"};
} // namespace

// 构造：3×3 网格；方向按钮按住连发（QToolButton autoRepeat），中央切 ×1/×10。
StepPadWidget::StepPadWidget(QWidget* parent) : QWidget(parent) {
    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(nums::mobile::kStepPadMarginPx, nums::mobile::kStepPadMarginPx,
                            nums::mobile::kStepPadMarginPx, nums::mobile::kStepPadMarginPx);
    grid->setSpacing(nums::mobile::kStepPadSpacingPx);

    for (const auto& d : kDirs) {
        const int dx = d.dx, dy = d.dy;
        auto* btn = new QToolButton(this);
        btn->setText(QString::fromUtf8(kDirGlyphs[&d - kDirs]));
        btn->setMinimumSize(nums::mobile::kMinButtonSizePt, nums::mobile::kMinButtonSizePt);
        btn->setAutoRepeat(true);          // 按住连发：长按持续微调（等效长按方向键）。
        connect(btn, &QToolButton::clicked, this, [this, dx, dy] { emit nudgeRequested(dx * step_, dy * step_); });
        grid->addWidget(btn, d.r, d.c);
    }

    // 中央：步长倍率切换（×1 = 方向键、×10 = Shift+方向键，桌面语义等价）。
    auto* stepBtn = new QToolButton(this);
    stepBtn->setText(QStringLiteral("×1"));
    stepBtn->setMinimumSize(nums::mobile::kMinButtonSizePt, nums::mobile::kMinButtonSizePt);
    stepBtn->setToolTip(IDC_GTR(tooltip, kStepFactorBtn));
    connect(stepBtn, &QToolButton::clicked, this, [this, stepBtn] {
        step_ = step_ == nums::canvas::kNudgeStepPx ? nums::canvas::kNudgeStepShiftPx : nums::canvas::kNudgeStepPx;
        stepBtn->setText(QStringLiteral("×%1").arg(step_));
    });
    grid->addWidget(stepBtn, 1, 1);
}

} // namespace idc::gui
