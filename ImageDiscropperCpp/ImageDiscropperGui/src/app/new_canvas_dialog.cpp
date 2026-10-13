// ============================================================================
// 文件：app/new_canvas_dialog.cpp
// 作用：实现 NewCanvasDialog（见同名头文件说明）——尺寸/附加图片/两级落位采参控件装配、
//       小尺寸代理实时预览，以及 accept() 时的全尺寸两段合成（图片→图层框→叠到图案底图）。
//       图案采参表单（底色/前景/类型/方向/参数/噪声）已拆至 panels/PatternFormWidget。
// 分块依据：装配（构造）/ 联动（updateControlAvailability：落位显隐 + 尺寸锁定）/
//           两段合成（buildAttachedLayer 内层 + composeResult 预览便捷）/ 预览（refreshPreview，走桥接）/
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
#include "app/controllers/preprocess_controller.h" // runInBusyDialog（全尺寸生成包忙碌框，避免 UI 冻结）
#include "app/canvas_prefs.h"                        // 新建画布默认尺寸（取初值 + 记住上次）
#include "model/engine_bridge.h"
#include "app/forms/pattern_form.h"
#include "app/forms/placement_row.h"
#include "util/gui_nums.h"
#include "i18n/gui_texts.h"
#include "i18n/gui_i18n.h"
#include "util/image_qt_adapter.h"

namespace idc::gui {

namespace {
// 依最长边把尺寸与周期/线宽/点半径等比缩到预览代理（factor ≤ 1）。
int scaled(const int v, const double factor) { return std::max(1, static_cast<int>(std::lround(v * factor))); }
} // namespace

NewCanvasDialog::NewCanvasDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(IDC_GTR(label, kDlgTitleNewCanvas));
    setModal(true);

    auto* root = new QVBoxLayout(this);
    auto* grid = new QGridLayout();
    grid->setContentsMargins(0, 0, 0, 0);
    root->addLayout(grid);

    int row = 0;
    // ---- 尺寸 ----
    widthSpin_ = new QSpinBox(this);
    widthSpin_->setRange(1, generate::kMaxDimension);
    widthSpin_->setValue(CanvasPrefs::instance().newCanvasWidth());   // 默认取应用级偏好（记住上次新建）；「添加图层」语境会随后预填真实画布。
    widthSpin_->setSuffix(QStringLiteral(" px"));
    heightSpin_ = new QSpinBox(this);
    heightSpin_->setRange(1, generate::kMaxDimension);
    heightSpin_->setValue(CanvasPrefs::instance().newCanvasHeight());
    heightSpin_->setSuffix(QStringLiteral(" px"));
    grid->addWidget(new QLabel(IDC_GTR(label, kWidthColonLabel), this), row, 0);
    grid->addWidget(widthSpin_, row, 1);
    grid->addWidget(new QLabel(IDC_GTR(label, kHeightColonLabel), this), row, 2);
    grid->addWidget(heightSpin_, row, 3);
    ++row;

    // ---- 图案参数表单（底色/前景/类型/方向/周期/线宽/点半径/噪声；整表单占一行四列，内部布局与旧版逐行一致）----
    patternForm_ = new PatternFormWidget(this);
    grid->addWidget(patternForm_, row, 0, 1, 4);
    ++row;

    // ---- 两级落位（panels/placement_row）：图层落位（图层 相对 底图）与图片落位（图片 相对 图层）
    //      各为「锚点/枢轴/偏移」三行一组的独立组件，整组占对话框一行四列，内部布局与旧版逐行一致。----
    layerPlaceRow_ = new PlacementRow(IDC_GTR(label, kLayerPlaceBox), this);
    layerPlaceRow_->setToolTip(IDC_GTR(tooltip, kLayerPlaceRowTip));
    imagePlaceRow_ = new PlacementRow(IDC_GTR(label, kImagePlaceBox), this);
    imagePlaceRow_->setToolTip(IDC_GTR(tooltip, kImagePlaceRowTip));

    // ---- 附加图片（可选）区块：开关驱动显隐；勾选后源图按「三适应 + 图片落位」先进图层框，再按图层落位叠到底图（两段）。----
    attachImageCheck_ = new QCheckBox(IDC_GTR(label, kAttachImageCheck), this);
    attachImageCheck_->setToolTip(IDC_GTR(tooltip, kAttachImageTip));
    imagePickButton_ = new QPushButton(IDC_GTR(label, kPickSourceImageButton), this);
    imagePickButton_->setToolTip(IDC_GTR(tooltip, kPickSourceImageBtn));
    imagePathLabel_ = new QLabel(IDC_GTR(label, kNoSourceImageChosen), this);
    imageFitLabel_ = new QLabel(IDC_GTR(label, kImageFitFieldLabel), this);
    imageFitCombo_ = new QComboBox(this);
    for (const auto&[fit, name] : std::array<std::pair<ImageFit, i18n::TextRef>, 3>{{
             {ImageFit::Crop, IDC_GT(label, kFitCropItem)},
             {ImageFit::Stretch, IDC_GT(label, kFitStretchItem)},
             {ImageFit::Native, IDC_GT(label, kFitNativeItem)}}}) {
        imageFitCombo_->addItem(i18n::qs(name));
        imageFitCombo_->setItemData(imageFitCombo_->count() - 1, static_cast<int>(fit));
    }
    imageFitCombo_->setCurrentIndex(0);   // 默认「裁剪」。

    // ---- 装配（每控件都需 addWidget；图层落位常显、与图片落位分块不混淆）----
    // 图层落位（常显，整组一行）。
    grid->addWidget(layerPlaceRow_, row, 0, 1, 4);
    ++row;
    // 附加图片开关 + 源图 + 适应。
    grid->addWidget(attachImageCheck_, row, 0, 1, 2);
    grid->addWidget(imagePickButton_, row, 2);
    grid->addWidget(imagePathLabel_, row, 3);
    ++row;
    grid->addWidget(imageFitLabel_, row, 0);
    grid->addWidget(imageFitCombo_, row, 1, 1, 3);
    ++row;
    // 图片落位（随开关显示，整组一行）。
    grid->addWidget(imagePlaceRow_, row, 0, 1, 4);
    ++row;

    // ---- 预览 ----
    previewLabel_ = new QLabel(IDC_GTR(label, kPreviewPlaceholder), this);
    previewLabel_->setAlignment(Qt::AlignCenter);
    previewLabel_->setMinimumSize(nums::dialog::kNewCanvasPreviewMinWidthPx,
                                  nums::dialog::kNewCanvasPreviewMinHeightPx);
    previewLabel_->setFrameShape(QFrame::StyledPanel);
    previewLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    root->addWidget(previewLabel_, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(IDC_GTR(label, kDialogOkButton));
    buttons->button(QDialogButtonBox::Cancel)->setText(IDC_GTR(label, kDialogCancelButton));
    root->addWidget(buttons);

    // ---- 连接 ----
    connect(patternForm_, &PatternFormWidget::paramsChanged, this, &NewCanvasDialog::onParamsChanged);
    connect(buttons, &QDialogButtonBox::accepted, this, &NewCanvasDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    for (const QSpinBox* s : {widthSpin_, heightSpin_})
        connect(s, qOverload<int>(&QSpinBox::valueChanged), this, &NewCanvasDialog::onParamsChanged);
    connect(attachImageCheck_, &QCheckBox::toggled, this, [this](const bool on) {
        // 首次开启：原生适应需把顶部宽/高（图层尺寸）回填为源图尺寸。
        if (on) syncCanvasSizeToSource();
        onParamsChanged();
    });
    connect(imagePickButton_, &QPushButton::clicked, this, &NewCanvasDialog::onPickImage);
    connect(imageFitCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        // 切到原生：图层尺寸等于源图尺寸（需回填并锁定）；切回裁剪/拉伸：保留现值。
        syncCanvasSizeToSource();
        onParamsChanged();
    });
    // 两级落位（图层 相对 底图常显 / 图片 相对 图层随开关）：任一控件变化即时刷新预览。
    connect(layerPlaceRow_, &PlacementRow::changed, this, &NewCanvasDialog::onParamsChanged);
    connect(imagePlaceRow_, &PlacementRow::changed, this, &NewCanvasDialog::onParamsChanged);

    patternForm_->updateAvailability();
    updateControlAvailability();
    refreshPreview();
}

// 依附加图片开关/语境/适应方式显隐落位组并锁定尺寸（图案相关行显隐与文案在 PatternFormWidget 内）。
void NewCanvasDialog::updateControlAvailability() const {
    // 图层落位（图层 相对 底图）：仅「添加图片图层」语境显示（新建画布产出扁平整幅、无叠层概念）；不受附加图片开关影响。
    layerPlaceRow_->setVisible(addLayerMode_);
    // 附加图片区块：图片相关（源图/适应/图片落位）显隐由开关驱动；「原生」锁定顶部宽/高（图层尺寸＝源图尺寸）。
    const bool attach = attachImageCheck_->isChecked();
    const auto attachFit = static_cast<ImageFit>(imageFitCombo_->currentData().toInt());
    attachImageCheck_->setEnabled(true);
    imagePickButton_->setVisible(attach);
    imagePathLabel_->setVisible(attach);
    imageFitLabel_->setVisible(attach);
    imageFitCombo_->setVisible(attach);
    // 图片落位（图片 相对 图层）：随开关显示。
    imagePlaceRow_->setVisible(attach);
    // 原生：锁定顶部宽/高（图层尺寸恒等于源图尺寸），其余模式宽/高可自由改。
    const bool lockSize = attach && attachFit == ImageFit::Native && !imageSource_.empty();
    widthSpin_->setEnabled(!lockSize);
    heightSpin_->setEnabled(!lockSize);
}

// 选源图：经 QFileDialog 取路径 → EngineBridge::loadImage（委托 Core 解码）→ 缓存到 imageSource_。
void NewCanvasDialog::onPickImage() {
    const QString path = QFileDialog::getOpenFileName(
        this, IDC_GTR(label, kDlgTitlePickSourceImage), QString(),
        IDC_GTR(label, kFilterSourceImage));
    if (path.isEmpty()) return;
    core::Image img;
    if (QString err; !EngineBridge::loadImage(path, img, err)) {
        imagePathLabel_->setText(IDC_GTR(label, kReadSourceFailedFmt).arg(err));
        return;
    }
    imageSource_ = std::move(img);
    imageSourcePath_ = path;   // 记住源图路径，供「添加图层」复用取相对名作 File 层 provenance。
    imagePathLabel_->setText(QFileInfo(path).fileName());
    syncCanvasSizeToSource();   // 原生时把顶部宽/高（图层尺寸）回填为源图尺寸（非原生不操作）。
    onParamsChanged();
}

// 原生适应（图层尺寸恒等于源图尺寸）：回填顶部宽/高为源图原生尺寸（信号阻塞，不触发预览联动——由调用方收尾）。
void NewCanvasDialog::syncCanvasSizeToSource() const {
    if (imageSource_.empty()) return;
    if (static_cast<ImageFit>(imageFitCombo_->currentData().toInt()) != ImageFit::Native) return;
    const QSignalBlocker bw(widthSpin_);
    const QSignalBlocker bh(heightSpin_);
    widthSpin_->setValue(imageSource_.width());
    heightSpin_->setValue(imageSource_.height());
}

void NewCanvasDialog::onParamsChanged() const {
    patternForm_->updateAvailability();
    updateControlAvailability();
    refreshPreview();
}

// 切「添加图层」语境：顶部宽/高 spin（即图层尺寸）预填真实画布 W×H 并保持可编辑（改小即裁剪、改大即留白）。
// 附加图片开关默认开（添加图层语境必带图）；图层落位组此时才显示（新建画布语境隐藏）。
// 合成语义与新建画布完全一致（底图图案 + 图层落位/图片落位两段合成 + 越界裁剪），不叠底图、不撑大。
void NewCanvasDialog::setAddLayerMode(const int canvasW, const int canvasH) {
    addLayerMode_ = true;
    setWindowTitle(IDC_GTR(label, kDlgTitleAddLayer));
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
void NewCanvasDialog::refreshPreview() const {
    // 尺寸恒取顶部 spin＝图层尺寸＝图层框：新建画布＝画布尺寸；添加图层＝图层扁平尺寸（预填真实画布、可改）。
    const int w = widthSpin_->value();
    const int h = heightSpin_->value();

    const bool attach = attachImageCheck_->isChecked();
    if (attach && imageSource_.empty()) {   // 勾了开关尚未选源图：预览直接提示，不跑生成。
        previewLabel_->setText(IDC_GTR(label, kNeedSourceImage));
        return;
    }
    const auto fit = static_cast<ImageFit>(imageFitCombo_->currentData().toInt());

    const int pw = scaled(w, std::max(w, h) > kPreviewMaxDim ? kPreviewMaxDim / static_cast<double>(std::max(w, h)) : 1.0);
    const int ph = scaled(h, std::max(w, h) > kPreviewMaxDim ? kPreviewMaxDim / static_cast<double>(std::max(w, h)) : 1.0);
    const double factor = w > 0 ? pw / static_cast<double>(w) : 1.0;
    const auto off = [factor](const int v) { return static_cast<int>(std::lround(v * factor)); };

    // 图层落位（图层 相对 底图，常显）与图片落位（图片 相对 图层，勾选时），偏移均按 factor 等比缩。
    canvas::ImageTransform layerPlace;
    canvas::ImageTransform imgPlace;
    layerPlace.inCanvasAnchor = layerPlaceRow_->anchor();
    layerPlace.layerPivot = layerPlaceRow_->pivot();
    layerPlace.x = off(layerPlaceRow_->offsetX());
    layerPlace.y = off(layerPlaceRow_->offsetY());
    if (attach) {
        imgPlace.inCanvasAnchor = imagePlaceRow_->anchor();
        imgPlace.layerPivot = imagePlaceRow_->pivot();
        imgPlace.x = off(imagePlaceRow_->offsetX());
        imgPlace.y = off(imagePlaceRow_->offsetY());
    }

    generate::PatternParams p;
    patternForm_->params(p);
    if (factor < 1.0) {   // 周期/线宽/点半径随代理等比缩，保持纹理视觉密度一致。
        p.period = scaled(p.period, factor);
        p.lineWidth = scaled(p.lineWidth, factor);
        p.dotRadius = scaled(p.dotRadius, factor);
    }

    const core::Image img = composeResult(p, pw, ph, factor, layerPlace, imgPlace, fit);
    if (img.empty()) { previewLabel_->setText(IDC_GTR(label, kBadCanvasSize)); return; }
    const QPixmap pm = toPixmap(img);
    previewLabel_->setPixmap(pm.scaled(previewLabel_->size(), Qt::KeepAspectRatio, Qt::FastTransformation));
}

// 定稿：全尺寸两段合成（图片→图层框→叠到底图）+ 记录摘要，成功后交存并接受（空图＝尺寸非法，不关闭）。
//   渐变/图案类在全尺寸下逐像素插值可耗时数秒，重采样/落位亦按总像素计，故大尺寸（总像素≥阈值）时
//   包一层模态忙碌框，小尺寸直接生成免闪烁。
void NewCanvasDialog::accept() {
    generate::PatternParams p;
    patternForm_->params(p);
    // 尺寸恒取 spin：新建画布＝画布尺寸；添加图层＝图层扁平尺寸（预填真实画布、可改，改小即裁剪、改大即留白）。
    const int w = widthSpin_->value();
    const int h = heightSpin_->value();

    // 附加图片早校验：勾选后必须先选源图；未勾选→直接产纯图案底图（可不加图导出）。
    const bool attach = attachImageCheck_->isChecked();
    if (attach && imageSource_.empty()) {
        previewLabel_->setText(IDC_GTR(label, kNeedSourceImage));
        return;
    }

    // 两级落位：与 refreshPreview / onAddImageLayer 同口径，走两段 composeCanvas 现算（所见即所得）。
    const auto fit = static_cast<ImageFit>(imageFitCombo_->currentData().toInt());
    canvas::ImageTransform layerPlace;   // 图层 相对 底图（常显；「添加图层」语境作新层 transform，可再拖）。
    canvas::ImageTransform imgPlace;     // 图片 相对 图层。
    layerPlace.inCanvasAnchor = layerPlaceRow_->anchor();
    layerPlace.layerPivot = layerPlaceRow_->pivot();
    layerPlace.x = layerPlaceRow_->offsetX();
    layerPlace.y = layerPlaceRow_->offsetY();
    if (attach) {
        imgPlace.inCanvasAnchor = imagePlaceRow_->anchor();
        imgPlace.layerPivot = imagePlaceRow_->pivot();
        imgPlace.x = imagePlaceRow_->offsetX();
        imgPlace.y = imagePlaceRow_->offsetY();
    }
    core::Image flat;   // 新建画布扁平整幅（亦作 add-layer 的合法性/预览像素）。
    // 总像素≥ 2M 才弹忙碌框（逐像素/重采样在此量级以上开始可感）。全屏标志由 MainWindow 依
    // busyOverlayMode() 下发（busyOverlay_）：MobileShell 下为 true 铺满屏幕作全屏遮罩（SPEC §8.2），
    // 桌面为 false 居中小框；两者模态阻断语义一致。
    const auto generate = [&] {
        flat = composeResult(p, w, h, 1.0, layerPlace, imgPlace, fit);   // 未选源→纯图案；否则单张附加层合成（内层+外层）。
    };
    if (static_cast<long long>(w) * static_cast<long long>(h) >= 2LL * 1000 * 1000) {
        runInBusyDialog(this, busyOverlay_, IDC_GTR(status, kNewCanvasGenerateBusy), generate);
    } else {
        generate();
    }
    if (flat.empty()) {
        if (previewLabel_) previewLabel_->setText(IDC_GTR(label, kBadCanvasSize));
        return;
    }

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
    summary_ = IDC_GTR(label, kSummaryBaseFmt)
                   .arg(w)
                   .arg(h)
                   .arg(QString::fromUtf8(generate::patternKindName(p.kind).c_str()))   // 显示名按当前语言现取（Qt5 需 c_str）
                   .arg(p.background.r).arg(p.background.g).arg(p.background.b).arg(p.background.a);
    // 非纯色时前景色（含 alpha）参与图案，一并回显。
    if (p.kind != generate::PatternKind::Solid)
        summary_ += IDC_GTR(label, kSummaryFgPartFmt)
                        .arg(p.foreground.r).arg(p.foreground.g).arg(p.foreground.b).arg(p.foreground.a);
    // 附加图片路回显适应模式与源图名。
    if (attach) {
        static constexpr std::array<std::pair<ImageFit, i18n::TextRef>, 3> kFitNames{{
            {ImageFit::Crop, IDC_GT(label, kFitNameCrop)}, {ImageFit::Stretch, IDC_GT(label, kFitNameStretch)},
            {ImageFit::Native, IDC_GT(label, kFitNameNative)}}};
        auto fitName = IDC_GT(label, kFitNameCrop);
        for (const auto&[f, name] : kFitNames)
            if (f == fit) { fitName = name; break; }
        summary_ += IDC_GTR(label, kSummaryAttachPartFmt)
                        .arg(i18n::qs(fitName), QFileInfo(imageSourcePath_).fileName());
    }

    // 新建画布语境（非「添加图层」）：记住本次尺寸，下次新建默认沿用（应用级 CanvasPrefs，不改历史）。
    if (!addLayerMode_) CanvasPrefs::instance().rememberNewCanvasSize(w, h);
    // 【新建画布语境不产图层】——result_ = 整幅扁平画布直接作底图 base（input 生态位、不入 layers、不可拖）。
    // 【添加图层语境产图层】resultLayer_ = 可序列化 File 层（原始源 + 两级落位 + 图层框底色），producesLayerCanvas_ 已置；config 完整往返。
    QDialog::accept();
}

} // namespace idc::gui
