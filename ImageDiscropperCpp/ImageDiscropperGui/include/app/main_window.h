// ============================================================================
// 文件：app/main_window.h
// 作用：主窗口装配壳（布局见本目录 README「布局结构与响应式规则」）——经 MainWindowUi 装配
//       菜单栏、工具栏、状态栏、左侧面板、中央画布与右侧选项卡；实例化四个控制器
//       （预览 PreviewController / 预处理 PreprocessController / 标注 AnnotationCoordinator / 历史 DocHistory），
//       并把各部件的信号槽串联为一条数据流：
//       用户交互 → Document（状态）→ EngineBridge（调 Core）→ CanvasScene（渲染）。
// 分块依据：
//   - MainWindow 只做「装配 + 编排 + 转发」（CONTRIBUTING.md「分层纪律」）；装配细节在 MainWindowUi，
//     预览/预处理/标注/历史编排分别在四个控制器（见 src/app/README.md「编排」），避免上帝文件；
//   - 构造拆为「骨架装配 → initCore 接线」两段（GuideLine 阶段 3）：移动端 MobileShell 经保护构造
//     换装骨架（画布为主 + 底部工具条/抽屉），接线序列与桌面逐行共用（行为同构、零分支）；
//   - 保留在 MainWindow 的：文件/编辑动作槽、Document 信号响应、画布交互写回（均直接读写 Document）。
// 说明：公共面不变（main.cpp 仅依赖构造与 openImageFromPath）；覆盖打开图像/缩放平移、
//       三种模式的切割遮罩与选区/单元交互、分离/坍缩/重排导出、预处理、标注工具与图层管理、
//       全局撤销重做与配置加载/保存（机制详见各控制器头文件与 GUI README 关键设计决策）。
// ============================================================================
#pragma once

#include <QMainWindow>

#include "model/annotation_bridge.h"
#include "model/document.h"

class QAction;
class QKeyEvent;
class QTabWidget;

namespace idc::gui {

class CanvasScene;
class CanvasView;
class LeftPanel;
class ParamPanel;
class ExportPanel;
class ImagePanel;
class ToolPanel;
class LayerPanel;
class AnnotationPropPanel;
class StatusBar;
class PreviewController;
class PreprocessController;
class AnnotationCoordinator;
class DocHistory;
class MainWindowUi;

// ---------------------------------------------------------------------------
// MainWindow：应用主窗口装配壳与总编排转发。
// ---------------------------------------------------------------------------
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    // 从给定路径打开图像（供命令行首参自动打开复用；不弹文件对话框）。
    // 与菜单/工具栏「打开」共用同一套载入 → 建预览 → 刷新逻辑。
    void openImageFromPath(const QString& path);

protected:
    // 移动端骨架标记（GuideLine 阶段 3）：跳过桌面装配与标题/尺寸预设，
    // 由子类（mobile/ 的 MobileShell）自行回填部件后调 initCore 完成接线。
    struct MobileShellTag {};
    explicit MainWindow(MobileShellTag, QWidget* parent = nullptr);

    // 控制器接线与初始同步（构造后半段逐行原迁）：须在全部部件回填完毕后调用；
    // 顺序不变式维持：connectAll 先于 history_->reset()（见 .cpp 注释）。
    void initCore();

    // 单键快捷键（1/2/3 切模式、K/R 切极性、Delete/Backspace 删标注）在此处理，而非用 QShortcut：
    // 只有当焦点控件（如坐标 QSpinBox）不消费该键时才冒泡到主窗口，故数值输入时天然不触发，
    // 避免 QShortcut 抢先拦截数字键导致坐标无法直接键入、只能点增减按钮（NFR-5）。
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    // 文件动作。
    void onOpen();           // 打开图像对话框 → openImageFromPath。
    void onExport();         // 导出（委托 EngineBridge → Core runEngine + exportImage）。
    void onLoadConfig();     // 文件→加载配置：读 JSON → DocHistory::recordConfigLoad（入栈可撤销）。
    void onSaveConfig();     // 文件→保存配置：把当前 EngineConfig 写为 JSON。
    // 编辑菜单撤销/重做：上下文路由——标注上下文走标注撤销，否则走文档参数撤销。
    void onUndo() const;
    void onRedo() const;
    // Document 信号响应。
    void onImageChanged() const;   // 换图：重建预览底图并刷新。
    void onDocChanged() const;     // 参数变更：刷新预览 + 同步面板（拖拽期跳过面板同步）。
    // 画布交互响应（写回 Document 的编排，保留在 MainWindow）。
    void onRubberSelect(const QRectF& sceneRect);   // 框选新建选区。
    void onSelectionEdited(const QRectF& sceneRect);// 拖动/缩放/拖边既有选区（拖边＝移动切割线）。
    void onMultiRectEdited(int index, const QRectF& sceneRect); // L2 多矩形：拖动/缩放第 index 个选区框。
    void onRectSelected(int index) const;                 // L2 多矩形：面板列表选中行→高亮画布对应选区框。
    void onCellToggled(int index);                  // L3 单击切换某单元。
    void onCellsMarquee(const std::vector<int>& indices); // L3 拖拽框选单元。
    void onCellReordered(int fromIndex, int toIndex); // L3 CUSTOM 拖拽调序。
    void onNudge(int dx, int dy);                   // 方向键微调选区。
    void onCursor(const QPointF& scenePos) const;         // 光标坐标/像素颜色回报。
    void onZoomChanged(qreal factor) const;               // 视图缩放倍数变化 → 更新状态栏放大倍数。
    void onClearCut();                              // 清除切割线（清选区）。
    void onToggleMasks() const;                           // 切换预览遮罩显隐。
    void onModeAction(int tierInt);                 // 工具栏/快捷键切换模式。
    void onPolarityShortcut(bool remove);           // K/R 快捷键切换极性。
    // 撤销/重做启用态回灌（DocHistory::availabilityChanged）。
    void onHistoryAvailabilityChanged(bool canUndo, bool canRedo) const;

private:
    friend class MainWindowUi;  // 装配建造者直接回填部件指针并连接私有槽。
    friend class MobileShellUi; // 移动端骨架建造者（mobile/）同样直接回填 + 连私有槽（§8.3 行为同构）。

    void buildDesktopUi();     // 桌面骨架装配（MainWindowUi 四步，逐行原迁自原构造）。
    void connectAll();         // 剩余跨件串联（Document/画布交互/历史启用态；其余连接在各控制器内）。
    // 同步左侧面板与右侧「参数/导出/图像」页到 Document（blockSignals 防回环）。
    void syncPanels() const;
    // 非模态提示（状态栏 + 提示标签），错误不打断用户。
    void notify(const QString& msg, bool isError) const;
    // 若焦点在数值输入控件上则忽略单键快捷键（避免打字误触）。
    static bool focusInTextInput();

    // 状态与桥接（单一真相源 + 唯一标注域入口）。
    Document doc_;
    AnnotationBridge annoBridge_;       // 标注域桥：唯一持有并驱动 Core AnnotationLayer（G-4/G-5）。

    // 控制器（QObject 子对象，本窗口拥有）。
    PreviewController* preview_{nullptr};         // 预览刷新状态机 + 导出预览缓存。
    PreprocessController* preprocess_{nullptr};   // 预处理 8 操作编排。
    AnnotationCoordinator* annotation_{nullptr};  // 标注 33 槽编排 + 手势分派。
    DocHistory* history_{nullptr};                // 文档参数态撤销/重做（防抖/抑制/上下文）。

    // 视图部件（MainWindowUi 装配回填）。
    CanvasScene* scene_{nullptr};
    CanvasView* view_{nullptr};
    LeftPanel* left_{nullptr};
    ParamPanel* param_{nullptr};
    ExportPanel* exportPanel_{nullptr};
    ImagePanel* imagePanel_{nullptr};   // 右侧「图像」页：预处理（FR-1）。
    ToolPanel* toolPanel_{nullptr};             // 左侧「标注工具」组（G-4）。
    LayerPanel* layerPanel_{nullptr};           // 左侧「图层」组（G-5）。
    AnnotationPropPanel* annoPropPanel_{nullptr}; // 右侧「标注」属性页（G-4）。
    QWidget* rightTabs_{nullptr};       // 右侧多页容器：桌面＝AccordionPanel 折叠式（多节可同开+整栏滚动）/
                                        // 移动端＝QTabWidget 选择式；跨形态访问经双型分发。
    StatusBar* status_{nullptr};

    // 工具栏模式动作（与左侧面板同步）。
    QAction* modeActionL1_{nullptr};
    QAction* modeActionL2_{nullptr};
    QAction* modeActionL3_{nullptr};

    // 编辑菜单撤销/重做动作：提为成员以便依历史深度与标注上下文动态启/禁。
    QAction* aUndo_{nullptr};
    QAction* aRedo_{nullptr};
};

} // namespace idc::gui
