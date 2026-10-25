// ============================================================================
// 文件：app/new_canvas_dialog.cpp
// 作用：实现 NewCanvasDialog（见同名头文件说明）——尺寸/附加图片/两级落位采参控件装配、
//       小尺寸代理实时预览，以及 accept() 时的全尺寸两段合成（图片→图层框→叠到图案底图）。
//       图案采参表单（底色/前景/类型/方向/参数/噪声）位于 app/forms/PatternFormWidget（pattern_form.h）。
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
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMargins>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
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
#include "util/scrollable_dialog.h"
#include "util/widget_factory.h"
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
    // 主体（表单 + 预览）装进纵向可滑外壳：小屏 / 横屏超高时内部滚动，配合底部固定按钮行 + 屏幕尺寸护栏，
    // 根治「显示不全 / 无法关闭」；勾选附加图片增删的行也落在滚动内容里，外层窗口不被撑大。
    // 按钮行留滚动区外恒可关闭；预览并入滚动内容（用户滚到底即可看到）。
    auto* body = new QWidget(this);
    auto* grid = new QGridLayout(body);
    grid->setContentsMargins(0, 0, 0, 0);
    // wrapScrollable 的「内容最小宽 + 纵滚条宽」预留是在调用一刻对 body->minimumSizeHint() 取快照，
    //   故须等 body 网格装配完毕再接入 root（见预览行之后）；此处空壳时快照≈0、预留会失效。

    int row = 0;
    // ---- 尺寸 ----
    widthSpin_ = makeSpin(this, 1, generate::kMaxDimension, QStringLiteral(" px"));
    widthSpin_->setValue(CanvasPrefs::instance().newCanvasWidth());   // 默认取应用级偏好（记住上次新建）；「添加图层」语境会随后预填真实画布。
    heightSpin_ = makeSpin(this, 1, generate::kMaxDimension, QStringLiteral(" px"));
    heightSpin_->setValue(CanvasPrefs::instance().newCanvasHeight());
    grid->addWidget(new QLabel(IDC_GTR(label, kWidthColonLabel), this), row, 0);
    grid->addWidget(widthSpin_, row, 1);
    grid->addWidget(new QLabel(IDC_GTR(label, kHeightColonLabel), this), row, 2);
    grid->addWidget(heightSpin_, row, 3);
    ++row;

    // ---- 整体不透明度（0–255，合成期 alpha 乘子）：作用产出的图层或底图（与图层编辑面板口径/用词一致）；
    //      画布级整体透明度不在本对话框采集（属建好后的画布态，交图层面板处理）。----
    opacitySpin_ = makeSpin(this, 0, 255);
    opacitySpin_->setValue(255);   // 默认不透明。
    opacitySpin_->setToolTip(IDC_GTR(tooltip, kLayerOpacitySpin));
    grid->addWidget(new QLabel(IDC_GTR(label, kOpacityFieldLabel), this), row, 0);
    grid->addWidget(opacitySpin_, row, 1);
    ++row;

    // ---- 图案参数表单（底色/前景/类型/方向/周期/线宽/点半径/噪声；整表单占一行四列）----
    patternForm_ = new PatternFormWidget(this);
    grid->addWidget(patternForm_, row, 0, 1, 4);
    ++row;

    // ---- 两级落位（app/forms/placement_row）：图层落位（图层 相对 底图）与图片落位（图片 相对 图层）
    //      各为「锚点/枢轴/偏移」三行一组的独立组件，整组占对话框一行四列。----
    layerPlaceRow_ = new PlacementRow(IDC_GTR(label, kLayerPlaceBox), this);
    layerPlaceRow_->setToolTip(IDC_GTR(tooltip, kLayerPlaceRowTip));
    imagePlaceRow_ = new PlacementRow(IDC_GTR(label, kImagePlaceBox), this);
    imagePlaceRow_->setToolTip(IDC_GTR(tooltip, kImagePlaceRowTip));
    // 图层落位偏移另接受百分比串（SPEC §2.1）：基准＝本对话框的尺寸框（即新建画布的宽/高），
    // 随宽/高实时变化；图片落位（图片相对图层）不接百分比——其基准是图层框而非画布，
    // 而图层框尺寸要到合成期才由适应方式推出，对话框内无确定基准。
    layerPlaceRow_->setOffsetBasisProvider([this](const bool horizontal) {
        return horizontal ? widthSpin_->value() : heightSpin_->value();
    });

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
             {ImageFit::Native, IDC_GT(label, kFitNativeItem)}
             }}) {
        imageFitCombo_->addItem(i18n::qs(name));
        imageFitCombo_->setItemData(imageFitCombo_->count() - 1, static_cast<int>(fit));
    }
    imageFitCombo_->setCurrentIndex(0);

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

    // ---- 预览（并入滚动内容，放在表单最后一行）----
    previewLabel_ = new QLabel(IDC_GTR(label, kPreviewPlaceholder), this);
    previewLabel_->setAlignment(Qt::AlignCenter);
    previewLabel_->setMinimumSize(nums::dialog::kNewCanvasPreviewMinWidthPx,
                                  nums::dialog::kNewCanvasPreviewMinHeightPx);
    previewLabel_->setFrameShape(QFrame::StyledPanel);
    previewLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    grid->addWidget(previewLabel_, row, 0, 1, 4);
    ++row;

    // 主体网格装配完毕 → 此刻接入可滑外壳：wrapScrollable 据此刻 body 的真实最小宽预留「内容最小宽 + 纵滚条宽」。
    root->addWidget(wrapScrollable(body), 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(IDC_GTR(label, kDialogOkButton));
    buttons->button(QDialogButtonBox::Cancel)->setText(IDC_GTR(label, kDialogCancelButton));
    root->addWidget(buttons);

    // ---- 连接 ----
    connect(patternForm_, &PatternFormWidget::paramsChanged, this, &NewCanvasDialog::onParamsChanged);
    connect(buttons, &QDialogButtonBox::accepted, this, &NewCanvasDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    for (const QSpinBox* s : {widthSpin_, heightSpin_, opacitySpin_})
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

    // 屏幕尺寸护栏：按可用几何钳最大宽 / 高（比例见 nums::mobile::kDialogMax*），首次显示收到
    // 「表单+预览自然尺寸 + 按钮行」与可用几何的较小者；小屏 / 横屏超出即内部滚动、按钮常驻，
    // 勾选附加图片增删的行落在滚动内容里、外层窗口不被撑大；桌面大屏下钳值远超内容、等效无约束。
    const QSize formHint = body->sizeHint();   // 含预览（已在 body 内）
    // 护栏首显按**整窗尺寸** resize：formHint 只是内容口径，须补根布局左右边距，
    // 否则视口恒比内容自然宽窄一条边距、最右列被裁（即使无纵滚）；纵滚宽度由 wrapScrollable
    // 抬滚动区最小宽另行预留。
    const QMargins rootMargin = root->contentsMargins();
    limitDialogToScreen(this, QSize(
        qMax(formHint.width(), nums::dialog::kNewCanvasPreviewMinWidthPx)
            + rootMargin.left() + rootMargin.right(),
        formHint.height() + 2 * nums::dialog::kTouchFieldMinHeightPx + nums::panels::kPanelSpacingPx));
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
        // 长错误串（含 content:// 内容 URI）按宽省略号截断，避免撑宽表单主体；完整文转入 tooltip。
        const QString msg = IDC_GTR(label, kReadSourceFailedFmt).arg(err);
        imagePathLabel_->setText(QFontMetrics(imagePathLabel_->font())
                                    .elidedText(msg, Qt::ElideMiddle, nums::mobile::kErrorPathMaxWidthPx));
        imagePathLabel_->setToolTip(msg);
        return;
    }
    imageSource_ = std::move(img);
    imageSourceOriginalPath_ = path;   // 留存原始选取路径（持久化前）：供 SessionController::onNew 接入 MRU 并据此跳过 content://。
    // content:// 不可重开、且 QFileInfo(content://).fileName() 只是数字 doc id：持久化到应用私有
    // 目录取稳定真实路径作 provenance（缓存物化会被回收，故另走 persistForProvenance）。
    // 非 content://（桌面）原样返回，行为不变；持久失败则回退原始路径作显示。
    if (const QString persisted = EngineBridge::persistForProvenance(path); !persisted.isEmpty())
        imageSourcePath_ = persisted;
    else
        imageSourcePath_ = path;
    imagePathLabel_->setText(QFileInfo(imageSourcePath_).fileName());
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
// 附加图片开关默认关（未勾即产纯图案 recipe 图层，勾选才产 file 图层）；图层落位组此时才显示（新建画布语境隐藏）。
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
    // 语境切换后刷新控件显隐与预览：构造时以「新建画布」语境（addLayerMode_＝假）算过一次显隐，图层落位组被隐藏；
    //   此处不改就停留在隐藏态，直到首个交互触发 onParamsChanged——故主动刷一次，使图层落位随语境即时可见。
    onParamsChanged();
}

// 组装一个可序列化的附加图片 File 图层：原始源（factor≠1 先等比预缩作预览代理）+ 两级落位（内层源图→
//   图层框 imgPlace/fit，外层图层框→画布 layerPlace）+ 图层框底色 boxBackdrop（即顶部图案参数）。
//   图层框尺寸 = boxW×boxH（顶部宽高）；外层 transform 的宽高留 0（用框原生尺寸，不缩放）。
canvas::ImageLayer NewCanvasDialog::buildAttachedLayer(const double factor, const canvas::ImageTransform& layerPlace,
                                                       const generate::PatternParams& boxBackdrop, const int boxW, const int boxH,
                                                       const canvas::ImageTransform& imgPlace, const ImageFit fit) const {
    canvas::ImageLayer layer;
    layer.decodedPixels = factor < 1.0 ? imageSource_.scaled(factor) : imageSource_;   // 预览代理先等比预缩源（无 recipe → 文件源派生）。
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
    if (imageSource_.empty()) {   // 未选：纯图案底图（底色已烤入像素）。
        auto flat = EngineBridge::generatePattern(outW, outH, p);
        // 纯图案 makePattern 不施 alpha 乘子（合成边界单点施加、防双淡的不变式）：预览在 layerPlace.opacity 上此处施加；
        //   定稿恒以 opacity=255 调本函数（flat 保持不透明，透度另由 baseOpacity/图层 transform 于运行时施加），故不双淡。
        if (const int op = layerPlace.opacity; op < 255) canvas::applyAlpha(flat, static_cast<std::uint8_t>(op));
        return flat;
    }
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
    layerPlace.opacity = opacitySpin_->value();   // 预览即时反映整体不透明度（附图路经 composeCanvas 合成可见）。
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
    // 总像素≥ 2M 才弹忙碌框（逐像素/重采样在此量级以上开始可感）。全屏标志由 SessionController 依
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
    // 整体不透明度：只随产出载体（图层/底图）携于声明字段，不烘进 flat 像素（底图透度由运行时 fadedCanvasBase 单点施加，避免与 baseOpacity 双淡）。
    const int op = opacitySpin_->value();
    resultParams_ = p;   // 留存汇编参数（供「添加图片图层」入口未勾图时产 Recipe 层，recipe 可序列化供 config 导出）。
    resultOpacity_ = op;
    result_ = flat;
    if (attach && addLayerMode_) {
        resultLayer_ = buildAttachedLayer(1.0, layerPlace, p, w, h, imgPlace, fit);   // decodedPixels＝原始源（config 仅存 fileName，重载重解码）。
        resultLayer_.transform.opacity = op;   // 图层整体不透明度（composeCanvas 合成期施加）
        producesLayerCanvas_ = true;
    }
    // 新建画布语境（非「添加图层」）：产出底图 ImageLayer 声明，使底图落位可随 §7 generate 段往返、可复现。
    if (!addLayerMode_) {
        canvas::ImageLayer base;
        base.zOrder = 0;   // 钉住最底（与叠加层 zOrder 域分离，语义标记）。
        base.transform.opacity = op;   // 底图整体不透明度：resetBaseWith 回填运行态 baseOpacity_（内存声明随后归一 255）。
        if (attach) {
            // 附图片底：文件源（携位图/落位）+ box＝画布尺寸 + boxBackdrop＝图案配方；transform 恒等（底图不可拖）。
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
            // 纯图案底：有 recipe + 恒等 transform、无落位（序列化为纯 recipe 形态的 generate 段）。
            base.recipe = p;
        }
        resultBaseLayer_ = base;
    }
    summary_ = IDC_GTR(label, kSummaryBaseFmt)
                   .arg(w)
                   .arg(h)
                   .arg(QString::fromUtf8(generate::patternKindName(p.kind).c_str()))   // 显示名按当前语言现取（经 c_str() 走 fromUtf8 的 const char* 重载）
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
