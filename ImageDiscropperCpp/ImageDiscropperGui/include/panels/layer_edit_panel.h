// ============================================================================
// 文件：panels/layer_edit_panel.h
// 作用：右侧「图层编辑」面板——针对**多层画布**（Document::hasLayers() 为真）的图片图层做声明态编辑：
//       ① 上 / 下移一层（邻换 zOrder）、删除图层、添加图片图层——均作用于**画布当前拾取层**
//          （Document::selectedLayerId，由画布点选驱动；本面板不含内置图层列表）；
//       ② 落位变换编辑：画布锚点 + 图片枢轴（各 9 向）+ 自由偏移 (x,y) + 目标宽高（<=0＝原生）+ 不透明度（0..255，往返 transform.opacity）；
//       ③ 画布尺寸编辑（可改；尺寸变化只清框选、保留图层与标注——SPEC §8.2）；
//       ④ 添加图片图层（复用「新建画布」对话框采参：程序图案→Recipe 可复现层 / 贴图→File 位图层；往现有画布追加，超出画布部分合成时裁剪、不撑大——SPEC §8.2）。
// 分块依据：面板只采集意图并发信号，真正的图层 CRUD / 解码 / 重组由 SessionController 落 Document 并经
//           PreprocessController 接 layersChanged 重组（CONTRIBUTING.md「分层纪律」：面板不持真相、不跑像素）。
// 说明：与「图层」可见性面板（LayerPanel，管底图/标注/遮罩等显示开关）职责不同——本面板管**图片图层**的
//       内容与叠放。术语固定：本面板的「图层」＝canvas::ImageLayer「图片图层」，非标注叠放次序。
// ============================================================================
#pragma once

#include <QWidget>

#include <optional>

#include "canvas/layer.h"
#include "i18n/retranslate.h"

class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;

namespace idc::gui {

class Document;
class CoordSpinBox;   // 落位偏移 / 目标宽高（整数像素 + "50%" 百分比输入，见 include/util/coord_spin.h）

// ---------------------------------------------------------------------------
// LayerEditPanel：图片图层增删序（作用于画布拾取层）+ 落位/画布尺寸编辑。
// 说明：图层「选中」由画布拾取承担、本面板不列图层；上/下移/删除以 Document::selectedLayerId() 为作用对象。
// ---------------------------------------------------------------------------
class LayerEditPanel : public QWidget {
    Q_OBJECT
public:
    explicit LayerEditPanel(QWidget* parent = nullptr);

    void setDocument(Document* doc);
    // 从 Document 反向同步：回填选中层变换控件 / 回填画布尺寸 / 依状态启停控件。
    void syncFromDocument();

    // 语言热切换重放：重跑本面板登记的静态文案（分区标题/行标签/下拉项/按钮/tooltip）。
    // 动态回显（选中层变换/画布尺寸）由调用方随后的 syncPanels→syncFromDocument 重派生。
    void retranslate() const { retranslator_.run(); }

signals:
    void layerMoveRequested(int id, bool up);                   // 上移（向顶，zOrder 更大）/ 下移（作用画布当前拾取层）
    void layerMoveEdgeRequested(int id, bool toTop);            // 移至顶层（toTop）/ 移至底层（作用画布当前拾取层）
    void layerRemoveRequested(int id);                          // 删除图层
    void layerTransformRequested(int id, canvas::ImageTransform transform);  // 应用落位编辑（锚点/枢轴/偏移/尺寸变化即时发出）
    void baseOpacityRequested(int opacity);    // 不透明度目标＝底图（Base，图层画布选中底图）时驱动底图整体透明度（0..255）
    void canvasOpacityRequested(int opacity);  // 不透明度目标＝画布（Canvas，图层画布未选层/底图）时驱动画布级整体透明度（0..255）
    void canvasSizeRequested(int width, int height);            // 应用画布尺寸（仅清框选）
    void addImageRequested();                                   // 从文件添加图片图层
    void clearAllLayersRequested();                              // 清空全部图片图层（回退底图，保留标注/预处理）

private slots:
    void onMoveUp();
    void onMoveDown();
    void onMoveToTop();
    void onMoveToBottom();
    void onRemove();
    void onApplyTransform();
    void onApplyCanvasSize();

private:
    // 把某层变换回填到配置控件（锚点/枢轴/偏移/宽高；blockSignals 防回环）。
    void loadTransformIntoControls(const canvas::ImageTransform& t) const;
    // 依「有图层画布 + 有选中层」启停各编辑控件。
    void setControlsEnabled(bool hasLayerCanvas, bool hasSelection) const;

    Document* doc_{nullptr};
    bool syncing_{false};   // 反向同步期置真，抑制由 setChecked 触发的意图信号回环

    QPushButton* moveUpBtn_{nullptr};
    QPushButton* moveDownBtn_{nullptr};
    QPushButton* moveTopBtn_{nullptr};      // 移至顶层
    QPushButton* moveBottomBtn_{nullptr};   // 移至底层
    QPushButton* removeBtn_{nullptr};
    QPushButton* addImageBtn_{nullptr};
    QPushButton* clearLayersBtn_{nullptr};   // 清空全部图片图层（回退底图）

    // 落位变换区（选中层可编辑）。
    QComboBox* anchorCombo_{nullptr};   // 画布锚点（9 向）
    QComboBox* pivotCombo_{nullptr};    // 图片枢轴（9 向）
    CoordSpinBox* posXSpin_{nullptr};
    CoordSpinBox* posYSpin_{nullptr};
    CoordSpinBox* widthSpin_{nullptr};  // 目标宽（0＝原生；另接受百分比，基准画布宽）
    CoordSpinBox* heightSpin_{nullptr}; // 目标高（0＝原生；另接受百分比，基准画布高）
    QSpinBox*  opacitySpin_{nullptr};   // 不透明度（0..255，255＝不透明；合成期 alpha 乘子，灰度层亦生效）
    QPushButton* applyTransformBtn_{nullptr};

    // 画布尺寸区（有图层画布即可编辑）。
    QSpinBox*  canvasWSpin_{nullptr};
    QSpinBox*  canvasHSpin_{nullptr};
    QPushButton* applyCanvasBtn_{nullptr};

    i18n::Retranslator retranslator_;   // 本面板文案重放登记表
};

} // namespace idc::gui
