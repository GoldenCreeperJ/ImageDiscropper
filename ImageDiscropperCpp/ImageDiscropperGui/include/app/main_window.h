// ============================================================================
// 文件：app/main_window.h
// 作用：主窗口装配壳（布局见本目录 README「布局结构与响应式规则」）——经 MainWindowUi 装配
//       菜单栏、工具栏、状态栏、左侧面板、中央画布与右侧选项卡；实例化八个控制器
//       （预览 PreviewController / 预处理 PreprocessController / 标注 AnnotationCoordinator /
//        历史 DocHistory / 画布交互 CanvasCoordinator / 会话 SessionController /
//        导出 ExportController / 配置 ConfigController），并把各部件的信号槽串联为一条数据流：
//       用户交互 → Document（状态）→ EngineBridge（调 Core）→ CanvasScene（渲染）。
// 分块依据：
//   - MainWindow 只做「装配 + 编排 + 转发」（CONTRIBUTING.md「分层纪律」）；装配细节在 MainWindowUi，
//     各域编排在八个控制器（见 src/app/README.md「编排」），本文件只是装配壳，避免上帝文件；
//   - 构造拆为「骨架装配 → initCore 接线」两段：移动端 MobileShell 经保护构造
//     换装骨架（画布为主 + 底部工具条/抽屉），接线序列与桌面逐行共用（行为同构、零分支）；
//   - 保留在 MainWindow 的：Document 信号响应、状态栏回报、模式/极性动作、撤销/重做路由、
//     单键快捷键事件入口（分派在 KeyRouter）与同步壳（委托 MainWindowUi::syncUi）。
// 说明：公共面不变（main.cpp 仅依赖构造与 openImageFromPath）；覆盖打开图像/缩放平移、
//       三种模式的切割遮罩与选区/单元交互、分离/坍缩/重排导出、预处理、标注工具与图层管理、
//       全局撤销重做与配置加载/保存（机制详见各控制器头文件与 GUI README 关键设计决策）。
// ============================================================================
#pragma once

#include <vector>

#include <QMainWindow>

#include "app/controllers/key_router.h"
#include "canvas/canvas_mode.h"
#include "model/annotation_bridge.h"
#include "model/document.h"
#include "i18n/retranslate.h"

class QAction;
class QDockWidget;
class QKeyEvent;
class QMenu;
class QScrollArea;
class QSplitter;
class QTabWidget;
class QWidget;

namespace idc::gui {

class AccordionPanel;
class CanvasScene;
class CanvasView;
class LeftPanel;
class ParamPanel;
class ExportPanel;
class PreprocessPanel;
class ModeToolPanel;
class LayerPanel;
class LayerEditPanel;
class AnnotationPropPanel;
class StatusBar;
class PreviewController;
class PreprocessController;
class AnnotationCoordinator;
class DocHistory;
class CanvasCoordinator;
class SessionController;
class ExportController;
class ConfigController;
class MainWindowUi;

// ---------------------------------------------------------------------------
// MainWindow：应用主窗口装配壳与总编排转发。
// ---------------------------------------------------------------------------
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    // 桌面主窗构造：装配骨架（buildDesktopUi）→ 接线（initCore）。Document/AnnotationBridge/
    // DocHistory 均按值或子对象随窗口构造/析构；语言热切换经 retranslateUi 原地重放，窗口不销毁。
    explicit MainWindow(QWidget* parent = nullptr);

    // 从给定路径打开图像（供命令行首参自动打开复用；不弹文件对话框）。
    // 转发 SessionController::openImageFromPath（与菜单/工具栏「打开」共用同一套载入逻辑）。
    void openImageFromPath(const QString& path) const;

    // 语言热切换重放（原地只改文本、不重建窗口）：先按新偏好设全局语言，再重跑本窗与
    // 各面板登记的重放闭包，然后重测左右栏最小宽并重派生动态文本。见 app/main_window.cpp。
    void retranslateUi();

protected:
    // 移动端骨架标记：跳过桌面装配与标题/尺寸预设，
    // 由子类（mobile/ 的 MobileShell）自行回填部件后调 initCore 完成接线。
    struct MobileShellTag {};
    explicit MainWindow(MobileShellTag, QWidget* parent = nullptr);

    // 控制器接线与初始同步（构造后半段逐行原迁）：须在全部部件回填完毕后调用；
    // 顺序不变式维持：各控制器 connectSignals 与 connectAll 先于 history_->reset()（见 .cpp 注释）。
    void initCore();

    // 子类（MobileShell）向本窗骨架登记表追加自身重放闭包的入口（retranslator_ 对本类私有）。
    void addRetranslate(std::function<void()> apply) { retranslator_.add(std::move(apply)); }

    // 单键快捷键事件入口：分派逻辑在 KeyRouter（见 app/controllers/key_router.h），
    // 此处只转发（仍须为 QMainWindow 虚函数）。数值输入时天然不触发（NFR-5）。
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    // Document 信号响应。
    void onImageChanged() const;   // 换图：重建预览底图并刷新。
    void onDocChanged() const;     // 参数变更：刷新预览 + 同步面板（拖拽期跳过面板同步）。
    // 状态栏回报。
    void onCursor(const QPointF& scenePos) const;  // 光标坐标/像素颜色回报。
    void onZoomChanged(qreal factor) const;        // 视图缩放倍数变化 → 更新状态栏放大倍数。
    // 模式/极性（工具栏/菜单/快捷键共用；写回 Document）。
    void onModeAction(int tierInt);
    void onPolarityShortcut(bool remove);
    // 编辑菜单撤销/重做：上下文路由——标注上下文走标注撤销，否则走文档参数撤销。
    void onUndo() const;
    void onRedo() const;
    // 撤销/重做可用性回灌：刷新编辑菜单/工具栏动作启用态。
    void onHistoryAvailabilityChanged(bool canUndo, bool canRedo) const;

private:
    friend class MainWindowUi;  // 装配建造者直接回填部件指针并连接私有槽。
    friend class MobileShellUi; // 移动端骨架建造者（mobile/）同样直接回填 + 连私有槽（§8.3 行为同构）。
    friend class KeyRouter;     // 单键快捷键分派直达私有槽与控制器成员。

    void buildDesktopUi();     // 桌面骨架装配（MainWindowUi 四步，逐行原迁自原构造）。
    void connectAll();         // 剩余跨件串联（Document/状态栏回报/历史启用态/菜单勾选态回同步；
                               // 其余连接住在属主旁：画布交互在 CanvasCoordinator、图层编辑在
                               // SessionController、导出在 ExportController、画布右键遮罩在 PreviewController）。
    // 同步左侧面板与右侧「参数/导出/图像」页 + 菜单/工具栏动作到 Document（委托 MainWindowUi::syncUi）。
    void syncPanels() const;

    // 按当前语言实测左右栏内容自然宽并抬高最小宽（std::max(设计下限, sizeHint)，
    // 见 main_window_ui.cpp 说明）：语言热切换后文本长度变化，需重跑一次以免英文长标签截断。
    void applyPanelMinWidths();

    // 状态与桥接（单一真相源 + 唯一标注域入口）：按值持有，随窗口构造/析构。
    Document doc_;
    AnnotationBridge annoBridge_;       // 标注域桥：唯一持有并驱动 AnnotationEditor（其下组合 Core AnnotationSet，G-4/G-5）。

    // 本窗骨架（菜单/工具栏/状态栏初值/窗口标题）的重放闭包登记表；各面板另有自己的一份。
    i18n::Retranslator retranslator_;

    // 控制器（QObject 子对象，本窗口拥有）。
    PreviewController* preview_{nullptr};         // 预览刷新状态机 + 导出预览缓存。
    PreprocessController* preprocess_{nullptr};   // 预处理 8 操作编排。
    AnnotationCoordinator* annotation_{nullptr};  // 标注 38 槽编排 + 手势分派。
    DocHistory* history_{nullptr};                // 文档参数态撤销/重做（防抖/抑制/上下文）。
    CanvasCoordinator* canvas_{nullptr};          // 画布四态落地 + 选区/单元交互写回。
    SessionController* session_{nullptr};         // 文档会话生命周期 + 图层编辑面板接线。
    ExportController* export_{nullptr};           // 导出编排（含 Android 发布分支）。
    ConfigController* config_{nullptr};           // 配置保存/加载编排。

    KeyRouter keyRouter_;               // 单键快捷键分派（keyPressEvent 转发至此）。

    // 视图部件（MainWindowUi 装配回填）。
    CanvasScene* scene_{nullptr};
    CanvasView* view_{nullptr};
    LeftPanel* left_{nullptr};
    ParamPanel* param_{nullptr};
    ExportPanel* exportPanel_{nullptr};
    PreprocessPanel* preprocessPanel_{nullptr};   // 右侧「预处理」页（FR-1）。
    ModeToolPanel* toolPanel_{nullptr};             // 左侧「标注工具」组（G-4）。
    LayerPanel* layerPanel_{nullptr};           // 左侧「图层」组（G-5）。
    LayerEditPanel* layerEditPanel_{nullptr};   // 右侧「图片」页：图层序/落位/画布尺寸编辑（多层画布，无内置图层列表）。
    AnnotationPropPanel* annoPropPanel_{nullptr}; // 右侧「标注」属性页（G-4）。
    QWidget* rightTabs_{nullptr};       // 右侧多页容器：桌面＝AccordionPanel 折叠式（多节可同开+整栏滚动）/
                                        // 移动端＝QTabWidget 选择式；跨形态访问经双型分发。
    StatusBar* status_{nullptr};

    // 三栏测量与容器成员（供语言热切换后 applyPanelMinWidths 重测最小宽；桌面装配回填）。
    QWidget* leftContainer_{nullptr};   // 左栏内容容器（其 sizeHint 驱动左滚动区最小宽）。
    QScrollArea* leftScroll_{nullptr};
    QScrollArea* rightScroll_{nullptr};
    QSplitter* split_{nullptr};         // 三栏分隔器（重测后 setSizes 保持左/右栏初始宽）。
    AccordionPanel* accordion_{nullptr}; // 桌面右侧折叠栏（节标题重放取此指针；rightTabs_ 的同一对象）。
    // applyPanelMinWidths 上次为左/右栏自动设定的宽（-1=尚未设）：据此判断栏当前是否仍处
    // “自动宽”（用户未手动拖宽），从而允许内容变窄时回收（如退出标注绘制态后缩回）。
    int lastLeftAutoMinW_{-1};
    int lastRightAutoMinW_{-1};
    // 移动端抽屉「页滚动区」重测目标（MobileShellUi::buildDrawer 回填，桌面恒空）：applyPanelMinWidths
    // 按与桌面**同一落点**——把 max(设计下限, 页内容自然宽) 抬到每页 QScrollArea 的最小宽上（桌面抬在
    // leftScroll_ 并用 splitter setSizes 让栏宽真的变），使抽屉页区实际加宽、进标注绘制态时形状子行不致被
    // 横向 AlwaysOff 裁掉；随语言重放与进出绘制态重测（防英文长标签截断）。
    std::vector<QScrollArea*> drawerPageScrolls_;
    QDockWidget* drawer_{nullptr};   // 移动端抽屉本体（applyPanelMinWidths 用 resizeDocks 强制重分配，同桌面 split_->setSizes）。

    // 工具栏模式动作（与左侧面板同步）。
    QAction* modeActionL1_{nullptr};
    QAction* modeActionL2_{nullptr};
    QAction* modeActionL3_{nullptr};

    // 模式菜单/工具栏极性动作（保留框内/删除框内，与左侧面板同步）。
    QAction* aPolKeep_{nullptr};
    QAction* aPolRemove_{nullptr};

    // 模式菜单「画布模式」子菜单（画布四态互斥，与工具面板顶行按钮镜像）。
    QAction* aCanvasMarquee_{nullptr};
    QAction* aCanvasImage_{nullptr};
    QAction* aCanvasAnnoSel_{nullptr};
    QAction* aCanvasAnnoCreate_{nullptr};
    // 模式菜单切割参数（L1 形状 / L2 方式 / 转网格 / L3 排序）：子菜单按当前切割模式启停。
    QMenu* mL1Shape_{nullptr};
    QAction* aL1Rect_{nullptr};
    QAction* aL1HBand_{nullptr};
    QAction* aL1VBand_{nullptr};
    QMenu* mL2Sub_{nullptr};
    QAction* aL2Cross_{nullptr};
    QAction* aL2HLine_{nullptr};
    QAction* aL2VLine_{nullptr};
    QAction* aL2Multi_{nullptr};
    QAction* aToGrid_{nullptr};
    QMenu* mSort_{nullptr};
    QAction* aSortRow_{nullptr};
    QAction* aSortCol_{nullptr};
    QAction* aSortCustom_{nullptr};
    QAction* aSortReverse_{nullptr};
    QAction* aSortSnake_{nullptr};
    // 文件菜单「导出设置」（输出方式/格式/烧录开关，与导出面板镜像；面板为唯一真相源）。
    QAction* aEmitSep_{nullptr};
    QAction* aEmitCol_{nullptr};
    QAction* aEmitRear_{nullptr};
    QAction* aFmtPng_{nullptr};
    QAction* aFmtJpg_{nullptr};
    QAction* aFmtWebp_{nullptr};
    QAction* aFmtBmp_{nullptr};
    QAction* aBurnAnno_{nullptr};
    QAction* aBurnLayers_{nullptr};
    // 标注菜单「工具」子菜单动作：按 AnnoTool 枚举序索引（Select=0 无菜单项，由「标注选取」画布模式承担）。
    std::vector<QAction*> toolActions_;
    // 标注菜单「叠放次序」（作用选中标注；无选中时置灰）与「填充」开关（勾选态随选中/默认值回同步）。
    QAction* aZRaise_{nullptr};
    QAction* aZLower_{nullptr};
    QAction* aZFront_{nullptr};
    QAction* aZBack_{nullptr};
    QAction* aFill_{nullptr};
    // 图层菜单动作（作用画布当前拾取层；无拾取层时 syncPanels 置灰）。
    QAction* aLayerUp_{nullptr};
    QAction* aLayerDown_{nullptr};
    QAction* aLayerTop_{nullptr};
    QAction* aLayerBottom_{nullptr};
    QAction* aLayerRemove_{nullptr};
    // 图层菜单「锚点/枢轴」子菜单（作用当前拾取层落位；动作按 generate::ImageAnchor 枚举序索引，勾选态随拾取层回同步）。
    QMenu* mLayerAnchor_{nullptr};
    QMenu* mLayerPivot_{nullptr};
    std::vector<QAction*> layerAnchorActions_;
    std::vector<QAction*> layerPivotActions_;
    // 模式菜单「L3 余量」子菜单（丢弃残缺/保留残缺/补白；按 RemainderPolicy 枚举序索引，仅 L3 启用）。
    QMenu* mRemainder_{nullptr};
    std::vector<QAction*> remainderActions_;

    // 编辑菜单 L3 选择集动作：提为成员以便仅在 L3 模式时启用。
    QAction* aCellsAll_{nullptr};
    QAction* aCellsInvert_{nullptr};
    QAction* aCellsClear_{nullptr};

    // 编辑菜单撤销/重做动作：提为成员以便依历史深度与标注上下文动态启/禁。
    QAction* aUndo_{nullptr};
    QAction* aRedo_{nullptr};

    // 图像菜单「黑白」动作：提为成员以便队列已产出灰度时置灰（灰度启停智能，与预处理面板同源）。
    QAction* aGrayMenu_{nullptr};
    // 图像菜单「色道分离」子菜单：与黑白同源置灰（灰度图无通道可分，与预处理面板判定一致）。
    QMenu* mSplit_{nullptr};
    // 图像菜单「反色」子菜单入口：无图像时整入口置灰（与 mSplit_ 同款；灰度图仍可反色，不受灰度智能约束）。
    QMenu* mInvert_{nullptr};
    // 图像菜单预处理动作集合（旋转/翻转/黑白/反色/分离/重置）：无图像时统一置灰。
    std::vector<QAction*> preprocessActions_;
};

} // namespace idc::gui
