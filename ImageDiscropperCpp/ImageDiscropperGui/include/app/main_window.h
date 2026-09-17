// ============================================================================
// 文件：app/main_window.h
// 作用：主窗口（guideline §4.1）——装配菜单栏、工具栏、状态栏、左侧面板、中央画布与
//       右侧参数/导出面板，并把各部件的信号槽串联为一条数据流：
//       用户交互 → Document（状态）→ EngineBridge（调 Core）→ CanvasScene（渲染）。
// 分块依据：
//   - MainWindow 只做「装配 + 编排 + 状态栏呈现」，不含切割/几何/导出实现（A-0.1）；
//     真正的 Core 调用集中在 EngineBridge，状态集中在 Document。
//   - 构建（buildCentral/buildMenus/...）与响应（on* 槽）分组，避免上帝方法。
// 说明：本阶段覆盖 G-1/G-2/G-6/G-7(单矩形)/G-8/G-11(分离+坍缩+基础重排)/G-14/G-10，
//       第三阶段的 G-3 预处理（旋转/翻转/缩放/尺寸/黑白/反色/色道分离，图像处理面板），
//       以及第四阶段的 G-4 标注工具 + G-5 图层管理（AnnotationBridge 驱动 Core annotation/geometry，
//       画布矢量叠加渲染、导出可选烧录）。第五阶段补齐全局撤销重做(G-12)与配置加载/保存(G-13)：
//       G-13 经 EngineBridge 委托 Core saveEngineConfig/loadEngineConfig，Document::applyEngineConfig 反向映射；
//       G-12 采「双历史 + 上下文路由」——文档参数态用 HistoryManager<EngineConfig> 快照（防抖合并拖拽），
//       标注沿用 Core AnnotationLayer 内建历史；标注工具激活/选中标注时 Ctrl+Z/Y 路由到标注撤销，否则走文档撤销。
//       预处理不纳入全局撤销（快照仅参数态，靠现有「重置预处理」回退）。
// ============================================================================
#pragma once

#include <QColor>
#include <QMainWindow>
#include <QPixmap>

#include <functional>

#include "engine/engine.h"
#include "history/history_manager.h"
#include "model/annotation_bridge.h"
#include "model/document.h"
#include "model/engine_bridge.h"
#include "util/preview_scaler.h"

class QLabel;
class QAction;
class QKeyEvent;
class QTabWidget;
class QTimer;

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
    void onLoadConfig();   // 文件→加载配置（G-13）：读 JSON → Document::applyEngineConfig（入栈可撤销）。
    void onSaveConfig();   // 文件→保存配置（G-13）：把当前 EngineConfig 写为 JSON。
    // 编辑菜单撤销/重做（G-12）：上下文路由——标注上下文走标注撤销，否则走文档参数撤销。
    void onUndo();
    void onRedo();
    // Document 信号响应。
    void onImageChanged();  // 换图：重建预览底图并刷新。
    void onDocChanged();    // 参数变更：刷新预览 + 同步面板 + 状态栏。
    void onRightTabChanged(); // 右侧选项卡切换：切到「导出」页且预览已脏时补渲染一次（A：不可见时不渲染）。
    // 全局撤销/重做（G-12）：文档参数态历史的防抖采集（连接 Document::changed 与定时器 timeout）。
    void scheduleHistoryCapture();  // 参数变更→重启防抖定时器（还原期间被 suppressHistory_ 抑制）。
    void onDocHistoryTimeout();     // 防抖到点：把当前 EngineConfig 快照压入历史并刷新启用态。
    // 画布交互响应。
    void onRubberSelect(const QRectF& sceneRect);   // 框选新建选区。
    void onSelectionEdited(const QRectF& sceneRect);// 拖动/缩放/拖边既有选区（拖边＝移动切割线）。
    void onMultiRectEdited(int index, const QRectF& sceneRect); // L2 多矩形：拖动/缩放第 index 个选区框。
    void onRectSelected(int index);                 // L2 多矩形：面板列表选中行→高亮画布对应选区框。
    void onCellToggled(int index);                  // L3 单击切换某单元。
    void onCellsMarquee(const std::vector<int>& indices); // L3 拖拽框选单元。
    void onCellReordered(int fromIndex, int toIndex); // L3 CUSTOM 拖拽调序。
    void onNudge(int dx, int dy);                   // 方向键微调选区。
    void onCursor(const QPointF& scenePos);         // 光标坐标/像素颜色回报。
    void onZoomChanged(qreal factor);               // 视图缩放倍数变化 → 更新状态栏放大倍数。
    void onClearCut();                              // 清除切割线（清选区）。
    void onToggleMasks();                           // 切换预览遮罩显隐。
    void onModeAction(int tierInt);                 // 工具栏/快捷键切换模式。
    void onPolarityShortcut(bool remove);           // K/R 快捷键切换极性。
    // 预处理（FR-1 / G-3）：各操作经 EngineBridge 调 Core pixel_ops 变换工作图后写回。
    void onRotate(int angleDeg);                    // 旋转 90 的整数倍（-90/90/180）。
    void onFlip(bool horizontal);                   // 水平/垂直翻转。
    void onScale(double factor);                    // 按比例缩放。
    void onResize(int newWidth, int newHeight);     // 目标尺寸缩放。
    void onGray();                                  // 黑白（灰度）。
    // 色道反色：invR/invG/invB 指示反相哪些通道（未反相的通道保持不变）。
    void onInvert(bool invR, bool invG, bool invB);
    void onSplit(bool keepR, bool keepG, bool keepB); // 色道分离（保留勾选通道）。
    void onResetPreprocess();                       // 重置预处理（恢复原图）。

    // ---- 标注（第四阶段 G-4/G-5）：面板/画布意图 → AnnotationBridge（调 Core）→ 画布重绘 ----
    void onToolSelected(AnnoTool tool);              // 工具面板：切换标注工具（先收笔再切）。
    void onAnnoBridgeChanged();                       // 标注列表/预览变化：重绘画布标注层 + 属性面板同步。
    void onAnnoPendingChanged();                     // 绘制拖拽预览（橡皮筋）变化：仅实时刷新预览图元。
    void onAnnoSelectionChanged();                   // 选中项变化：重绘高亮 + 属性面板同步。
    void onAnnoToolChanged();                        // 工具变化：同步工具面板 + 画布绘制态门控。
    void onAnnoDragStart(const QPointF& scenePos);   // 画布绘制手势起点（依工具分派两点/折线/画笔/文字）。
    void onAnnoDragMove(const QPointF& scenePos);    // 画布绘制手势拖拽（两点形状预览/画笔追点/折线橡皮筋）。
    void onAnnoDragEnd(const QPointF& scenePos);     // 画布绘制手势释放（提交两点形状/画笔）。
    void onAnnoHover(const QPointF& scenePos);       // 绘制态悬停（未按键）：折线实时预览落点与连线。
    void onAnnoFinish();                             // 绘制态右键：收笔折线（提交）或取消当前预览。
    void onAnnoEscape();                             // 绘制态 Esc：收笔折线或取消当前预览。
    void onAnnotationSelect(const QPointF& scenePos);// SELECT 工具下点中标注图元：Core hitTest 选中。
    void onAnnotationMoved(int index, double dx, double dy); // 拖动选中标注：Core 平移几何。
    void onAnnotationTransformed(int index, double sx, double sy, double rotateDeg); // 拖定向包围盒手柄：Core 缩放/旋转。
    void onAnnotationTransformPreview(int index, double sx, double sy, double rotateDeg); // 手柄拖拽中：预览值实时回显到属性面板。
    void onAnnoColorPicked(const QColor& c);         // 属性面板：颜色。
    void onAnnoStrokeChanged(int width);             // 属性面板：描边粗细。
    void onAnnoFillChanged(bool fill);               // 属性面板：填充开关。
    void onAnnoTextChanged(const QString& text);     // 属性面板：文字内容。
    void onAnnoFontSizeChanged(double size);         // 属性面板：字号。
    void onAnnoTransformApply(double sx, double sy, double rotateDeg); // 属性面板：缩放/旋转选中标注（Core 非破坏性变换）。
    void onBaseVisibilityChanged(bool visible);      // 图层面板：底图显隐（切画布 base 图元）。
    void onMaskVisibilityChanged(bool visible);      // 图层面板：遮罩（保留/删除预览）显隐。
    void onGridVisibilityChanged(bool visible);      // 图层面板：网格线显隐（重建落地）。
    void onCutLineVisibilityChanged(bool visible);   // 图层面板：切割线显隐。
    void onSelectionVisibilityChanged(bool visible); // 图层面板：选取边框显隐。
    void onAnnotationVisibilityChanged(bool visible);// 图层面板：标注图层显隐。
    void onBurnInChanged(bool on);                   // 图层面板：导出烧录开关。
    void onAnnoUndo();                               // 标注菜单：撤销（Core revoke）。
    void onAnnoRedo();                               // 标注菜单：重做（Core redo）。
    void onAnnoDeleteSelected();                     // 标注菜单/Delete 键：删除选中标注。
    void onAnnoClearAll();                           // 标注菜单：清除全部标注。

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
    // 依引擎结果刷新导出面板的输出预览缩略图（G-11 / §4.6）：缓存 res.ok/composition 后转 renderExportPreviewFromCache。
    // 导出选项卡不可见时不渲染（仅置脏标志，A），避免无谓开销；切回该页时由 onRightTabChanged 补渲染。
    void updateExportPreview(const idc::engine::EngineResult& res);
    // 用缓存的 lastComposition_/lastExportResOk_ 重渲染输出预览（不重跑 Core）：导出页可见时渲染、否则置脏。
    // 供烧录开关/标注变更等「不影响切割几何」的事件轻量刷新（B）与切回导出页时补渲染（A）共用。
    void renderExportPreviewFromCache();
    // 输出预览的源图（B）：默认返回未烧录的小源图 exportSrcPixmap_；若「导出时烧录标注」开启且有标注，
    // 则委托 util::bakeAnnotationsInto 把标注矢量烘焙到其副本上（渲染已下沉到 util，app 层只做编排）。
    QPixmap exportPreviewSource() const;
    // 预处理公共收尾：维度变化时清除失效选区（坐标基于旧尺寸），再写回工作图（触发 imageChanged）。
    void applyWorkingImage(idc::core::Image next, const QString& okMsg);
    // 在模态忙碌对话框（不可取消、阻断其余输入）内同步执行 op：用于缩放/尺寸等可能耗时的重采样，
    // 给用户明确「正在处理」提示并禁止期间误触其他操作（防御性编程，NFR 卡顿兜底）。
    void runWithBusyDialog(const QString& text, const std::function<void()>& op);
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

    // ---- 全局撤销/重做（G-12）：文档参数态历史（双历史之一；标注历史在 AnnotationBridge/Core）----
    void resetDocHistory();          // 清空历史并以当前态为唯一基线（构造/换图/载入配置后调用）。
    void undoDocument();             // 文档参数撤销：弹当前态到重做栈，还原新栈顶（上一态）。
    void redoDocument();             // 文档参数重做：从重做栈弹回并还原。
    void updateUndoRedoEnabled();    // 依文档历史深度与标注上下文刷新编辑菜单撤销/重做启用态。
    // 标注上下文判定：标注工具激活（非 SELECT）或存在选中标注时，Ctrl+Z/Y 路由到标注撤销/重做。
    bool annotationContextActive() const;

    // 状态与桥接。
    Document doc_;
    EngineBridge bridge_;
    AnnotationBridge annoBridge_;       // 标注域桥：唯一持有并驱动 Core AnnotationLayer（G-4/G-5）。
    PreviewImage preview_;            // 当前预览副本 + 放大系数
    // 输出预览（G-11 / §4.6）专用的小源图：由 rebuildPreviewPixmap 把底图再降到最长边 ≤ 512，
    // 缩略图只在这张小图上 blit——即便原图 8000×8000 也能快速出预览（像素少、速度快）。
    // exportSrcScale* 为 working（原图）→ 小源图 的放大系数，供把 Composition 的源区域映射到小图坐标。
    QPixmap exportSrcPixmap_;
    double exportSrcScaleX_{1.0};
    double exportSrcScaleY_{1.0};
    bool exportPreviewDirty_{false};   // 导出页不可见期间参数有变→置脏；切回该页时补渲染（A）。
    // 上次引擎结果的合成缓存：供烧录/标注变更时**只重渲染缩略图、不重跑 Core**（切割几何不受标注影响）。
    idc::engine::Composition lastComposition_;
    bool lastExportResOk_{false};
    // 预览最长边上限（NFR-3 降采样阈值）。取 4096：让绝大多数图片 1:1 显示，
    // 使放大后底图像素与精确整数坐标的切割线/遮罩对齐（仅超长边大图才降采样）。
    int previewMaxDim_{4096};
    // 重采样（缩放/尺寸）进行中标志：防止模态对话框期间的重入（如快捷键再次触发）。
    bool busyResample_{false};

    // ---- 全局撤销/重做（G-12）：文档参数态历史（复用 Core HistoryManager，A-0.10）----
    // 快照类型为 EngineConfig（与 G-13 配置同构）：capture=buildEngineConfig、restore=applyEngineConfig，
    // 栈顶恒为「当前态」；undo 弹栈顶到重做栈后还原新栈顶，redo 反之。仅快照参数态，不含图像/预处理。
    idc::history::HistoryManager<idc::engine::EngineConfig> docHistory_;
    QTimer* docHistoryTimer_{nullptr};  // 防抖定时器：把拖拽/连点的连续 changed() 合并为一条历史。
    bool suppressHistory_{false};       // 还原（undo/redo/applyEngineConfig）期间抑制历史采集，防回环污染。

    // 视图部件。
    CanvasScene* scene_{nullptr};
    CanvasView* view_{nullptr};
    LeftPanel* left_{nullptr};
    ParamPanel* param_{nullptr};
    ExportPanel* exportPanel_{nullptr};
    ImagePanel* imagePanel_{nullptr};   // 右侧「图像」页：预处理（FR-1）。
    ToolPanel* toolPanel_{nullptr};             // 左侧「标注工具」组（G-4）。
    LayerPanel* layerPanel_{nullptr};           // 左侧「图层」组（G-5）。
    AnnotationPropPanel* annoPropPanel_{nullptr}; // 右侧「标注」属性页（G-4）。
    QTabWidget* rightTabs_{nullptr};    // 右侧选项卡容器（参数/导出/图像/标注），供菜单定位到某页。

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

    // 编辑菜单撤销/重做动作（G-12）：提为成员以便依历史深度与标注上下文动态启/禁。
    QAction* aUndo_{nullptr};
    QAction* aRedo_{nullptr};
};

} // namespace idc::gui
