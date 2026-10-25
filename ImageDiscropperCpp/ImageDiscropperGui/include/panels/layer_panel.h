// ============================================================================
// 文件：panels/layer_panel.h
// 作用：左侧「图层」面板——集中管理画布各图层的显示/隐藏（按画布 z 序排列）：
//       底图 / 图片图层 / 标注 / 遮罩（保留删除预览）/ 网格线与切割线（同一开关）/ 选取边框 / 单元编号（共 7 项）。
//       面板只采集意图并发信号，真正的图层可见性落地由 AnnotationCoordinator（多数图层）与 SessionController（图片图层整体）分派到 CanvasScene
//       与 AnnotationBridge（CONTRIBUTING.md「分层纪律」）。
// 分块依据：每层一行「名称 + 显示开关」；信号一一对应各类意图，参数即最小信息（bool）。
//           syncFromModel 从模型反向同步标注可见态（防回环）。
// 说明：遮罩开关是本面板的正式图层项；视图菜单/右键均可切换——菜单经 setMaskVisible 前向驱动（发信号走同一落地链），
//       右键由 PreviewController 自行落地后调 syncMaskVisible 纯反向同步本面板复选框。标注「导出时烧录」开关在导出面板（见 export_panel）。各图层的隐藏仅影响画布预览，不影响导出。
// ============================================================================
#pragma once

#include <QWidget>

#include "model/annotation_bridge.h"
#include "i18n/retranslate.h"

class QCheckBox;

namespace idc::gui {

class Document;

// ---------------------------------------------------------------------------
// LayerPanel：各图层显示/隐藏。每层一行「名称  显示」复选框，一行一开关，无逐子项拆分：
//   底图 / 图片图层 / 标注 / 遮罩 / 网格线与切割线（同一开关，按模式各司其职）/ 选取边框 / 单元编号
//   （画布 z 序：底图 < 图片图层 < 标注）。
//   图片图层是「所有图片」合成的单一层，用一个开关整体控制其显隐：
//   · 显示开关以 Document 整体可见态为真相→落到运行态标志 imageLayersVisible_（不入配置），**仅切换画布预览**（发 imageLayersVisibleChanged 交场景切图层像素图元与落位框可见性，不重组 working_、不影响导出）；
//     导出是否并入叠加层由导出面板「烧录图片图层」开关独立决定（对标标注的「显示 / 烧录」两开关，二者正交）；
//   · 交互（拖拽落位框）统一由全局四态 ImagePick 模式机控制，面板不逐层拆分。
// ---------------------------------------------------------------------------
class LayerPanel : public QWidget {
    Q_OBJECT
public:
    explicit LayerPanel(QWidget* parent = nullptr);

    // 绑定标注模型（用于反向同步标注可见态）。
    void setModel(AnnotationBridge* model);
    // 绑定图层数据源（供「图片图层」整体开关读取显隐态 / 反向同步勾选态）。
    void setDocument(Document* doc);
    // 从模型反向同步：标注显示开关 = layerVisible()（blockSignals 防回环）。
    void syncFromModel() const;
    // 从 Document 反向同步「图片图层」整体开关：行常驻可见且始终可用；勾选态取整体可见态（blockSignals 防回环）。
    void syncFromDocument() const;
    // 视图菜单前向驱动：切换对应复选框并让其 toggled 自然发意图信号，走与用户直接勾选同一条落地链
    //（信号 → AnnotationCoordinator / SessionController → 场景/模型）；复选框勾选态随之改变，菜单勾选态
    //  经面板信号反向同步。这些 setXVisible **不** blockSignals——静默翻转会使菜单点击只动勾选框而不落地。
    void setMaskVisible(bool visible) const;
    void setBaseVisible(bool visible) const;
    void setImageVisible(bool visible) const;
    void setGridVisible(bool visible) const;
    void setSelectionVisible(bool visible) const;
    void setNumberVisible(bool visible) const;
    void setAnnotationVisible(bool visible) const;

    // 遮罩勾选态的**纯反向同步**（blockSignals 防回环）：供画布右键「切换预览遮罩」在自行落地后镜像复选框，
    // 不重发意图信号（落地由调用方自理，语义与上面的前向驱动相反）。
    void syncMaskVisible(bool visible) const;

    // 语言热切换重放：重跑本面板登记的分组标题与各开关文字/tooltip 闭包。
    void retranslate() const { retranslator_.run(); }

signals:
    // 底图图层显示/隐藏（AnnotationCoordinator 切换画布 base 图元可见性）。
    void baseVisibilityChanged(bool visible);
    // 遮罩（保留/删除预览）图层显示/隐藏。
    void maskVisibilityChanged(bool visible);
    // 网格线与切割线共用同一开关：L3 为灰色网格线，L1/L2 为贯穿全图的切割线（延伸段 + 多矩形诱导线）。
    void gridVisibilityChanged(bool visible);
    // 选取边框图层显示/隐藏。
    void selectionVisibilityChanged(bool visible);
    // 单元编号角标图层显示/隐藏（L3 Custom 序的单元序号角标）。
    void numberVisibilityChanged(bool visible);
    // 标注图层显示/隐藏。
    void annotationVisibilityChanged(bool visible);
    // 图片图层整体显隐切换（一个开关控所有图片图层，仅影响预览；SessionController → Document::setImageLayersVisible 落运行态标志并发 imageLayersVisibleChanged，MainWindow 转 CanvasScene::setImageLayersVisible 纯显隐，不重组、不影响导出）。
    void imageLayerVisibilityChanged(bool visible);

private:
    AnnotationBridge* model_{nullptr};
    Document* doc_{nullptr};
    QCheckBox* baseVisible_{nullptr};
    QCheckBox* imageVisible_{nullptr};   // 图片图层整体显隐（单开关，行位于底图与标注之间）
    QCheckBox* maskVisible_{nullptr};
    QCheckBox* gridVisible_{nullptr};   // 网格线与切割线（同一开关，按模式各司其职）
    QCheckBox* selectionVisible_{nullptr};
    QCheckBox* numberVisible_{nullptr};
    QCheckBox* annoVisible_{nullptr};

    i18n::Retranslator retranslator_;   // 本面板文案重放登记表
};

} // namespace idc::gui
