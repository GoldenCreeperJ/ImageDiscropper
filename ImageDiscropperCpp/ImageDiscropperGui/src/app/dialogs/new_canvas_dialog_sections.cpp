// ============================================================================
// 文件：app/dialogs/new_canvas_dialog_sections.cpp
// 作用：实现 NewCanvasDialog 的三个折叠节构建器——节点外观节（尺寸/整体不透明度/图层落位 +
//       节点外观简表）、图案元素节（采参表单 | 色彩外观组）、图片元素节（「附加图片」开关放节
//       标题行 + [源图/适应/图片落位 | 两区外观]）。各节自登记进 body 网格（row 按节递增），
//       图片节的「展开态舒适宽」经出参回传（分隔条初宽与护栏首显共用）。
// 分块依据：见 new_canvas_dialog.cpp 头注（四 TU 拆分）；本 TU 只建控件与节内接线，共享装配件
//       （pinnedTop/CollapsibleSection）来自 new_canvas_dialog_internal.h。
// ============================================================================
#include "app/new_canvas_dialog.h"

#include <array>
#include <utility>

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "app/canvas_prefs.h"        // 新建画布默认尺寸（取初值 + 记住上次）
#include "app/forms/pattern_form.h"
#include "app/forms/placement_row.h"
#include "generate/pattern.h"        // generate::kMaxDimension（尺寸上限护栏真相源在 Core）
#include "panels/element_appearance_group.h"
#include "util/widget_factory.h"     // makeSpin
#include "i18n/gui_texts.h"
#include "i18n/gui_i18n.h"

#include "new_canvas_dialog_internal.h" // pinnedTop / CollapsibleSection（节折叠与钉顶装配件）

namespace idc::gui {

    // ---- 节点外观节（横贯，可折叠）：左列＝尺寸（节点区域＝图层框 W×H）/整体不透明度/图层落位，
    //      右列＝节点外观简表（新建画布语境＝底图口径仅色彩步，添加图层语境放开几何步）。
    //      画布级整体透明度不在本对话框采集（属建好后的画布态，交图层面板处理）。----
void NewCanvasDialog::buildNodeSection(QWidget* body, QGridLayout* grid, int& row, const bool narrow) {
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
                                           [this](const bool) { refreshModeChain(); }), row, 0);
    ++row;
}

    // ---- 图案元素节（横贯，可折叠）：图案采参表单 | 色彩外观组 左右并排（窄屏上下叠）。----
void NewCanvasDialog::buildBackdropSection(QWidget* body, QGridLayout* grid, int& row, const bool narrow) {
    auto* backdropBox = new QFrame(body);
    backdropBox->setFrameShape(QFrame::StyledPanel);
    auto* backdropLay = backdropLay_ = new QHBoxLayout(backdropBox);
    if (narrow) backdropLay->setDirection(QBoxLayout::TopToBottom);
    patternForm_ = new PatternFormWidget(backdropBox);   // 底色/前景/类型/方向/周期/线宽/点半径/噪声。
    backdropLay->addWidget(pinnedTop(patternForm_), 1);
    backdropAppearance_ = new ElementAppearanceGroup(backdropBox, false);   // 图案元素外观：仅色彩步（图案＝底图口径，几何步改区域尺寸）。
    backdropLay->addWidget(pinnedTop(backdropAppearance_), 1);
    grid->addWidget(new CollapsibleSection(IDC_GTR(label, kBackdropElementBox), backdropBox, body, nullptr,
                                           [this](const bool) { refreshModeChain(); }), row, 0);
    ++row;
}

    // ---- 图片元素节（横贯，可折叠）：「附加图片」开关放节标题行（勾选＝启用并展开、取消＝自动收起）；
    //      内容＝[源图/适应/图片落位 | 两区外观] 左右并排（窄屏上下叠）。----
void NewCanvasDialog::buildImageSection(QWidget* body, QGridLayout* grid, int& row, const bool narrow, int& expandedFormW) {
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
    if (narrow) {   // 窄形态外观组操作行恒 2×2 紧凑（构造期即按窄形态显式收紧；形态链随后同口径维持）。
        nodeAppearance_->setCompactLayout(true);
        backdropAppearance_->setCompactLayout(true);
        imageAppearance_->setCompactLayout(true);
    }
    auto* imageSection = new CollapsibleSection(IDC_GTR(label, kImageElementBox), imageBox, body, attachImageCheck_,
                                                [this](const bool) { refreshModeChain(); });
    connect(attachImageCheck_, &QCheckBox::toggled, imageSection, [imageSection](const bool on) {
        imageSection->setExpanded(on);   // 勾选=展开、取消=自动收起（箭头仍可独立折叠，勾选语义不丢）。
    });
    imageSection->setExpanded(false);   // 初始收起（未勾附加图片）：勾选才展开。
    imageSection->setExpanded(true);    // 暂开取「展开态」舒适宽（初始列宽预算）。
    expandedFormW = body->sizeHint().width();
    imageSection->setExpanded(false);
    grid->addWidget(imageSection, row, 0);
    ++row;
}

} // namespace idc::gui