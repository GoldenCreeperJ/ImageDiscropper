// ============================================================================
// 文件：app/preview_controller.h
// 作用：PreviewController——切割预览与导出前输出预览的编排控制器：从 MainWindow 拆出
//       refreshPreview 五分支状态机、预览底图重建与导出预览缓存/按需渲染（A/B 规则）。
// 分块依据：
//   - 「跑 Core 预览 → 刷画布/状态栏/面板」是应用主数据流的核心，独占一块（原 refreshPreview 143 行）；
//   - 导出预览缓存（lastComposition_/脏标志/小源图）只服务于本块，随迁。
// 说明：无信号输出，只被 MainWindow（重建/刷新）与 AnnotationCoordinator（B 规则重渲染、显隐槽刷新）调用；
//       Core 调用仍经 EngineBridge/AnnotationBridge（分层纪律）。
// ============================================================================
#pragma once

#include <QObject>
#include <QPixmap>

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
    void setMultiPageContainer(QWidget* tabs);       // A 规则：导出页可见性（AccordionPanel 或 QTabWidget）
    void connectSignals();                             // rightTabs currentChanged → onRightTabChanged

    void rebuildPreviewPixmap();         // 依工作图重建降采样预览底图 + 导出预览小源图
    void refreshPreview();               // 五分支状态机：跑 Core 预览并刷新画布/状态栏/面板
    void refreshExportPreviewFromCache(); // 用缓存重渲染输出预览（不重跑 Core；A 切回补渲染 / B 轻量刷新共用）

private slots:
    void onRightTabChanged();            // 切到「导出」页且预览已脏时补渲染

private:
    void updateExportPreview(const engine::EngineResult& res);  // 缓存 res 后转 refreshExportPreviewFromCache
    QPixmap exportPreviewSource() const;                        // 烧录开启且有标注时返回烘焙副本

    Document* doc_{nullptr};
    CanvasScene* scene_{nullptr};
    CanvasView* view_{nullptr};
    ExportPanel* exportPanel_{nullptr};
    ParamPanel* param_{nullptr};
    StatusBar* status_{nullptr};
    AnnotationBridge* anno_{nullptr};
    QWidget* rightTabs_{nullptr};      // 拥有者 MainWindow 的右侧多页容器（桌面 QToolBox / 移动 QTabWidget）

    // 输出预览（导出前预览）专用的小源图：由 rebuildPreviewPixmap 把底图再降到最长边 ≤ 512，
    // 缩略图只在这张小图上 blit——即便原图 8000×8000 也能快速出预览（像素少、速度快）。
    // exportSrcScale* 为 working（原图）→ 小源图 的放大系数，供把 Composition 的源区域映射到小图坐标。
    QPixmap exportSrcPixmap_;
    double exportSrcScaleX_{1.0};
    double exportSrcScaleY_{1.0};
    bool exportPreviewDirty_{false};   // 导出页不可见期间参数有变→置脏；切回该页时补渲染（A）。
    // 上次引擎结果的合成缓存：供烧录/标注变更时**只重渲染缩略图、不重跑 Core**（切割几何不受标注影响）。
    engine::Composition lastComposition_;
    bool lastExportResOk_{false};
    // 预览最长边上限（NFR-3 降采样阈值）。取 4096：让绝大多数图片 1:1 显示，
    // 使放大后底图像素与精确整数坐标的切割线/遮罩对齐（仅超长边大图才降采样）。
    int previewMaxDim_{4096};
};

} // namespace idc::gui
