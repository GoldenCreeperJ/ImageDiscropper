// ============================================================================
// 文件：panels/layer_panel.h
// 作用：左侧「图层」面板——集中管理画布各图层的显示/隐藏（按画布 z 序排列）：
//       底图 / 标注 / 遮罩（保留删除预览）/ 网格线 / 切割线 / 选取边框 / 单元编号。
//       面板只采集意图并发信号，真正的图层可见性落地由 MainWindow 分派到 CanvasScene
//       与 AnnotationBridge（CONTRIBUTING.md「分层纪律」）。
// 分块依据：每层一行「名称 + 显示开关」；信号一一对应各类意图，参数即最小信息（bool）。
//           syncFromModel 从模型反向同步标注可见态（防回环）。
// 说明：遮罩开关已从工具栏/视图菜单迁入本面板（成为正式图层项）；视图菜单/右键仍可切换，
//       切换后由 MainWindow 调 setMaskVisible 反向同步本面板复选框。标注「导出时烧录」开关
//       已迁至导出面板（见 export_panel）。各图层的隐藏仅影响画布预览，不影响导出。
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
//   底图 / 图片图层 / 标注 / 遮罩 / 网格线 / 切割线 / 选取边框 / 单元编号（画布 z 序：底图 < 图片图层 < 标注）。
//   图片图层是「所有图片」合成的单一层，用一个开关整体控制其显隐：
//   · 显示开关以 Document 整体可见态为真相→落到运行态标志 imageLayersVisible_（不入配置），**仅影响画布预览**（重组当前 working_ 与落位框显隐）；
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
    // 外部（视图菜单）改动各图层显隐后，反向同步对应复选框（blockSignals 防回环）；
    // 复选框仍是唯一真相源——菜单只翻转复选框，落地链（信号 → 控制器 → 场景）保持不变。
    void setMaskVisible(bool visible) const;
    void setBaseVisible(bool visible) const;
    void setImageVisible(bool visible) const;
    void setGridVisible(bool visible) const;
    void setCutLineVisible(bool visible) const;
    void setSelectionVisible(bool visible) const;
    void setNumberVisible(bool visible) const;
    void setAnnotationVisible(bool visible) const;

    // 语言热切换重放：重跑本面板登记的分组标题与各开关文字/tooltip 闭包。
    void retranslate() const { retranslator_.run(); }

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
    // 单元编号角标图层显示/隐藏（L3 CUSTOM 序的单元序号角标）。
    void numberVisibilityChanged(bool visible);
    // 标注图层显示/隐藏。
    void annotationVisibilityChanged(bool visible);
    // 图片图层整体显隐切换（一个开关控所有图片图层，仅影响预览；MainWindow → Document::setImageLayersVisible → layersChanged 重组预览）。
    void imageLayerVisibilityChanged(bool visible);

private:
    AnnotationBridge* model_{nullptr};
    Document* doc_{nullptr};
    QCheckBox* baseVisible_{nullptr};
    QCheckBox* imageVisible_{nullptr};   // 图片图层整体显隐（单开关，行位于底图与标注之间）
    QCheckBox* maskVisible_{nullptr};
    QCheckBox* gridVisible_{nullptr};
    QCheckBox* cutLineVisible_{nullptr};
    QCheckBox* selectionVisible_{nullptr};
    QCheckBox* numberVisible_{nullptr};
    QCheckBox* annoVisible_{nullptr};

    i18n::Retranslator retranslator_;   // 本面板文案重放登记表
};

} // namespace idc::gui
