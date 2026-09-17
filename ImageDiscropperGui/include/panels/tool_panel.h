// ============================================================================
// 文件：panels/tool_panel.h
// 作用：左侧「标注工具」面板（guideline §4.5.5）——选择/移动 + 各形状 + 多线段 + 文字 + 画笔
//       的互斥按钮组。面板只采集用户意图并发 toolSelected(AnnoTool)，真正切工具由 MainWindow
//       写回 AnnotationBridge（A-0.1：面板不含几何 / 光栅化）。
// 分块依据：一个 QButtonGroup（互斥）承载全部工具按钮，id = AnnoTool 的整数值，避免额外映射表；
//           syncFromModel 从模型反向同步选中态（blockSignals 防回环），与 LeftPanel 同步 Document 同构。
// 说明：本轮不含箭头（Core ShapeType 无 ARROW，见计划「已知限制」）。
// ============================================================================
#pragma once

#include <QWidget>

#include "model/annotation_bridge.h"

class QButtonGroup;

namespace idc::gui {

// ---------------------------------------------------------------------------
// ToolPanel：标注工具互斥按钮组。
// ---------------------------------------------------------------------------
class ToolPanel : public QWidget {
    Q_OBJECT
public:
    explicit ToolPanel(QWidget* parent = nullptr);

    // 绑定标注模型（仅用于反向同步当前工具选中态）。
    void setModel(AnnotationBridge* model);
    // 从模型反向同步：勾选当前工具对应按钮（blockSignals 防回环）。
    void syncFromModel();

signals:
    // 用户选择了某个标注工具。
    void toolSelected(AnnoTool tool);

private slots:
    void onToolToggled(int id, bool checked);

private:
    AnnotationBridge* model_{nullptr};
    QButtonGroup* toolGroup_{nullptr};
};

} // namespace idc::gui
