// ============================================================================
// 文件：app/preview_controller.cpp
// 作用：实现 PreviewController（见同名头文件说明）。
// 分块依据：
//   - rebuildPreviewPixmap：预览底图 + 导出预览小源图重建；
//   - refreshPreview：五分支状态机（无图 / L3 网格 / L2 多矩形 / 无选区 / 单选区）；
//   - 导出预览缓存与按需渲染：updateExportPreview → refreshExportPreviewFromCache（A/B 规则）。
// ============================================================================
#include "app/preview_controller.h"

#include <algorithm>

#include <QTabWidget>

#include "app/status_bar.h"
#include "canvas/canvas_scene.h"
#include "canvas/canvas_view.h"
#include "model/annotation_bridge.h"
#include "model/document.h"
#include "model/engine_bridge.h"
#include "panels/export_panel.h"
#include "panels/param_panel.h"
#include "util/image_qt_adapter.h"
#include "util/output_preview_renderer.h"
#include "util/preview_scaler.h"

namespace idc::gui {

// 输出预览（导出前预览）尺寸参数：
//   kExportSrcMaxDim   —— 预览专用小源图的最长边上限（缩略图只在此小图上 blit，保证快）。
//   kExportPreviewMaxDim —— 输出缩略图的最长边上限（像素少、仅示意，与面板标签框相区隔）。
constexpr int kExportSrcMaxDim = 512;
constexpr int kExportPreviewMaxDim = 192;

PreviewController::PreviewController(QObject* parent) : QObject(parent) {}

void PreviewController::setDocument(Document* doc) { doc_ = doc; }
void PreviewController::setScene(CanvasScene* scene) { scene_ = scene; }
void PreviewController::setView(CanvasView* view) { view_ = view; }
void PreviewController::setExportPanel(ExportPanel* panel) { exportPanel_ = panel; }
void PreviewController::setParamPanel(ParamPanel* panel) { param_ = panel; }
void PreviewController::setStatusBar(StatusBar* status) { status_ = status; }
void PreviewController::setAnnotationBridge(AnnotationBridge* anno) { anno_ = anno; }
void PreviewController::setTabWidget(QTabWidget* tabs) { rightTabs_ = tabs; }

// 切到「导出」页时补渲染输出预览（A：不可见时不渲染，切回时若已脏则重算）。
void PreviewController::connectSignals() {
    connect(rightTabs_, &QTabWidget::currentChanged, this, &PreviewController::onRightTabChanged);
}

// 依工作图重建降采样预览底图（NFR-3）。
void PreviewController::rebuildPreviewPixmap() {
    if (!doc_->hasImage()) {
        scene_->clearAll();
        exportSrcPixmap_ = QPixmap();   // 无图像：清空输出预览小源图。
        return;
    }
    const auto [image, scaleX, scaleY] = makePreview(doc_->working(), previewMaxDim_);
    const QPixmap pm = toPixmap(image);
    // 场景坐标 = 原图像素坐标；底图用放大系数把预览 pixmap 铺到原图尺寸。
    scene_->setBaseImage(pm, scaleX, scaleY, doc_->width(), doc_->height());
    // 输出预览专用小源图：把底图再降到最长边 ≤ kExportSrcMaxDim，使缩略图只在小图上 blit。
    const int longest = std::max(pm.width(), pm.height());
    exportSrcPixmap_ = longest > kExportSrcMaxDim
        ? pm.scaled(kExportSrcMaxDim, kExportSrcMaxDim, Qt::KeepAspectRatio, Qt::SmoothTransformation)
        : pm;
    // working（原图）→ 小源图 的放大系数（供 util::composeOutputThumbnail 把 Composition 源区域映射到小图坐标）。
    exportSrcScaleX_ = exportSrcPixmap_.width()  > 0 ? static_cast<double>(doc_->width())  / exportSrcPixmap_.width()  : 1.0;
    exportSrcScaleY_ = exportSrcPixmap_.height() > 0 ? static_cast<double>(doc_->height()) / exportSrcPixmap_.height() : 1.0;
    view_->fitToWindow();
}

// 跑 Core 预览并刷新画布与状态（CONTRIBUTING.md「分层纪律」 实时预览）。
void PreviewController::refreshPreview() {
    status_->setModeTier(doc_->mode());
    // 重排为 L3 专属：先把重排上下文清零（非 L3 保持 0），L3 分支再回灌真实保留块数/格尺寸。
    exportPanel_->setRearrangeContext(0, 0, 0);

    // 无图像：清空叠加层。
    if (!doc_->hasImage()) {
        scene_->clearCutLines();
        scene_->clearGrid();
        scene_->clearMultiRects();
        scene_->updateMasks(engine::EngineResult{});
        updateExportPreview(engine::EngineResult{});
        status_->setCount(0);
        status_->setHint(QStringLiteral("请打开图像"));
        exportPanel_->setPreviewInfo(QStringLiteral("尚未加载图像。"));
        return;
    }

    // L3 网格分割：网格由「基准点 + 单元尺寸 + 余量策略」定义，不依赖选区矩形，
    // 故在选区判定之前单独处理。网格线交给 GridLayer 画灰色虚线；橙色选区框在 L3 隐藏，
    // 单元点选改由 CellPickerItem 承担（见下方 updateCellSelection）。遮罩仍复用 runEngine 的 kept 结果（红底 + 绿块）。
    if (doc_->mode() == engine::Tier::L3) {
        const engine::EngineConfig cfg = doc_->buildEngineConfig();
        // 依 Core 网格产出刷新网格线，并回灌派生行列数（供参数面板只读显示）。
        const engine::Grid grid = EngineBridge::buildGrid(cfg.cut, cfg.source);
        doc_->setDerivedGridSize(grid.rowCount(), grid.colCount());
        scene_->updateGrid(grid, true);
        scene_->clearCutLines();                   // L3 不画 L1/L2 的橙色贯穿切割线。
        scene_->clearMultiRects();                 // L3 不显示 L2 多矩形轮廓。
        scene_->syncSelection(doc_->rect(), false); // 隐藏橙色选区框。
        // 把当前选择集与排序策略下发给点选图元（高亮已选单元）。
        scene_->updateCellSelection(doc_->selectedCells(), doc_->order().strategy);

        const engine::EngineResult res = EngineBridge::runPreview(doc_->working(), cfg);
        scene_->updateMasks(res);
        updateExportPreview(res);

        const int kept = res.ok ? static_cast<int>(res.kept.size()) : 0;
        status_->setCount(kept);
        status_->setHint(res.ok ? QStringLiteral("就绪") : QString::fromStdString(res.error));
        exportPanel_->setCollapsible(res.collapsible);
        // 回灌重排上下文：保留块数 + 网格单元尺寸（供自动 cols/rows 与画布/单元尺寸警告）。
        exportPanel_->setRearrangeContext(kept, doc_->gridParams().cellWidth, doc_->gridParams().cellHeight);
        if (res.ok) {
            exportPanel_->setPreviewInfo(QStringLiteral("网格: %1×%2 · 输出画布 %3×%4 · 保留 %5 块")
                .arg(grid.rowCount()).arg(grid.colCount())
                .arg(res.composition.canvasWidth).arg(res.composition.canvasHeight).arg(kept));
        } else {
            exportPanel_->setPreviewInfo(QStringLiteral("无有效结果：%1").arg(QString::fromStdString(res.error)));
        }
        return;
    }

    // 非 L3：清空网格线（若从 L3 切回）。
    scene_->clearGrid();

    // L2 多矩形并集剔除：选区来自 rects_（非单 rect_），单独处理。
    // 用 Core 诱导网格画灰色网格线（各矩形十字带并集诱导），但不启动单元点选图元——
    // picker 会 grab 鼠标、阻断画布框选（多矩形靠框选逐个追加）。橙色轮廓标出各矩形，
    // 单选区框隐藏。遮罩复用 runPreview 的 kept 结果（CONTRIBUTING.md「分层纪律」：GUI 不算并集）。
    if (doc_->mode() == engine::Tier::L2 && doc_->l2Sub() == L2Sub::MULTI_RECT) {
        scene_->updateMultiRects(doc_->rects());     // 增量刷新可拖拽选区框（拖拽中不回设正在拖者）。
        scene_->clearCutLines();
        scene_->syncSelection(doc_->rect(), false);  // 隐藏单选区框（改用多矩形轮廓）。
        if (doc_->rects().empty()) {
            // 尚无矩形：不跑引擎（Core 对空 rects 的 MULTI_RECT 会报错），提示框选追加。
            scene_->updateMultiRectCutLines(engine::Grid{}, false);
            scene_->updateMasks(engine::EngineResult{});
            updateExportPreview(engine::EngineResult{});
            status_->setCount(0);
            status_->setHint(QStringLiteral("在画布上拖拽以追加矩形"));
            exportPanel_->setPreviewInfo(QStringLiteral("等待矩形…"));
            return;
        }
        const engine::EngineConfig cfg = doc_->buildEngineConfig();
        const engine::Grid grid = EngineBridge::buildGrid(cfg.cut, cfg.source);
        scene_->updateMultiRectCutLines(grid, true);
        const engine::EngineResult res = EngineBridge::runPreview(doc_->working(), cfg);
        scene_->updateMasks(res);
        updateExportPreview(res);

        const int kept = res.ok ? static_cast<int>(res.kept.size()) : 0;
        status_->setCount(kept);
        status_->setHint(res.ok ? QStringLiteral("就绪") : QString::fromStdString(res.error));
        param_->setCollapseHint(res.collapsible, res.ok ? QString() : QString::fromStdString(res.error));
        exportPanel_->setCollapsible(res.collapsible);
        if (res.ok) {
            exportPanel_->setPreviewInfo(QStringLiteral("多矩形: %1 个 · 输出画布 %2×%3 · 保留 %4 块")
                .arg(static_cast<int>(doc_->rects().size()))
                .arg(res.composition.canvasWidth).arg(res.composition.canvasHeight).arg(kept));
        } else {
            exportPanel_->setPreviewInfo(QStringLiteral("无有效结果：%1").arg(QString::fromStdString(res.error)));
        }
        return;
    }

    // 非多矩形：清空多矩形轮廓（从 MULTI_RECT 切回其他子功能/模式时）。
    scene_->clearMultiRects();

    // 有图但无选区：提示用户拖拽创建选区（不跑引擎，避免 E-1 噪声）。
    if (!doc_->hasRect()) {
        scene_->clearCutLines();
        scene_->updateMasks(engine::EngineResult{});
        updateExportPreview(engine::EngineResult{});
        scene_->syncSelection(doc_->rect(), false);
        status_->setCount(0);
        status_->setHint(QStringLiteral("在画布上拖拽以创建选区"));
        exportPanel_->setPreviewInfo(QStringLiteral("等待选区…"));
        return;
    }

    // 组装配置 → 调 Core 预览（仅区域数学，实时）。
    const engine::EngineConfig cfg = doc_->buildEngineConfig();
    const engine::EngineResult res = EngineBridge::runPreview(doc_->working(), cfg);

    // 切割线来自 Core generateCutLines（GUI 不自算几何）；场景据此判定选区哪几条边有贯穿切割线，
    // 下发给选区框绘制为橙色贯穿线——切割线即选区边的延伸，拖动选区边＝移动切割线（同一图元）。
    scene_->updateCutLines(EngineBridge::cutLines(cfg.cut, cfg.source), doc_->rect());
    scene_->updateMasks(res);
    updateExportPreview(res);
    // 拖拽进行中不用 Document 的取整矩形回设选区框：它已被手势精确定位（qreal 亚像素），
    // 逐帧回设会带来整数量化抖动 + 覆盖全图 boundingRect 的 prepareGeometryChange/update 卡顿；
    // 释放时（已退出拖拽态）会照常回设，保证最终与 Document 一致。
    if (!scene_->isDraggingSelection()) scene_->syncSelection(doc_->rect(), true);

    // 状态栏与面板提示。
    const int kept = res.ok ? static_cast<int>(res.kept.size()) : 0;
    status_->setCount(kept);
    status_->setHint(res.ok ? QStringLiteral("就绪") : QString::fromStdString(res.error));

    param_->setCollapseHint(res.collapsible, res.ok ? QString() : QString::fromStdString(res.error));
    exportPanel_->setCollapsible(res.collapsible);
    if (res.ok) {
        exportPanel_->setPreviewInfo(QStringLiteral("输出画布: %1×%2 · 保留 %3 块 · %4")
            .arg(res.composition.canvasWidth)
            .arg(res.composition.canvasHeight)
            .arg(kept)
            .arg(res.collapsible ? QStringLiteral("可坍缩") : QStringLiteral("需重排")));
    } else {
        exportPanel_->setPreviewInfo(QStringLiteral("无有效结果：%1").arg(QString::fromStdString(res.error)));
    }
}

// 依引擎结果刷新导出面板的输出预览缩略图（导出前预览）：先缓存 res.ok/composition，再转 refreshExportPreviewFromCache。
// 缓存让后续「不影响切割几何」的事件（烧录开关/标注变更）只需重渲染缩略图、无需重跑 Core。
void PreviewController::updateExportPreview(const engine::EngineResult& res) {
    lastExportResOk_ = res.ok;
    lastComposition_ = res.composition;   // 拷贝 placements（相对 Core 区域计算是小头），供轻量重渲染复用。
    refreshExportPreviewFromCache();
}

// 用缓存的 lastComposition_/lastExportResOk_ 重渲染输出预览（不重跑 Core）。
// A：导出选项卡不可见时只置脏标志，切回该页由 onRightTabChanged 调本函数补渲染，省去后台无谓开销。
void PreviewController::refreshExportPreviewFromCache() {
    if (rightTabs_ && rightTabs_->currentWidget() != exportPanel_) { exportPreviewDirty_ = true; return; }
    exportPreviewDirty_ = false;
    if (!lastExportResOk_) { exportPanel_->setPreviewPixmap(QPixmap()); return; }
    exportPanel_->setPreviewPixmap(composeOutputThumbnail(
        lastComposition_, exportPreviewSource(), 1.0 / exportSrcScaleX_, 1.0 / exportSrcScaleY_,
        kExportPreviewMaxDim));
}

// 右侧选项卡切换：切到「导出」页且预览已脏时用缓存补渲染一次（不重跑 Core）。
void PreviewController::onRightTabChanged() {
    if (rightTabs_ && rightTabs_->currentWidget() == exportPanel_ && exportPreviewDirty_) refreshExportPreviewFromCache();
}

// 输出预览的源图（B）：默认用未烧录的小源图；若「导出时烧录标注」开启且有标注，则委托 util::bakeAnnotationsInto
// 把各标注按世界路径矢量烘焙到小源图副本上（working→小图变换），使预览与最终「烧录后随像素被切割落位」一致。
// 渲染逻辑已下沉到 util（无状态），本方法只做「是否烧录 + 取哪张源图」的编排（app 层只编排，CONTRIBUTING.md「分层纪律」）。
QPixmap PreviewController::exportPreviewSource() const {
    if (exportSrcPixmap_.isNull()) return exportSrcPixmap_;
    if (!anno_->burnInEnabled() || anno_->count() == 0) return exportSrcPixmap_;
    return bakeAnnotationsInto(exportSrcPixmap_, 1.0 / exportSrcScaleX_, 1.0 / exportSrcScaleY_,
                               anno_->annotations());
}

} // namespace idc::gui
