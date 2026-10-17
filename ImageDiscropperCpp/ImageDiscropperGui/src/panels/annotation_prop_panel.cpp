// ============================================================================
// 文件：panels/annotation_prop_panel.cpp
// 作用：实现标注属性页的构建、取色器与模型回显（见同名头文件说明）。
// 分块依据：QFormLayout 排布「边框颜色 / 粗细 / 填充 / 填充颜色 / 文字 / 字号 / 圆角」等行；控件变更转发为信号，
//           取色器为纯 UI 关注点（QColorDialog）；槽内无任何几何 / 光栅化逻辑（CONTRIBUTING.md「分层纪律」）。
// ============================================================================
#include "panels/annotation_prop_panel.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "util/path_qt_adapter.h"
#include "util/gui_nums.h"
#include "i18n/gui_texts.h"
#include "i18n/gui_i18n.h"

namespace idc::gui {

namespace {

// 依颜色刷新色块按钮背景（含 Alpha：rgba 背景 + 文案标注不透明度；浅色用深字、深色用浅字，保证可读）。
// 用 rgba() 而非 name()（后者丢弃 alpha），使色块能预览半透明。
void applySwatch(QPushButton* btn, const QColor& c) {
    const QString fg = c.lightness() < 128 ? QStringLiteral("#fff") : QStringLiteral("#000");
    const QString bg = QStringLiteral("rgba(%1,%2,%3,%4)")
                           .arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
    btn->setStyleSheet(QStringLiteral("QPushButton{background:%1;color:%2;border:1px solid palette(mid);"
                                            "border-radius:4px;padding:4px;}"
                                            // :disabled 分支必需：色块背景由样式表接管，Qt 默认置灰不会生效，
                                            // 不补禁用态外观则「填充色被禁用」几乎看不出（与灰色默认按钮无差异）。
                                            // 禁用态一律用 palette 令牌而非字面色：本串在 syncFromModel 每次重设，
                                            // 字面 #dcdcdc 等在深色下会成一片亮灰斑，且 re-polish 只能重解析令牌、救不了字面量。
                                            // 禁用底取 window（非 mid）：placeholderText 是专为 window/base 底配的淡字，
                                            // 配 mid 底（浅色模式为块中浅灰）会与字色同明度而糊成一片、看不清；故取 window 底保持对比。
                                            "QPushButton:disabled{background:palette(window);color:palette(placeholderText);"
                                            "border:1px solid palette(dark);}")
                                 .arg(bg, fg));
    btn->setText(IDC_GTR(status, kSwatchColorText)
                     .arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha()));
}

// 通用取色：弹出带 Alpha 的非原生 QColorDialog；确认且颜色有效则写回 inout 并返回 true。
// 说明：标注颜色支持 alpha（Core Color 含 a 分量、rasterizer 已做 src-over 混合），故开启 ShowAlphaChannel。
//       因需 Alpha 通道，Qt 在 Windows 无法用原生取色框而回退到自带固定尺寸对话框（min==max）；若不显式
//       设定初始 geometry，会被 Windows 夹到默认最小尺寸而反复刷 setGeometry 噪声告警。故以顶层窗口为父 +
//       DontUseNativeDialog，并在 exec 前显式给定 >= 最小尺寸的初始 geometry（取自 sizeHint、居中到父窗口）。
bool pickColorWithAlpha(QWidget* dlgParent, QColor& inout, const QString& title) {
    QColorDialog dlg(inout, dlgParent);
    dlg.setWindowTitle(title);
    dlg.setOption(QColorDialog::ShowAlphaChannel, true);
    dlg.setOption(QColorDialog::DontUseNativeDialog, true);
    const QSize dlgSz = dlg.minimumSizeHint().expandedTo(dlg.sizeHint());
    QPoint pos(80, 80);
    if (const QWidget* pw = dlg.parentWidget()) {
        const QRect pg = pw->window()->geometry();
        pos = pg.center() - QPoint(dlgSz.width() / 2, dlgSz.height() / 2);
    }
    dlg.setGeometry(pos.x(), pos.y(), dlgSz.width(), dlgSz.height());
    if (dlg.exec() != QDialog::Accepted) return false;
    const QColor c = dlg.currentColor();
    if (!c.isValid()) return false;
    inout = c;
    return true;
}

} // namespace

// 构建属性页表单。
AnnotationPropPanel::AnnotationPropPanel(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(nums::panels::kPanelMarginPx, nums::panels::kPanelMarginPx,
                             nums::panels::kPanelMarginPx, nums::panels::kPanelMarginPx);
    root->setSpacing(nums::panels::kPanelSpacingPx);

    auto* form = new QFormLayout();
    form->setSpacing(nums::panels::kPanelSpacingPx);

    // 边框 / 描边颜色：色块按钮，点击打开取色器（填充色为下方独立控件）。
    colorBtn_ = new QPushButton(IDC_GTR(label, kPickColorButton), this);   // 初值即被色块 updateSwatch 覆盖，不绑（动态）
    i18n::bindToolTip(retranslator_, colorBtn_, IDC_GT(tooltip, kStrokeColorBtn));
    i18n::bindFormRow(retranslator_, form, colorBtn_, IDC_GT(label, kStrokeColorFieldLabel));

    // 描边粗细。
    strokeSpin_ = new QSpinBox(this);
    strokeSpin_->setRange(nums::model::kStrokeWidthMinPx, nums::model::kStrokeWidthMaxPx);
    strokeSpin_->setValue(nums::model::kDefaultStrokeWidthPx);
    strokeSpin_->setSuffix(QStringLiteral(" px"));
    i18n::bindToolTip(retranslator_, strokeSpin_, IDC_GT(tooltip, kStrokeWidthSpin));
    i18n::bindFormRow(retranslator_, form, strokeSpin_, IDC_GT(label, kStrokeWidthFieldLabel));

    // 填充开关。
    fillCheck_ = new QCheckBox(this);
    i18n::bindButton(retranslator_, fillCheck_, IDC_GT(label, kFillCheckLabel));
    i18n::bindToolTip(retranslator_, fillCheck_, IDC_GT(tooltip, kFillCheckTip));
    i18n::bindFormRow(retranslator_, form, fillCheck_, IDC_GT(label, kFillFieldLabel));

    // 填充颜色：独立于边框色的取色按钮（仅填充开启时可见效果）。
    fillColorBtn_ = new QPushButton(IDC_GTR(label, kPickColorButton), this);   // 色块动态，不绑
    i18n::bindToolTip(retranslator_, fillColorBtn_, IDC_GT(tooltip, kFillColorBtn));
    i18n::bindFormRow(retranslator_, form, fillColorBtn_, IDC_GT(label, kFillColorFieldLabel));

    // 文字内容。
    textEdit_ = new QLineEdit(this);
    textEdit_->setText(IDC_GTR(label, kDefaultAnnotationText));   // 模型驱动，不绑（动态）
    i18n::bindToolTip(retranslator_, textEdit_, IDC_GT(tooltip, kTextEditField));
    i18n::bindFormRow(retranslator_, form, textEdit_, IDC_GT(label, kTextFieldLabel));

    // 字号。
    fontSpin_ = new QSpinBox(this);
    fontSpin_->setRange(nums::model::kFontSizeMinPx, nums::model::kFontSizeMaxPx);
    fontSpin_->setValue(static_cast<int>(nums::model::kDefaultFontSizePx));
    i18n::bindToolTip(retranslator_, fontSpin_, IDC_GT(tooltip, kFontSizeSpin));
    i18n::bindFormRow(retranslator_, form, fontSpin_, IDC_GT(label, kFontSizeFieldLabel));

    // 转角圆角半径（多边形 / 折线顶点倒角；直线 / 圆 / 文字无转角，设置无效）。
    cornerSpin_ = new QDoubleSpinBox(this);
    cornerSpin_->setRange(nums::model::kCornerRadiusMinPx, nums::model::kCornerRadiusMaxPx);
    cornerSpin_->setDecimals(1);
    cornerSpin_->setSingleStep(nums::model::kCornerRadiusStepPx);
    cornerSpin_->setValue(nums::model::kDefaultCornerRadiusPx);
    cornerSpin_->setSuffix(QStringLiteral(" px"));
    cornerSpin_->setKeyboardTracking(false);
    i18n::bindToolTip(retranslator_, cornerSpin_, IDC_GT(tooltip, kCornerRadiusSpin));
    i18n::bindFormRow(retranslator_, form, cornerSpin_, IDC_GT(label, kCornerRadiusFieldLabel));

    root->addLayout(form);

    // ---- 变换区（仅对选中标注生效）：显示选中形状的**绝对累积变换**（缩放%/旋转°，负缩放=翻转）----
    // 无「应用」按钮：改动任一 spin 即直接下发（见下方 valueChanged 连接）；keyboardTracking 关闭，
    // 键入时不逐字符触发（回车/失焦或点箭头才更新），避免中间态反复变换。
    transformGroup_ = new QGroupBox(this);
    i18n::bindTitle(retranslator_, transformGroup_, IDC_GT(label, kTransformGroupBox));
    auto* tf = new QFormLayout(transformGroup_);
    tf->setSpacing(nums::panels::kPanelSpacingPx);
    scaleXSpin_ = new QSpinBox(transformGroup_);
    scaleXSpin_->setRange(-nums::model::kAnnoScalePercentLimit, nums::model::kAnnoScalePercentLimit);   // 含负：忠实反映翻转与大幅缩放
    scaleXSpin_->setValue(100);
    scaleXSpin_->setSuffix(QStringLiteral(" %"));
    scaleXSpin_->setKeyboardTracking(false);
    i18n::bindToolTip(retranslator_, scaleXSpin_, IDC_GT(tooltip, kScaleXSpin));
    i18n::bindFormRow(retranslator_, tf, scaleXSpin_, IDC_GT(label, kScaleXFieldLabel));
    scaleYSpin_ = new QSpinBox(transformGroup_);
    scaleYSpin_->setRange(-nums::model::kAnnoScalePercentLimit, nums::model::kAnnoScalePercentLimit);
    scaleYSpin_->setValue(100);
    scaleYSpin_->setSuffix(QStringLiteral(" %"));
    scaleYSpin_->setKeyboardTracking(false);
    i18n::bindToolTip(retranslator_, scaleYSpin_, IDC_GT(tooltip, kScaleYSpin));
    i18n::bindFormRow(retranslator_, tf, scaleYSpin_, IDC_GT(label, kScaleYFieldLabel));
    rotateSpin_ = new QSpinBox(transformGroup_);
    rotateSpin_->setRange(-nums::model::kAnnoRotateDegLimit, nums::model::kAnnoRotateDegLimit);   // 宽区间：容纳多次旋转的累积角度
    rotateSpin_->setValue(0);
    rotateSpin_->setSuffix(QStringLiteral(" °"));
    rotateSpin_->setKeyboardTracking(false);
    i18n::bindToolTip(retranslator_, rotateSpin_, IDC_GT(tooltip, kRotateSpin));
    i18n::bindFormRow(retranslator_, tf, rotateSpin_, IDC_GT(label, kRotateFieldLabel));
    transformGroup_->setEnabled(false);   // 初始无选中→置灰（syncFromModel 依选中态刷新）
    root->addWidget(transformGroup_);

    // ---- 叠放次序区（仅对选中标注生效）：上移一层 / 下移一层 / 置于顶层 / 置于底层 ----
    // 层序属“选中项属性”，故归位于属性页（而非菜单）；无选中时整区置灰。
    // 列表下标即 z 序（大＝顶，见 Core hitTest/burnIn），本区仅发意图、真正重排在 AnnotationBridge→Core moveAnnotation。
    stackGroup_ = new QGroupBox(this);
    i18n::bindTitle(retranslator_, stackGroup_, IDC_GT(label, kStackOrderGroupBox));
    // 2×2 网格（上移/下移 第一行，置顶/置底 第二行），按钮文字较短时单行四钮会挤，改两行更清晰。
    auto* stackGrid = new QGridLayout(stackGroup_);
    stackGrid->setSpacing(nums::panels::kPanelSpacingPx);
    raiseBtn_ = new QPushButton(stackGroup_);
    i18n::bindButton(retranslator_, raiseBtn_, IDC_GT(label, kRaiseOneButton));
    i18n::bindToolTip(retranslator_, raiseBtn_, IDC_GT(tooltip, kStackRaiseBtn));
    lowerBtn_ = new QPushButton(stackGroup_);
    i18n::bindButton(retranslator_, lowerBtn_, IDC_GT(label, kLowerOneButton));
    i18n::bindToolTip(retranslator_, lowerBtn_, IDC_GT(tooltip, kStackLowerBtn));
    toFrontBtn_ = new QPushButton(stackGroup_);
    i18n::bindButton(retranslator_, toFrontBtn_, IDC_GT(label, kToFrontButton));
    i18n::bindToolTip(retranslator_, toFrontBtn_, IDC_GT(tooltip, kStackToFrontBtn));
    toBackBtn_ = new QPushButton(stackGroup_);
    i18n::bindButton(retranslator_, toBackBtn_, IDC_GT(label, kToBackButton));
    i18n::bindToolTip(retranslator_, toBackBtn_, IDC_GT(tooltip, kStackToBackBtn));
    stackGrid->addWidget(raiseBtn_, 0, 0);
    stackGrid->addWidget(lowerBtn_, 0, 1);
    stackGrid->addWidget(toFrontBtn_, 1, 0);
    stackGrid->addWidget(toBackBtn_, 1, 1);
    stackGroup_->setEnabled(false);   // 初始无选中→置灰
    root->addWidget(stackGroup_);

    // ---- 删除选中 / 一键清除全部标注（删除作于选中项、无选中置灰；清除作用于全体，
    //      不依赖选中，协调器弹确认后清空并保留可撤销）---- 两者并排 1×2。
    deleteSelBtn_ = new QPushButton(this);
    i18n::bindButton(retranslator_, deleteSelBtn_, IDC_GT(label, kDeleteSelectedAnnoButton));
    i18n::bindToolTip(retranslator_, deleteSelBtn_, IDC_GT(tooltip, kDeleteSelectedAnnoBtn));
    clearAllBtn_ = new QPushButton(this);
    i18n::bindButton(retranslator_, clearAllBtn_, IDC_GT(label, kClearAllAnnoButton));
    i18n::bindToolTip(retranslator_, clearAllBtn_, IDC_GT(tooltip, kClearAllAnnoBtn));
    auto* delClearRow = new QHBoxLayout();
    delClearRow->setSpacing(nums::panels::kPanelSpacingPx);
    delClearRow->addWidget(deleteSelBtn_);
    delClearRow->addWidget(clearAllBtn_);
    root->addLayout(delClearRow);

    root->addStretch(1);

    // 控件变更 → 面板意图信号。
    connect(colorBtn_, &QPushButton::clicked, this, &AnnotationPropPanel::onPickColor);
    connect(fillColorBtn_, &QPushButton::clicked, this, &AnnotationPropPanel::onPickFillColor);
    connect(deleteSelBtn_, &QPushButton::clicked, this, &AnnotationPropPanel::deleteSelectedRequested);
    connect(clearAllBtn_, &QPushButton::clicked, this, &AnnotationPropPanel::clearAllRequested);
    connect(strokeSpin_, &QSpinBox::valueChanged, this, &AnnotationPropPanel::strokeChanged);
    connect(fillCheck_, &QCheckBox::toggled, this, &AnnotationPropPanel::fillChanged);
    connect(textEdit_, &QLineEdit::editingFinished,
            this, [this] { emit textChanged(textEdit_->text()); });
    connect(fontSpin_, &QSpinBox::valueChanged,
            this, [this](const int v) { emit fontSizeChanged(v); });
    connect(cornerSpin_, &QDoubleSpinBox::valueChanged,
            this, &AnnotationPropPanel::cornerRadiusChanged);
    // 变换区改动即生效（无「应用」按钮）：面板显示**绝对累积值**，故每个 spin 按其单轴把目标绝对值
    // 换算为相对基准 curObb* 的增量（缩放取比、旋转取差，其余两轴传恒等 1.0/0.0）再下发——只动本轴、
    // 不牵连其余两轴（避免 % 取整误差扰动）。因 curObb* 即当前底层绝对值，增量恰把该轴移到 spin 指定的
    // 目标绝对值，故连续编辑无漂移；提交后模型发 changed → syncFromModel 依最新累积值回显（不回弹）。
    connect(scaleXSpin_, &QSpinBox::valueChanged, this, [this](const int v) {
        const double tx = v / 100.0;   // 目标绝对缩放
        emit transformApplyRequested(std::fabs(curObbSx_) > 1e-9 ? tx / curObbSx_ : 1.0, 1.0, 0.0);
    });
    connect(scaleYSpin_, &QSpinBox::valueChanged, this, [this](const int v) {
        const double ty = v / 100.0;
        emit transformApplyRequested(1.0, std::fabs(curObbSy_) > 1e-9 ? ty / curObbSy_ : 1.0, 0.0);
    });
    connect(rotateSpin_, &QSpinBox::valueChanged, this, [this](const int v) {
        emit transformApplyRequested(1.0, 1.0, static_cast<double>(v) - curObbRot_);
    });
    // 叠放次序 4 按钮 → 意图信号（由协调器转交 AnnotationBridge：raise/lower/toFront/toBack）。
    connect(raiseBtn_, &QPushButton::clicked, this, &AnnotationPropPanel::stackRaiseRequested);
    connect(lowerBtn_, &QPushButton::clicked, this, &AnnotationPropPanel::stackLowerRequested);
    connect(toFrontBtn_, &QPushButton::clicked, this, &AnnotationPropPanel::stackToFrontRequested);
    connect(toBackBtn_, &QPushButton::clicked, this, &AnnotationPropPanel::stackBackRequested);

    updateSwatch();
    updateFillSwatch();
}

void AnnotationPropPanel::setModel(AnnotationBridge* model) {
    model_ = model;
    syncFromModel();
}

// 从模型回显当前 / 选中项属性（blockSignals 防回环写回）。
void AnnotationPropPanel::syncFromModel() {
    if (!model_) return;

    color_ = toQColor(model_->displayColor());
    updateSwatch();

    fillColor_ = toQColor(model_->displayFillColor());
    updateFillSwatch();

    strokeSpin_->blockSignals(true);
    strokeSpin_->setValue(model_->displayStrokeWidth());
    strokeSpin_->blockSignals(false);

    fillCheck_->blockSignals(true);
    fillCheck_->setChecked(model_->displayFill());
    fillCheck_->blockSignals(false);

    textEdit_->blockSignals(true);
    textEdit_->setText(QString::fromStdString(model_->currentText()));
    textEdit_->blockSignals(false);

    fontSpin_->blockSignals(true);
    fontSpin_->setValue(static_cast<int>(model_->currentFontSize()));
    fontSpin_->blockSignals(false);

    cornerSpin_->blockSignals(true);
    cornerSpin_->setValue(model_->displayCornerRadius());
    cornerSpin_->blockSignals(false);

    // 依当前形状类型的能力置灰不适用控件：无转角的类型（直线 / 圆 / 椭圆 / 文字）禁用圆角数值；
    // 「不可填充」由「填充」复选框置灰承载（勾选框禁用态显眼；色块按钮自带彩色背景、禁用态难辨），
    // 填充色按钮仍仅在可填充且已勾选时可用（其禁用态由 applySwatch 的 :disabled 样式保证可见）。
    cornerSpin_->setEnabled(model_->contextSupportsCornerRadius());
    fillCheck_->setEnabled(model_->contextSupportsFill());
    fillColorBtn_->setEnabled(model_->contextSupportsFill() && fillCheck_->isChecked());

    // 变换区仅在有选中标注时可用（无选中时变换无作用对象）；数值回显选中形状的**绝对累积变换**（忠实反映底层）。
    const bool hasSel = model_->selectedIndex().has_value();
    transformGroup_->setEnabled(hasSel);
    stackGroup_->setEnabled(hasSel);   // 叠放次序区同“变换区”：仅在有选中标注时可用
    deleteSelBtn_->setEnabled(hasSel); // 删除选中同规则：无选中无作用对象
    curObbSx_ = hasSel ? model_->displayObbScaleX() : 1.0;
    curObbSy_ = hasSel ? model_->displayObbScaleY() : 1.0;
    curObbRot_ = hasSel ? model_->displayObbRotationDeg() : 0.0;
    scaleXSpin_->blockSignals(true);
    scaleYSpin_->blockSignals(true);
    rotateSpin_->blockSignals(true);
    scaleXSpin_->setValue(qRound(curObbSx_ * 100.0));   // 系数→百分比（含负）；超范围自动钳制
    scaleYSpin_->setValue(qRound(curObbSy_ * 100.0));
    rotateSpin_->setValue(qRound(curObbRot_));
    scaleXSpin_->blockSignals(false);
    scaleYSpin_->blockSignals(false);
    rotateSpin_->blockSignals(false);
}

// 手柄拖拽进行中：把预览的**绝对**缩放系数/旋转角实时回显到变换区数值（blockSignals 防回环，绝不触发应用）。
// 缩放 spin 范围已含负，故拖手柄越过对边翻转时数值会从正连续变小、穿过 0 进入负值（忠实反映底层）。
void AnnotationPropPanel::setTransformPreview(const double sx, const double sy, const double rotateDeg) const {
    scaleXSpin_->blockSignals(true);
    scaleYSpin_->blockSignals(true);
    rotateSpin_->blockSignals(true);
    scaleXSpin_->setValue(qRound(sx * 100.0));   // 系数→百分比（含负）；超范围自动钳制
    scaleYSpin_->setValue(qRound(sy * 100.0));
    rotateSpin_->setValue(qRound(rotateDeg));    // 预览角为绝对累积值，落在宽区间内
    scaleXSpin_->blockSignals(false);
    scaleYSpin_->blockSignals(false);
    rotateSpin_->blockSignals(false);
}

// 打开边框色取色器；确认后刷新色块并发 colorPicked。
void AnnotationPropPanel::onPickColor() {
    if (!pickColorWithAlpha(window(), color_, IDC_GTR(label, kDlgTitlePickStrokeColor))) return;
    updateSwatch();
    emit colorPicked(color_);
}

// 打开填充色取色器；确认后刷新色块并发 fillColorPicked。
void AnnotationPropPanel::onPickFillColor() {
    if (!pickColorWithAlpha(window(), fillColor_, IDC_GTR(label, kDlgTitlePickFillColor))) return;
    updateFillSwatch();
    emit fillColorPicked(fillColor_);
}

// 刷新边框色块按钮背景。
void AnnotationPropPanel::updateSwatch() const { applySwatch(colorBtn_, color_); }

// 刷新填充色块按钮背景。
void AnnotationPropPanel::updateFillSwatch() const { applySwatch(fillColorBtn_, fillColor_); }

} // namespace idc::gui
