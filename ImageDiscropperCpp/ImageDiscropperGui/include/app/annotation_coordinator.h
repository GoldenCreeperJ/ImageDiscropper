// ============================================================================
// 文件：app/annotation_coordinator.h
// 作用：AnnotationCoordinator——标注域编排控制器：从 MainWindow 拆出 33 个标注槽
//       （面板/画布意图 → AnnotationBridge → 画布/属性面板回显）与绘制手势分派状态机。
// 分块依据：
//   - 标注（G-4/G-5）自成一条数据流（工具/手势/属性/图层/烧录），独占一块；
//   - 只做编排：把意图转交 AnnotationBridge（内部调 Core annotation/geometry），不含几何/光栅化（分层纪律）。
// 说明：撤销/重做启用态不再由本控制器触碰——MainWindow 把 annoBridge 的 changed/selectionChanged/toolChanged
//       同源连接到 DocHistory::updateEnabled，替代原三处 updateUndoRedoEnabled() 调用。
// ============================================================================
#pragma once

#include <QColor>
#include <QObject>
#include <QPointF>

#include "model/annotation_bridge.h"

class QWidget;

namespace idc::gui {

class CanvasScene;
class CanvasView;
class ToolPanel;
class LayerPanel;
class AnnotationPropPanel;
class ExportPanel;
class PreviewController;

// ---------------------------------------------------------------------------
// AnnotationCoordinator：标注面板/画布意图的编排（工具/手势/属性/图层/烧录）。
// ---------------------------------------------------------------------------
class AnnotationCoordinator : public QObject {
    Q_OBJECT
public:
    explicit AnnotationCoordinator(QObject* parent = nullptr);

    // 依赖注入（setter 模式，与面板一致）。
    void setAnnotationBridge(AnnotationBridge* anno);
    void setScene(CanvasScene* scene);
    void setView(CanvasView* view);
    void setToolPanel(ToolPanel* panel);
    void setLayerPanel(LayerPanel* panel);
    void setAnnoPropPanel(AnnotationPropPanel* panel);
    void setExportPanel(ExportPanel* panel);               // burnInChanged + 初始 setBurnInChecked
    void setPreviewController(PreviewController* preview); // B 规则重渲染 + 显隐槽内 refreshPreview
    void setDialogParent(QWidget* parent);                 // QInputDialog/QMessageBox 父窗口（MainWindow）
    void connectSignals();                                 // 标注域全部信号连接 + 烧录初始同步

public slots:
    void onToolSelected(AnnoTool tool) const;              // 工具面板：切换标注工具（先收笔再切）。
    void onAnnoBridgeChanged() const;                             // 标注列表/预览变化：重绘画布标注层 + 属性面板同步。
    void onAnnoPendingChanged() const;                      // 绘制拖拽预览（橡皮筋）变化：仅实时刷新预览图元。
    void onAnnoSelectionChanged() const;                    // 选中项变化：重绘高亮 + 属性面板同步。
    void onAnnoToolChanged() const;                         // 工具变化：同步工具面板 + 画布绘制态门控。
    void onAnnoDragStart(const QPointF& scenePos) const;   // 画布绘制手势起点（依工具分派两点/折线/画笔/文字）。
    void onAnnoDragMove(const QPointF& scenePos) const;    // 画布绘制手势拖拽（两点形状预览/画笔追点/折线橡皮筋）。
    void onAnnoDragEnd(const QPointF& scenePos) const;     // 画布绘制手势释放（提交两点形状/画笔）。
    void onAnnoHover(const QPointF& scenePos) const;       // 绘制态悬停（未按键）：折线实时预览落点与连线。
    void onAnnoFinish() const;                             // 绘制态右键：收笔折线（提交）或取消当前预览。
    void onAnnoEscape() const;                             // 绘制态 Esc：收笔折线或取消当前预览。
    void onAnnotationSelect(const QPointF& scenePos) const;// SELECT 工具下点中标注图元：Core hitTest 选中。
    void onAnnotationMoved(int index, double dx, double dy) const; // 拖动选中标注：Core 平移几何。
    void onAnnotationTransformed(int index, double sx, double sy, double rotateDeg) const; // 拖定向包围盒手柄：Core 缩放/旋转。
    void onAnnotationTransformPreview(int index, double sx, double sy, double rotateDeg) const; // 手柄拖拽中：预览值实时回显到属性面板。
    void onAnnoColorPicked(const QColor& c) const;         // 属性面板：颜色。
    void onAnnoStrokeChanged(int width) const;             // 属性面板：描边粗细。
    void onAnnoFillChanged(bool fill) const;               // 属性面板：填充开关。
    void onAnnoTextChanged(const QString& text) const;     // 属性面板：文字内容。
    void onAnnoFontSizeChanged(double size) const;         // 属性面板：字号。
    void onAnnoTransformApply(double sx, double sy, double rotateDeg) const; // 属性面板：缩放/旋转选中标注（Core 非破坏性变换）。
    void onBaseVisibilityChanged(bool visible) const;      // 图层面板：底图显隐（切画布 base 图元）。
    void onMaskVisibilityChanged(bool visible) const;      // 图层面板：遮罩（保留/删除预览）显隐。
    void onGridVisibilityChanged(bool visible) const;      // 图层面板：网格线显隐（重建落地）。
    void onCutLineVisibilityChanged(bool visible) const;   // 图层面板：切割线显隐。
    void onSelectionVisibilityChanged(bool visible) const; // 图层面板：选取边框显隐。
    void onAnnotationVisibilityChanged(bool visible) const;// 图层面板：标注图层显隐。
    void onBurnInChanged(bool on) const;                   // 导出面板：导出烧录开关。
    void onAnnoUndo() const;                               // 标注菜单：撤销（Core revoke）。
    void onAnnoRedo() const;                               // 标注菜单：重做（Core redo）。
    void onAnnoDeleteSelected() const;                     // 标注菜单/Delete 键：删除选中标注。
    void onAnnoClearAll() const;                           // 标注菜单：清除全部标注。

private:
    AnnotationBridge* anno_{nullptr};
    CanvasScene* scene_{nullptr};
    CanvasView* view_{nullptr};
    ToolPanel* toolPanel_{nullptr};
    LayerPanel* layerPanel_{nullptr};
    AnnotationPropPanel* annoPropPanel_{nullptr};
    ExportPanel* exportPanel_{nullptr};
    PreviewController* preview_{nullptr};
    QWidget* dialogParent_{nullptr};
};

} // namespace idc::gui
