// ============================================================================
// 文件：app/dialogs/new_canvas_dialog.cpp
// 作用：实现 NewCanvasDialog（见同名头文件说明）——SPEC §2.2a 合成模型采参视图：节点外观横贯
//       （尺寸/整体不透明度/图层落位 + 节点外观简表）、图案/图片两栏元素分组（各携元素外观简表），
//       三组皆可纯折叠展开；小尺寸代理实时预览，accept() 按 §2.2a 装配序（唯一口径）物化。
//       图案采参表单（底色/前景/类型/方向/参数/噪声）位于 app/forms/PatternFormWidget。
// 分块依据：装配（构造）/ 联动（updateControlAvailability：落位显隐 + 尺寸锁定）/
//           合成（buildAttachedLayer 组装 + composeResult 预览便捷）/ 预览（refreshPreview，走桥接）/
//           定稿（accept，走桥接）各成一块。
// ============================================================================
#include "app/new_canvas_dialog.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMargins>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QScreen>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "generate/pattern.h"
#include "app/controllers/preprocess_controller.h" // runInBusyDialog（全尺寸生成包忙碌框，避免 UI 冻结）
#include "app/canvas_prefs.h"                        // 新建画布默认尺寸（取初值 + 记住上次）
#include "model/engine_bridge.h"
#include "app/forms/pattern_form.h"
#include "app/forms/placement_row.h"
#include "panels/element_appearance_group.h"
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

// 并排列的顶对齐包装：内容钉顶——列被兄弟列撑高时多余空间沉底（不居中浮空）。
QWidget* pinnedTop(QWidget* child) {
    auto* w = new QWidget(child->parentWidget());
    auto* lay = new QVBoxLayout(w);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->addWidget(child);
    lay->addStretch(1);
    return w;
}

// 纯折叠分组：标题行（箭头按钮 + 标题），点击切换内容显隐。无勾选语义——折叠只是显隐，
//   不改任何声明（图案组收起 ≠ 去掉图案元素）。
//   check 可为空（纯折叠）；非空时作为**声明开关**放标题行最左——勾选＝启用并展开、取消＝自动收起，
//   折叠箭头仍可独立收起内容（勾选语义与折叠显隐分离）。
//   onToggled：展开/收起回调（供对话框重算表单最小宽——内容显隐改变最小宽）。
class CollapsibleSection : public QWidget {
public:
    CollapsibleSection(const QString& title, QWidget* content, QWidget* parent, QWidget* check = nullptr,
                       std::function<void(bool)> onToggled = {})
        : QWidget(parent), content_(content), onToggled_(std::move(onToggled)) {
        auto* lay = new QVBoxLayout(this);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(2);
        auto* head = new QHBoxLayout();
        head->setContentsMargins(0, 0, 0, 0);
        if (check) head->addWidget(check);
        arrow_ = new QToolButton(this);
        arrow_->setArrowType(Qt::DownArrow);
        arrow_->setAutoRaise(true);
        arrow_->setCheckable(true);
        arrow_->setChecked(true);
        auto* label = new QLabel(title, this);
        QFont f = label->font();
        f.setBold(true);
        label->setFont(f);
        head->addWidget(arrow_);
        head->addWidget(label, 1);
        lay->addLayout(head);
        lay->addWidget(content_);
        lay->addStretch(1);   // 内容钉顶：折叠节被所在列等高拉伸时，多余空间沉底（不居中浮空）。
        connect(arrow_, &QToolButton::toggled, this, [this](const bool expanded) {
            arrow_->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
            content_->setVisible(expanded);
            if (onToggled_) onToggled_(expanded);
        });
    }

    // 程序化展开/收起（开关联动：勾选=展开、取消=收起）。
    void setExpanded(const bool expanded) const { arrow_->setChecked(expanded); }

private:
    QToolButton* arrow_{nullptr};
    QWidget* content_{nullptr};
    std::function<void(bool)> onToggled_;
};
} // namespace

NewCanvasDialog::NewCanvasDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(IDC_GTR(label, kDlgTitleNewCanvas));
    setModal(true);

    auto* root = rootLayout_ = new QVBoxLayout(this);
    // 主体（表单 + 预览）装进纵向可滑外壳：小屏 / 横屏超高时内部滚动，配合底部固定按钮行 + 屏幕尺寸护栏，
    // 根治「显示不全 / 无法关闭」；勾选附加图片增删的行也落在滚动内容里，外层窗口不被撑大。
    // 按钮行留滚动区外恒可关闭；预览并入滚动内容（用户滚到底即可看到）。
    auto* body = new QWidget(this);
    body_ = body;
    auto* grid = new QGridLayout(body);
    grid->setContentsMargins(0, 0, 0, 0);
    // wrapScrollable 的「内容最小宽 + 纵滚条宽」预留是在调用一刻对 body->minimumSizeHint() 取快照，
    //   故须等 body 网格装配完毕再接入 root（见预览行之后）；此处空壳时快照≈0、预留会失效。

    int row = 0;
    // ---- 窄屏判据（按对话框目标屏；未显示时回退主屏，判空守卫）：节内左右并排与表单/预览分栏共用。----
    const QScreen* sc = screen();
    if (!sc) sc = QGuiApplication::primaryScreen();
    const bool narrow = !sc || sc->availableGeometry().width() < nums::mobile::kStackColumnsMaxWidthPx;
    narrow_ = narrow;

    // ---- 节点外观节（横贯，可折叠）：左列＝尺寸（节点区域＝图层框 W×H）/整体不透明度/图层落位，
    //      右列＝节点外观简表（新建画布语境＝底图口径仅色彩步，添加图层语境放开几何步）。
    //      画布级整体透明度不在本对话框采集（属建好后的画布态，交图层面板处理）。----
    auto* nodeBox = new QFrame(body);
    nodeBox->setFrameShape(QFrame::StyledPanel);
    auto* nodeLay = nodeLay_ = new QHBoxLayout(nodeBox);
    if (narrow) nodeLay->setDirection(QBoxLayout::TopToBottom);
    auto* nodeParams = new QWidget(nodeBox);   // 左列：尺寸/不透明度/图层落位。
    auto* nodeGrid = new QGridLayout(nodeParams);
    nodeGrid->setContentsMargins(0, 0, 0, 0);
    widthSpin_ = makeSpin(nodeParams, 1, generate::kMaxDimension, QStringLiteral(" px"));
    widthSpin_->setValue(CanvasPrefs::instance().newCanvasWidth());   // 默认取应用级偏好（记住上次新建）；「添加图层」语境会随后预填真实画布。
    heightSpin_ = makeSpin(nodeParams, 1, generate::kMaxDimension, QStringLiteral(" px"));
    heightSpin_->setValue(CanvasPrefs::instance().newCanvasHeight());
    opacitySpin_ = makeSpin(nodeParams, 0, 255);
    opacitySpin_->setValue(255);   // 默认不透明。
    opacitySpin_->setToolTip(IDC_GTR(tooltip, kLayerOpacitySpin));
    // 图层落位（图层 相对 底图，transform 节点落位）：仅「添加图片图层」语境显示；偏移另接受百分比串，
    //   基准＝本对话框的尺寸框（随宽/高实时变化）。
    layerPlaceRow_ = new PlacementRow(IDC_GTR(label, kLayerPlaceBox), nodeParams);
    layerPlaceRow_->setToolTip(IDC_GTR(tooltip, kLayerPlaceRowTip));
    layerPlaceRow_->setOffsetBasisProvider([this](const bool horizontal) {
        return horizontal ? widthSpin_->value() : heightSpin_->value();
    });
    if (narrow) {   // 窄屏：网格 2 列化（宽/高、不透明度各占一行），压最小宽防竖屏撑爆。
        nodeGrid->addWidget(new QLabel(IDC_GTR(label, kWidthColonLabel), nodeParams), 0, 0);
        nodeGrid->addWidget(widthSpin_, 0, 1);
        nodeGrid->addWidget(new QLabel(IDC_GTR(label, kHeightColonLabel), nodeParams), 1, 0);
        nodeGrid->addWidget(heightSpin_, 1, 1);
        nodeGrid->addWidget(new QLabel(IDC_GTR(label, kOpacityFieldLabel), nodeParams), 2, 0);
        nodeGrid->addWidget(opacitySpin_, 2, 1);
        nodeGrid->addWidget(layerPlaceRow_, 3, 0, 1, 2);
    } else {
        nodeGrid->addWidget(new QLabel(IDC_GTR(label, kWidthColonLabel), nodeParams), 0, 0);
        nodeGrid->addWidget(widthSpin_, 0, 1);
        nodeGrid->addWidget(new QLabel(IDC_GTR(label, kHeightColonLabel), nodeParams), 0, 2);
        nodeGrid->addWidget(heightSpin_, 0, 3);
        nodeGrid->addWidget(new QLabel(IDC_GTR(label, kOpacityFieldLabel), nodeParams), 1, 0);
        nodeGrid->addWidget(opacitySpin_, 1, 1);
        nodeGrid->addWidget(layerPlaceRow_, 2, 0, 1, 4);
    }
    nodeLay->addWidget(pinnedTop(nodeParams), 1);
    nodeAppearance_ = new ElementAppearanceGroup(nodeBox, false, false);   // 右列：节点外观简表（默认仅色彩步；不显示不透明度行——由左列「不透明度」独家采集；setAddLayerMode 放开几何步）。
    nodeLay->addWidget(pinnedTop(nodeAppearance_), 1);
    grid->addWidget(new CollapsibleSection(IDC_GTR(label, kNodeAppearanceBox), nodeBox, body, nullptr,
                                           [this](const bool) { resizeToContent(); }), row, 0);
    ++row;

    // ---- 图案元素节（横贯，可折叠）：图案采参表单 | 色彩外观组 左右并排（窄屏上下叠）。----
    auto* backdropBox = new QFrame(body);
    backdropBox->setFrameShape(QFrame::StyledPanel);
    auto* backdropLay = backdropLay_ = new QHBoxLayout(backdropBox);
    if (narrow) backdropLay->setDirection(QBoxLayout::TopToBottom);
    patternForm_ = new PatternFormWidget(backdropBox);   // 底色/前景/类型/方向/周期/线宽/点半径/噪声。
    backdropLay->addWidget(pinnedTop(patternForm_), 1);
    backdropAppearance_ = new ElementAppearanceGroup(backdropBox, false);   // 图案元素外观：仅色彩步（图案＝底图口径，几何步改区域尺寸）。
    backdropLay->addWidget(pinnedTop(backdropAppearance_), 1);
    grid->addWidget(new CollapsibleSection(IDC_GTR(label, kBackdropElementBox), backdropBox, body, nullptr,
                                           [this](const bool) { resizeToContent(); }), row, 0);
    ++row;
    // ---- 图片元素节（横贯，可折叠）：「附加图片」开关放节标题行（勾选＝启用并展开、取消＝自动收起）；
    //      内容＝[源图/适应/图片落位 | 两区外观] 左右并排（窄屏上下叠）。----
    auto* imageBox = new QFrame(body);
    imageBox->setFrameShape(QFrame::StyledPanel);
    auto* imageLay = imageLay_ = new QHBoxLayout(imageBox);
    if (narrow) imageLay->setDirection(QBoxLayout::TopToBottom);
    attachImageCheck_ = new QCheckBox(IDC_GTR(label, kAttachImageCheck));   // 标题行开关（声明语义；折叠箭头仅显隐）。
    attachImageCheck_->setToolTip(IDC_GTR(tooltip, kAttachImageTip));
    imageSub_ = new QWidget(imageBox);   // 左列：源图/适应/图片落位。
    auto* imageSubLay = new QVBoxLayout(imageSub_);
    imageSubLay->setContentsMargins(0, 0, 0, 0);
    imagePickButton_ = new QPushButton(IDC_GTR(label, kPickSourceImageButton), imageSub_);
    imagePickButton_->setToolTip(IDC_GTR(tooltip, kPickSourceImageBtn));
    imagePathLabel_ = new QLabel(IDC_GTR(label, kNoSourceImageChosen), imageSub_);
    imageFitLabel_ = new QLabel(IDC_GTR(label, kImageFitFieldLabel), imageSub_);
    imageFitCombo_ = new QComboBox(imageSub_);
    for (const auto&[fit, name] : std::array<std::pair<ImageFit, i18n::TextRef>, 3>{{
             {ImageFit::Crop, IDC_GT(label, kFitCropItem)},
             {ImageFit::Stretch, IDC_GT(label, kFitStretchItem)},
             {ImageFit::Native, IDC_GT(label, kFitNativeItem)}
             }}) {
        imageFitCombo_->addItem(i18n::qs(name));
        imageFitCombo_->setItemData(imageFitCombo_->count() - 1, static_cast<int>(fit));
    }
    imageFitCombo_->setCurrentIndex(0);
    imageSubLay->addWidget(imagePickButton_);
    imageSubLay->addWidget(imagePathLabel_);
    imageSubLay->addWidget(imageFitLabel_);
    imageSubLay->addWidget(imageFitCombo_);
    // 图片落位（图片 相对 图层）：不接百分比（基准是图层框、合成期才定）。
    imagePlaceRow_ = new PlacementRow(IDC_GTR(label, kImagePlaceBox), imageSub_);
    imagePlaceRow_->setToolTip(IDC_GTR(tooltip, kImagePlaceRowTip));
    imageSubLay->addWidget(imagePlaceRow_);
    imageLay->addWidget(pinnedTop(imageSub_), 1);
    imageAppearance_ = new ElementAppearanceGroup(imageBox);   // 右列：图片元素外观（可含几何步，落位前施于源）。
    imageLay->addWidget(pinnedTop(imageAppearance_), 1);
    auto* imageSection = new CollapsibleSection(IDC_GTR(label, kImageElementBox), imageBox, body, attachImageCheck_,
                                                [this](const bool) { resizeToContent(); });
    connect(attachImageCheck_, &QCheckBox::toggled, imageSection, [imageSection](const bool on) {
        imageSection->setExpanded(on);   // 勾选=展开、取消=自动收起（箭头仍可独立折叠，勾选语义不丢）。
    });
    imageSection->setExpanded(false);   // 初始收起（未勾附加图片）：勾选才展开。
    imageSection->setExpanded(true);    // 暂开取「展开态」两个宽度口径：舒适宽（初始列宽）与内容最小宽（左列拖窄下限）。
    const int expandedFormW = body->sizeHint().width();
    const int expandedFormMinW = body->minimumSizeHint().width();
    imageSection->setExpanded(false);
    grid->addWidget(imageSection, row, 0);
    ++row;

    // ---- 预览独立列：滚动区外固定（宽屏＝表单右侧恒可见；窄屏＝表单下方恒可见），不随表单滚动。----
    previewLabel_ = new QLabel(IDC_GTR(label, kPreviewPlaceholder), this);
    previewLabel_->setAlignment(Qt::AlignCenter);
    previewLabel_->setMinimumSize(nums::dialog::kNewCanvasPreviewMinWidthPx,
                                  nums::dialog::kNewCanvasPreviewMinHeightPx);
    previewLabel_->setFrameShape(QFrame::StyledPanel);
    previewLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* previewCol = previewCol_ = new QWidget(this);
    auto* pvLay = new QVBoxLayout(previewCol);
    pvLay->setContentsMargins(0, 0, 0, 0);
    pvLay->addWidget(previewLabel_);

    // 主体网格装配完毕 → 此刻接入外壳：宽屏＝[左可滚表单 | 右预览列] 的**可拖分隔条**（预览恒可见、
    //   宽度可调）；窄屏＝[上可滚表单 / 下固定预览]。wrapScrollable 据此刻 body 的真实最小宽预留
    //   「内容最小宽 + 纵滚条宽」；后续折叠/展开变化经 resizeToContent 动态重算。
    formScroll_ = wrapScrollable(body_);
    if (narrow) {
        // 横屏高度紧张：滚动区最小高归零、预览最小高收缩，整窗下限让位于护栏钳制（不超出屏幕）。
        formScroll_->setMinimumHeight(0);
        previewLabel_->setMinimumHeight(120);
        root->addWidget(formScroll_, 1);
        root->addWidget(previewCol);
    } else {
        // 左列最小宽护栏＝展开态内容最小宽 + 纵滚条宽（下限不裁输入；拖窄分隔条到此即止）。
        formScroll_->setMinimumWidth(qMax(expandedFormMinW + verticalScrollBarExtent(formScroll_), 480));
        auto* splitter = splitter_ = new QSplitter(Qt::Horizontal, this);
        splitter->addWidget(formScroll_);
        previewCol->setMinimumWidth(nums::dialog::kNewCanvasPreviewMinWidthPx);
        splitter->addWidget(previewCol);
        splitter->setChildrenCollapsible(false);
        splitter->setStretchFactor(0, 1);
        splitter->setStretchFactor(1, 0);
        const int formW = qMax(expandedFormW, 640);
        splitter->setSizes({formW, nums::dialog::kNewCanvasPreviewMinWidthPx + 60});   // 预览列初值 340，可拖分隔条调整。
        root->addWidget(splitter, 1);
    }

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
    // 元素外观组（图案/图片两槽位）：步骤或透明度变化即时刷新预览（与合成单一真相源同链）。
    connect(backdropAppearance_, &ElementAppearanceGroup::changed, this, &NewCanvasDialog::onParamsChanged);
    connect(imageAppearance_, &ElementAppearanceGroup::changed, this, &NewCanvasDialog::onParamsChanged);

    patternForm_->updateAvailability();
    updateControlAvailability();
    refreshPreview();

    // 屏幕尺寸护栏：按可用几何钳最大宽 / 高（比例见 nums::mobile::kDialogMax*），首次显示收到
    // 「表单自然尺寸 + 预览列 + 按钮行」与可用几何的较小者；小屏 / 横屏超出即内部滚动、按钮常驻，
    // 勾选附加图片增删的行落在滚动内容里、外层窗口不被撑大；桌面大屏下钳值远超内容、等效无约束。
    const QSize formHint = body->sizeHint();        // 高度口径（初始宽度用「展开态舒适宽」预算 + 640 下限）。
    const QMargins rootMargin = root->contentsMargins();
    const int preferredW = qMax(expandedFormW, 640)
                           + (narrow ? 0 : nums::dialog::kNewCanvasPreviewMinWidthPx + 60)
                           + rootMargin.left() + rootMargin.right();
    const int preferredH = formHint.height() + (narrow ? 120 : 0)
                           + 2 * nums::dialog::kTouchFieldMinHeightPx + nums::panels::kPanelSpacingPx;
    limitDialogToScreen(this, QSize(preferredW, preferredH));
    resize(preferredW, preferredH);   // 构造期即定宽：不依赖护栏首显回调（双保险；护栏仍管超屏钳制）。
    // 屏幕几何变化（旋转）→ 宽/窄形态动态切换。延迟一拍执行：旋转时序里平台在事件批处理之后
    //   才重摆窗口，立即改会被覆盖。
    if (sc) connect(sc, &QScreen::availableGeometryChanged, this, [this] {
        QTimer::singleShot(0, this, [this] { applyLayoutMode(); });
    });
}

// 依附加图片开关/语境/适应方式显隐落位组并锁定尺寸（图案相关行显隐与文案在 PatternFormWidget 内）。
void NewCanvasDialog::updateControlAvailability() const {
    // 图层落位（图层 相对 底图）：仅「添加图片图层」语境显示（新建画布产出扁平整幅、无叠层概念）；不受附加图片开关影响。
    layerPlaceRow_->setVisible(addLayerMode_);
    // 附加图片区块：内容显隐由折叠节承管（开关放节标题行：勾选＝启用并展开、取消＝自动收起）；
    //   「原生」锁定顶部宽/高（图层尺寸＝源图尺寸）。
    const bool attach = attachImageCheck_->isChecked();
    const auto attachFit = static_cast<ImageFit>(imageFitCombo_->currentData().toInt());
    attachImageCheck_->setEnabled(true);
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

// 折叠/展开改变内容最小宽 → 重算表单滚动区最小宽，并在宽屏下让窗口跟上（只增长不收缩：
//   用户手动拖过分隔条/窗口时不打扰；护栏过滤器在超屏时钳制）。
void NewCanvasDialog::resizeToContent() {
    if (!formScroll_ || !body_) return;
    const int contentMin = body_->minimumSizeHint().width() + verticalScrollBarExtent(formScroll_);
    formScroll_->setMinimumWidth(narrow_ ? contentMin : qMax(480, contentMin));
    if (narrow_) return;   // 窄屏纵向：窗口宽由护栏管，无需调整。
    const QMargins m = layout()->contentsMargins();
    const int wantW = qMax(body_->sizeHint().width(), 480)
                      + nums::dialog::kNewCanvasPreviewMinWidthPx + 60
                      + m.left() + m.right();
    if (wantW > width()) resize(wantW, height());
}

// 屏幕几何变化（旋转）→ 宽/窄形态动态切换：三段节内方向、外观组操作行形态、表单/预览外壳
//   随之重排，并按新形态的舒适尺寸重收整窗（护栏继续钳超屏）。
void NewCanvasDialog::applyLayoutMode() {
    const QScreen* sc = screen();
    if (!sc) sc = QGuiApplication::primaryScreen();
    const bool narrow = !sc || sc->availableGeometry().width() < nums::mobile::kStackColumnsMaxWidthPx;
    if (narrow == narrow_) return;
    narrow_ = narrow;

    // 节内方向 + 外观组操作行形态 + 预览最小高。
    const QBoxLayout::Direction dir = narrow ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight;
    nodeLay_->setDirection(dir);
    backdropLay_->setDirection(dir);
    imageLay_->setDirection(dir);
    nodeAppearance_->setCompactLayout(narrow);
    backdropAppearance_->setCompactLayout(narrow);
    imageAppearance_->setCompactLayout(narrow);
    previewLabel_->setMinimumHeight(narrow ? 120 : nums::dialog::kNewCanvasPreviewMinHeightPx);

    // 表单/预览外壳：窄屏纵向堆叠 ↔ 宽屏可拖分隔条（布局重挂会自动改父）。
    rootLayout_->removeWidget(formScroll_);
    rootLayout_->removeWidget(previewCol_);
    if (splitter_) rootLayout_->removeWidget(splitter_);
    if (!narrow) {
        if (!splitter_) {
            splitter_ = new QSplitter(Qt::Horizontal, this);
            splitter_->setChildrenCollapsible(false);
            splitter_->setStretchFactor(0, 1);
            splitter_->setStretchFactor(1, 0);
        }
        // 宽形态最小宽随屏自适应：小屏（手机横屏）下 480/280 固定下限会顶过护栏钳值而溢出。
        const QRect avail = sc ? sc->availableGeometry() : QRect();
        const int availW = avail.isNull() ? INT_MAX : avail.width();
        const int previewMinW = qMin(nums::dialog::kNewCanvasPreviewMinWidthPx, availW / 3);
        previewCol_->setMinimumWidth(previewMinW);
        const int contentMin = body_->minimumSizeHint().width() + verticalScrollBarExtent(formScroll_);
        formScroll_->setMinimumWidth(qMax(contentMin, qMin(480, availW - previewMinW - 60)));
        formScroll_->setMinimumHeight(0);
        splitter_->addWidget(formScroll_);
        splitter_->addWidget(previewCol_);
        rootLayout_->insertWidget(0, splitter_, 1);
    } else {
        if (splitter_) {
            // 子件先摘出再删分隔条（QSplitter 无 removeWidget；子件为其 QObject 子级、不摘出会随删）。
            formScroll_->setParent(nullptr);
            previewCol_->setParent(nullptr);
            delete splitter_;
            splitter_ = nullptr;
        }
        formScroll_->setMinimumHeight(0);
        rootLayout_->insertWidget(0, formScroll_, 1);   // 布局接管（重挂父）。
        rootLayout_->insertWidget(1, previewCol_);
    }
    resizeToContent();
    body_->updateGeometry();
    const QSize formHint = body_->sizeHint();
    const QMargins m = layout()->contentsMargins();
    int wantW = qMax(formHint.width(), narrow ? 0 : 640)
                + (narrow ? 0 : nums::dialog::kNewCanvasPreviewMinWidthPx + 60)
                + m.left() + m.right();
    int wantH = formHint.height() + (narrow ? 120 : 0)
                + 2 * nums::dialog::kTouchFieldMinHeightPx + nums::panels::kPanelSpacingPx;
    // 显式钳到护栏口径（不依赖 Resize 事件次序）；随后按父窗口居中（无父则按屏居中）——
    //   旋转时序里屏幕几何先变、父窗口后变，护栏的父窗口处理器会随后再居中回正一次。
    const QRect avail = sc ? sc->availableGeometry() : QRect();
    if (!avail.isNull()) {
        wantW = qMin(wantW, static_cast<int>(avail.width() * nums::mobile::kDialogMaxWidthRatio));
        wantH = qMin(wantH, static_cast<int>(avail.height() * nums::mobile::kDialogMaxHeightRatio));
    }
    resize(wantW, wantH);
    // 以当前屏可用几何为基准（屏幕几何是本次触发的权威来源，父窗口几何可能尚未更新）：
    //   框内居中 + 四边越界强制拉回——保证恒不超出屏幕。
    if (!avail.isNull()) {
        QRect dlg = geometry();
        dlg.moveCenter(avail.center());
        if (dlg.right() > avail.right()) dlg.moveRight(avail.right());
        if (dlg.bottom() > avail.bottom()) dlg.moveBottom(avail.bottom());
        if (dlg.left() < avail.left()) dlg.moveLeft(avail.left());
        if (dlg.top() < avail.top()) dlg.moveTop(avail.top());
        move(dlg.topLeft());
    } else if (auto* p = parentWidget()) {
        const QPoint tl = p->isWindow() ? p->frameGeometry().topLeft() : p->mapToGlobal(QPoint(0, 0));
        const QSize ps = p->isWindow() ? p->frameGeometry().size() : p->size();
        move(tl.x() + (ps.width() - width()) / 2, tl.y() + (ps.height() - height()) / 2);
    }
    // 【临时诊断】标题栏实时几何（对话框 尺寸×屏可用几何 ×父窗口尺寸）：定位后移除。
    QString refInfo;
    if (auto* p = parentWidget()) refInfo = QStringLiteral("/parent %1x%2").arg(p->width()).arg(p->height());
    setWindowTitle(IDC_GTR(label, kDlgTitleNewCanvas) + QStringLiteral(" [%1x%2 / avail %3x%4%5]")
                       .arg(width()).arg(height())
                       .arg(avail.width()).arg(avail.height()).arg(refInfo));
}

// 切「添加图层」语境：顶部宽/高 spin（即图层尺寸）预填真实画布 W×H 并保持可编辑（改小即裁剪、改大即留白）。
// 附加图片开关默认关（未勾即产纯图案层，勾选才产图片层）；图层落位组此时才显示（新建画布语境隐藏）。
// 合成语义与新建画布完全一致（底图图案 + 图层落位/图片落位两段合成 + 越界裁剪），不叠底图、不撑大。
void NewCanvasDialog::setAddLayerMode(const int canvasW, const int canvasH) {
    addLayerMode_ = true;
    setWindowTitle(IDC_GTR(label, kDlgTitleAddLayer));
    nodeAppearance_->setGeometricAllowed(true);   // 图层节点外观可含几何步（仅色彩步是新建画布＝底图口径）。
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

// 组装一个可序列化的附加图片图层：原始源（factor≠1 先等比预缩作预览代理）+ 两级落位（内层源图→
//   图层框 imgPlace/fit，外层图层框→画布 layerPlace）+ 图案元素 backdrop（即顶部图案参数）+ 两元素外观。
//   图层框尺寸 = boxW×boxH（顶部宽高）；外层 transform 的宽高留 0（用框原生尺寸，不缩放）。
canvas::ImageLayer NewCanvasDialog::buildAttachedLayer(const double factor, const canvas::ImageTransform& layerPlace,
                                                       const generate::PatternParams& backdrop, const int boxW, const int boxH,
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
    layer.backdrop = backdrop;   // 图案元素（图层框底色/图案，Core 先铺底再叠源）。
    // 元素外观（SPEC §2.2a 槽位法则）：图案元素外观 + 图片元素外观，由对话框两外观组采集。
    layer.backdropPreprocess = backdropAppearance_->pipeline();
    layer.backdropOpacity = static_cast<std::uint8_t>(std::clamp(backdropAppearance_->opacity(), 0, 255));
    layer.imagePreprocess = imageAppearance_->pipeline();
    layer.imageOpacity = static_cast<std::uint8_t>(std::clamp(imageAppearance_->opacity(), 0, 255));
    layer.preprocess = nodeAppearance_->pipeline();   // 节点外观预处理（对话框第三简表）。
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
        // 图案元素外观：预处理简表 + 元素不透明度（装配序：生成→其预处理→其透明度，与 Core materializeLayer 同口径）。
        if (!backdropAppearance_->pipeline().empty())
            flat = backdropAppearance_->pipeline().applyExec(flat);
        canvas::applyAlpha(flat, static_cast<std::uint8_t>(std::clamp(backdropAppearance_->opacity(), 0, 255)));
        // 节点外观预处理（第三简表）：装配序在区域缩放之后（纯图案按目标尺寸生成、无缩放，直接施）。
        if (!nodeAppearance_->pipeline().empty())
            flat = nodeAppearance_->pipeline().applyExec(flat);
        // 纯图案 makePattern 不施节点 alpha 乘子（合成边界单点施加、防双淡的不变式）：预览在 layerPlace.opacity 上此处施加；
        //   定稿恒以 opacity=255 调本函数（flat 保持不透明，透度另由 baseOpacity/图层 transform 于运行时施加），故不双淡。
        if (const int op = layerPlace.opacity; op < 255) canvas::applyAlpha(flat, static_cast<std::uint8_t>(op));
        return flat;
    }
    std::vector<canvas::ImageLayer> layers;   // 无 base（图案元素 backdrop 已含于层内）。
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
    resultParams_ = p;   // 留存汇编参数（供「添加图片图层」入口未勾图时产纯图案层，可序列化供 config 导出）。
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
            // 附图片底：文件源（携位图/落位）+ box＝画布尺寸 + backdrop＝图案元素；transform 恒等（底图不可拖）。
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
            base.backdrop = p;   // 图案元素（图层框底色/图案，Core 先铺底再叠源）。
            // 元素外观（与 buildAttachedLayer 同口径采集；底图为落位声明底，元素外观随 §7 generate 段往返）。
            base.backdropPreprocess = backdropAppearance_->pipeline();
            base.backdropOpacity = static_cast<std::uint8_t>(std::clamp(backdropAppearance_->opacity(), 0, 255));
            base.imagePreprocess = imageAppearance_->pipeline();
            base.imageOpacity = static_cast<std::uint8_t>(std::clamp(imageAppearance_->opacity(), 0, 255));
        } else {
            // 纯图案底：图案元素 + 无图片元素、transform 恒等（序列化为纯 backdrop 形态的 generate 段）。
            base.backdrop = p;
            base.backdropPreprocess = backdropAppearance_->pipeline();
            base.backdropOpacity = static_cast<std::uint8_t>(std::clamp(backdropAppearance_->opacity(), 0, 255));
        }
        base.preprocess = nodeAppearance_->pipeline();   // 节点外观＝底图整体外观（仅色彩步；SPEC §7 generate 段 preprocess 同口径）。
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

preprocess::PreprocessPipeline NewCanvasDialog::resultBackdropPreprocess() const {
    return backdropAppearance_ ? backdropAppearance_->pipeline() : preprocess::PreprocessPipeline{};
}

int NewCanvasDialog::resultBackdropOpacity() const {
    return backdropAppearance_ ? backdropAppearance_->opacity() : 255;
}

preprocess::PreprocessPipeline NewCanvasDialog::resultNodePreprocess() const {
    return nodeAppearance_ ? nodeAppearance_->pipeline() : preprocess::PreprocessPipeline{};
}

} // namespace idc::gui
