// ============================================================================
// 文件：app/dialogs/new_canvas_dialog.cpp
// 作用：实现 NewCanvasDialog（见同名头文件说明）的装配壳——SPEC §2.2a 合成模型采参视图：
//       三节（节点外观 / 图案元素 / 图片元素）由按节拆 TU 的构建器装配（_sections），形态编排
//       （_layout）与合成/定稿（_compose）亦各成 TU；本文件承载构造装配、控件显隐联动
//       （updateControlAvailability）、源图选择（onPickImage/syncCanvasSizeToSource）、
//       参数变更入口（onParamsChanged）、语境切换（setAddLayerMode）与结果访问器。
//       图案采参表单（底色/前景/类型/方向/参数/噪声）位于 app/forms/PatternFormWidget。
// 分块依据：装配（构造）/ 联动（updateControlAvailability）/ 选图 / 语境切换各成一块，共享装配件
//       （scaled/pinnedTop/CollapsibleSection）集中 new_canvas_dialog_internal.h（三 TU 共用，
//       本 TU 不再定义）。
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

#include <iterator>   // std::size（旋转重钳时刻数组遍历）

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

    // 三个折叠节（节点外观 / 图案元素 / 图片元素）构建器按节拆 TU（new_canvas_dialog_sections.cpp），
    // 各节自登记进 body 网格（row 按节递增）；图片节的「展开态舒适宽」经出参回传（分隔条初宽与护栏首显共用）。
    int expandedFormW = 0;
    buildNodeSection(body, grid, row, narrow);
    buildBackdropSection(body, grid, row, narrow);
    buildImageSection(body, grid, row, narrow, expandedFormW);

    grid->setRowStretch(row, 1);   // 尾行拉伸兜底：内容比视口矮时（节折叠后）多余高度沉底，三节恒钉顶（不随视口均摊浮空）。

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
        previewLabel_->setMinimumHeight(nums::dialog::kNewCanvasPreviewMinHeightNarrowPx);
        root->addWidget(formScroll_, 1);
        root->addWidget(previewCol);
    } else {
        // 宽形态下限随屏自适应（小屏横屏下固定表单/预览下限会顶过护栏钳值而溢出）。
        const QRect availAtCtor = sc ? sc->availableGeometry() : QRect();
        applyWideMinimums(availAtCtor);
        auto* splitter = splitter_ = new QSplitter(Qt::Horizontal, this);
        splitter->setHandleWidth(nums::dialog::kNewCanvasSplitterHandleWidthPx);   // 触控把手：默认 ~5px 细条手指拖不动。
        splitter->addWidget(formScroll_);
        previewCol->setMinimumWidth(nums::dialog::kNewCanvasPreviewMinWidthPx);
        splitter->addWidget(previewCol);
        splitter->setChildrenCollapsible(false);
        splitter->setStretchFactor(0, 1);
        splitter->setStretchFactor(1, 0);
        const int formW = qMax(expandedFormW, nums::dialog::kNewCanvasWideFormFloorPx);
        splitter->setSizes({formW, nums::dialog::kNewCanvasPreviewMinWidthPx + nums::dialog::kNewCanvasWideShellExtraPx});   // 预览列初值 340，可拖分隔条调整。
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
    // 节点外观（第三简表）同样即时刷新预览——步骤/不透明度变化重跑合成代理（此前漏接，预览不随节点预处理变化）。
    connect(nodeAppearance_, &ElementAppearanceGroup::changed, this, &NewCanvasDialog::onParamsChanged);

    patternForm_->updateAvailability();
    updateControlAvailability();
    refreshPreview();

    // 屏幕尺寸护栏：按可用几何钳最大宽 / 高（比例见 nums::mobile::kDialogMax*），首次显示收到
    // 「表单自然尺寸 + 预览列 + 按钮行」与可用几何的较小者；小屏 / 横屏超出即内部滚动、按钮常驻，
    // 勾选附加图片增删的行落在滚动内容里、外层窗口不被撑大；桌面大屏下钳值远超内容、等效无约束。
    const QSize formHint = body->sizeHint();        // 高度口径（初始宽度用「展开态舒适宽」预算 + kNewCanvasWideFormFloorPx 下限）。
    const QMargins rootMargin = root->contentsMargins();
    const int preferredW = qMax(expandedFormW, nums::dialog::kNewCanvasWideFormFloorPx)
                           + (narrow ? 0 : nums::dialog::kNewCanvasPreviewMinWidthPx + nums::dialog::kNewCanvasWideShellExtraPx)
                           + rootMargin.left() + rootMargin.right();
    const int preferredH = formHint.height() + (narrow ? nums::dialog::kNewCanvasPreviewMinHeightNarrowPx : 0)
                           + 2 * nums::dialog::kTouchFieldMinHeightPx + nums::panels::kPanelSpacingPx;
    limitDialogToScreen(this, QSize(preferredW, preferredH));
    resize(preferredW, preferredH);   // 构造期即定宽：不依赖护栏首显回调（双保险；护栏仍管超屏钳制）。
    // 形态链首跑（与旋转共用）：横屏首开时全宽操作行会把整窗最小宽顶过屏宽钳值，护栏按钳值收尺寸时
    //   收不住（下限卡死）；先按实际最小宽量测降档，护栏首显再收即可入屏。
    applyModeChain(sc ? sc->availableGeometry() : QRect());
    // 屏幕几何变化（旋转）→ 宽/窄形态动态切换 + 几何收钳。旋转动画收尾时平台会按窗口 sizeHint 再摆
    //   一次（宽形态 sizeHint 超屏宽时推出屏边/顶回最小宽），故多拍重钳：动画期间（0/250/600ms）只收
    //   尺寸不摆位置（平台在摆位，抢位即抖动），末拍（800ms）也只做越界拉回；每拍仅在几何不对时才动，
    //   正常情形全程无可见动作。
    if (sc) connect(sc, &QScreen::availableGeometryChanged, this, [this] {
        // 前三拍只收尺寸（平台在摆位，抢位即抖动）；末拍（数组末元素）带位置收钳（仅越界拉回）。
        constexpr int last = static_cast<int>(std::size(nums::mobile::kRotationSettleMs)) - 1;
        for (int i = 0; i <= last; ++i)
            QTimer::singleShot(nums::mobile::kRotationSettleMs[i], this, [this, i, last] { applyLayoutMode(i == last); });
    });
    // 旋转主通道：父窗口 Resize/Move 事件（Android 旋转会重建 QScreen 对象，上面的屏幕信号连接随旧
    //   屏对象销毁失效；父窗口事件恒定存活——与护栏挂父窗口同口径，屏幕信号仅作备份）。
    if (auto* p = parentWidget()) p->installEventFilter(this);
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

void NewCanvasDialog::onParamsChanged() {
    patternForm_->updateAvailability();
    updateControlAvailability();
    refreshPreview();
    refreshModeChain();   // 落位组显隐随适应方式/开关变化改变内容最小宽：重算并重跑形态链（量测以当前内容为准）。
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