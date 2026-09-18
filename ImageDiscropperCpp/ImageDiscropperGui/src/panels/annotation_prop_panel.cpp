// ============================================================================
// 文件：src/panels/annotation_prop_panel.cpp
// 作用：实现标注属性页的构建、取色器与模型回显（见同名头文件说明）。
// 分块依据：QFormLayout 排布「颜色 / 粗细 / 填充 / 文字 / 字号」五行；控件变更转发为信号，
//           取色器为纯 UI 关注点（QColorDialog）；槽内无任何几何 / 光栅化逻辑（A-0.1）。
// ============================================================================
#include "panels/annotation_prop_panel.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "util/path_qt_adapter.h"

namespace idc::gui {

// 构建属性页表单。
AnnotationPropPanel::AnnotationPropPanel(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    auto* form = new QFormLayout();
    form->setSpacing(6);

    // 颜色：色块按钮，点击打开取色器（描边与填充共用同一颜色）。
    colorBtn_ = new QPushButton(QStringLiteral("选择颜色…"), this);
    colorBtn_->setToolTip(QStringLiteral("标注描边 / 填充颜色"));
    form->addRow(QStringLiteral("颜色"), colorBtn_);

    // 描边粗细。
    strokeSpin_ = new QSpinBox(this);
    strokeSpin_->setRange(1, 200);
    strokeSpin_->setValue(2);
    strokeSpin_->setSuffix(QStringLiteral(" px"));
    strokeSpin_->setToolTip(QStringLiteral("描边线宽（原图像素），烧录时按此粗细合成"));
    form->addRow(QStringLiteral("粗细"), strokeSpin_);

    // 填充开关。
    fillCheck_ = new QCheckBox(QStringLiteral("填充内部"), this);
    fillCheck_->setToolTip(QStringLiteral("闭合形状是否填充（填充色同描边色；直线无效）"));
    form->addRow(QStringLiteral("填充"), fillCheck_);

    // 文字内容。
    textEdit_ = new QLineEdit(this);
    textEdit_->setText(QStringLiteral("A"));
    textEdit_->setToolTip(QStringLiteral("文字标注的内容（选择文字工具后单击落点使用）"));
    form->addRow(QStringLiteral("文字"), textEdit_);

    // 字号。
    fontSpin_ = new QSpinBox(this);
    fontSpin_->setRange(1, 2000);
    fontSpin_->setValue(200);
    fontSpin_->setToolTip(QStringLiteral("文字标注的字号（像素）"));
    form->addRow(QStringLiteral("字号"), fontSpin_);

    root->addLayout(form);

    // ---- 变换区（仅对选中标注生效）：显示选中形状的**绝对累积变换**（缩放%/旋转°，负缩放=翻转）----
    // 无「应用」按钮：改动任一 spin 即直接下发（见下方 valueChanged 连接）；keyboardTracking 关闭，
    // 键入时不逐字符触发（回车/失焦或点箭头才更新），避免中间态反复变换。
    transformGroup_ = new QGroupBox(QStringLiteral("变换（选中标注）"), this);
    auto* tf = new QFormLayout(transformGroup_);
    tf->setSpacing(6);
    scaleXSpin_ = new QSpinBox(transformGroup_);
    scaleXSpin_->setRange(-100000, 100000);   // 含负：忠实反映翻转与大幅缩放
    scaleXSpin_->setValue(100);
    scaleXSpin_->setSuffix(QStringLiteral(" %"));
    scaleXSpin_->setKeyboardTracking(false);
    scaleXSpin_->setToolTip(QStringLiteral("水平缩放绝对值（100%=原尺寸；负=翻转）；改动即生效，与画布手柄拖拽实时联动"));
    tf->addRow(QStringLiteral("水平缩放"), scaleXSpin_);
    scaleYSpin_ = new QSpinBox(transformGroup_);
    scaleYSpin_->setRange(-100000, 100000);
    scaleYSpin_->setValue(100);
    scaleYSpin_->setSuffix(QStringLiteral(" %"));
    scaleYSpin_->setKeyboardTracking(false);
    scaleYSpin_->setToolTip(QStringLiteral("垂直缩放绝对值（100%=原尺寸；负=翻转）；改动即生效，与画布手柄拖拽实时联动"));
    tf->addRow(QStringLiteral("垂直缩放"), scaleYSpin_);
    rotateSpin_ = new QSpinBox(transformGroup_);
    rotateSpin_->setRange(-36000, 36000);   // 宽区间：容纳多次旋转的累积角度
    rotateSpin_->setValue(0);
    rotateSpin_->setSuffix(QStringLiteral(" °"));
    rotateSpin_->setKeyboardTracking(false);
    rotateSpin_->setToolTip(QStringLiteral("旋转绝对角度（累积）；改动即生效，与画布手柄拖拽实时联动"));
    tf->addRow(QStringLiteral("旋转"), rotateSpin_);
    transformGroup_->setEnabled(false);   // 初始无选中→置灰（syncFromModel 依选中态刷新）
    root->addWidget(transformGroup_);

    root->addStretch(1);

    // 控件变更 → 面板意图信号。
    connect(colorBtn_, &QPushButton::clicked, this, &AnnotationPropPanel::onPickColor);
    connect(strokeSpin_, &QSpinBox::valueChanged, this, &AnnotationPropPanel::strokeChanged);
    connect(fillCheck_, &QCheckBox::toggled, this, &AnnotationPropPanel::fillChanged);
    connect(textEdit_, &QLineEdit::editingFinished,
            this, [this] { emit textChanged(textEdit_->text()); });
    connect(fontSpin_, &QSpinBox::valueChanged,
            this, [this](const int v) { emit fontSizeChanged(v); });
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

    updateSwatch();
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

    // 变换区仅在有选中标注时可用（无选中时变换无作用对象）；数值回显选中形状的**绝对累积变换**（忠实反映底层）。
    const bool hasSel = model_->selectedIndex().has_value();
    transformGroup_->setEnabled(hasSel);
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

// 打开取色器（带 Alpha 通道）；确认后刷新色块并发 colorPicked。
// 说明：标注颜色支持 alpha（Core Color 含 a 分量、rasterizer 已做 src-over 混合），故取色器
//       开启 ShowAlphaChannel。因需 Alpha 通道，Qt 在 Windows 无法用原生取色框而回退到自带的
//       固定尺寸对话框 QColorDialogClassWindow（min==max）。若不显式设定初始 geometry，Qt 会以
//       一个默认小尺寸（如 180x45）初始化，show 时被 Windows 强制夹到该固定最小尺寸，从而反复刷
//       "QWindowsWindow::setGeometry: Unable to set geometry" 噪声告警（告警良性但噪声大）。
//       故：以顶层窗口为父 + DontUseNativeDialog，并在 exec 前**显式给定一个 >= 对话框最小尺寸的
//       初始 geometry**（尺寸取自对话框自身 sizeHint，随 DPI 自适应、居中到父窗口），从源头规避该告警。
//       （与导出面板填充色取色框同一处理方式。）
void AnnotationPropPanel::onPickColor() {
    QColorDialog dlg(color_, window());                 // 以顶层窗口为父，便于居中且不以窄面板推导初始位置。
    dlg.setWindowTitle(QStringLiteral("选择标注颜色"));
    dlg.setOption(QColorDialog::ShowAlphaChannel, true);
    dlg.setOption(QColorDialog::DontUseNativeDialog, true); // Alpha 需求下本就走非原生，显式声明意图。
    // 关键：显式设定初始 geometry（>= 固定最小尺寸、居中到父窗口），避免默认小尺寸被 Windows 夹取而刷 setGeometry 告警。
    const QSize dlgSz = dlg.minimumSizeHint().expandedTo(dlg.sizeHint());
    QPoint dlgPos(80, 80);
    if (const QWidget* pw = dlg.parentWidget()) {
        const QRect pg = pw->window()->geometry();       // 顶层窗口 geometry 为全局屏幕坐标。
        dlgPos = pg.center() - QPoint(dlgSz.width() / 2, dlgSz.height() / 2);
    }
    dlg.setGeometry(dlgPos.x(), dlgPos.y(), dlgSz.width(), dlgSz.height());
    if (dlg.exec() != QDialog::Accepted) return;        // 用户取消。
    const QColor c = dlg.currentColor();
    if (!c.isValid()) return;
    color_ = c;
    updateSwatch();
    emit colorPicked(c);
}

// 依当前颜色刷新色块按钮背景（含 Alpha：rgba 背景 + 文案标注不透明度；浅色用深字、深色用浅字，保证可读）。
void AnnotationPropPanel::updateSwatch() const {
    const QString fg = color_.lightness() < 128 ? QStringLiteral("#fff") : QStringLiteral("#000");
    // 用 rgba() 而非 name()（后者丢弃 alpha），使色块能预览半透明；文案回显 RGBA 分量。
    const QString bg = QStringLiteral("rgba(%1,%2,%3,%4)")
                           .arg(color_.red()).arg(color_.green()).arg(color_.blue()).arg(color_.alpha());
    colorBtn_->setStyleSheet(QStringLiteral("QPushButton{background:%1;color:%2;border:1px solid #999;"
                                            "border-radius:4px;padding:4px;}")
                                 .arg(bg, fg));
    colorBtn_->setText(QStringLiteral("RGBA(%1,%2,%3,%4)")
                           .arg(color_.red()).arg(color_.green()).arg(color_.blue()).arg(color_.alpha()));
}

} // namespace idc::gui
