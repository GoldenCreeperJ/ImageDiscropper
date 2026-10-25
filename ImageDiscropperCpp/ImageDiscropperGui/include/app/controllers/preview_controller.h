// ============================================================================
// 文件：app/controllers/preview_controller.h
// 作用：PreviewController——切割预览与导出前输出预览的编排控制器：承担
//       refreshPreview 五分支状态机、预览底图重建与导出预览缓存/按需渲染（A/B 规则）。
// 分块依据：
//   - 「跑 Core 预览 → 刷画布/状态栏/面板」是应用主数据流的核心，独占一块；
//   - 导出预览缓存（lastComposition_/脏标志/小源图）只服务于本块。
// 说明：无信号输出，只被 MainWindow（重建/刷新）与 AnnotationCoordinator（B 规则重渲染、显隐槽刷新）调用；
//       Core 调用仍经 EngineBridge/AnnotationBridge（分层纪律）。
// ============================================================================
#pragma once

#include <QObject>
#include <QPixmap>

#include "core/image.h"
#include "engine/engine.h"

class QTabWidget;

namespace idc::gui {

class Document;
class CanvasScene;
class CanvasView;
class ExportPanel;
class ParamPanel;
class StatusBar;
class AnnotationBridge;
class LayerPanel;
class PreprocessController;

// ---------------------------------------------------------------------------
// PreviewController：预览刷新状态机 + 导出前输出预览缓存（A/B 按需渲染）。
// ---------------------------------------------------------------------------
class PreviewController : public QObject {
    Q_OBJECT
public:
    explicit PreviewController(QObject* parent = nullptr);

    // 依赖注入（setter 模式，与面板一致）。
    void setDocument(Document* doc);
    void setScene(CanvasScene* scene);
    void setView(CanvasView* view);                    // rebuildPreviewPixmap 末尾 fitToWindow
    void setExportPanel(ExportPanel* panel);
    void setParamPanel(ParamPanel* panel);             // setCollapseHint
    void setStatusBar(StatusBar* status);
    void setAnnotationBridge(AnnotationBridge* anno);  // 导出预览源图烧录判定
    void setMultiPageContainer(QWidget* tabs);       // A 规则：导出页可见性（AccordionPanel 或 QTabWidget / QStackedWidget）
    void setLayerPanel(LayerPanel* panel);           // 遮罩切换反向同步图层面板复选框
    void setPreprocessController(PreprocessController* preprocess);  // 解耦渲染：底图/图层像素分离合成
    void connectSignals();                             // rightTabs currentChanged / 画布右键遮罩切换

    void rebuildPreviewPixmap();         // 依工作图重建降采样预览底图 + 导出预览小源图
    void refreshPreview();               // 五分支状态机：跑 Core 预览并刷新画布/状态栏/面板
    void refreshExportPreviewFromCache(); // 用缓存重渲染输出预览（不重跑 Core；A 切回补渲染 / B 轻量刷新共用）
    void refreshExportSource();          // 按当前「烧录图片图层」开关重算导出预览小源图后重渲染（不重跑 Core 几何）

    // 预览重建时留存的「仅底图」全分辨率合成（不含叠加层）：图层显隐是纯预览态、working_ 恒含层，
    //   隐藏图层后状态栏取色须读本图才与屏幕所见一致（见 MainWindow::onCursor）；随 rebuildPreviewPixmap 刷新。
    const core::Image& displayBaseImage() const { return displayBaseImage_; }
    // 与 displayBaseImage 对称的「仅图层」全分辨率合成（图层于透明底、不含底图）：底图隐藏后状态栏取色
    //   须读本图（透明处即屏幕空白），否则会取到已被隐藏的底图像素（所见≠所取）。同样随重建刷新。
    const core::Image& displayLayersImage() const { return displayLayersImage_; }

public slots:
    void onToggleMasks();                // 切换预览遮罩显隐（视图菜单 / 画布右键）；同步图层面板复选框。

private slots:
    void onRightTabChanged();            // 切到「导出」页且预览已脏时补渲染

private:
    void updateExportPreview(const engine::EngineResult& res);  // 缓存 res 后转 refreshExportPreviewFromCache
    QPixmap exportPreviewSource() const;                        // 烧录开启且有标注时返回烘焙副本
    void rebuildExportSourcePixmap();                           // 依 layersBurnIn 从工作图/仅底图重算 exportSrcPixmap_ 与缩放

    Document* doc_{nullptr};
    CanvasScene* scene_{nullptr};
    CanvasView* view_{nullptr};
    ExportPanel* exportPanel_{nullptr};
    ParamPanel* param_{nullptr};
    StatusBar* status_{nullptr};
    AnnotationBridge* anno_{nullptr};
    LayerPanel* layerPanel_{nullptr};  // 遮罩切换反向同步（blockSignals 防回环）
    PreprocessController* preprocess_{nullptr};  // 解耦渲染：取底图/图层像素分离合成
    QWidget* rightTabs_{nullptr};      // 拥有者 MainWindow 的右侧多页容器（桌面 AccordionPanel / 移动 QStackedWidget）

    // 输出预览（导出前预览）专用的小源图：由 rebuildPreviewPixmap 把底图再降到最长边 ≤ 512，
    // 缩略图只在这张小图上 blit——即便原图 8000×8000 也能快速出预览（像素少、速度快）。
    // exportSrcScale* 为 working（原图）→ 小源图 的放大系数，供把 Composition 的源区域映射到小图坐标。
    QPixmap exportSrcPixmap_;
    double exportSrcScaleX_{1.0};
    double exportSrcScaleY_{1.0};
    // 与场景底图 item 同源的全分辨率「仅底图」合成：图层隐藏时状态栏取色的真相源（working_ 恒含叠加层，不可直接取）。
    core::Image displayBaseImage_;
    // 与场景图层覆盖 item 同源的全分辨率「仅图层」合成：底图隐藏时状态栏取色的真相源（透明像素即屏幕空白）。
    core::Image displayLayersImage_;
    bool exportPreviewDirty_{false};   // 导出页不可见期间参数有变→置脏；切回该页时补渲染（A）。
    // 上次引擎结果的合成缓存：供烧录/标注变更时**只重渲染缩略图、不重跑 Core**（切割几何不受标注影响）。
    engine::Composition lastComposition_;
    bool lastExportResOk_{false};
    // 上次 fitToWindow 时的输出尺寸：仅当工作图尺寸变化（换图 / resize）才复位缩放，
    // 轻编辑不改尺寸 → 保持用户当前缩放不被复位（需求③）。
    int lastFitW_{-1};
    int lastFitH_{-1};
    // 预览最长边上限（NFR-3 降采样阈值）。取 4096：让绝大多数图片 1:1 显示，
    // 使放大后底图像素与精确整数坐标的切割线/遮罩对齐（仅超长边大图才降采样）。
    int previewMaxDim_{4096};
};

} // namespace idc::gui
