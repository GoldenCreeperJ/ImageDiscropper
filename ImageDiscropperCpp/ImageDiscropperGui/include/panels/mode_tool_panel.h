// ============================================================================
// 文件：panels/mode_tool_panel.h
// 作用：左侧「交互模式」面板——顶部为画布四态互斥单选（框选 / 图片拾取 / 标注选取 / 标注绘制），
//       仅「标注绘制」态在下方展开各形状 + 多线段 + 文字 + 画笔的子行按钮组。面板只采集用户意图
//       并发 modeChanged(CanvasMode) / toolSelected(AnnoTool)，真正的模式落地由 CanvasCoordinator 写回
//       CanvasView / CanvasScene、切工具由 AnnotationCoordinator 写回 AnnotationBridge（CONTRIBUTING.md「分层纪律」：面板不含几何 / 光栅化）。
// 分块依据：modeGroup_ 与 toolGroup_ 两个互斥 QButtonGroup 分别承载「模式」与「形状工具」，
//           模式 id = CanvasMode 整数值、工具 id = AnnoTool 整数值，避免额外映射表；
//           shapeArea_ 依当前模式显隐；syncFromModel 从标注模型反向同步形状选中态（blockSignals 防回环）。
// ============================================================================
#pragma once

#include <QWidget>

#include "canvas/canvas_mode.h"
#include "model/annotation_bridge.h"
#include "i18n/retranslate.h"

class QButtonGroup;
class QWidget;

namespace idc::gui {

// ---------------------------------------------------------------------------
// ModeToolPanel：交互模式（四态互斥）+ 标注绘制形状子行。
// ---------------------------------------------------------------------------
class ModeToolPanel : public QWidget {
    Q_OBJECT
public:
    explicit ModeToolPanel(QWidget* parent = nullptr);

    // 绑定标注模型（仅用于反向同步当前形状工具选中态）。
    void setModel(AnnotationBridge* model);
    // 从模型反向同步：勾选当前形状工具对应按钮（blockSignals 防回环）。
    void syncFromModel() const;
    // 依当前模式仅显隐形状子行（不触发信号）：供外部编程式同步（如撤销恢复绘制态）。
    void setShapeRowVisible(bool visible) const;
    // 编程式切换当前模式（勾选对应按钮）：经正常 idToggled 路径发 modeChanged（与用户点击同构）。
    void setMode(CanvasMode mode) const;

    // 语言热切换重放：重跑本面板登记的文案闭包（分组标题/模式与工具按钮及其 tooltip）。
    void retranslate() const { retranslator_.run(); }

signals:
    // 用户切换了画布交互模式（四态互斥）。
    void modeChanged(CanvasMode mode);
    // 用户选择了某个标注形状工具（仅「标注绘制」态可见可点）。
    void toolSelected(AnnoTool tool);

private slots:
    void onModeToggled(int id, bool checked);
    void onToolToggled(int id, bool checked);

private:
    AnnotationBridge* model_{nullptr};
    QButtonGroup* modeGroup_{nullptr};
    QButtonGroup* toolGroup_{nullptr};
    QWidget* shapeArea_{nullptr};   // 形状子行容器（仅 AnnoCreate 模式可见）

    i18n::Retranslator retranslator_;   // 本面板文案重放登记表
};

} // namespace idc::gui
