// ============================================================================
// 文件：panels/param_panel.h
// 作用：右侧「参数面板」（guideline §4.5）——随模式切换的 QStackedWidget：
//       L1 形状+坐标、L2 子功能+坐标+坍缩可行性提示、L3 占位（第二阶段接入）。
//       面板只把用户输入写回 Document（数值直接输入回车生效，NFR-5），不含几何逻辑（A-0.1）。
// 分块依据：每个模式一页，页内控件与该模式的一两个 Document 字段对应；坍缩提示由 MainWindow
//           依 Core 的 isCollapsible 结果回灌（面板不自算可行性）。
// 说明：坐标 spinbox 关闭 keyboardTracking，避免逐键触发预览；提交（回车/失焦/箭头）才写回。
// ============================================================================
#pragma once

#include <QWidget>

#include "engine/engine.h"

class QComboBox;
class QSpinBox;
class QLabel;
class QStackedWidget;

namespace idc::gui {

class Document;

// ---------------------------------------------------------------------------
// ParamPanel：模式相关的参数输入面板。
// ---------------------------------------------------------------------------
class ParamPanel : public QWidget {
    Q_OBJECT
public:
    explicit ParamPanel(QWidget* parent = nullptr);

    void setDocument(Document* doc);
    // 从 Document 反向同步控件（形状/子功能/坐标），blockSignals 防回环。
    void syncFromDocument();
    // 切换到某模式对应的页（L1→0, L2→1, L3→2）。
    void setMode(idc::engine::Tier tier);
    // 由 MainWindow 依 Core isCollapsible 结果回灌坍缩可行性提示（L2 页）。
    void setCollapseHint(bool collapsible, const QString& reason);

private slots:
    void onL1ShapeChanged(int index);
    void onL2SubChanged(int index);
    void onCoordEdited(); // L1/L2 坐标变更统一入口。

private:
    // 构建 L1 / L2 / L3 三页并加入 stack_。
    QWidget* buildL1Page();
    QWidget* buildL2Page();
    QWidget* buildL3Page();
    // 把四个坐标 spinbox 组装为规范化矩形写回 Document。
    void applyCoordsToDocument();

    Document* doc_{nullptr};
    QStackedWidget* stack_{nullptr};

    // L1 控件。
    QComboBox* l1Shape_{nullptr};
    QSpinBox* l1x1_{nullptr};
    QSpinBox* l1y1_{nullptr};
    QSpinBox* l1x2_{nullptr};
    QSpinBox* l1y2_{nullptr};

    // L2 控件。
    QComboBox* l2Sub_{nullptr};
    QSpinBox* l2x1_{nullptr};
    QSpinBox* l2y1_{nullptr};
    QSpinBox* l2x2_{nullptr};
    QSpinBox* l2y2_{nullptr};
    QLabel* collapseHint_{nullptr};
};

} // namespace idc::gui
