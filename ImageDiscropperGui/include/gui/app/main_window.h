// ============================================================================
// 文件：app/main_window.h
// 作用：主窗口（guideline §4.1）——装配菜单栏、工具栏、状态栏、左侧面板、中央画布与
//       右侧参数/导出面板，并把各部件的信号槽串联为一条数据流：
//       用户交互 → Document（状态）→ EngineBridge（调 Core）→ CanvasScene（渲染）。
// 分块依据：
//   - MainWindow 只做「装配 + 编排 + 状态栏呈现」，不含切割/几何/导出实现（A-0.1）；
//     真正的 Core 调用集中在 EngineBridge，状态集中在 Document。
//   - 构建（buildCentral/buildMenus/...）与响应（on* 槽）分组，避免上帝方法。
// 说明：本阶段覆盖 G-1/G-2/G-6/G-7(单矩形)/G-8/G-11(分离+坍缩+基础重排)/G-14/G-10。
//       L3 网格、预处理、标注、撤销重做、配置在后续阶段接入（对应菜单/工具项先禁用占位）。
// ============================================================================
#pragma once

#include <QMainWindow>

#include "engine/engine.h"
#include "model/document.h"
#include "model/engine_bridge.h"
#include "util/preview_scaler.h"

class QLabel;
class QAction;
class QKeyEvent;

namespace idc::gui {

class CanvasScene;
class CanvasView;
class LeftPanel;
class ParamPanel;
class ExportPanel;

// ---------------------------------------------------------------------------
// MainWindow：应用主窗口与总编排。
// ---------------------------------------------------------------------------
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    // 从给定路径打开图像（供命令行首参自动打开复用；不弹文件对话框）。
    // 与菜单/工具栏「打开」共用同一套载入 → 建预览 → 刷新逻辑。
    void openImageFromPath(const QString& path);

protected:
    // 单键快捷键（1/2/3 切模式、K/R 切极性）在此处理，而非用 QShortcut：
    // 只有当焦点控件（如坐标 QSpinBox）不消费该键时才冒泡到主窗口，故数值输入时天然不触发，
    // 避免 QShortcut 抢先拦截数字键导致坐标无法直接键入、只能点增减按钮（NFR-5）。
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    // 文件动作。
    void onOpen();
    void onExport();
    // Document 信号响应。
    void onImageChanged();  // 换图：重建预览底图并刷新。
    void onDocChanged();    // 参数变更：刷新预览 + 同步面板 + 状态栏。
    // 画布交互响应。
    void onRubberSelect(const QRectF& sceneRect);   // 框选新建选区。
    void onSelectionEdited(const QRectF& sceneRect);// 拖动/缩放/拖边既有选区（拖边＝移动切割线）。
    void onNudge(int dx, int dy);                   // 方向键微调选区。
    void onCursor(const QPointF& scenePos);         // 光标坐标/像素颜色回报。
    void onZoomChanged(qreal factor);               // 视图缩放倍数变化 → 更新状态栏放大倍数。
    void onClearCut();                              // 清除切割线（清选区）。
    void onToggleMasks();                           // 切换预览遮罩显隐。
    void onModeAction(int tierInt);                 // 工具栏/快捷键切换模式。
    void onPolarityShortcut(bool remove);           // K/R 快捷键切换极性。

private:
    // 构建各部分。
    void buildCentral();
    void buildMenus();
    void buildToolbar();
    void buildStatus();
    void connectAll();
    // 依工作图重建降采样预览底图（NFR-3）。
    void rebuildPreviewPixmap();
    // 跑 Core 预览并刷新画布遮罩/切割线/状态栏/面板提示。
    void refreshPreview();
    // 同步三个面板到 Document（blockSignals 防回环）。
    void syncPanels();
    // 非模态提示（状态栏 + 提示标签），错误不打断用户（§5.2）。
    void notify(const QString& msg, bool isError);
    // 首次进入引导（§5.2）。
    void showFirstRunGuide();
    // 若焦点在数值输入控件上则忽略单键快捷键（避免打字误触）。
    bool focusInTextInput() const;
    // 模式中文名。
    static QString modeName(idc::engine::Tier tier);

    // 状态与桥接。
    Document doc_;
    EngineBridge bridge_;
    PreviewImage preview_;            // 当前预览副本 + 放大系数
    // 预览最长边上限（NFR-3 降采样阈值）。取 4096：让绝大多数图片 1:1 显示，
    // 使放大后底图像素与精确整数坐标的切割线/遮罩对齐（仅超长边大图才降采样）。
    int previewMaxDim_{4096};

    // 视图部件。
    CanvasScene* scene_{nullptr};
    CanvasView* view_{nullptr};
    LeftPanel* left_{nullptr};
    ParamPanel* param_{nullptr};
    ExportPanel* exportPanel_{nullptr};

    // 状态栏标签。
    QLabel* stCoord_{nullptr};
    QLabel* stColor_{nullptr};
    QLabel* stMode_{nullptr};
    QLabel* stCount_{nullptr};
    QLabel* stZoom_{nullptr};
    QLabel* stHint_{nullptr};

    // 工具栏模式动作（与左侧面板同步）。
    QAction* modeActionL1_{nullptr};
    QAction* modeActionL2_{nullptr};
    QAction* modeActionL3_{nullptr};
};

} // namespace idc::gui
