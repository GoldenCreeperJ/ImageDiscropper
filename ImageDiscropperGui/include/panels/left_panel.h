// ============================================================================
// 文件：panels/left_panel.h
// 作用：左侧面板（guideline §4.1/§4.3/§4.4）——模式切换（L1/L2/L3）与极性开关（keep/remove）。
//       面板是 Document 的控制器：只把用户选择写回 Document，不含任何切割/几何逻辑（A-0.1）。
//       「工具」「图层」分组已于第四阶段迁出为独立的 ToolPanel / LayerPanel，由主窗口左侧容器装配。
// 分块依据：模式与极性两个 QButtonGroup 各自映射到 Document 的一个字段。
// 说明：L2 反向剔除为核心特色，按钮用强调色区分（§4.3）；极性 keep 绿 / remove 红（§4.4）。
// ============================================================================
#pragma once

#include <QWidget>

class QButtonGroup;

namespace idc::gui {

class Document;

// ---------------------------------------------------------------------------
// LeftPanel：模式切换 + 极性开关 + 工具/图层占位。
// ---------------------------------------------------------------------------
class LeftPanel : public QWidget {
    Q_OBJECT
public:
    explicit LeftPanel(QWidget* parent = nullptr);

    // 绑定 Document（面板据此读写状态）。
    void setDocument(Document* doc);
    // 从 Document 反向同步按钮选中态（用 blockSignals 防回环）。
    void syncFromDocument();

private slots:
    // 模式按钮切换（id：1=L1, 2=L2, 3=L3）。
    void onModeToggled(int id, bool checked);
    // 极性按钮切换（id：0=keep, 1=remove）。
    void onPolarityToggled(int id, bool checked);

private:
    Document* doc_{nullptr};
    QButtonGroup* modeGroup_{nullptr};
    QButtonGroup* polarityGroup_{nullptr};
};

} // namespace idc::gui
