// ============================================================================
// 文件：app/new_canvas_dialog.cpp
// 作用：实现 NewCanvasDialog（见同名头文件说明）——采参控件装配、按图案类型与附加图片开关
//       的可用性联动、小尺寸代理实时预览，以及 accept() 时的全尺寸两段合成（图片→图层框→叠到图案底图）。
// 分块依据：装配（构造）/ 联动（updateControlAvailability）/ 采参（buildParams）/
//           两段合成（composeBox 段1 + composeOntoBase 段2，composeResult 为预览便捷）/ 预览（refreshPreview，走桥接）/
//           定稿（accept，走桥接）各成一块。
// ============================================================================
#include "app/new_canvas_dialog.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

#include <QCheckBox>
#include <QComboBox>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include "generate/pattern.h"
#include "app/preprocess_controller.h" // runInBusyDialog（全尺寸生成包忙碌框，避免 UI 冻结）
#include "model/engine_bridge.h"
#include "util/image_qt_adapter.h"

namespace idc::gui {

namespace {
// 依最长边把尺寸与周期/线宽/点半径等比缩到预览代理（factor ≤ 1）。
inline int scaled(int v, double factor) { return std::max(1, static_cast<int>(std::lround(v * factor))); }
} // namespace

NewCanvasDialog::NewCanvasDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("新建画布"));
    setModal(true);

    auto* root = new QVBoxLayout(this);
    auto* grid = new QGridLayout();
    grid->setContentsMargins(0, 0, 0, 0);
    root->addLayout(grid);

    int row = 0;
    // ---- 尺寸 ----
    widthSpin_ = new QSpinBox(this);
    widthSpin_->setRange(1, generate::kMaxDimension);
    widthSpin_->setValue(1024);
    widthSpin_->setSuffix(QStringLiteral(" px"));
    heightSpin_ = new QSpinBox(this);
    heightSpin_->setRange(1, generate::kMaxDimension);
    heightSpin_->setValue(768);
    heightSpin_->setSuffix(QStringLiteral(" px"));
    grid->addWidget(new QLabel(QStringLiteral("宽度："), this), row, 0);
    grid->addWidget(widthSpin_, row, 1);
    grid->addWidget(new QLabel(QStringLiteral("高度："), this), row, 2);
    grid->addWidget(heightSpin_, row, 3);
    ++row;

    // ---- 图案类型 ----（旧「图片」条目已移除：图片不再是图案，改走下方「附加图片」独立入口）
    kindCombo_ = new QComboBox(this);
    for (const auto k : {generate::PatternKind::Solid, generate::PatternKind::LinearGradient,
                         generate::PatternKind::RadialGradient, generate::PatternKind::Checkerboard,
                         generate::PatternKind::Stripes, generate::PatternKind::GridLines,
                         generate::PatternKind::DotGrid, generate::PatternKind::Concentric,
                         generate::PatternKind::RadialRays, generate::PatternKind::HueGradient,
                         generate::PatternKind::Noise}) {
        kindCombo_->addItem(QString::fromUtf8(generate::patternKindName(k)));
        // itemData 存枚举值，避免依赖显示序与枚举数值一致。
        kindCombo_->setItemData(kindCombo_->count() - 1, static_cast<int>(k));
    }
    grid->addWidget(new QLabel(QStringLiteral("图案："), this), row, 0);
    grid->addWidget(kindCombo_, row, 1, 1, 3);
    ++row;

    // ---- 方向（图案切换时由 rebuildOrientationCombo 依类型动态重建）----
    orientationLabel_ = new QLabel(QStringLiteral("方向："), this);
    orientationCombo_ = new QComboBox(this);
    grid->addWidget(orientationLabel_, row, 0);
    grid->addWidget(orientationCombo_, row, 1, 1, 3);
    ++row;

    // ---- 底色（点击取色对话框选 RGBA，含 alpha）----
    bgSwatch_ = new QPushButton(this);
    bgSwatch_->setFixedSize(64, 22);
    bgSwatch_->setToolTip(QStringLiteral("点击选择底色（可在取色对话框设定透明度，0＝全透明画布）"));
    grid->addWidget(new QLabel(QStringLiteral("底色："), this), row, 0);
    grid->addWidget(bgSwatch_, row, 1);
    ++row;

    // ---- 前景色（仅非纯色图案，点击取色对话框选 RGBA）----
    fgSwatch_ = new QPushButton(this);
    fgSwatch_->setFixedSize(64, 22);
    fgSwatch_->setToolTip(QStringLiteral("点击选择前景/图案色（可在取色对话框设定透明度，仅非纯色图案生效）"));
    fgLabel_ = new QLabel(QStringLiteral("前景色："), this);
    grid->addWidget(fgLabel_, row, 0);
    grid->addWidget(fgSwatch_, row, 1);
    ++row;

    // ---- 图案参数 ----
    periodLabel_ = new QLabel(QStringLiteral("周期/格径："), this);
    periodSpin_ = new QSpinBox(this);
    periodSpin_->setRange(2, 4096);
    periodSpin_->setValue(32);
    periodSpin_->setSuffix(QStringLiteral(" px"));
    lineWidthLabel_ = new QLabel(QStringLiteral("线宽："), this);
    lineWidthSpin_ = new QSpinBox(this);
    lineWidthSpin_->setRange(1, 256);
    lineWidthSpin_->setValue(1);
    dotRadiusLabel_ = new QLabel(QStringLiteral("点半径："), this);
    dotRadiusSpin_ = new QSpinBox(this);
    dotRadiusSpin_->setRange(1, 256);
    dotRadiusSpin_->setValue(2);
    grid->addWidget(periodLabel_, row, 0);
    grid->addWidget(periodSpin_, row, 1);
    grid->addWidget(lineWidthLabel_, row, 2);
    grid->addWidget(lineWidthSpin_, row, 3);
    ++row;
    grid->addWidget(dotRadiusLabel_, row, 0);
    grid->addWidget(dotRadiusSpin_, row, 1);
    ++row;

    // ---- 噪声专属参数（仅 Noise 图案显隐）：种子 + 配色模式 ----
    seedLabel_ = new QLabel(QStringLiteral("种子："), this);
    seedSpin_ = new QSpinBox(this);
    seedSpin_->setRange(0, 2147483647);
    seedSpin_->setValue(0);
    seedSpin_->setToolTip(QStringLiteral("确定性噪声随机种子：同种子+同尺寸恒同图（仅噪声图案生效）"));
    noiseModeLabel_ = new QLabel(QStringLiteral("噪声模式："), this);
    noiseModeCombo_ = new QComboBox(this);
    for (const auto&[mode, name] : std::array<std::pair<generate::NoiseMode, const char*>, 3>{{
             {generate::NoiseMode::FullColor, "全彩"},
             {generate::NoiseMode::Grayscale, "灰度"},
             {generate::NoiseMode::TwoColor, "双色(bg↔fg)"}}}) {
        noiseModeCombo_->addItem(QString::fromUtf8(name));
        noiseModeCombo_->setItemData(noiseModeCombo_->count() - 1, static_cast<int>(mode));
    }
    grid->addWidget(seedLabel_, row, 0);
    grid->addWidget(seedSpin_, row, 1);
    grid->addWidget(noiseModeLabel_, row, 2);
    grid->addWidget(noiseModeCombo_, row, 3);
    ++row;

    // 9 向锚点条目构造器（图片锚点/枢轴、图层锚点/枢轴 共用）。
    auto makeAnchorCombo = [this](QComboBox*& combo) {
        combo = new QComboBox(this);
        for (const auto&[anchor, name] : std::array<std::pair<generate::ImageAnchor, const char*>, 9>{{
                 {generate::ImageAnchor::TopLeft, "左上"},
                 {generate::ImageAnchor::TopCenter, "上中"},
                 {generate::ImageAnchor::TopRight, "右上"},
                 {generate::ImageAnchor::MiddleLeft, "左中"},
                 {generate::ImageAnchor::Center, "居中"},
                 {generate::ImageAnchor::MiddleRight, "右中"},
                 {generate::ImageAnchor::BottomLeft, "左下"},
                 {generate::ImageAnchor::BottomCenter, "下中"},
                 {generate::ImageAnchor::BottomRight, "右下"}}}) {
            combo->addItem(QString::fromUtf8(name));
            combo->setItemData(combo->count() - 1, static_cast<int>(anchor));
        }
        combo->setCurrentIndex(4);   // 默认「居中」。
    };

    // ---- 图层落位（图层 相对 底图）：独立成组、常显——调整不依赖「附加图片」开关（段2 决定图层框摆到底图哪里）。----
    layerGroupLabel_ = new QLabel(QStringLiteral("图层落位"), this);
    layerGroupLabel_->setStyleSheet(QStringLiteral("font-weight:bold; padding-top:4px;"));
    layerAnchorLabel_ = new QLabel(QStringLiteral("图层锚点："), this);
    makeAnchorCombo(layerAnchorCombo_);
    layerPivotLabel_ = new QLabel(QStringLiteral("图层枢轴："), this);
    makeAnchorCombo(layerPivotCombo_);
    layerPosLabel_ = new QLabel(QStringLiteral("图层位置："), this);
    layerPosXSpin_ = new QSpinBox(this);
    layerPosXSpin_->setRange(-1000000, 1000000);
    layerPosXSpin_->setSuffix(QStringLiteral(" ,"));
    layerPosXSpin_->setToolTip(QStringLiteral("图层框相对底图的自由偏移 X（叠加在图层锚点/枢轴之上）"));
    layerPosYSpin_ = new QSpinBox(this);
    layerPosYSpin_->setRange(-1000000, 1000000);
    layerPosYSpin_->setSuffix(QStringLiteral(" px"));
    layerPosYSpin_->setToolTip(QStringLiteral("图层框相对底图的自由偏移 Y"));

    // ---- 附加图片（可选）区块：开关驱动显隐；勾选后源图按「三适应 + 图片落位」先进图层框，再按图层落位叠到底图（两段）。----
    attachImageCheck_ = new QCheckBox(QStringLiteral("附加图片"), this);
    attachImageCheck_->setToolTip(QStringLiteral("勾选后把一张源图按适应方式放进图层框、再按图层落位摆到底图上（两段合成）；\n"
                                                 "不勾选即可直接导出纯图案画布。"));
    imagePickButton_ = new QPushButton(QStringLiteral("选择源图…"), this);
    imagePickButton_->setToolTip(QStringLiteral("选择要附加的源图（PNG/JPG/BMP/WebP 等），由引擎解码"));
    imagePathLabel_ = new QLabel(QStringLiteral("未选择"), this);
    imageFitLabel_ = new QLabel(QStringLiteral("适应方式："), this);
    imageFitCombo_ = new QComboBox(this);
    for (const auto&[fit, name] : std::array<std::pair<ImageFit, const char*>, 3>{{
             {ImageFit::Crop, "裁剪（源图原生贴入图层框，超框部分裁掉）"},
             {ImageFit::Stretch, "拉伸（缩放铺满图层框，忽略宽高比）"},
             {ImageFit::Native, "原生（图层尺寸恒等于源图尺寸）"}}}) {
        imageFitCombo_->addItem(QString::fromUtf8(name));
        imageFitCombo_->setItemData(imageFitCombo_->count() - 1, static_cast<int>(fit));
    }
    imageFitCombo_->setCurrentIndex(0);   // 默认「裁剪」。
    // 图片落位（图片 相对 图层）：锚点 / 枢轴 / 自由位置（随开关显示）。
    imageGroupLabel_ = new QLabel(QStringLiteral("图片落位"), this);
    imageGroupLabel_->setStyleSheet(QStringLiteral("font-weight:bold; padding-top:4px;"));
    imageAnchorLabel_ = new QLabel(QStringLiteral("图片锚点："), this);
    makeAnchorCombo(imageAnchorCombo_);
    imagePivotLabel_ = new QLabel(QStringLiteral("图片枢轴："), this);
    makeAnchorCombo(imagePivotCombo_);
    imagePosLabel_ = new QLabel(QStringLiteral("图片位置："), this);
    imagePosXSpin_ = new QSpinBox(this);
    imagePosXSpin_->setRange(-1000000, 1000000);
    imagePosXSpin_->setSuffix(QStringLiteral(" ,"));
    imagePosXSpin_->setToolTip(QStringLiteral("图片相对图层框的自由偏移 X（叠加在图片锚点/枢轴之上）"));
    imagePosYSpin_ = new QSpinBox(this);
    imagePosYSpin_->setRange(-1000000, 1000000);
    imagePosYSpin_->setSuffix(QStringLiteral(" px"));
    imagePosYSpin_->setToolTip(QStringLiteral("图片相对图层框的自由偏移 Y"));

    // ---- 装配（每控件都需 addWidget；图层落位常显、与图片落位分块不混淆）----
    // 图层落位（常显）。
    grid->addWidget(layerGroupLabel_, row, 0, 1, 4);
    ++row;
    grid->addWidget(layerAnchorLabel_, row, 0);
    grid->addWidget(layerAnchorCombo_, row, 1);
    grid->addWidget(layerPivotLabel_, row, 2);
    grid->addWidget(layerPivotCombo_, row, 3);
    ++row;
    grid->addWidget(layerPosLabel_, row, 0);
    grid->addWidget(layerPosXSpin_, row, 1);
    grid->addWidget(layerPosYSpin_, row, 2, 1, 2);
    ++row;
    // 附加图片开关 + 源图 + 适应。
    grid->addWidget(attachImageCheck_, row, 0, 1, 2);
    grid->addWidget(imagePickButton_, row, 2);
    grid->addWidget(imagePathLabel_, row, 3);
    ++row;
    grid->addWidget(imageFitLabel_, row, 0);
    grid->addWidget(imageFitCombo_, row, 1, 1, 3);
    ++row;
    // 图片落位（随开关显示）。
    grid->addWidget(imageGroupLabel_, row, 0, 1, 4);
    ++row;
    grid->addWidget(imageAnchorLabel_, row, 0);
    grid->addWidget(imageAnchorCombo_, row, 1);
    grid->addWidget(imagePivotLabel_, row, 2);
    grid->addWidget(imagePivotCombo_, row, 3);
    ++row;
    grid->addWidget(imagePosLabel_, row, 0);
    grid->addWidget(imagePosXSpin_, row, 1);
    grid->addWidget(imagePosYSpin_, row, 2, 1, 2);
    ++row;

    // ---- 预览 ----
    previewLabel_ = new QLabel(QStringLiteral("预览"), this);
    previewLabel_->setAlignment(Qt::AlignCenter);
    previewLabel_->setMinimumSize(280, 200);
    previewLabel_->setFrameShape(QFrame::StyledPanel);
    previewLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    root->addWidget(previewLabel_, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确定"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    root->addWidget(buttons);

    // ---- 连接 ----
    connect(bgSwatch_, &QPushButton::clicked, this, &NewCanvasDialog::onPickBackground);
    connect(fgSwatch_, &QPushButton::clicked, this, &NewCanvasDialog::onPickForeground);
    connect(buttons, &QDialogButtonBox::accepted, this, &NewCanvasDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    for (QSpinBox* s : {widthSpin_, heightSpin_, periodSpin_, lineWidthSpin_, dotRadiusSpin_, seedSpin_})
        connect(s, qOverload<int>(&QSpinBox::valueChanged), this, &NewCanvasDialog::onParamsChanged);
    connect(kindCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, &NewCanvasDialog::onParamsChanged);
    connect(orientationCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, &NewCanvasDialog::onParamsChanged);
    connect(noiseModeCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, &NewCanvasDialog::onParamsChanged);
    connect(attachImageCheck_, &QCheckBox::toggled, this, [this](const bool on) {
        // 首次开启：原生适应需把顶部宽/高（图层尺寸）回填为源图尺寸。
        if (on) syncCanvasSizeToSource();
        onParamsChanged();
    });
    connect(imagePickButton_, &QPushButton::clicked, this, &NewCanvasDialog::onPickImage);
    connect(imageAnchorCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, &NewCanvasDialog::onParamsChanged);
    connect(imageFitCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        // 切到原生：图层尺寸等于源图尺寸（需回填并锁定）；切回裁剪/拉伸：保留现值。
        syncCanvasSizeToSource();
        onParamsChanged();
    });
    // 图片枢轴 / 偏移 X/Y 即时刷新预览。
    connect(imagePivotCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, &NewCanvasDialog::onParamsChanged);
    connect(imagePosXSpin_, qOverload<int>(&QSpinBox::valueChanged), this, &NewCanvasDialog::onParamsChanged);
    connect(imagePosYSpin_, qOverload<int>(&QSpinBox::valueChanged), this, &NewCanvasDialog::onParamsChanged);
    // 图层落位（图层 相对 底图，常显）：锚点/枢轴/偏移 X/Y 即时刷新预览（段2）。
    connect(layerAnchorCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, &NewCanvasDialog::onParamsChanged);
    connect(layerPivotCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, &NewCanvasDialog::onParamsChanged);
    connect(layerPosXSpin_, qOverload<int>(&QSpinBox::valueChanged), this, &NewCanvasDialog::onParamsChanged);
    connect(layerPosYSpin_, qOverload<int>(&QSpinBox::valueChanged), this, &NewCanvasDialog::onParamsChanged);

    updateSwatches();
    updateControlAvailability();
    refreshPreview();
}

// 依当前图案类型 + 附加图片开关显隐/启停相关控件（未用的行整行收起，避免空白）。
//   图案切换时（kind != shownKind_）重建方向条目，并在进入色相渐变时把色相波长初值置 128。
void NewCanvasDialog::rebuildOrientationCombo(const generate::PatternKind kind) {
    const QSignalBlocker blocker(orientationCombo_);   // 重建条目不触发联动（避免递归）。
    const QVariant prev = orientationCombo_->currentData();   // 尽量保留已选方向。
    orientationCombo_->clear();

    std::vector<std::pair<generate::Orientation, const char*>> items;
    if (kind == generate::PatternKind::Stripes) {
        items = {{generate::Orientation::Horizontal, "水平（竖条）"},
                 {generate::Orientation::Vertical, "垂直（横条）"},
                 {generate::Orientation::DiagonalDown, "斜 ↘"},
                 {generate::Orientation::DiagonalUp, "斜 ↗"}};
    } else if (kind == generate::PatternKind::Concentric) {
        items = {{generate::Orientation::Horizontal, "圆环"},
                 {generate::Orientation::Vertical, "方环"}};
    } else if (kind == generate::PatternKind::LinearGradient || kind == generate::PatternKind::HueGradient) {
        items = {{generate::Orientation::Horizontal, "水平"},
                 {generate::Orientation::Vertical, "垂直"}};
    }
    for (const auto&[o, name] : items) {
        orientationCombo_->addItem(QString::fromUtf8(name));
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

void NewCanvasDialog::updateControlAvailability() {
    const auto kind = static_cast<generate::PatternKind>(kindCombo_->currentData().toInt());
    // 图案切换：重建方向条目；色相渐变进入时把色相波长初值置 128。
    if (kind != shownKind_) {
        shownKind_ = kind;
        rebuildOrientationCombo(kind);
        if (kind == generate::PatternKind::HueGradient) periodSpin_->setValue(128);
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
    // 图层落位（图层 相对 底图）：仅「添加图片图层」语境显示（新建画布产出扁平整幅、无叠层概念）；不受附加图片开关影响。
    const bool showLayerPlace = addLayerMode_;
    layerGroupLabel_->setVisible(showLayerPlace);
    layerAnchorLabel_->setVisible(showLayerPlace);
    layerAnchorCombo_->setVisible(showLayerPlace);
    layerPivotLabel_->setVisible(showLayerPlace);
    layerPivotCombo_->setVisible(showLayerPlace);
    layerPosLabel_->setVisible(showLayerPlace);
    layerPosXSpin_->setVisible(showLayerPlace);
    layerPosYSpin_->setVisible(showLayerPlace);
    // 附加图片区块：图片相关（源图/适应/图片落位）显隐由开关驱动；「原生」锁定顶部宽/高（图层尺寸＝源图尺寸）。
    const bool attach = attachImageCheck_->isChecked();
    const auto attachFit = static_cast<ImageFit>(imageFitCombo_->currentData().toInt());
    attachImageCheck_->setEnabled(true);
    imagePickButton_->setVisible(attach);
    imagePathLabel_->setVisible(attach);
    imageFitLabel_->setVisible(attach);
    imageFitCombo_->setVisible(attach);
    // 图片落位（图片 相对 图层）：随开关显示。
    imageGroupLabel_->setVisible(attach);
    imageAnchorLabel_->setVisible(attach);
    imageAnchorCombo_->setVisible(attach);
    imagePivotLabel_->setVisible(attach);
    imagePivotCombo_->setVisible(attach);
    imagePosLabel_->setVisible(attach);
    imagePosXSpin_->setVisible(attach);
    imagePosYSpin_->setVisible(attach);
    // 原生：锁定顶部宽/高（图层尺寸恒等于源图尺寸），其余模式宽/高可自由改。
    const bool lockSize = attach && attachFit == ImageFit::Native && !imageSource_.empty();
    widthSpin_->setEnabled(!lockSize);
    heightSpin_->setEnabled(!lockSize);
    // 周期标签随类型微调文案。
    if (kind == generate::PatternKind::Checkerboard) periodLabel_->setText(QStringLiteral("格子边长："));
    else if (kind == generate::PatternKind::Concentric) periodLabel_->setText(QStringLiteral("环间距："));
    else if (kind == generate::PatternKind::RadialRays) periodLabel_->setText(QStringLiteral("疏密："));
    else if (kind == generate::PatternKind::HueGradient) periodLabel_->setText(QStringLiteral("色相波长："));
    else periodLabel_->setText(QStringLiteral("周期："));
}

// 重绘两个色块按钮的半透明底（rgba 直接反映 alpha，透出按钮默认底以示意透明）。
void NewCanvasDialog::updateSwatches() {
    const auto style = [](const QColor& c) {
        return QStringLiteral("background-color: rgba(%1,%2,%3,%4); border:1px solid #888;")
            .arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
    };
    bgSwatch_->setStyleSheet(style(bgColor_));
    fgSwatch_->setStyleSheet(style(fgColor_));
}

void NewCanvasDialog::onPickBackground() {
    const QColor c = QColorDialog::getColor(bgColor_, this, QStringLiteral("选择底色"),
                                            QColorDialog::ShowAlphaChannel);
    if (c.isValid()) {
        bgColor_ = c;    // 取 RGBA，alpha 由取色对话框设定。
        updateSwatches();
        refreshPreview();
    }
}

void NewCanvasDialog::onPickForeground() {
    const QColor c = QColorDialog::getColor(fgColor_, this, QStringLiteral("选择前景色"),
                                            QColorDialog::ShowAlphaChannel);
    if (c.isValid()) {
        fgColor_ = c;    // 取 RGBA，alpha 由取色对话框设定。
        updateSwatches();
        refreshPreview();
    }
}

// 选源图：经 QFileDialog 取路径 → EngineBridge::loadImage（委托 Core 解码）→ 缓存到 imageSource_。
void NewCanvasDialog::onPickImage() {
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选择源图"), QString(),
                                                      QStringLiteral("图像文件 (*.png *.jpg *.jpeg *.bmp *.webp *.gif *.tga);;所有文件 (*)"));
    if (path.isEmpty()) return;
    QString err;
    core::Image img;
    if (!EngineBridge::loadImage(path, img, err)) {
        imagePathLabel_->setText(QStringLiteral("读取失败：%1").arg(err));
        return;
    }
    imageSource_ = std::move(img);
    imageSourcePath_ = path;   // 记住源图路径，供「添加图层」复用取相对名作 File 层 provenance。
    imagePathLabel_->setText(QFileInfo(path).fileName());
    syncCanvasSizeToSource();   // 原生时把顶部宽/高（图层尺寸）回填为源图尺寸（非原生不操作）。
    onParamsChanged();
}

// 原生适应（图层尺寸恒等于源图尺寸）：回填顶部宽/高为源图原生尺寸（信号阻塞，不触发预览联动——由调用方收尾）。
void NewCanvasDialog::syncCanvasSizeToSource() {
    if (imageSource_.empty()) return;
    if (static_cast<ImageFit>(imageFitCombo_->currentData().toInt()) != ImageFit::Native) return;
    const QSignalBlocker bw(widthSpin_);
    const QSignalBlocker bh(heightSpin_);
    widthSpin_->setValue(imageSource_.width());
    heightSpin_->setValue(imageSource_.height());
}

void NewCanvasDialog::onParamsChanged() {
    updateControlAvailability();
    updateSwatches();
    refreshPreview();
}

// 从控件汇编全尺寸语义的 PatternParams。
generate::PatternParams NewCanvasDialog::buildParams() const {
    generate::PatternParams p;
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
    return p;
}

// 切「添加图层」语境：顶部宽/高 spin（即图层尺寸）预填真实画布 W×H 并保持可编辑（改小即裁剪、改大即留白）。
// 附加图片开关默认开（添加图层语境必带图）；图层落位组此时才显示（新建画布语境隐藏）。
// 合成语义与新建画布完全一致（底图图案 + 图层落位/图片落位两段合成 + 越界裁剪），不叠底图、不撑大。
void NewCanvasDialog::setAddLayerMode(const int canvasW, const int canvasH) {
    addLayerMode_ = true;
    setWindowTitle(QStringLiteral("添加图片图层"));
    if (canvasW > 0 && canvasH > 0) {
        const QSignalBlocker bw(widthSpin_);
        const QSignalBlocker bh(heightSpin_);
        widthSpin_->setValue(canvasW);
        heightSpin_->setValue(canvasH);
    }
    attachImageCheck_->setChecked(true);   // 添加图层语境必带图（旧语义：选「图片」图案才产图层）。
}

// 组装一个可序列化的附加图片 File 图层：原始源（factor≠1 先等比预缩作预览代理）+ 两级落位（内层源图→
//   图层框 imgPlace/fit，外层图层框→画布 layerPlace）+ 图层框底色 boxBackdrop（即顶部图案参数）。
//   图层框尺寸 = boxW×boxH（顶部宽高）；外层 transform 的宽高留 0（用框原生尺寸，不再缩放）。
canvas::ImageLayer NewCanvasDialog::buildAttachedLayer(const double factor, const canvas::ImageTransform& layerPlace,
                                                       const generate::PatternParams& boxBackdrop, const int boxW, const int boxH,
                                                       const canvas::ImageTransform& imgPlace, const ImageFit fit) const {
    canvas::ImageLayer layer;
    layer.sourceKind = canvas::LayerSourceKind::File;
    layer.decodedPixels = factor < 1.0 ? imageSource_.scaled(factor) : imageSource_;   // 预览代理先等比预缩源。
    layer.hasImagePlacement = true;
    layer.boxWidth = boxW;
    layer.boxHeight = boxH;
    // GUI ImageFit 与 Core canvas::ImageFit 同序（Crop/Stretch/Native），经 int 中转移换。
    layer.imageFit = static_cast<canvas::ImageFit>(static_cast<int>(fit));
    layer.imageAnchor = imgPlace.inCanvasAnchor;
    layer.imagePivot = imgPlace.layerPivot;
    layer.imageX = imgPlace.x;
    layer.imageY = imgPlace.y;
    layer.boxBackdrop = boxBackdrop;   // 图层框底色/图案（Core 内层先铺底再叠源）。
    layer.transform = layerPlace;      // 外层：图层框 相对 画布 落位（可拖、可序列化）。
    return layer;
}

// 预览便捷：未选源图→纯图案底图；否则单张附加层（内层图层框合成 + 外层落位）走 Core composeCanvas。
//   factor≠1 为预览代理（源图与图层框尺寸/偏移已由调用方等比缩）。与定稿、与 config 重载同一条合成路径。
core::Image NewCanvasDialog::composeResult(const generate::PatternParams& p, const int outW, const int outH,
                                           const double factor, const canvas::ImageTransform& layerPlace,
                                           const canvas::ImageTransform& imgPlace, const ImageFit fit) const {
    if (imageSource_.empty()) return EngineBridge::generatePattern(outW, outH, p);   // 未选：纯图案底图（底色已烤入像素）。
    std::vector<canvas::ImageLayer> layers;   // 无 base（图层框底色已含于层内 boxBackdrop）。
    layers.push_back(buildAttachedLayer(factor, layerPlace, p, outW, outH, imgPlace, fit));
    return EngineBridge::composeCanvas(layers, outW, outH);
}

// 小尺寸代理两段合成并缩放到预览标签（factor ≤ 1，图案参数/尺寸/偏移等比缩放，避免大图逐像素卡顿）。
// 附加图片路走 composeResult（底图→段1 图片入图层框→段2 图层框按图层落位叠到底图），令 适应方式 / 两级落位 各控件实时反映。
void NewCanvasDialog::refreshPreview() {
    // 尺寸恒取顶部 spin＝图层尺寸＝图层框：新建画布＝画布尺寸；添加图层＝图层扁平尺寸（预填真实画布、可改）。
    const int w = widthSpin_->value();
    const int h = heightSpin_->value();

    const bool attach = attachImageCheck_->isChecked();
    if (attach && imageSource_.empty()) {   // 勾了开关尚未选源图：预览直接提示，不跑生成。
        previewLabel_->setText(QStringLiteral("请先选择源图"));
        return;
    }
    const ImageFit fit = static_cast<ImageFit>(imageFitCombo_->currentData().toInt());

    const int pw = scaled(w, std::max(w, h) > kPreviewMaxDim ? kPreviewMaxDim / double(std::max(w, h)) : 1.0);
    const int ph = scaled(h, std::max(w, h) > kPreviewMaxDim ? kPreviewMaxDim / double(std::max(w, h)) : 1.0);
    const double factor = w > 0 ? pw / double(w) : 1.0;
    const auto off = [factor](const int v) { return static_cast<int>(std::lround(v * factor)); };

    // 图层落位（图层 相对 底图，常显）与图片落位（图片 相对 图层，勾选时），偏移均按 factor 等比缩。
    canvas::ImageTransform layerPlace;
    canvas::ImageTransform imgPlace;
    layerPlace.inCanvasAnchor = static_cast<generate::ImageAnchor>(layerAnchorCombo_->currentData().toInt());
    layerPlace.layerPivot = static_cast<generate::ImageAnchor>(layerPivotCombo_->currentData().toInt());
    layerPlace.x = off(layerPosXSpin_->value());
    layerPlace.y = off(layerPosYSpin_->value());
    if (attach) {
        imgPlace.inCanvasAnchor = static_cast<generate::ImageAnchor>(imageAnchorCombo_->currentData().toInt());
        imgPlace.layerPivot = static_cast<generate::ImageAnchor>(imagePivotCombo_->currentData().toInt());
        imgPlace.x = off(imagePosXSpin_->value());
        imgPlace.y = off(imagePosYSpin_->value());
    }

    generate::PatternParams p = buildParams();
    if (factor < 1.0) {   // 周期/线宽/点半径随代理等比缩，保持纹理视觉密度一致。
        p.period = scaled(p.period, factor);
        p.lineWidth = scaled(p.lineWidth, factor);
        p.dotRadius = scaled(p.dotRadius, factor);
    }

    const core::Image img = composeResult(p, pw, ph, factor, layerPlace, imgPlace, fit);
    if (img.empty()) { previewLabel_->setText(QStringLiteral("尺寸非法")); return; }
    const QPixmap pm = toPixmap(img);
    previewLabel_->setPixmap(pm.scaled(previewLabel_->size(), Qt::KeepAspectRatio, Qt::FastTransformation));
}

// 定稿：全尺寸两段合成（图片→图层框→叠到底图）+ 记录摘要，成功后交存并接受（空图＝尺寸非法，不关闭）。
//   渐变/图案类在全尺寸下逐像素插值可耗时数秒，重采样/落位亦按总像素计，故大尺寸（总像素≥阈值）时
//   包一层模态忙碌框，小尺寸直接生成免闪烁。
void NewCanvasDialog::accept() {
    const generate::PatternParams p = buildParams();
    // 尺寸恒取 spin：新建画布＝画布尺寸；添加图层＝图层扁平尺寸（预填真实画布、可改，改小即裁剪、改大即留白）。
    const int w = widthSpin_->value();
    const int h = heightSpin_->value();

    // 附加图片早校验：勾选后必须先选源图；未勾选→直接产纯图案底图（可不加图导出）。
    const bool attach = attachImageCheck_->isChecked();
    if (attach && imageSource_.empty()) {
        previewLabel_->setText(QStringLiteral("请先选择源图"));
        return;
    }

    // 两级落位：与 refreshPreview / onAddImageLayer 同口径，走两段 composeCanvas 现算（所见即所得）。
    const ImageFit fit = static_cast<ImageFit>(imageFitCombo_->currentData().toInt());
    canvas::ImageTransform layerPlace;   // 图层 相对 底图（常显；「添加图层」语境作新层 transform，可再拖）。
    canvas::ImageTransform imgPlace;     // 图片 相对 图层。
    if (layerAnchorCombo_->currentData().isValid())
        layerPlace.inCanvasAnchor = static_cast<generate::ImageAnchor>(layerAnchorCombo_->currentData().toInt());
    if (layerPivotCombo_->currentData().isValid())
        layerPlace.layerPivot = static_cast<generate::ImageAnchor>(layerPivotCombo_->currentData().toInt());
    layerPlace.x = layerPosXSpin_->value();
    layerPlace.y = layerPosYSpin_->value();
    if (attach) {
        if (imageAnchorCombo_->currentData().isValid())
            imgPlace.inCanvasAnchor = static_cast<generate::ImageAnchor>(imageAnchorCombo_->currentData().toInt());
        if (imagePivotCombo_->currentData().isValid())
            imgPlace.layerPivot = static_cast<generate::ImageAnchor>(imagePivotCombo_->currentData().toInt());
        imgPlace.x = imagePosXSpin_->value();
        imgPlace.y = imagePosYSpin_->value();
    }
    core::Image flat;   // 新建画布扁平整幅（亦作 add-layer 的合法性/预览像素）。
    // 总像素≥ 2M 才弹忙碌框（逐像素/重采样在此量级以上开始可感）。全屏标志由 MainWindow 依
    // busyOverlayMode() 下发（busyOverlay_）：MobileShell 下为 true 铺满屏幕作全屏遮罩（SPEC §8.2），
    // 桌面为 false 居中小框；两者模态阻断语义一致。
    const auto generate = [&] {
        flat = composeResult(p, w, h, 1.0, layerPlace, imgPlace, fit);   // 未选源→纯图案；否则单张附加层合成（内层+外层）。
    };
    if (static_cast<long long>(w) * static_cast<long long>(h) >= 2LL * 1000 * 1000) {
        runInBusyDialog(this, busyOverlay_, QStringLiteral("正在生成图像，请稍候…"), generate);
    } else {
        generate();
    }
    if (flat.empty()) { if (previewLabel_) previewLabel_->setText(QStringLiteral("尺寸非法")); return; }

    // 存结果：新建画布 → result_ = flat 扁平 base；附加图片「添加图层」 → 额外构造可序列化 resultLayer_（原始源 + 两级落位 + 图层框底色）。
    resultParams_ = p;   // 留存汇编参数（供「添加图片图层」入口未勾图时产 Recipe 层，recipe 可序列化供 config 导出）。
    result_ = flat;
    if (attach && addLayerMode_) {
        resultLayer_ = buildAttachedLayer(1.0, layerPlace, p, w, h, imgPlace, fit);   // decodedPixels＝原始源（config 仅存 fileName，重载重解码）。
        producesLayerCanvas_ = true;
    }
    // 新建画布语境（非「添加图层」）：产出底图 ImageLayer 声明，使底图落位可随 §7 generate 段往返、可复现。
    if (!addLayerMode_) {
        canvas::ImageLayer base;
        base.zOrder = 0;   // 钉住最底（与叠加层 zOrder 域分离，语义标记）。
        if (attach) {
            // 附图片底：File 源 + 内层图片落位 + box＝画布尺寸 + boxBackdrop＝图案配方；transform 恒等（底图不可拖）。
            base.sourceKind = canvas::LayerSourceKind::File;
            base.fileName = QFileInfo(imageSourcePath_).fileName().toStdString();   // 相对名 provenance（供落/取 assets）。
            base.decodedPixels = imageSource_;   // 会话合成用原始源像素；序列化时上游剖离。
            base.hasImagePlacement = true;
            base.boxWidth = w;
            base.boxHeight = h;
            base.imageFit = static_cast<canvas::ImageFit>(static_cast<int>(fit));
            base.imageAnchor = imgPlace.inCanvasAnchor;
            base.imagePivot = imgPlace.layerPivot;
            base.imageX = imgPlace.x;
            base.imageY = imgPlace.y;
            base.boxBackdrop = p;   // 图层框底色/图案（Core 内层先铺底再叠源）。
        } else {
            // 纯图案底：Recipe 源 + 恒等 transform、无落位（序列化后与旧 generate 段逐字段等价）。
            base.sourceKind = canvas::LayerSourceKind::Recipe;
            base.recipe = p;
        }
        resultBaseLayer_ = base;
    }
    summary_ = QStringLiteral("%1×%2 · %3 · 底色(%4,%5,%6,%7)")
                   .arg(w)
                   .arg(h)
                   .arg(QString::fromUtf8(generate::patternKindName(p.kind)))
                   .arg(p.background.r).arg(p.background.g).arg(p.background.b).arg(p.background.a);
    // 非纯色时前景色（含 alpha）参与图案，一并回显。
    if (p.kind != generate::PatternKind::Solid)
        summary_ += QStringLiteral(" · 前景(%1,%2,%3,%4)")
                        .arg(p.foreground.r).arg(p.foreground.g).arg(p.foreground.b).arg(p.foreground.a);
    // 附加图片路回显适应模式与源图名。
    if (attach) {
        static constexpr std::array<std::pair<ImageFit, const char*>, 3> kFitNames{{
            {ImageFit::Crop, "裁剪"}, {ImageFit::Stretch, "拉伸"}, {ImageFit::Native, "原生"}}};
        const char* fitName = "裁剪";
        for (const auto&[f, name] : kFitNames)
            if (f == fit) { fitName = name; break; }
        summary_ += QStringLiteral(" · 附图(%1, %2)")
                        .arg(QString::fromUtf8(fitName), QFileInfo(imageSourcePath_).fileName());
    }

    // 【新建画布语境不产图层】——result_ = 整幅扁平画布直接作底图 base（input 生态位、不入 layers、不可拖）。
    // 【添加图层语境产图层】resultLayer_ = 可序列化 File 层（原始源 + 两级落位 + 图层框底色），producesLayerCanvas_ 已置；config 完整往返。
    QDialog::accept();
}

} // namespace idc::gui
