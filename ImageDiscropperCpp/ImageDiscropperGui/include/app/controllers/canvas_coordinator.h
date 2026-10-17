// ============================================================================
// 文件：app/controllers/canvas_coordinator.h
// 作用：CanvasCoordinator——画布交互域编排：画布四态互斥模式落地 + 用户交互意图写回
//       Document（框选/选区拖改/L3 单元点选与调序/微调/清除选区/单元全选反选清空）+
//       状态栏交互提示。承载 onRubberSelect 等 12 个交互槽 + applyCanvasMode +
//       updateInteractionHint，并串联画布/场景/参数面板相关信号。
// 分块依据：画布交互是独占关注点——写回路径全部「意图 → Document（或场景高亮）→
//       由 PreviewController 刷新预览」，与文件/配置/导出域无耦合（CONTRIBUTING.md「分层纪律」）。
// 说明：模式落地 applyCanvasMode 仍接 ModeToolPanel::modeChanged（与桌面/移动共用同一接线）；
//       MainWindow::connectAll 另连一条 modeChanged → MainWindowUi::syncCanvasModeActions
//       回同步「画布模式」菜单勾选态（连接序保证先落地后读态）。
// ============================================================================
#pragma once

#include <QObject>
#include <QRectF>
#include <QPointF>

#include <optional>
#include <vector>

#include "canvas/canvas_mode.h"

namespace idc::gui {

class Document;
class AnnotationBridge;
class CanvasScene;
class CanvasView;
class ParamPanel;
class StatusBar;
class ModeToolPanel;

// ---------------------------------------------------------------------------
// CanvasCoordinator：画布交互编排（写回 Document / 场景高亮 + 状态栏提示）。
// ---------------------------------------------------------------------------
class CanvasCoordinator : public QObject {
    Q_OBJECT
public:
    explicit CanvasCoordinator(QObject* parent = nullptr);

    // 依赖注入（setter 模式，与面板一致）。
    void setDocument(Document* doc);
    void setAnnotationBridge(AnnotationBridge* anno);
    void setScene(CanvasScene* scene);
    void setView(CanvasView* view);
    void setParamPanel(ParamPanel* param);
    void setStatusBar(StatusBar* status);
    void setToolPanel(ModeToolPanel* toolPanel);
    void connectSignals();   // view_/scene_/param_/toolPanel_ 意图信号 → 本控制器槽

    // 画布四态互斥模式落地：任一时刻只处一态——
    //   · 切视图模式（CanvasView 据此路由左键手势）+ 场景图元可交互性（拾取框 / 标注图元）；
    //   · 进入一态即取消前态选中：标注相关态清标注选中、非图片相关态清选中层（回到「画布」预处理目标）。
    void applyCanvasMode(CanvasMode mode) const;
    // 状态栏提示：显式回报当前交互模式与当前预处理目标（画布 / 某图片图层）；供 MainWindow 初始态调用。
    void updateInteractionHint() const;

public slots:
    // ---- 画布交互写回 ----
    void onRubberSelect(const QRectF& sceneRect) const;    // 框选新建选区（L3 并入单元选择集）。
    void onSelectionEdited(const QRectF& sceneRect) const; // 拖动/缩放/拖边既有选区。
    void onMultiRectEdited(int index, const QRectF& sceneRect) const; // L2 多矩形：拖动/缩放第 index 个选区框。
    void onRectSelected(int index) const;            // L2 多矩形：面板列表选中行→高亮画布对应选区框。
    void onCellToggled(int index) const;                   // L3 单击切换某单元。
    void onCellsMarquee(const std::vector<int>& indices) const; // L3 拖拽框选单元。
    void onCellReordered(int fromIndex, int toIndex) const;     // L3 Custom 拖拽调序。
    void onNudge(int dx, int dy) const;                    // 方向键微调选区（Shift 大步在视图侧换算）。
    void onClearCut() const;                               // 清除切割线（按模式路由：L3 清单元、L2 多矩形清列表）。
    void onCellsSelectAll() const;                         // L3 全选单元（编辑菜单）。
    void onCellsInvert() const;                            // L3 反选单元（编辑菜单）。
    void onCellsClear() const;                             // L3 清空单元选择集（编辑菜单）。

private:
    Document* doc_{nullptr};
    AnnotationBridge* anno_{nullptr};
    CanvasScene* scene_{nullptr};
    CanvasView* view_{nullptr};
    ParamPanel* param_{nullptr};
    StatusBar* status_{nullptr};
    ModeToolPanel* toolPanel_{nullptr};
};

} // namespace idc::gui
