// ============================================================================
// 文件：panels/element_appearance_group.h
// 作用：「元素外观」紧凑编辑组（新建画布/添加图层对话框内嵌，SPEC §2.2a 合成模型）——
//       一个 QGroupBox 承载外观**预处理简表**与不透明度（0..255）。三实例：图案元素外观、
//       图片元素外观、节点外观（对话框第三简表；新建画布语境＝底图整体外观，仅色彩步）。
//       简表与预处理面板同构分块：**几何 / 色彩两个独立区**（各区＝类型组合框 + 添加 + 各自步骤
//       列表 + 各自上移/下移/删除；色彩区另含 R/G/B 通道勾）——几何区恒在色彩区之前，
//       pipeline 的「几何在前、色彩在后」由结构直接保证（与预处理面板 geo→color 不变式同口径；
//       简表无 resize，resize 属画布尺寸区/独立设置，不在本组）。
//       几何步按实例分化：**图案元素外观仅色彩步**（几何区整体隐藏——图案按区域尺寸生成后才施外观，
//       几何步会改尺寸；图案担任「底图」功能、底图不改画布尺寸的纪律照搬）；**图片元素/节点外观可含
//       几何步**（图片元素预处理在落位前施于源、无框时区域随预处理后尺寸；节点外观在物化后施加）。
// 分块依据：纯采参组件（不持真相、不跑像素）；PreprocessPipeline 直接出入，由对话框
//       accept() 时落进 canvas::ImageLayer 的外观字段（imagePreprocess/imageOpacity /
//       backdropPreprocess/backdropOpacity / 节点 preprocess）。
// 说明：行文本与通道勾复用预处理面板同源文案（kGeoSectionBox / kColorSectionBox /
//       kColorTypeGray·Invert·Split / kStepRotateDesc / menu::kFlipHorizontal·kFlipVertical /
//       kChannelFieldLabel——面板的几何/色彩区为其私有装配、非独立组件，故此处同款同源实现而非复用其类）。
// ============================================================================
#pragma once

#include <QGroupBox>

#include "i18n/retranslate.h"
#include "preprocess/preprocess_pipeline.h"

class QCheckBox;
class QComboBox;
class QGridLayout;
class QListWidget;
class QPushButton;
class QSpinBox;

namespace idc::gui {

// ---------------------------------------------------------------------------
// ElementAppearanceGroup：外观（几何/色彩两区预处理简表 + 不透明度）紧凑编辑组。
// ---------------------------------------------------------------------------
class ElementAppearanceGroup : public QGroupBox {
    Q_OBJECT
public:
    // geometricAllowed：简表是否含几何区（旋转/翻转）。图案元素外观传 false、图片元素外观传 true；
    //   节点外观按语境切换（新建画布＝底图口径 false，添加图层 true，见 setGeometricAllowed）。
    // showOpacity：是否显示不透明度行。图案/图片元素外观恒显示；节点外观实例传 false——节点不透明度
    //   由对话框「整体不透明度」行独家采集，本组再显一行即重复。
    explicit ElementAppearanceGroup(QWidget* parent = nullptr, bool geometricAllowed = true, bool showOpacity = true);

    // 当前声明态（由对话框 accept() 时采集落入外观字段）。
    preprocess::PreprocessPipeline pipeline() const;
    int opacity() const;   // 实现在 cpp（避免头文件内联解引用不完整类型 QSpinBox）。
    void setPipeline(const preprocess::PreprocessPipeline& pipeline);
    void setOpacity(int opacity) const;

    // 切换几何步可用性：收窄时几何区隐藏、现有队列里的几何步被剔除（仅留色彩步，与 Core 写出/还原过滤同口径）。
    void setGeometricAllowed(bool allowed);

    // 切换操作行紧凑布局（窄屏 2×2 两行排 ↔ 宽屏单行排）：屏幕几何变化时由对话框驱动（旋转后随形态切换）。
    void setCompactLayout(bool compact);

    // 语言热切换重放（与面板 retranslate 同构）。
    void retranslate() const { retranslator_.run(); }

signals:
    void changed();   // 步骤/不透明度任一变化（驱动对话框预览刷新）。

private slots:
    void onAddGeoStep();
    void onAddColorStep();
    void onUpdateGeoStep();
    void onUpdateColorStep();
    void onRemoveGeoStep();
    void onRemoveColorStep();
    void onMoveGeoStep(bool down);
    void onMoveColorStep(bool down);

private:
    // 组合框条目文案（setItemText 原地改字，保 currentIndex/userData，retranslate.h 纪律）。
    void applyGeoKindNames() const;
    void applyColorKindNames() const;
    // 作用通道行显隐：仅色彩类型为反色/分离时显示。
    void updateChanRow() const;
    // 两列表重建：几何区＝pipeline 前段几何块、色彩区＝后段色彩块（几何恒前由结构保证）；保选中行。
    void rebuildLists() const;

    // 操作行重排（compact 形态切换共用）：同一网格按形态重摆按钮（单行 4 列 / 2×2 两行）。
    void rebuildOpRows() const;

    QWidget* geoSection_{nullptr};   // 几何区整块（类型行 + 列表 + 操作行）：仅 geometricAllowed 显示。
    QComboBox* geoCombo_{nullptr};
    QPushButton* geoAddBtn_{nullptr};
    QListWidget* geoList_{nullptr};
    QPushButton* geoUpdateBtn_{nullptr};
    QPushButton* geoMoveUpBtn_{nullptr};
    QPushButton* geoMoveDownBtn_{nullptr};
    QPushButton* geoRemoveBtn_{nullptr};
    QGridLayout* geoOpGrid_{nullptr}; // 几何操作行网格（单行 4 列 / 2×2，随 compact 重摆）。
    QComboBox* colorCombo_{nullptr};
    QPushButton* colorAddBtn_{nullptr};
    QWidget* chanRow_{nullptr};   // 「作用通道」行（R/G/B 三勾，仅反色/分离显示）。
    QCheckBox* chR_{nullptr};
    QCheckBox* chG_{nullptr};
    QCheckBox* chB_{nullptr};
    QListWidget* colorList_{nullptr};
    QPushButton* colorUpdateBtn_{nullptr};
    QPushButton* colorMoveUpBtn_{nullptr};
    QPushButton* colorMoveDownBtn_{nullptr};
    QPushButton* colorRemoveBtn_{nullptr};
    QGridLayout* colorOpGrid_{nullptr}; // 色彩操作行网格（同上）。
    QSpinBox* opacitySpin_{nullptr};

    bool compact_{false};

    preprocess::PreprocessPipeline pipeline_;
    bool geometricAllowed_{true};
    bool showOpacity_{true};
    i18n::Retranslator retranslator_;
};

} // namespace idc::gui
