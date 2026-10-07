// ============================================================================
// 文件：app/preprocess_controller.cpp
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
#include "app/preprocess_controller.h"

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
#include "panels/image_panel.h"
#include "preprocess/preprocess_config.h"

namespace idc::gui {


PreprocessController::PreprocessController(QObject* parent) : QObject(parent) {}

void PreprocessController::setDocument(Document* doc) { doc_ = doc; }
void PreprocessController::setAnnotationBridge(AnnotationBridge* anno) { anno_ = anno; }
void PreprocessController::setStatusBar(StatusBar* status) { status_ = status; }
void PreprocessController::setDialogParent(QWidget* parent) { dialogParent_ = parent; }

// 忙碌全屏遮罩模式：仅 MobileShell 开启（SPEC §8.2 行为同构的唯一例外之一，形态适配）。
void PreprocessController::setBusyOverlayMode(const bool overlay) { busyOverlay_ = overlay; }
void PreprocessController::setImagePanel(ImagePanel* panel) { imagePanel_ = panel; }

// 图像处理面板：轻队列意图信号 + 独立 resize 信号 → 对应槽（经 EngineBridge 物化）。
void PreprocessController::connectSignals() {
    connect(imagePanel_, &ImagePanel::stepAppendRequested, this, &PreprocessController::onStepAppend);
    connect(imagePanel_, &ImagePanel::stepReplaceRequested, this, &PreprocessController::onStepReplace);
    connect(imagePanel_, &ImagePanel::stepRemoveRequested, this, &PreprocessController::onStepRemove);
    connect(imagePanel_, &ImagePanel::stepMoveRequested, this, &PreprocessController::onStepMove);
    connect(imagePanel_, &ImagePanel::resetRequested, this, &PreprocessController::onResetPreprocess);
    // 独立「画布尺寸」区：应用绝对尺寸 / 按比例缩放 / 清除。
    connect(imagePanel_, &ImagePanel::resizeAppliedRequested, this, &PreprocessController::onSetResizeTarget);
    connect(imagePanel_, &ImagePanel::resizeScaleRequested, this, &PreprocessController::onScaleResize);
    connect(imagePanel_, &ImagePanel::resizeClearedRequested, this, &PreprocessController::onClearResize);
}

// 编辑公共提交：以「新轻队列 nextSteps + 新 resize nextResize」为结果态：
//   ① geomSig 前后比较判尺寸门（尺寸/净旋转象限变 → 清选区/标注）；
//   ② 依 base_ 缓存重放：resize 前缀变（baseDirty）才重烤 base_（唯一重采样点），否则复用；
//      working_ = 在 base_ 上重放 nextSteps（空队列 → base_ 副本）；
//   ③ 回灌 Document（setSteps + setResize + setWorkingImage）。
// 忙碌门控：仅当 baseDirty && 确有 resize 需重采样（prefix 非空）才弹模态框；
//           无 resize 的轻编辑（含首步 base=原图副本、清除 resize）走 doWork 不弹框（消除卡顿感）。
void PreprocessController::commitPipeline(const preprocess::PreprocessPipeline& nextSteps,
                                          const std::optional<ResizeTarget>& nextResize,
                                          const QString& okMsg, const QString& busyText) {
    if (!doc_->hasImage()) return;

    // 编辑前几何签名（对原图尺寸跑「有效管线」的纯元数据推演）。
    const auto [bWidth, bHeight, bRotQuad] = doc_->geomSig();
    // 编辑后几何签名（结果态有效管线）。
    const auto [aWidth, aHeight, aRotQuad] = PreprocessSession::makeEffective(nextResize, nextSteps)
                                                .geometryOutput(doc_->original().width(),
                                                                doc_->original().height());
    const bool geomChanged = aWidth != bWidth || aHeight != bHeight || aRotQuad != bRotQuad;

    // base_ 前缀 = [resize?]（单元素或空）；baseDirty：无缓存，或缓存前缀与之逐字段不等。
    const preprocess::PreprocessPipeline prefix =
        PreprocessSession::makeEffective(nextResize, preprocess::PreprocessPipeline{});
    const bool baseDirty = doc_->baseDirtyFor(prefix);

    auto doWork = [this, &nextSteps, &nextResize, &prefix, geomChanged, baseDirty, okMsg] {
        // 轻队列重放基准图 baseSrc：无 resize → 指向不可变 original_（零拷贝）；有 resize → base_（按需重烤复用）。
        // base_ 仅在「确有 resize 前缀」时维护；无 resize 不再克隆一份与 original_ 相同的全图（稳态省一份拷贝）。
        const core::Image* baseSrc = &doc_->original();
        if (!prefix.empty()) {
            if (baseDirty) {
                core::Image newBase = EngineBridge::applyPipeline(doc_->original(), prefix);
                if (newBase.empty()) {
                    status_->notify(QStringLiteral("预处理失败：结果为空图"), true);
                    return;
                }
                doc_->setBase(std::move(newBase), prefix);
            }
            baseSrc = &doc_->base();
        } else if (doc_->hasBase()) {
            doc_->clearBase();   // 由「有 resize」切到「无 resize」：释放陈旧缓存，稳态仅 original_ + working_ 两份。
        }
        // 轻队列恒快：在 base_ 上重放 nextSteps（空队列 → 直接取 base_ 副本）。
        // 重放经 EngineBridge::applyPipeline → Core applyExec（执行层融合：色彩折单次 channelMap、几何折单次 D4、灰度后死步自动吞除；
        // resize 已为 base_ 前缀，轻队列不含 resize → 整条恒单块）：逐像素结果不变、趟数与队列长度解耦。
        // 注：仅物化时融合；steps_ 存储 / 序列化 / 上面的几何签名均用原序 nextSteps（融合不改净几何签名）。
        // ⚠ working 必须以指针读取基准图（original_ 或 base_）后 clone / 重放产出，绝不能 move base_：base_ 是跨多次轻编辑
        //    复用的重放缓存，一旦被搬进 working_ 即被搬空，下次编辑被迫从 original 重跑（含 resize 重采样）——勿“优化”成移动语义。
        core::Image out = nextSteps.empty()
                              ? baseSrc->clone()
                              : EngineBridge::applyPipeline(*baseSrc, nextSteps);
        if (out.empty()) {
            status_->notify(QStringLiteral("预处理失败：结果为空图"), true);
            return;
        }
        if (geomChanged) {
            // 尺寸/净旋转象限变：既有选区坐标与标注基于旧几何，全部失效 → 清空。
            doc_->clearRect();
            doc_->clearRects();
            doc_->clearCells();
            if (anno_) anno_->clearAll();
        }
        // 画布级整体透明度：产出边界单次施加（working_ 每次从基准重放、非累加 → 无重复淡出），不烘焙缓存位图。
        canvas::applyAlpha(out, static_cast<std::uint8_t>(std::clamp(doc_->canvasOpacity(), 0, 255)));
        doc_->setSteps(nextSteps);
        doc_->setResize(nextResize);
        doc_->setWorkingImage(std::move(out));   // 触发 imageChanged → 重建预览底图 + 刷新。
        status_->notify(okMsg, false);
    };

    if (baseDirty && !prefix.empty() && !busyResample_) {
        // resize 前缀真变且确有重采样：包模态忙碌框并加重入守卫（重跑整条前缀，文案不特指单步）。
        const QString shownText = busyText.isEmpty()
                                      ? QStringLiteral("正在重放预处理队列，请稍候…")
                                      : busyText;
        busyResample_ = true;
        runWithBusyDialog(shownText, doWork);
        busyResample_ = false;
    } else {
        doWork();
    }
}

// 在应用级模态、不可取消的忙碌对话框内同步执行 op（单点实现，见头文件说明）。
void runInBusyDialog(QWidget* parent, const bool fullscreen, const QString& text,
                     const std::function<void()>& op) {
    QProgressDialog dlg(text, QString(), 0, 0, parent);
    dlg.setWindowTitle(QStringLiteral("正在处理图像"));
    dlg.setWindowModality(Qt::ApplicationModal); // 模态阻断全部其他窗口的输入。
    dlg.setCancelButton(nullptr);                // 不可取消（耗时操作中途无法安全回退）。
    dlg.setMinimumDuration(0);                   // 立即显示，不等阈值。
    dlg.setRange(0, 0);                          // 不确定进度（忙碌滚动样式）。
    dlg.show();
    if (fullscreen && parent) {
        // 移动端全屏遮罩（SPEC §8.2）：忙碌对话框铺满屏幕替代桌面居中小对话框，模态语义不变。
        dlg.setGeometry(QApplication::primaryScreen()->geometry());
    }
    QApplication::processEvents();               // 先让对话框绘制出来，再进入耗时处理。
    op();
    dlg.close();
}

// 在模态忙碌对话框内同步执行 op：含 resize 的重采样在大图上可能耗时，委托 runInBusyDialog
// （父窗口 dialogParent_、全屏标志 busyOverlay_），避免用户误以为卡死而重复点击。
void PreprocessController::runWithBusyDialog(const QString& text, const std::function<void()>& op) const {
    runInBusyDialog(dialogParent_, busyOverlay_ && dialogParent_ != nullptr, text, op);
}

// 配置载入：把加载的整条队列归一化——抽出 ResizeOp 为独立 resize（多个取最后一个正尺寸），
// 其余按序进轻队列（散落 ResizeOp 一律丢弃，绝不入 steps_）。结果恒为「resize 在前 + 轻操作在后」。
// 注：「多个 resize 取最后一个」是刻意归一（SPEC「resize 恒从原图重烤、不叠加」），并非等价重放——
//     多次双线性重采样 ≠ 一次（对上次插值结果再采样会二次失真），故只保留末个正尺寸并单次从原图采样。
void PreprocessController::applyLoadedQueue(const preprocess::PreprocessPipeline& pipeline) {
    if (!doc_->hasImage()) return;
    // 归一化交 session 静态策略：抽出散落 ResizeOp 为独立 resize（多个取末个正尺寸）、其余重排 geo 前 color 后。
    const auto [steps, resize] = PreprocessSession::normalizeLoaded(pipeline);
    commitPipeline(steps, resize,
                   QStringLiteral("已载入预处理队列"),
                   QStringLiteral("正在按载入的队列重放图像，请稍候…"));
}

// 刷新 Document 图层会话库的物化缓存：逐层比签名，仅「签名变 / 新层」才重跑 materializeLayer（写回 store），
// 命中层复用缓存位图；末尾经 store.prune 剔除已删层。刷新后 store.materialized() 直作 composeCanvas 的 materialized 入参。
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
    // 先按签名刷新逐层物化缓存（缺口 1）：仅来源态变化的层重跑预处理，改 transform（拖动/落位/不透明度）复用缓存位图。
    //   无论显隐都刷全层（切回可见时免重跑）。
    syncLayerMaterialCache(layers);
    // 预览显隐关（doc_->imageLayersVisible()==false）→ 传空 layers（仅落底图），与导出侧同口径；Core 不再持显隐态。
    static const std::vector<canvas::ImageLayer> kNoLayers;
    const std::vector<canvas::ImageLayer>& toCompose = doc_->imageLayersVisible() ? layers : kNoLayers;
    core::Image composed = EngineBridge::composeCanvas(toCompose, doc_->canvasWidth(), doc_->canvasHeight(), doc_->fadedCanvasBase(),
                                                       &doc_->layerSessions().materialized());
    if (composed.empty()) {
        status_->notify(QStringLiteral("图层合成失败（空图）"), true);
        return;
    }
    doc_->setOriginalImage(std::move(composed));
    // 在新基准上重放画布级预处理（steps_/resize_ 不变），产出 working_（触发 imageChanged）。
    commitPipeline(doc_->steps(), doc_->resizeTarget(), QStringLiteral("已重组多层画布"),
                   QStringLiteral("正在重组画布，请稍候…"));
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
    // includeOverlays 决定叠全层还是传空（仅底图）：Core 不持显隐态，显隐下沉到调用方传参。
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

// 在现有 original_ 基准上重放当前画布级预处理声明，产出 working_（供新建/清图层替换底图后补算）。
//   走 commitPipeline 同一入口：几何签名前后同序一致 → geomChanged=false → 不误清选区/标注；
//   resize_ 已于 resetBaseWith 清为 nullopt，故仅重放色彩轻队列（无声明时→original_ 副本）。
void PreprocessController::replayCanvasPreprocess() {
    if (!doc_ || !doc_->hasImage()) return;
    commitPipeline(doc_->steps(), doc_->resizeTarget(), QStringLiteral("已应用画布预处理"), QString());
}

// 当前目标会话的声明态只读视图：Document::activeSession 已按选中图层/画布路由。
const preprocess::PreprocessPipeline& PreprocessController::activeSteps() const {
    return doc_->activeSession().steps();
}
const std::optional<ResizeTarget>& PreprocessController::activeResize() const {
    return doc_->activeSession().resizeTarget();
}

// 提交到当前预处理目标：无选中图层（或图层定位失败）→ 走画布 commitPipeline；
// 选中图片图层→写其会话声明后折进 layer.preprocess（= effectivePipeline），交 updateLayer 发
// layersChanged 触发 recomposeLayerCanvas 重组展平画布（逐层预处理物化下沉到 Core materializeLayer）。
// 图层编辑不改画布尺寸 → 无需尺寸门/清选区（决策 3），也无需 base_ 缓存（重组走 Core 全量物化）。
void PreprocessController::commitActive(const preprocess::PreprocessPipeline& nextSteps,
                                        const std::optional<ResizeTarget>& nextResize,
                                        const QString& okMsg, const QString& busyText) {
    // 底图目标（Base）：仅色彩预处理写入 baseSession_（几何/resize 已在面板禁用，updateBasePreprocess 再次过滤），
    //   变更则发 layersChanged 交 recomposeLayerCanvas 以 fadedCanvasBase 重组预览（边界施加、不烘焙 baseImage_）。
    if (doc_->preprocessTarget() == PreprocessTarget::Base) {
        if (doc_->updateBasePreprocess(nextSteps))
            status_->notify(okMsg, false);
        return;
    }
    const auto id = doc_->selectedLayerId();
    const canvas::ImageLayer* layer =
        doc_->preprocessTarget() == PreprocessTarget::Layer && id.has_value() ? doc_->layerById(*id) : nullptr;
    if (layer == nullptr) {
        commitPipeline(nextSteps, nextResize, okMsg, busyText);   // 画布目标：既有 working_ 物化链路不变。
        return;
    }
    PreprocessSession& sess = doc_->activeSession();   // 该层独立会话（activeSession 已路由到 layerSessions_ 内该 id 条目的 preprocess）。
    sess.setSteps(nextSteps);
    sess.setResize(nextResize);
    canvas::ImageLayer updated = *layer;               // 拷贝现声明（保留 decodedPixels/transform/zOrder），仅换 preprocess。
    updated.preprocess = sess.effectivePipeline();
    doc_->updateLayer(*id, updated);                   // 发 layersChanged → 控制器重组展平画布。
    status_->notify(okMsg, false);
}

// ---- 轻操作步骤增删移改（面板步骤列表驱动；仅作用于 steps_，resize 保持不变）----
// 克隆现轻队列 → mutate → 连同现 resize 一并交 commitPipeline。resize 未变 → baseDirty=false（除首次）
// → 复用 base_ 只重放轻后缀，不重采样、免弹框。
void PreprocessController::onStepAppend(const preprocess::PreprocessConfig& cfg) {
    if (!doc_->hasImage()) return;
    preprocess::PreprocessPipeline next = activeSteps();
    PreprocessSession::appendKeepingInvariant(next, cfg);   // 几何入几何块尾、色彩入队尾（保持 geo 前 color 后）
    commitActive(next, activeResize(), QStringLiteral("已添加预处理步骤"), QString());
}

void PreprocessController::onStepReplace(const int index, preprocess::PreprocessConfig cfg) {
    if (index < 0) return;
    preprocess::PreprocessPipeline next = activeSteps();
    next.replaceAt(static_cast<std::size_t>(index), std::move(cfg));
    commitActive(next, activeResize(), QStringLiteral("已更新预处理步骤"), QString());
}

void PreprocessController::onStepRemove(const int index) {
    if (index < 0) return;
    preprocess::PreprocessPipeline next = activeSteps();
    next.remove(static_cast<std::size_t>(index));
    commitActive(next, activeResize(), QStringLiteral("已删除预处理步骤"), QString());
}

void PreprocessController::onStepMove(const int index, const bool down) {
    if (index < 0) return;
    preprocess::PreprocessPipeline next = activeSteps();
    // 块内移动限幅交 session 静态策略：仅相邻同类才移一格，越界/跨块返回 false（保持 geo→color 不变式）。
    if (!PreprocessSession::moveWithinBlock(next, static_cast<std::size_t>(index), down)) return;
    commitActive(next, activeResize(), QStringLiteral("已调整步骤顺序"), QString());
}

// 重置预处理：清空 resize 与轻队列、工作图恢复原图（Document 内部处理，无需重放）。
// 失效门与 commitPipeline 同标准的完整签名（宽高 + 净旋转象限）：与恒等签名 {原宽,原高,0} 不等即清；
// 只看宽高会让 [rotate180]/[rotate90×2] 重置（内容转回 180°、象限 2→0）时漏清基于旧几何的选区/标注。
void PreprocessController::onResetPreprocess() const {
    if (!doc_->hasImage()) return;
    // 底图目标（Base）：清空 baseSession_ 色彩预处理声明 → updateBasePreprocess(空) 发 layersChanged 重组预览。
    if (doc_->preprocessTarget() == PreprocessTarget::Base) {
        if (doc_->updateBasePreprocess(preprocess::PreprocessPipeline{}))
            status_->notify(QStringLiteral("已重置底图预处理"), false);
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
            status_->notify(QStringLiteral("已重置该图层预处理"), false);
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
        if (anno_) anno_->clearAll();
    }
    doc_->resetPreprocess();
    status_->notify(QStringLiteral("已重置预处理，恢复原图"), false);
}

// ---- 独立 resize（面板「画布尺寸」区驱动；恒最先重放、从原图重采样）----
// 设定绝对目标尺寸（非正尺寸忽略）。
void PreprocessController::onSetResizeTarget(const int width, const int height) {
    if (!doc_->hasImage() || width <= 0 || height <= 0) return;
    commitActive(activeSteps(), ResizeTarget{width, height},
                   QStringLiteral("已设置画布尺寸 %1×%2").arg(width).arg(height),
                   QStringLiteral("正在缩放到目标尺寸，请稍候…"));
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
                   QStringLiteral("已按比例缩放至 %1×%2").arg(tw).arg(th),
                   QStringLiteral("正在按比例缩放图像，请稍候…"));
}

// 清除独立 resize（回到无 resize 的原图基准）。
void PreprocessController::onClearResize() {
    if (!doc_->hasImage()) return;
    commitActive(activeSteps(), std::nullopt,
                   QStringLiteral("已清除画布尺寸"), QString());
}

// ---- 菜单/快捷键入口：各追加一步轻操作（走不变式追加：几何插几何块尾、色彩追加队尾）----
void PreprocessController::onRotate(const int angleDeg) {
    if (!doc_->hasImage()) return;
    preprocess::PreprocessPipeline next = activeSteps();
    PreprocessSession::appendKeepingInvariant(next, preprocess::RotateOp{angleDeg});
    commitActive(next, activeResize(), QStringLiteral("已添加旋转 %1°").arg(angleDeg), QString());
}

void PreprocessController::onFlip(const bool horizontal) {
    if (!doc_->hasImage()) return;
    preprocess::PreprocessPipeline next = activeSteps();
    PreprocessSession::appendKeepingInvariant(next, preprocess::FlipOp{horizontal});
    commitActive(next, activeResize(),
                   horizontal ? QStringLiteral("已添加水平翻转") : QStringLiteral("已添加垂直翻转"),
                   QString());
}

void PreprocessController::onGray() {
    if (!doc_->hasImage()) return;
    preprocess::PreprocessPipeline next = activeSteps();
    PreprocessSession::appendKeepingInvariant(next, preprocess::GrayOp{});
    commitActive(next, activeResize(), QStringLiteral("已添加黑白"), QString());
}

// 色道反色：拼长度 3 反相掩码（'1' 反相、'0' 保持）追加 InvertOp（灰度图：mask 全 '0' 为空操作，否则逐字节整体反相）。
void PreprocessController::onInvert(const bool invR, const bool invG, const bool invB) {
    if (!doc_->hasImage()) return;
    const std::string mask = std::string(invR ? "1" : "0") + (invG ? "1" : "0") + (invB ? "1" : "0");
    preprocess::PreprocessPipeline next = activeSteps();
    PreprocessSession::appendKeepingInvariant(next, preprocess::InvertOp{mask});
    commitActive(next, activeResize(), QStringLiteral("已添加按通道反色"), QString());
}

// 色道分离：拼长度 3 保留掩码（'1' 保留、'0' 置零）追加 SplitOp。
void PreprocessController::onSplit(const bool keepR, const bool keepG, const bool keepB) {
    if (!doc_->hasImage()) return;
    const std::string mask = std::string(keepR ? "1" : "0") + (keepG ? "1" : "0") + (keepB ? "1" : "0");
    preprocess::PreprocessPipeline next = activeSteps();
    PreprocessSession::appendKeepingInvariant(next, preprocess::SplitOp{mask});
    commitActive(next, activeResize(), QStringLiteral("已添加按通道分离"), QString());
}

} // namespace idc::gui
