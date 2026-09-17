// ============================================================================
// 文件：panels/layer_panel.h
// 作用：左侧「图层」面板（guideline §4.5.6）——集中管理画布各图层的显示/隐藏：
//       底图 / 遮罩（保留删除预览）/ 网格线 / 切割线 / 选取边框 / 标注。
//       面板只采集意图并发信号，真正的图层可见性落地由 MainWindow 分派到 CanvasScene
//       与 AnnotationBridge（A-0.1）。
// 分块依据：每层一行「名称 + 显示开关」；信号一一对应各类意图，参数即最小信息（bool）。
//           syncFromModel 从模型反向同步标注可见态（防回环）。
// 说明：遮罩开关已从工具栏/视图菜单迁入本面板（成为正式图层项）；视图菜单/右键仍可切换，
//       切换后由 MainWindow 调 setMaskVisible 反向同步本面板复选框。标注「导出时烧录」开关
//       已迁至导出面板（见 export_panel）。底图/网格线/切割线/选取边框的隐藏仅影响画布预览，
//       不影响导出（见计划「已知限制」）。
// ============================================================================
#pragma once

#include <QWidget>

#include "model/annotation_bridge.h"

class QCheckBox;

namespace idc::gui {

// ---------------------------------------------------------------------------
// LayerPanel：各图层显示/隐藏。
// ---------------------------------------------------------------------------
class LayerPanel : public QWidget {
    Q_OBJECT
public:
    explicit LayerPanel(QWidget* parent = nullptr);

    // 绑定标注模型（用于反向同步标注可见态）。
    void setModel(AnnotationBridge* model);
    // 从模型反向同步：标注显示开关 = layerVisible()（blockSignals 防回环）。
    void syncFromModel();
    // 外部（视图菜单 / 右键「切换预览遮罩」）改动遮罩显隐后，反向同步遮罩复选框（blockSignals 防回环）。
    void setMaskVisible(bool visible);

signals:
    // 底图图层显示/隐藏（MainWindow 切换画布 base 图元可见性）。
    void baseVisibilityChanged(bool visible);
    // 遮罩（保留/删除预览）图层显示/隐藏。
    void maskVisibilityChanged(bool visible);
    // 网格线图层显示/隐藏。
    void gridVisibilityChanged(bool visible);
    // 切割线图层显示/隐藏。
    void cutLineVisibilityChanged(bool visible);
    // 选取边框图层显示/隐藏。
    void selectionVisibilityChanged(bool visible);
    // 标注图层显示/隐藏。
    void annotationVisibilityChanged(bool visible);

private:
    AnnotationBridge* model_{nullptr};
    QCheckBox* baseVisible_{nullptr};
    QCheckBox* maskVisible_{nullptr};
    QCheckBox* gridVisible_{nullptr};
    QCheckBox* cutLineVisible_{nullptr};
    QCheckBox* selectionVisible_{nullptr};
    QCheckBox* annoVisible_{nullptr};
};

} // namespace idc::gui
