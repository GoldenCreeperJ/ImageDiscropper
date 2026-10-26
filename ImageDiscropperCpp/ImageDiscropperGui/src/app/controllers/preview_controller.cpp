// ============================================================================
// 文件：app/controllers/preview_controller.cpp
// 作用：实现 PreviewController（见同名头文件说明）。
// 分块依据：
//   - rebuildPreviewPixmap：预览底图 + 导出预览小源图重建；
//   - refreshPreview：五分支状态机（无图 / L3 网格 / L2 多矩形 / 无选区 / 单选区）；
//   - 导出预览缓存与按需渲染：updateExportPreview → refreshExportPreviewFromCache（A/B 规则）。
// ============================================================================
#include "app/controllers/preview_controller.h"

#include "app/controllers/preprocess_controller.h"

#include <algorithm>
#include <utility>

#include <QStackedWidget>
#include <QTabWidget>

#include "panels/accordion_panel.h"

#include "app/status_bar.h"
#include "canvas/canvas_scene.h"
#include "canvas/canvas_view.h"
#include "model/annotation_bridge.h"
#include "model/document.h"
#include "model/engine_bridge.h"
#include "panels/export_panel.h"
#include "panels/layer_panel.h"
#include "panels/param_panel.h"
#include "i18n/gui_texts.h"
#include "i18n/gui_i18n.h"
#include "util/image_qt_adapter.h"
#include "util/output_preview_renderer.h"
#include "util/preview_scaler.h"

namespace idc::gui {

// 输出预览（导出前预览）尺寸参数：
//   kExportSrcMaxDim   —— 预览专用小源图的最长边上限（缩略图只在此小图上 blit，保证快）。
//   kExportPreviewMaxDim —— 输出缩略图的最长边上限（像素少、仅示意，与面板标签框相区隔）。
constexpr int kExportSrcMaxDim = 512;
constexpr int kExportPreviewMaxDim = 192;

// 「导出」页是否正在展示（多页容器多型分发）：
//   · 桌面 AccordionPanel——折叠式多节可同开，「展示」＝该节展开；
//   · 移动端 QStackedWidget（抽屉为横向可滑按钮条 + QStackedWidget 页区；本函数对 QTabWidget 亦同口径兼容）——页面外套 QScrollArea，
//     页面 widget 是 exportPanel_ 的祖先容器而非其本身，故相等比较放宽为「位于当前页内」（面板即直接页面时等价）。
bool showingExportPage(QWidget* container, const QWidget* exportPanel) {
    if (!container || !exportPanel) return false;
    if (const auto* acc = qobject_cast<AccordionPanel*>(container)) return acc->isSectionExpanded(exportPanel);
    // 多页容器（QTabWidget / QStackedWidget）均有 currentWidget()，祖先上溯比较当前页同口径。
    const QWidget* page = nullptr;
    if (const auto* tabs = qobject_cast<QTabWidget*>(container)) page = tabs->currentWidget();
    else if (const auto* stack = qobject_cast<QStackedWidget*>(container)) page = stack->currentWidget();
    if (page) {
        for (const QWidget* x = exportPanel; x; x = x->parentWidget())
            if (x == page) return true;
    }
    return false;
}

PreviewController::PreviewController(QObject* parent) : QObject(parent) {}

void PreviewController::setDocument(Document* doc) { doc_ = doc; }
void PreviewController::setScene(CanvasScene* scene) { scene_ = scene; }
void PreviewController::setView(CanvasView* view) { view_ = view; }
void PreviewController::setExportPanel(ExportPanel* panel) { exportPanel_ = panel; }
void PreviewController::setParamPanel(ParamPanel* panel) { param_ = panel; }
void PreviewController::setStatusBar(StatusBar* status) { status_ = status; }
void PreviewController::setAnnotationBridge(AnnotationBridge* anno) { anno_ = anno; }
void PreviewController::setMultiPageContainer(QWidget* tabs) { rightTabs_ = tabs; }
void PreviewController::setLayerPanel(LayerPanel* panel) { layerPanel_ = panel; }
void PreviewController::setPreprocessController(PreprocessController* preprocess) { preprocess_ = preprocess; }

// 切到「导出」页时补渲染输出预览（A：不可见时不渲染，切回时若已脏则重算）。
// 多型分发：桌面 AccordionPanel::sectionToggled / 移动端 QStackedWidget::currentChanged 各自接线（另兼容 QTabWidget）。
// 画布右键「切换预览遮罩」也在此接线（同属预览域）。
void PreviewController::connectSignals() {
    if (const auto* acc = qobject_cast<AccordionPanel*>(rightTabs_))
        connect(acc, &AccordionPanel::sectionToggled, this, &PreviewController::onRightTabChanged);
    else if (const auto* tabs = qobject_cast<QTabWidget*>(rightTabs_))
        connect(tabs, &QTabWidget::currentChanged, this, &PreviewController::onRightTabChanged);
    else if (const auto* stack = qobject_cast<QStackedWidget*>(rightTabs_))
        connect(stack, &QStackedWidget::currentChanged, this, &PreviewController::onRightTabChanged);
    if (view_) connect(view_, &CanvasView::toggleMasksRequested, this, &PreviewController::onToggleMasks);
}

// 切换预览遮罩显隐（画布右键）：本方法直接落地场景，再静默镜像图层面板的遮罩复选框（不走前向驱动，防重复落地）。
void PreviewController::onToggleMasks() {
    if (!scene_ || !layerPanel_) return;
    const bool v = !scene_->masksVisible();
    scene_->setMasksVisible(v);
    layerPanel_->syncMaskVisible(v);   // 反向同步面板勾选态（blockSignals 防回环）。
    refreshPreview();
    status_->notify(v ? IDC_GTR(status, kMaskShown)
                      : IDC_GTR(status, kMaskHidden), false);
}

// 依工作图重建降采样预览底图（NFR-3）。
// 解耦渲染：底图（仅底图形态）与图片图层（图层于透明底）各作**独立像素图元**叠放，图层显隐与底图显隐互不连带；
//   工作图（含图层的引擎真相）不被显示态污染。输出预览小源图另按「烧录图片图层」开关现算（与显示正交）。
void PreviewController::rebuildPreviewPixmap() {
    if (!doc_->hasImage()) {
        scene_->clearAll();
        exportSrcPixmap_ = QPixmap();   // 无图像：清空输出预览小源图。
        displayBaseImage_ = core::Image{};    // 无底图可取：清空取色真相源。
        displayBaseValid_ = false;
        displayLayersImage_ = core::Image{};  // 无图层可取：清空取色真相源（与底图侧对称）。
        displayLayersValid_ = false;
        lastFitW_ = lastFitH_ = -1;     // 清空后复位尺寸追踪，下次载入必然 fit。
        return;
    }
    // core::Image → 预览 pixmap：最长边在 previewMaxDim_ 内直接转换（免整幅深拷贝），仅超长边大图才降采样（需求③）。
    // 返回预览→原图放大系数 sx/sy（底图与图层覆盖同尺寸画布，坐标对齐）。
    auto toPreview = [this](const core::Image& img, double& sx, double& sy) -> QPixmap {
        sx = sy = 1.0;
        if (img.empty()) return QPixmap();
        if (previewMaxDim_ > 0 && std::max(img.width(), img.height()) > previewMaxDim_) {
            const auto [image, x, y] = makePreview(img, previewMaxDim_);
            sx = x;
            sy = y;
            return toPixmap(image);
        }
        return toPixmap(img);
    };

    // 底图：非图层画布→working_（全量即底图）；图层画布→仅底图合成（不含叠加层，故隐藏底图时图层覆盖不受牵连）。
    const core::Image baseOnly = doc_->hasLayers() ? preprocess_->composeLayerCanvasForExport(false) : doc_->working();
    double bsx = 1.0, bsy = 1.0;
    // 场景坐标 = 原图像素坐标；底图用放大系数把预览 pixmap 铺到原图尺寸。
    scene_->setBaseImage(toPreview(baseOnly, bsx, bsy), bsx, bsy, doc_->width(), doc_->height());
    // 取色真相源不再常驻留存：失效惰性缓存（仅在「图层隐藏 + 状态栏取色」时按需物化，见 displayBaseImage()）。
    displayBaseImage_ = core::Image{};
    displayBaseValid_ = false;

    // 图层覆盖：图层于透明底合成（非图层画布返回空 → 清空覆盖）；可见性由场景 imageLayersVisible 落地（纯显示切换）。
    double osx = 1.0, osy = 1.0;
    const core::Image layers = doc_->hasLayers() ? preprocess_->composeLayersForDisplay() : core::Image{};
    scene_->setImageLayerOverlay(toPreview(layers, osx, osy), osx, osy, doc_->width(), doc_->height());
    // 同上：取色真相源惰性化（仅在「底图隐藏 + 状态栏取色」时按需物化，见 displayLayersImage()）。
    displayLayersImage_ = core::Image{};
    displayLayersValid_ = false;

    // 输出预览专用小源图：按当前「烧录图片图层」决定含层/仅底图（与显示正交）。
    rebuildExportSourcePixmap();

    // 仅当输出尺寸变化（换图 / resize）才 fitToWindow 复位缩放；轻编辑尺寸不变则保持用户当前缩放（需求③）。
    if (const int ow = doc_->width(), oh = doc_->height(); ow != lastFitW_ || oh != lastFitH_) {
        view_->fitToWindow();
        lastFitW_ = ow;
        lastFitH_ = oh;
    }

    // 多层画布：刷新各图片图层的落位框（渲染于图层像素覆盖之上、标注之下；拾取拖拽的目标）。
    // 落位几何由 Core canvas::layerPlacement 现算（此处按当前图层栈 + 画布尺寸 + 选中层下发）；
    // canvasGeom＝画布级有效管线（[resize?]+轻队列）：落位框需经其几何步正向映射到工作图坐标系，与底图/图层覆盖对齐。
    // 恒按图层栈重建落位框（不随 imageLayersVisible 增删）；其可见性由场景 setImageLayersVisible 统一切换。
    if (doc_->hasLayers()) {
        const auto canvasGeom = PreprocessSession::makeEffective(doc_->resizeTarget(), doc_->steps());
        scene_->updateImageLayers(doc_->layerStack(), doc_->canvasWidth(), doc_->canvasHeight(),
                                  canvasGeom, doc_->selectedLayerId());
    } else {
        scene_->clearImageLayers();
    }
    // 新图/重建后把场景图层显隐旗标回同步到 Document（换图默认可见），保证图层像素覆盖与落位框可见性一致。
    scene_->setImageLayersVisible(doc_->imageLayersVisible());
}

// 「仅底图」取色真相源（惰性，见头文件）：无叠加层时底图形态即工作图（零物化直取）；
//   有叠加层时首次取色现算并缓存，随 rebuildPreviewPixmap 失效清空。
const core::Image& PreviewController::displayBaseImage() {
    static const core::Image kEmpty;
    if (!doc_ || !doc_->hasImage()) return kEmpty;
    if (doc_->layerStack().empty()) return doc_->working();
    if (!displayBaseValid_) {
        displayBaseImage_ = preprocess_ ? preprocess_->composeLayerCanvasForExport(false) : core::Image{};
        displayBaseValid_ = true;
    }
    return displayBaseImage_;
}

// 「仅图层」取色真相源（惰性，见头文件）：无叠加层恒空图；否则首次取色现算并缓存、随重建失效。
const core::Image& PreviewController::displayLayersImage() {
    static const core::Image kEmpty;
    if (!doc_ || !preprocess_ || doc_->layerStack().empty()) return kEmpty;
    if (!displayLayersValid_) {
        displayLayersImage_ = preprocess_->composeLayersForDisplay();
        displayLayersValid_ = true;
    }
    return displayLayersImage_;
}

// 依当前「烧录图片图层」开关重算导出预览小源图（含层→复用 working_；不含→仅底图合成）并降到 ≤ kExportSrcMaxDim。
void PreviewController::rebuildExportSourcePixmap() {
    if (!doc_->hasImage()) { exportSrcPixmap_ = QPixmap(); return; }
    // 烧录开：working_ 即全量合成，直接复用；烧录关（且图层画布）：现算仅底图形态。
    const core::Image burnSrc = doc_->hasLayers() && !doc_->layersBurnIn()
                                    ? preprocess_->composeLayerCanvasForExport(false)
                                    : doc_->working();
    // 降到最长边 ≤ kExportSrcMaxDim（makePreview 内置不超阈原样返回），缩略图只在这张小图上 blit。
    exportSrcPixmap_ = toPixmap(makePreview(burnSrc, kExportSrcMaxDim).image);
    // working（原图）→ 小源图 的放大系数（供 util::composeOutputThumbnail 把 Composition 源区域映射到小图坐标）。
    exportSrcScaleX_ = exportSrcPixmap_.width()  > 0 ? static_cast<double>(doc_->width())  / exportSrcPixmap_.width()  : 1.0;
    exportSrcScaleY_ = exportSrcPixmap_.height() > 0 ? static_cast<double>(doc_->height()) / exportSrcPixmap_.height() : 1.0;
}

// 「烧录图片图层」开关切换：小源图含层/不含层随之变化，重算后轻量重渲染输出预览（不重跑 Core 几何）。
void PreviewController::refreshExportSource() {
    rebuildExportSourcePixmap();
    refreshExportPreviewFromCache();
}

// 跑 Core 预览并刷新画布与状态（CONTRIBUTING.md「分层纪律」 实时预览）。
void PreviewController::refreshPreview() {
    // 重排为 L3 专属：先把重排上下文清零（非 L3 保持 0），L3 分支再回灌真实保留块数/格尺寸。
    exportPanel_->setRearrangeContext(0, 0, 0);

    // 无图像：清空叠加层。
    if (!doc_->hasImage()) {
        scene_->clearCutLines();
        scene_->clearGrid();
        scene_->clearMultiRects();
        scene_->updateMasks(engine::EngineResult{});
        updateExportPreview(engine::EngineResult{});
        status_->setHint(IDC_GTR(status, kNeedImage));
        exportPanel_->setPreviewInfo(IDC_GTR(status, kNoImageLoaded));
        return;
    }

    // L3 网格分割：网格由「基准点 + 单元尺寸 + 余量策略」定义，不依赖选区矩形，
    // 故在选区判定之前单独处理。网格线交给 GridLayer 画灰色虚线；橙色选区框在 L3 隐藏，
    // 单元点选由 CellPickerItem 承担（见下方 updateCellSelection）。遮罩仍复用 runEngine 的 kept 结果（红底 + 绿块）。
    if (doc_->mode() == engine::Tier::Grid) {
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

        const auto resExp = EngineBridge::runPreviewGeometry(cfg.source, cfg);
        const bool ok = resExp.has_value();
        const engine::EngineResult res = resExp.value_or(engine::EngineResult{});   // 失败时取空哨兵（kept 空 → 遮罩/预览自动清空）。
        const QString errText = ok ? QString() : QString::fromStdString(resExp.error().message);
        scene_->updateMasks(res);
        updateExportPreview(res);

        const int kept = ok ? static_cast<int>(res.kept.size()) : 0;
        status_->setHint(ok ? IDC_GTR(status, kStatusReady) : errText);
        exportPanel_->setCollapsible(res.collapsible);
        // 回灌重排上下文：保留块数 + 单元尺寸（供自动 cols/rows 与画布/单元尺寸警告）。
        // 单元尺寸取该轴的有效周期；分割数/比例轴无固定周期，取 Core 产出首单元的实际尺寸
        // （行主序首单元即首列/首行，GUI 不自算等分几何）。
        const auto renderCell = [&grid](const engine::GridParams& params, const bool horizontal) {
            if (const engine::GridAxisSpec axis = engine::effectiveAxis(params, horizontal); axis.mode == engine::GridAxisMode::Pitch) return axis.cell > 0 ? axis.cell : 1;
            if (grid.cells().empty()) return 1;
            const engine::RectRegion& first = grid.cells().front().area;
            return horizontal ? first.width() : first.height();
        };
        exportPanel_->setRearrangeContext(kept, renderCell(doc_->gridParams(), true),
                                          renderCell(doc_->gridParams(), false),
                                          !ok || res.uniformBlocks);
        // 重排准入门限：块尺寸不全等时 Core 已把布局回退为坍缩/分离，面板同步回退（可坍缩→坍缩，
        // 否则→分离）使导出路径与产出形式一致；引擎失败时不动用户选择。
        if (ok) {
            exportPanel_->setPreviewInfo(IDC_GTR(status, kGridPreviewFmt)
                .arg(grid.rowCount()).arg(grid.colCount())
                .arg(res.composition.canvasWidth).arg(res.composition.canvasHeight).arg(kept));
        } else {
            exportPanel_->setPreviewInfo(IDC_GTR(status, kPreviewNoResultFmt)
                .arg(errText));
        }
        return;
    }

    // 非 L3：清空网格线（若从 L3 切回）。
    scene_->clearGrid();

    // L2 多矩形并集剔除：选区来自 rects_（非单 rect_），单独处理。
    // 用 Core 诱导网格画灰色网格线（各矩形十字带并集诱导），但不启动单元点选图元——
    // picker 会 grab 鼠标、阻断画布框选（多矩形靠框选逐个追加）。橙色轮廓标出各矩形，
    // 单选区框隐藏。遮罩复用 runPreviewGeometry 的 kept 结果（CONTRIBUTING.md「分层纪律」：GUI 不算并集）。
    if (doc_->mode() == engine::Tier::Erase && doc_->l2Sub() == L2Sub::MultiRect) {
        scene_->updateMultiRects(doc_->rects());     // 增量刷新可拖拽选区框（拖拽中不回设正在拖者）。
        scene_->clearCutLines();
        scene_->syncSelection(doc_->rect(), false);  // 隐藏单选区框（改用多矩形轮廓）。
        if (doc_->rects().empty()) {
            // 尚无矩形：不跑引擎（Core 对空 rects 的 MULTI_RECT 会报错），提示框选追加。
            scene_->updateMultiRectCutLines(engine::Grid{}, false);
            scene_->updateMasks(engine::EngineResult{});
            updateExportPreview(engine::EngineResult{});
            status_->setHint(IDC_GTR(status, kDragAppendRect));
            exportPanel_->setPreviewInfo(IDC_GTR(status, kWaitingRect));
            return;
        }
        const engine::EngineConfig cfg = doc_->buildEngineConfig();
        const engine::Grid grid = EngineBridge::buildGrid(cfg.cut, cfg.source);
        scene_->updateMultiRectCutLines(grid, true);
        const auto resExp = EngineBridge::runPreviewGeometry(cfg.source, cfg);
        const bool ok = resExp.has_value();
        const engine::EngineResult res = resExp.value_or(engine::EngineResult{});   // 失败时取空哨兵。
        const QString errText = ok ? QString() : QString::fromStdString(resExp.error().message);
        scene_->updateMasks(res);
        updateExportPreview(res);

        const int kept = ok ? static_cast<int>(res.kept.size()) : 0;
        status_->setHint(ok ? IDC_GTR(status, kStatusReady) : errText);
        param_->setCollapseHint(res.collapsible, ok ? QString() : errText);
        exportPanel_->setCollapsible(res.collapsible);
        if (ok) {
            exportPanel_->setPreviewInfo(IDC_GTR(status, kMultiRectPreviewFmt)
                .arg(static_cast<int>(doc_->rects().size()))
                .arg(res.composition.canvasWidth).arg(res.composition.canvasHeight).arg(kept));
        } else {
            exportPanel_->setPreviewInfo(IDC_GTR(status, kPreviewNoResultFmt)
                .arg(errText));
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
        status_->setHint(IDC_GTR(status, kDragCreateSel));
        exportPanel_->setPreviewInfo(IDC_GTR(status, kWaitingSelection));
        return;
    }

    // 组装配置 → 调 Core 预览（仅区域数学，实时）。
    const engine::EngineConfig cfg = doc_->buildEngineConfig();
    const auto resExp = EngineBridge::runPreviewGeometry(cfg.source, cfg);
    const bool ok = resExp.has_value();
    const engine::EngineResult res = resExp.value_or(engine::EngineResult{});   // 失败时取空哨兵（kept 空 → 遮罩/预览自动清空）。
    const QString errText = ok ? QString() : QString::fromStdString(resExp.error().message);

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
    const int kept = ok ? static_cast<int>(res.kept.size()) : 0;
    status_->setHint(ok ? IDC_GTR(status, kStatusReady) : errText);

    param_->setCollapseHint(res.collapsible, ok ? QString() : errText);
    exportPanel_->setCollapsible(res.collapsible);
    if (ok) {
        exportPanel_->setPreviewInfo(IDC_GTR(status, kOutPreviewFmt)
            .arg(res.composition.canvasWidth)
            .arg(res.composition.canvasHeight)
            .arg(kept)
            .arg(res.collapsible ? IDC_GTR(status, kCollapseYes)
                                 : IDC_GTR(status, kCollapseNo)));
    } else {
        exportPanel_->setPreviewInfo(IDC_GTR(status, kPreviewNoResultFmt)
            .arg(errText));
    }
}

// 依引擎结果刷新导出面板的输出预览缩略图（导出前预览）：先缓存 res.ok/composition，再转 refreshExportPreviewFromCache。
// 缓存让后续「不影响切割几何」的事件（烧录开关/标注变更）只需重渲染缩略图、无需重跑 Core。
void PreviewController::updateExportPreview(const engine::EngineResult& res) {
    lastExportResOk_ = !res.composition.placements.empty();   // 成功结果 placements 恒非空；空哨兵 → false → 清空缩略图。
    lastComposition_ = res.composition;   // 拷贝 placements（相对 Core 区域计算是小头），供轻量重渲染复用。
    refreshExportPreviewFromCache();
}

// 用缓存的 lastComposition_/lastExportResOk_ 重渲染输出预览（不重跑 Core）。
// A：导出选项卡不可见时只置脏标志，切回该页由 onRightTabChanged 调本函数补渲染，省去后台无谓开销。
void PreviewController::refreshExportPreviewFromCache() {
    if (rightTabs_ && !showingExportPage(rightTabs_, exportPanel_)) { exportPreviewDirty_ = true; return; }
    exportPreviewDirty_ = false;
    if (!lastExportResOk_) { exportPanel_->setPreviewPixmap(QPixmap()); return; }
    exportPanel_->setPreviewPixmap(composeOutputThumbnail(
        lastComposition_, exportPreviewSource(), 1.0 / exportSrcScaleX_, 1.0 / exportSrcScaleY_,
        kExportPreviewMaxDim));
}

// 右侧选项卡切换：切到「导出」页且预览已脏时用缓存补渲染一次（不重跑 Core）。
void PreviewController::onRightTabChanged() {
    if (rightTabs_ && showingExportPage(rightTabs_, exportPanel_) && exportPreviewDirty_) refreshExportPreviewFromCache();
}

// 输出预览的源图（B）：默认用未烧录的小源图；若「导出时烧录标注」开启且有标注，则委托 util::bakeAnnotationsInto
// 把各标注按世界路径矢量烘焙到小源图副本上（working→小图变换），使预览与最终「烧录后随像素被切割落位」一致。
// 渲染逻辑在 util（无状态），本方法只做「是否烧录 + 取哪张源图」的编排（app 层只编排，CONTRIBUTING.md「分层纪律」）。
QPixmap PreviewController::exportPreviewSource() const {
    if (exportSrcPixmap_.isNull()) return exportSrcPixmap_;
    if (!anno_->burnInEnabled() || anno_->count() == 0) return exportSrcPixmap_;
    return bakeAnnotationsInto(exportSrcPixmap_, 1.0 / exportSrcScaleX_, 1.0 / exportSrcScaleY_,
                               anno_->annotations());
}

} // namespace idc::gui
