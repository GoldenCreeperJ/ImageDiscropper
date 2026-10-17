// ============================================================================
// 文件：app/controllers/preprocess_controller.cpp
// 作用：实现 PreprocessController——独立 resize + 声明式轻操作队列的编排（见同名头文件说明）。
// 分块依据：
//   - commitPipeline：编辑公共提交（结果态 = 新轻队列 + 新 resize → geomSig 尺寸门 → base_ 缓存重放物化 → 回灌）；
//   - runWithBusyDialog：resize 前缀变化（base_ 需重烤）时的模态忙碌对话框；
//   - onStep*：面板「几何 / 色彩」两列表驱动的轻操作增/删/移/改（仅作用于 steps_；信号携扁平下标）；
//   - onSetResizeTarget/onScaleResize/onClearResize：独立「画布尺寸」区驱动的 resize 编辑；
//   - onRotate/onFlip/onGray/onInvert/onSplit：菜单/快捷键「追加一步轻操作」薄封装。
// 说明：模型 = original_（不可变）→ resize_（可选，恒最先重放、从原图重采样）→ steps_（轻操作队列，绝不含 ResizeOp）→ working_。
//       面板把轻队列拆为「几何(旋转/翻转)」与「色彩(黑白/反色/分离)」两个独立列表；本控制器保证 steps_ 恒为
//       「全部几何块在前 + 全部色彩块在后」不变式（几何按类型插到几何块尾、色彩追加队尾；块内移动限幅；载入重排），
//       故底部提交按 resize→geo→color 顺序合并、且与折叠物化（连续几何段压为 ≤2 op、连续色彩段就地规整）天然一致。
//       base_ = resize(original)（无 resize 则为原图副本），basePath_ 为对应的 [resize?] 前缀（单元素或空）。
//       轻编辑不改 resize → baseDirty=false（除首次）→ 复用 base_ 只重放轻后缀，不重采样、免弹框（需求 ③）；
//       仅 resize 变化才重烤 base_（唯一重采样点）。配置载入时 applyLoadedQueue 把散落的 ResizeOp 归一化抽出为独立 resize（需求 ①）。
// ============================================================================
#include "app/controllers/preprocess_controller.h"

#include <QApplication>
#include <QProgressDialog>
#include <QScreen>
#include <QString>

#include <algorithm>
#include <cmath>
#include <optional>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <variant>

#include "app/status_bar.h"
#include "canvas/layer.h"
#include "model/annotation_bridge.h"
#include "model/document.h"
#include "model/engine_bridge.h"
#include "model/layer_session_store.h"
#include "model/preprocess_session.h"
#include "panels/preprocess_panel.h"
#include "preprocess/preprocess_config.h"
#include "i18n/gui_texts.h"
#include "i18n/gui_i18n.h"

namespace idc::gui {


PreprocessController::PreprocessController(QObject* parent) : QObject(parent) {}

void PreprocessController::setDocument(Document* doc) { doc_ = doc; }
void PreprocessController::setStatusBar(StatusBar* status) { status_ = status; }
void PreprocessController::setDialogParent(QWidget* parent) { dialogParent_ = parent; }

// 忙碌全屏遮罩模式：仅 MobileShell 开启（SPEC §8.2 行为同构的唯一例外之一，形态适配）。
void PreprocessController::setBusyOverlayMode(const bool overlay) { busyOverlay_ = overlay; }
void PreprocessController::setPreprocessPanel(PreprocessPanel* panel) { preprocessPanel_ = panel; }

// 预处理面板：轻队列意图信号 + 独立 resize 信号 → 对应槽（经 EngineBridge 物化）。
void PreprocessController::connectSignals() {
    connect(preprocessPanel_, &PreprocessPanel::stepAppendRequested, this, &PreprocessController::onStepAppend);
    connect(preprocessPanel_, &PreprocessPanel::stepReplaceRequested, this, &PreprocessController::onStepReplace);
    connect(preprocessPanel_, &PreprocessPanel::stepRemoveRequested, this, &PreprocessController::onStepRemove);
    connect(preprocessPanel_, &PreprocessPanel::stepMoveRequested, this, &PreprocessController::onStepMove);
    connect(preprocessPanel_, &PreprocessPanel::resetRequested, this, &PreprocessController::onResetPreprocess);
    // 独立「画布尺寸」区：应用绝对尺寸 / 按比例缩放 / 清除。
    connect(preprocessPanel_, &PreprocessPanel::resizeAppliedRequested, this, &PreprocessController::onSetResizeTarget);
    connect(preprocessPanel_, &PreprocessPanel::resizeScaleRequested, this, &PreprocessController::onScaleResize);
    connect(preprocessPanel_, &PreprocessPanel::resizeClearedRequested, this, &PreprocessController::onClearResize);
}
void PreprocessController::applyLoadedQueue(const preprocess::PreprocessPipeline& pipeline) {
    if (!doc_->hasImage()) return;
    // 归一化交 session 静态策略：抽出散落 ResizeOp 为独立 resize（多个取末个正尺寸）、其余重排 geo 前 color 后。
    const auto [steps, resize] = PreprocessSession::normalizeLoaded(pipeline);
    commitPipeline(steps, resize,
                   IDC_GTR(status, kPrepQueueLoaded),
                   IDC_GTR(status, kPrepQueueReplayBusy));
}

// 在现有 original_ 基准上重放当前画布级预处理声明，产出 working_（供新建/清图层替换底图后补算）。
//   走 commitPipeline 同一入口：几何签名前后同序一致 → geomChanged=false → 不误清选区/标注；
//   resize_ 已于 resetBaseWith 清为 nullopt，故仅重放色彩轻队列（无声明时→original_ 副本）。
void PreprocessController::replayCanvasPreprocess() {
    if (!doc_ || !doc_->hasImage()) return;
    commitPipeline(doc_->steps(), doc_->resizeTarget(), IDC_GTR(status, kPrepCanvasApplied), QString());
}

// 当前目标会话的声明态只读视图：Document::activeSession 已按选中图层/画布路由。
const preprocess::PreprocessPipeline& PreprocessController::activeSteps() const {
    return doc_->activeSession().steps();
}
const std::optional<ResizeTarget>& PreprocessController::activeResize() const {
    return doc_->activeSession().resizeTarget();
}
void PreprocessController::onStepAppend(const preprocess::PreprocessConfig& cfg) {
    if (!doc_->hasImage()) return;
    preprocess::PreprocessPipeline next = activeSteps();
    PreprocessSession::appendKeepingInvariant(next, cfg);   // 几何入几何块尾、色彩入队尾（保持 geo 前 color 后）
    commitActive(next, activeResize(), IDC_GTR(status, kPrepStepAdded), QString());
}

void PreprocessController::onStepReplace(const int index, preprocess::PreprocessConfig cfg) {
    if (index < 0) return;
    preprocess::PreprocessPipeline next = activeSteps();
    next.replaceAt(static_cast<std::size_t>(index), std::move(cfg));
    commitActive(next, activeResize(), IDC_GTR(status, kPrepStepUpdated), QString());
}

void PreprocessController::onStepRemove(const int index) {
    if (index < 0) return;
    preprocess::PreprocessPipeline next = activeSteps();
    next.remove(static_cast<std::size_t>(index));
    commitActive(next, activeResize(), IDC_GTR(status, kPrepStepRemoved), QString());
}

void PreprocessController::onStepMove(const int index, const bool down) {
    if (index < 0) return;
    preprocess::PreprocessPipeline next = activeSteps();
    // 块内移动限幅交 session 静态策略：仅相邻同类才移一格，越界/跨块返回 false（保持 geo→color 不变式）。
    if (!PreprocessSession::moveWithinBlock(next, static_cast<std::size_t>(index), down)) return;
    commitActive(next, activeResize(), IDC_GTR(status, kPrepStepMoved), QString());
}

// 重置预处理：清空 resize 与轻队列、工作图恢复原图（Document 内部处理，无需重放）。
// 失效门与 commitPipeline 同标准的完整签名（宽高 + 净旋转象限）：与恒等签名 {原宽,原高,0} 不等即清框选；
// 只看宽高会让 [rotate180]/[rotate90×2] 重置（内容转回 180°、象限 2→0）时漏清基于旧几何的框选。
// 标注/图层恒不清（矢量叠加可存在于画布外，SPEC §8.2）。
void PreprocessController::onResetPreprocess() const {
    if (!doc_->hasImage()) return;
    // 底图目标（Base）：清空 baseSession_ 色彩预处理声明 → updateBasePreprocess(空) 发 layersChanged 重组预览。
    if (doc_->preprocessTarget() == PreprocessTarget::Base) {
        if (doc_->updateBasePreprocess(preprocess::PreprocessPipeline{}))
            status_->notify(IDC_GTR(status, kPrepBaseReset), false);
        return;
    }
    // 图层目标：清该层会话声明 + 折回空 preprocess → updateLayer 重组（不涉及选区/标注尺寸门）。
    if (const auto id = doc_->selectedLayerId();
        doc_->preprocessTarget() == PreprocessTarget::Layer && id.has_value()) {
        if (const canvas::ImageLayer* layer = doc_->layerById(*id)) {
            if (!doc_->activeSession().hasPreprocess()) return;
            doc_->activeSession().clear();
            canvas::ImageLayer updated = *layer;
            updated.preprocess.clear();
            doc_->updateLayer(*id, updated);
            status_->notify(IDC_GTR(status, kPrepLayerReset), false);
        }
        return;
    }
    if (!doc_->hasPreprocess()) return;
    const auto [width, height, rotQuad] = doc_->geomSig();
    const int ow = doc_->original().width();
    if (const int oh = doc_->original().height();
        width != ow || height != oh || rotQuad != 0) {
        doc_->clearRect();
        doc_->clearRects();
        doc_->clearCells();
    }
    doc_->resetPreprocess();
    status_->notify(IDC_GTR(status, kPrepReset), false);
}

// ---- 独立 resize（面板「画布尺寸」区驱动；恒最先重放、从原图重采样）----
// 设定绝对目标尺寸（非正尺寸忽略）。
void PreprocessController::onSetResizeTarget(const int width, const int height) {
    if (!doc_->hasImage() || width <= 0 || height <= 0) return;
    commitActive(activeSteps(), ResizeTarget{width, height},
                   IDC_GTR(status, kPrepResizeSet).arg(width).arg(height),
                   IDC_GTR(status, kPrepResizeBusy));
}

// 按比例缩放：以当前 resize 目标（无则为原图尺寸）为基准 × 倍数换算新绝对目标——
// 恒从原图重烤（不变式），故基准是 resize 自身目标尺寸而非含旋转的最终工作图。
void PreprocessController::onScaleResize(const double factor) {
    if (!doc_->hasImage() || factor <= 0.0) return;
    int bw = doc_->original().width();
    int bh = doc_->original().height();
    if (const auto r = activeResize(); r.has_value()) {
        bw = r->width;
        bh = r->height;
    }
    const int tw = std::max(1, static_cast<int>(std::lround(bw * factor)));
    const int th = std::max(1, static_cast<int>(std::lround(bh * factor)));
    commitActive(activeSteps(), ResizeTarget{tw, th},
                   IDC_GTR(status, kPrepResizeScaled).arg(tw).arg(th),
                   IDC_GTR(status, kPrepResizeScaleBusy));
}

// 清除独立 resize（回到无 resize 的原图基准）。
void PreprocessController::onClearResize() {
    if (!doc_->hasImage()) return;
    commitActive(activeSteps(), std::nullopt,
                   IDC_GTR(status, kPrepResizeCleared), QString());
}

// ---- 菜单/快捷键入口：各追加一步轻操作（走不变式追加：几何插几何块尾、色彩追加队尾）----
void PreprocessController::onRotate(const int angleDeg) {
    if (!doc_->hasImage()) return;
    preprocess::PreprocessPipeline next = activeSteps();
    PreprocessSession::appendKeepingInvariant(next, preprocess::RotateOp{angleDeg});
    commitActive(next, activeResize(), IDC_GTR(status, kPrepRotateAdded).arg(angleDeg), QString());
}

void PreprocessController::onFlip(const bool horizontal) {
    if (!doc_->hasImage()) return;
    preprocess::PreprocessPipeline next = activeSteps();
    PreprocessSession::appendKeepingInvariant(next, preprocess::FlipOp{horizontal});
    commitActive(next, activeResize(),
                   horizontal ? IDC_GTR(status, kPrepFlipHAdded)
                              : IDC_GTR(status, kPrepFlipVAdded),
                   QString());
}

void PreprocessController::onGray() {
    if (!doc_->hasImage()) return;
    preprocess::PreprocessPipeline next = activeSteps();
    PreprocessSession::appendKeepingInvariant(next, preprocess::GrayOp{});
    commitActive(next, activeResize(), IDC_GTR(status, kPrepGrayAdded), QString());
}

// 色道反色：拼长度 3 反相掩码（'1' 反相、'0' 保持）追加 InvertOp（灰度图：mask 全 '0' 为空操作，否则逐字节整体反相）。
void PreprocessController::onInvert(const bool invR, const bool invG, const bool invB) {
    if (!doc_->hasImage()) return;
    const std::string mask = std::string(invR ? "1" : "0") + (invG ? "1" : "0") + (invB ? "1" : "0");
    preprocess::PreprocessPipeline next = activeSteps();
    PreprocessSession::appendKeepingInvariant(next, preprocess::InvertOp{mask});
    commitActive(next, activeResize(), IDC_GTR(status, kPrepInvertAdded), QString());
}

// 色道分离：拼长度 3 保留掩码（'1' 保留、'0' 置零）追加 SplitOp。
void PreprocessController::onSplit(const bool keepR, const bool keepG, const bool keepB) {
    if (!doc_->hasImage()) return;
    const std::string mask = std::string(keepR ? "1" : "0") + (keepG ? "1" : "0") + (keepB ? "1" : "0");
    preprocess::PreprocessPipeline next = activeSteps();
    PreprocessSession::appendKeepingInvariant(next, preprocess::SplitOp{mask});
    commitActive(next, activeResize(), IDC_GTR(status, kPrepSplitAdded), QString());
}

} // namespace idc::gui
