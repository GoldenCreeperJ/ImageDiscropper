// ============================================================================
// 文件：panels/image_panel.cpp
// 作用：实现图像处理面板（预处理）的控件装配与意图信号发送（见同名头文件说明）。
// 分块依据：构造函数只做「排版 + 串联信号」；各组控件的创建分散到 build*Group（避免上帝方法）；
//           槽函数只把控件值翻译成 *Requested 信号，绝不触碰 Core / 不做像素运算（A-0.1）。
// 视觉规范（§5.1）：面板内边距 8px、控件间距 6px、分组间距 12px。
// ============================================================================
#include "panels/image_panel.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>

#include "model/document.h"

namespace idc::gui {

// 构造：竖向排列各分组 + 底部「重置预处理」按钮。
ImagePanel::ImagePanel(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);   // 面板内边距 8px（§5.1）。
    root->setSpacing(12);                    // 分组间距 12px。
    root->addWidget(buildRotateGroup());
    root->addWidget(buildFlipGroup());
    root->addWidget(buildScaleGroup());
    root->addWidget(buildColorGroup());

    // 重置预处理：恢复到原图（§4.5.4「重置预处理」按钮）。仅在已预处理时可用（syncFromDocument 控制）。
    resetBtn_ = new QPushButton(QStringLiteral("重置预处理（恢复原图）"), this);
    resetBtn_->setToolTip(QStringLiteral("丢弃全部预处理，把工作图恢复为最初打开的原图。"));
    connect(resetBtn_, &QPushButton::clicked, this, &ImagePanel::resetRequested);
    root->addWidget(resetBtn_);

    root->addStretch(1);                     // 顶部对齐，剩余空间留白。
}

// 旋转分组：左转 90° / 右转 90° / 180°。
QGroupBox* ImagePanel::buildRotateGroup() {
    auto* box = new QGroupBox(QStringLiteral("旋转（FR-1.1）"), this);
    auto* lay = new QHBoxLayout(box);
    lay->setSpacing(6);                      // 控件间距 6px。
    rotLeftBtn_ = new QPushButton(QStringLiteral("左转 90°"), box);
    rotRightBtn_ = new QPushButton(QStringLiteral("右转 90°"), box);
    rot180Btn_ = new QPushButton(QStringLiteral("180°"), box);
    rotLeftBtn_->setToolTip(QStringLiteral("逆时针旋转 90°（宽高互换）。"));
    rotRightBtn_->setToolTip(QStringLiteral("顺时针旋转 90°（宽高互换）。"));
    rot180Btn_->setToolTip(QStringLiteral("旋转 180°（尺寸不变）。"));
    connect(rotLeftBtn_, &QPushButton::clicked, this, [this] { emit rotateRequested(-90); });
    connect(rotRightBtn_, &QPushButton::clicked, this, [this] { emit rotateRequested(90); });
    connect(rot180Btn_, &QPushButton::clicked, this, [this] { emit rotateRequested(180); });
    lay->addWidget(rotLeftBtn_);
    lay->addWidget(rotRightBtn_);
    lay->addWidget(rot180Btn_);
    return box;
}

// 翻转分组：水平翻转 / 垂直翻转。
QGroupBox* ImagePanel::buildFlipGroup() {
    auto* box = new QGroupBox(QStringLiteral("翻转（FR-1.2）"), this);
    auto* lay = new QHBoxLayout(box);
    lay->setSpacing(6);
    flipHBtn_ = new QPushButton(QStringLiteral("水平翻转"), box);
    flipVBtn_ = new QPushButton(QStringLiteral("垂直翻转"), box);
    flipHBtn_->setToolTip(QStringLiteral("左右镜像翻转（尺寸不变）。"));
    flipVBtn_->setToolTip(QStringLiteral("上下镜像翻转（尺寸不变）。"));
    connect(flipHBtn_, &QPushButton::clicked, this, [this] { emit flipRequested(true); });
    connect(flipVBtn_, &QPushButton::clicked, this, [this] { emit flipRequested(false); });
    lay->addWidget(flipHBtn_);
    lay->addWidget(flipVBtn_);
    return box;
}

// 缩放 / 尺寸分组：按比例缩放 + 目标尺寸缩放（FR-1.4）。
QGroupBox* ImagePanel::buildScaleGroup() {
    auto* box = new QGroupBox(QStringLiteral("缩放 / 图像尺寸（FR-1.4）"), this);
    auto* grid = new QGridLayout(box);
    grid->setSpacing(6);

    // 按比例缩放：百分比 + 应用。
    scalePct_ = new QSpinBox(box);
    scalePct_->setRange(1, 1000);            // 1%..1000%，避免 0 或负比例（Core scale 要求 > 0）。
    scalePct_->setValue(100);
    scalePct_->setSuffix(QStringLiteral("%"));
    scalePct_->setToolTip(QStringLiteral("按当前工作图尺寸的百分比缩放（100% 为不变）。"));
    scaleApplyBtn_ = new QPushButton(QStringLiteral("按比例应用"), box);
    scaleApplyBtn_->setToolTip(QStringLiteral("按上面百分比缩放工作图。"));
    connect(scaleApplyBtn_, &QPushButton::clicked, this, &ImagePanel::onScaleApply);
    grid->addWidget(new QLabel(QStringLiteral("比例"), box), 0, 0);
    grid->addWidget(scalePct_, 0, 1);
    grid->addWidget(scaleApplyBtn_, 0, 2);

    // 目标尺寸：宽 / 高 + 保持宽高比 + 应用。
    targetW_ = new QSpinBox(box);
    targetH_ = new QSpinBox(box);
    for (QSpinBox* s : {targetW_, targetH_}) {
        s->setRange(1, 20000);               // 上限覆盖 NFR-3 的 8000×8000 大图并留余量。
        s->setToolTip(QStringLiteral("目标边长（像素）。勾选「保持宽高比」时改宽会自动算高。"));
    }
    keepRatio_ = new QCheckBox(QStringLiteral("保持宽高比"), box);
    keepRatio_->setChecked(true);
    keepRatio_->setToolTip(QStringLiteral("勾选后，修改目标宽会按当前工作图比例联动目标高。"));
    resizeApplyBtn_ = new QPushButton(QStringLiteral("按尺寸应用"), box);
    resizeApplyBtn_->setToolTip(QStringLiteral("把工作图缩放到上面的目标宽高。"));
    connect(resizeApplyBtn_, &QPushButton::clicked, this, &ImagePanel::onResizeApply);
    connect(targetW_, &QSpinBox::valueChanged, this, &ImagePanel::onWidthEdited);
    grid->addWidget(new QLabel(QStringLiteral("宽"), box), 1, 0);
    grid->addWidget(targetW_, 1, 1);
    grid->addWidget(new QLabel(QStringLiteral("高"), box), 2, 0);
    grid->addWidget(targetH_, 2, 1);
    grid->addWidget(keepRatio_, 1, 2);
    grid->addWidget(resizeApplyBtn_, 2, 2);
    return box;
}

// 颜色调整分组：黑白 / 色道反色 / 色道分离（后两者共用 R/G/B 通道选择）（FR-1.4）。
QGroupBox* ImagePanel::buildColorGroup() {
    auto* box = new QGroupBox(QStringLiteral("颜色调整（FR-1.4）"), this);
    auto* grid = new QGridLayout(box);
    grid->setSpacing(6);

    grayBtn_ = new QPushButton(QStringLiteral("黑白"), box);
    grayBtn_->setToolTip(QStringLiteral("转为灰度图（BT.601 加权）。"));
    invertBtn_ = new QPushButton(QStringLiteral("色道反色"), box);
    invertBtn_->setToolTip(QStringLiteral("对勾选的通道取反（255 - 值）；未勾选的通道保持不变。灰度图整体反相。"));
    connect(grayBtn_, &QPushButton::clicked, this, &ImagePanel::grayRequested);
    // 色道反色与色道分离共用下方 R/G/B 通道选择：反色＝对勾选通道取反（点击时才读取，故此处连接安全）。
    connect(invertBtn_, &QPushButton::clicked, this, [this] {
        emit invertRequested(chR_->isChecked(), chG_->isChecked(), chB_->isChecked());
    });
    grid->addWidget(grayBtn_, 0, 0);
    grid->addWidget(invertBtn_, 0, 1);

    // 通道选择（R/G/B）：色道反色与色道分离共用——反色对勾选通道取反、分离仅保留勾选通道。
    chR_ = new QCheckBox(QStringLiteral("R"), box);
    chG_ = new QCheckBox(QStringLiteral("G"), box);
    chB_ = new QCheckBox(QStringLiteral("B"), box);
    for (QCheckBox* c : {chR_, chG_, chB_}) c->setChecked(true);
    chR_->setToolTip(QStringLiteral("红色通道：『色道反色』反色它、『色道分离』保留它。"));
    chG_->setToolTip(QStringLiteral("绿色通道：『色道反色』反色它、『色道分离』保留它。"));
    chB_->setToolTip(QStringLiteral("蓝色通道：『色道反色』反色它、『色道分离』保留它。"));
    splitBtn_ = new QPushButton(QStringLiteral("色道分离"), box);
    splitBtn_->setToolTip(QStringLiteral("仅保留勾选的通道，其余通道置零。"));
    connect(splitBtn_, &QPushButton::clicked, this, [this] {
        emit splitRequested(chR_->isChecked(), chG_->isChecked(), chB_->isChecked());
    });
    grid->addWidget(new QLabel(QStringLiteral("作用通道"), box), 1, 0);
    grid->addWidget(splitBtn_, 0, 2);  // 与「黑白」「色道反色」同处第 0 行。
    grid->addWidget(chR_, 1, 1);
    grid->addWidget(chG_, 1, 2);
    grid->addWidget(chB_, 1, 3);
    return box;
}

// 绑定 Document。
void ImagePanel::setDocument(Document* doc) {
    doc_ = doc;
    syncFromDocument();
}

// 从 Document 反向同步：整板启停、尺寸框回灌当前工作图宽高、按灰度态启停色道分离、重置按钮启停。
void ImagePanel::syncFromDocument() {
    const bool has = doc_ && doc_->hasImage();
    setEnabled(has);                         // 无图像时整板禁用（§5.2 即时反馈）。
    if (!has) return;
    // 目标尺寸框回灌当前工作图宽高（阻断信号，避免联动回调把高改乱）。
    for (QSpinBox* s : {targetW_, targetH_}) s->blockSignals(true);
    targetW_->setValue(doc_->width());
    targetH_->setValue(doc_->height());
    for (QSpinBox* s : {targetW_, targetH_}) s->blockSignals(false);
    // 灰度（黑白）后：色道分离无 R/G/B 可分、通道选择对其无意义、黑白本身幂等 → 一并禁用；
    // 色道反色对灰度仍有效（Core 忽略掩码、整体反相）→ 保持可用。
    const bool gray = doc_->isWorkingGray();
    grayBtn_->setEnabled(!gray);
    splitBtn_->setEnabled(!gray);
    chR_->setEnabled(!gray);
    chG_->setEnabled(!gray);
    chB_->setEnabled(!gray);
    invertBtn_->setEnabled(true);
    // 仅在已预处理时允许「重置预处理」。
    resetBtn_->setEnabled(doc_->hasPreprocess());
}

// 「按比例应用」：把百分比换算为倍数发出（Core scale 要求 factor > 0）。
void ImagePanel::onScaleApply() {
    const double factor = static_cast<double>(scalePct_->value()) / 100.0;
    emit scaleRequested(factor);
}

// 「按尺寸应用」：直接发出目标宽高。
void ImagePanel::onResizeApply() {
    emit resizeRequested(targetW_->value(), targetH_->value());
}

// 保持宽高比时，改宽联动算高（比例取当前工作图宽高；纯 UI 便利，不涉及切割几何）。
void ImagePanel::onWidthEdited(const int value) const {
    if (!keepRatio_->isChecked() || !doc_ || !doc_->hasImage()) return;
    const int w = doc_->width();
    if (w <= 0) return;
    const int h = qRound(static_cast<double>(value) * static_cast<double>(doc_->height()) / static_cast<double>(w));
    targetH_->blockSignals(true);
    targetH_->setValue(h < 1 ? 1 : h);
    targetH_->blockSignals(false);
}

} // namespace idc::gui
