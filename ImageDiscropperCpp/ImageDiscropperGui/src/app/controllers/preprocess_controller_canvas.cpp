// ============================================================================
// 文件：app/controllers/preprocess_controller_canvas.cpp
// 作用：PreprocessController 的「多层画布」成员实现（TU 拆分）——图层会话物化缓存刷新
//       syncLayerMaterialCache、展平重组 recomposeLayerCanvas、导出侧现算
//       composeLayerCanvasForExport——类不拆分，仅按职责分 TU 编译。
// ============================================================================
#include "app/controllers/preprocess_controller.h"

#include <algorithm>
#include <unordered_set>

#include "app/status_bar.h"
#include "canvas/layer.h"
#include "model/document.h"
#include "model/engine_bridge.h"
#include "model/layer_session_store.h"
#include "i18n/gui_texts.h"
#include "i18n/gui_i18n.h"

namespace idc::gui {

// 刷新 Document 图层会话库的物化缓存：逐层比签名（含来源/图层框/底色/预处理/目标尺寸），仅「签名变 / 新层」才重跑 materializeLayer（写回 store），
// 命中层复用缓存整层位图；末尾经 store.prune 剔除已删层。刷新后 store.materialized() 直作 composeCanvas 的 materialized 入参。
void PreprocessController::syncLayerMaterialCache(const std::vector<canvas::ImageLayer>& layers) const {
    auto& store = doc_->layerSessions();
    std::unordered_set<int> alive;
    for (const auto& layer : layers) {
        alive.insert(layer.id);
        auto&[preprocess, layerSig] = store.ensure(layer.id);
        const LayerSig sig = LayerSessionStore::makeSig(layer);
        if (layerSig && *layerSig == sig) continue;   // 来源态未变：复用缓存位图，不重跑预处理
        store.materialized()[layer.id] = EngineBridge::materializeLayer(layer);
        layerSig = sig;
    }
    store.prune(alive);   // 剔除已删层（会话 + 位图一并回收，防陈旧驻留 / id 复用误命中）
}

// 多层画布重组：叠加层栈 / 画布尺寸声明变时由本方法展平回灌。非图层画布（单一载入图）早退。
// 步骤：① 以**画布底图 canvasBase()** 为基、当前叠加层声明栈调 composeCanvas 得 width×height 展平底图；
//       ② setOriginalImage 回灌为 original_（失效画布 base_）；③ 在新基准上走 commitPipeline 重放画布级预处理→working_。
//       画布预处理声明未变 → commitPipeline 内部 geomSig 前后一致 → 不误清选区（尺寸变已在 setCanvasSize 清）。
void PreprocessController::recomposeLayerCanvas() {
    if (!doc_->hasLayers()) return;
    if (const int w = doc_->canvasWidth(), h = doc_->canvasHeight(); w <= 0 || h <= 0) return;
    // 以当前声明栈为准（各层已含 transform/zOrder/preprocess）；底图作 base 铺于叠加层之下。
    const std::vector<canvas::ImageLayer>& layers = doc_->layerStack();
    // 先按签名刷新逐层物化缓存：仅签名（来源/图层框/底色/预处理/目标尺寸）变化的层重跑 materializeLayer，改落位/不透明度（transform.x/y/锚点/opacity）复用缓存整层位图。
    //   无论显隐都刷全层（切回可见时免重跑）。
    syncLayerMaterialCache(layers);
    // 预览显隐关（doc_->imageLayersVisible()==false）→ 传空 layers（仅落底图），与导出侧同口径；Core 不持显隐态。
    static const std::vector<canvas::ImageLayer> kNoLayers;
    const std::vector<canvas::ImageLayer>& toCompose = doc_->imageLayersVisible() ? layers : kNoLayers;
    core::Image composed = EngineBridge::composeCanvas(toCompose, doc_->canvasWidth(), doc_->canvasHeight(), doc_->fadedCanvasBase(),
                                                       &doc_->layerSessions().materialized());
    if (composed.empty()) {
        status_->notify(IDC_GTR(status, kLayerComposeFailed), true);
        return;
    }
    doc_->setOriginalImage(std::move(composed));
    // 在新基准上重放画布级预处理（steps_/resize_ 不变），产出 working_（触发 imageChanged）。
    commitPipeline(doc_->steps(), doc_->resizeTarget(), IDC_GTR(status, kLayerRecomposed),
                   IDC_GTR(status, kLayerRecomposeBusy));
}

// 导出侧「图片图层是否并入」现算（对标标注烧录：与「显示」预览开关正交）。无副作用：不改 original_/working_/会话缓存。
// 与当前预览态一致 → working_ 已是该形态，直接复用；否则在栈副本上翻转 imageLayersVisible 门控（不碰 Core 逻辑）
// 重跑 composeCanvas，再施加与 working_ 同一条画布级有效管线（resize 前缀 + 轻队列），保证几何/预处理口径一致。
core::Image PreprocessController::composeLayerCanvasForExport(const bool includeOverlays) const {
    if (!doc_) return core::Image{};
    if (!doc_->hasLayers()) return doc_->working();   // 非图层画布：叠加层无从谈起，恒回工作图。
    // 与当前预览显隐一致 → working_ 即该形态，直接复用，省一次全量合成 + 管线重放。
    if (includeOverlays == doc_->imageLayersVisible()) return doc_->working();
    const int w = doc_->canvasWidth(), h = doc_->canvasHeight();
    if (w <= 0 || h <= 0) return doc_->working();
    // includeOverlays 决定叠全层还是传空（仅底图）：Core 不持显隐态，显隐由调用方传参决定。
    static const std::vector<canvas::ImageLayer> kNoLayers;
    const std::vector<canvas::ImageLayer>& layers = includeOverlays ? doc_->layerStack() : kNoLayers;
    // 复用已按签名刷新的物化缓存（图层层内容/preprocess 与预览态一致）：命中直取、未命中 Core 回退现算。
    const core::Image composed = EngineBridge::composeCanvas(layers, w, h, doc_->fadedCanvasBase(), &doc_->layerSessions().materialized());
    if (composed.empty()) return doc_->working();      // 合成失败回退当前工作图（与重组早退同口径）。
    const auto eff = PreprocessSession::makeEffective(doc_->resizeTarget(), doc_->steps());
    core::Image out = eff.empty() ? composed.clone() : EngineBridge::applyPipeline(composed, eff);
    if (out.empty()) return doc_->working();
    // 与预览同口径：导出侧同样在产出边界施加画布级整体透明度（working_ 复用路径已在 commitPipeline 施加）。
    canvas::applyAlpha(out, static_cast<std::uint8_t>(std::clamp(doc_->canvasOpacity(), 0, 255)));
    return out;
}

} // namespace idc::gui
