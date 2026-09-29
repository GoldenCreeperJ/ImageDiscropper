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
#include <utility>
#include <variant>

#include "app/status_bar.h"
#include "model/annotation_bridge.h"
#include "model/document.h"
#include "model/engine_bridge.h"
#include "panels/image_panel.h"
#include "preprocess/preprocess_config.h"

namespace idc::gui {

namespace {

// 组装「有效管线」= [resize?] + 轻操作队列——几何签名推演与 base_ 前缀的唯一真相源。
preprocess::PreprocessPipeline effectivePipeline(const std::optional<ResizeTarget>& resize,
                                                 const preprocess::PreprocessPipeline& steps) {
    preprocess::PreprocessPipeline out;
    if (resize) out.add(preprocess::ResizeOp{resize->width, resize->height});
    for (const auto& cfg : steps.steps()) out.add(cfg);
    return out;
}

// 轻操作分类：几何步（Rotate/Flip）判定——口径统一委托 Core preprocess::isGeometryStep（与面板 / 折叠同源）。
// ResizeOp 不入轻队列（已独立成区），故不会到达此处。
bool isGeoCfg(const preprocess::PreprocessConfig& cfg) {
    return preprocess::isGeometryStep(cfg);
}

// 队列首色下标 = 几何块长度（几何恒在前、色彩恒在后）：几何块 [0, geoLen)、色彩块 [geoLen, n)。
std::size_t geoBlockLen(const preprocess::PreprocessPipeline& steps) {
    std::size_t n = 0;
    for (const auto& cfg : steps.steps()) {
        if (!isGeoCfg(cfg)) break;
        ++n;
    }
    return n;
}

// 按类型不变式追加一步：几何步插到几何块末尾（首色之前）、色彩步追加到队尾——
// 由此 steps_ 恒为「[全部几何][全部色彩]」，与面板双列表及提交合并 resize→geo→color 完全一致。
void appendLightKeepingInvariant(preprocess::PreprocessPipeline& steps,
                                 const preprocess::PreprocessConfig& cfg) {
    if (isGeoCfg(cfg)) steps.insert(geoBlockLen(steps), cfg);   // 落在几何块末尾、色彩块之前
    else steps.add(cfg);                                        // 色彩步恒在队尾
}

} // namespace

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
    const auto [aWidth, aHeight, aRotQuad] = effectivePipeline(nextResize, nextSteps)
                                                .geometryOutput(doc_->original().width(),
                                                                doc_->original().height());
    const bool geomChanged = aWidth != bWidth || aHeight != bHeight || aRotQuad != bRotQuad;

    // base_ 前缀 = [resize?]（单元素或空）；baseDirty：无缓存，或缓存前缀与之逐字段不等。
    const preprocess::PreprocessPipeline prefix =
        effectivePipeline(nextResize, preprocess::PreprocessPipeline{});
    const bool baseDirty = !doc_->hasBase() || !(doc_->basePrefix() == prefix);

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
        // 重放走折叠版 folded()（连续几何段折为 ≤2 op、连续色彩段就地规整、灰度后死步自动吞除；段间不跨越，resize 已为 base_ 前缀）：逐像素结果不变、全分辨率趟数更少。
        // 注：仅物化时折叠；steps_ 存储 / 序列化 / 上面的几何签名均用原序 nextSteps（折叠不改净几何签名）。
        // ⚠ working 必须以指针读取基准图（original_ 或 base_）后 clone / 重放产出，绝不能 move base_：base_ 是跨多次轻编辑
        //    复用的重放缓存，一旦被搬进 working_ 即被搬空，下次编辑被迫从 original 重跑（含 resize 重采样）——勿“优化”成移动语义。
        core::Image out = nextSteps.empty()
                              ? baseSrc->clone()
                              : EngineBridge::applyPipeline(*baseSrc, nextSteps.folded());
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

// 在模态忙碌对话框内同步执行 op：含 resize 的重采样在大图上可能耗时，弹出不可取消、应用级模态的
// 进度对话框（不确定进度条）阻断其余输入，避免用户误以为卡死而重复点击。
void PreprocessController::runWithBusyDialog(const QString& text, const std::function<void()>& op) const {
    QProgressDialog dlg(text, QString(), 0, 0, dialogParent_);
    dlg.setWindowTitle(QStringLiteral("正在处理图像"));
    dlg.setWindowModality(Qt::ApplicationModal); // 模态阻断全部其他窗口的输入。
    dlg.setCancelButton(nullptr);                // 不可取消（重采样中途无法安全回退）。
    dlg.setMinimumDuration(0);                   // 立即显示，不等阈值。
    dlg.setRange(0, 0);                          // 不确定进度（忙碌滚动样式）。
    dlg.show();
    if (busyOverlay_ && dialogParent_) {
        // 移动端全屏遮罩（SPEC §8.2）：忙碌对话框铺满屏幕替代桌面居中小对话框，模态语义不变。
        dlg.setGeometry(QApplication::primaryScreen()->geometry());
    }
    QApplication::processEvents();               // 先让对话框绘制出来，再进入耗时处理。
    op();
    dlg.close();
}

// 配置载入：把加载的整条队列归一化——抽出 ResizeOp 为独立 resize（多个取最后一个正尺寸），
// 其余按序进轻队列（散落 ResizeOp 一律丢弃，绝不入 steps_）。结果恒为「resize 在前 + 轻操作在后」。
// 注：「多个 resize 取最后一个」是刻意归一（SPEC「resize 恒从原图重烤、不叠加」），并非等价重放——
//     多次双线性重采样 ≠ 一次（对上次插值结果再采样会二次失真），故只保留末个正尺寸并单次从原图采样。
void PreprocessController::applyLoadedQueue(const preprocess::PreprocessPipeline& pipeline) {
    if (!doc_->hasImage()) return;
    preprocess::PreprocessPipeline light;
    std::optional<ResizeTarget> resize;
    // 先扫一遍抽出 resize（多个取最后一个正尺寸），其余按类型归位：几何段在前、色彩段在后（各保持相对顺序）。
    preprocess::PreprocessPipeline geo, color;
    for (const auto& cfg : pipeline.steps()) {
        if (std::holds_alternative<preprocess::ResizeOp>(cfg)) {
            if (const auto&[width, height] = std::get<preprocess::ResizeOp>(cfg); width > 0 && height > 0)
                resize = ResizeTarget{width, height};
            continue;   // 任何 ResizeOp 都不入轻队列（含非正尺寸的退化 resize）。
        }
        if (isGeoCfg(cfg)) geo.add(cfg);
        else color.add(cfg);
    }
    for (const auto& cfg : geo.steps()) light.add(cfg);
    for (const auto& cfg : color.steps()) light.add(cfg);
    commitPipeline(light, resize,
                   QStringLiteral("已载入预处理队列"),
                   QStringLiteral("正在按载入的队列重放图像，请稍候…"));
}

// ---- 轻操作步骤增删移改（面板步骤列表驱动；仅作用于 steps_，resize 保持不变）----
// 克隆现轻队列 → mutate → 连同现 resize 一并交 commitPipeline。resize 未变 → baseDirty=false（除首次）
// → 复用 base_ 只重放轻后缀，不重采样、免弹框。
void PreprocessController::onStepAppend(const preprocess::PreprocessConfig& cfg) {
    if (!doc_->hasImage()) return;
    preprocess::PreprocessPipeline next = doc_->steps();
    appendLightKeepingInvariant(next, cfg);   // 几何入几何块尾、色彩入队尾（保持 geo 前 color 后）
    commitPipeline(next, doc_->resizeTarget(), QStringLiteral("已添加预处理步骤"), QString());
}

void PreprocessController::onStepReplace(const int index, preprocess::PreprocessConfig cfg) {
    if (index < 0) return;
    preprocess::PreprocessPipeline next = doc_->steps();
    next.setStep(static_cast<std::size_t>(index), std::move(cfg));
    commitPipeline(next, doc_->resizeTarget(), QStringLiteral("已更新预处理步骤"), QString());
}

void PreprocessController::onStepRemove(const int index) {
    if (index < 0) return;
    preprocess::PreprocessPipeline next = doc_->steps();
    next.remove(static_cast<std::size_t>(index));
    commitPipeline(next, doc_->resizeTarget(), QStringLiteral("已删除预处理步骤"), QString());
}

void PreprocessController::onStepMove(const int index, const bool down) {
    if (index < 0) return;
    preprocess::PreprocessPipeline next = doc_->steps();
    const auto& steps = next.steps();
    const auto i = static_cast<std::size_t>(index);
    if (i >= steps.size()) return;
    // 块内移动限幅：仅当相邻项与本项同类（同属几何块或同属色彩块）才换序，避免跨块破坏 geo→color 不变式。
    const std::size_t j = down ? i + 1 : i - 1;
    if (down && j >= steps.size()) return;
    if (!down && j >= steps.size()) return;   // i==0 时 i-1 下溢落入此分支
    if (isGeoCfg(steps[i]) != isGeoCfg(steps[j])) return;
    if (down) next.moveDown(i);
    else next.moveUp(i);
    commitPipeline(next, doc_->resizeTarget(), QStringLiteral("已调整步骤顺序"), QString());
}

// 重置预处理：清空 resize 与轻队列、工作图恢复原图（Document 内部处理，无需重放）。
// 失效门与 commitPipeline 同标准的完整签名（宽高 + 净旋转象限）：与恒等签名 {原宽,原高,0} 不等即清；
// 只看宽高会让 [rotate180]/[rotate90×2] 重置（内容转回 180°、象限 2→0）时漏清基于旧几何的选区/标注。
void PreprocessController::onResetPreprocess() const {
    if (!doc_->hasImage() || !doc_->hasPreprocess()) return;
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
    commitPipeline(doc_->steps(), ResizeTarget{width, height},
                   QStringLiteral("已设置画布尺寸 %1×%2").arg(width).arg(height),
                   QStringLiteral("正在缩放到目标尺寸，请稍候…"));
}

// 按比例缩放：以当前 resize 目标（无则为原图尺寸）为基准 × 倍数换算新绝对目标——
// 恒从原图重烤（不变式），故基准是 resize 自身目标尺寸而非含旋转的最终工作图。
void PreprocessController::onScaleResize(const double factor) {
    if (!doc_->hasImage() || factor <= 0.0) return;
    int bw = doc_->original().width();
    int bh = doc_->original().height();
    if (const auto r = doc_->resizeTarget(); r.has_value()) {
        bw = r->width;
        bh = r->height;
    }
    const int tw = std::max(1, static_cast<int>(std::lround(bw * factor)));
    const int th = std::max(1, static_cast<int>(std::lround(bh * factor)));
    commitPipeline(doc_->steps(), ResizeTarget{tw, th},
                   QStringLiteral("已按比例缩放至 %1×%2").arg(tw).arg(th),
                   QStringLiteral("正在按比例缩放图像，请稍候…"));
}

// 清除独立 resize（回到无 resize 的原图基准）。
void PreprocessController::onClearResize() {
    if (!doc_->hasImage()) return;
    commitPipeline(doc_->steps(), std::nullopt,
                   QStringLiteral("已清除画布尺寸"), QString());
}

// ---- 菜单/快捷键入口：各追加一步轻操作（走不变式追加：几何插几何块尾、色彩追加队尾）----
void PreprocessController::onRotate(const int angleDeg) {
    if (!doc_->hasImage()) return;
    preprocess::PreprocessPipeline next = doc_->steps();
    appendLightKeepingInvariant(next, preprocess::RotateOp{angleDeg});
    commitPipeline(next, doc_->resizeTarget(), QStringLiteral("已添加旋转 %1°").arg(angleDeg), QString());
}

void PreprocessController::onFlip(const bool horizontal) {
    if (!doc_->hasImage()) return;
    preprocess::PreprocessPipeline next = doc_->steps();
    appendLightKeepingInvariant(next, preprocess::FlipOp{horizontal});
    commitPipeline(next, doc_->resizeTarget(),
                   horizontal ? QStringLiteral("已添加水平翻转") : QStringLiteral("已添加垂直翻转"),
                   QString());
}

void PreprocessController::onGray() {
    if (!doc_->hasImage()) return;
    preprocess::PreprocessPipeline next = doc_->steps();
    appendLightKeepingInvariant(next, preprocess::GrayOp{});
    commitPipeline(next, doc_->resizeTarget(), QStringLiteral("已添加黑白"), QString());
}

// 色道反色：拼长度 3 反相掩码（'1' 反相、'0' 保持）追加 InvertOp（灰度图：mask 全 '0' 为空操作，否则逐字节整体反相）。
void PreprocessController::onInvert(const bool invR, const bool invG, const bool invB) {
    if (!doc_->hasImage()) return;
    const std::string mask = std::string(invR ? "1" : "0") + (invG ? "1" : "0") + (invB ? "1" : "0");
    preprocess::PreprocessPipeline next = doc_->steps();
    appendLightKeepingInvariant(next, preprocess::InvertOp{mask});
    commitPipeline(next, doc_->resizeTarget(), QStringLiteral("已添加按通道反色"), QString());
}

// 色道分离：拼长度 3 保留掩码（'1' 保留、'0' 置零）追加 SplitOp。
void PreprocessController::onSplit(const bool keepR, const bool keepG, const bool keepB) {
    if (!doc_->hasImage()) return;
    const std::string mask = std::string(keepR ? "1" : "0") + (keepG ? "1" : "0") + (keepB ? "1" : "0");
    preprocess::PreprocessPipeline next = doc_->steps();
    appendLightKeepingInvariant(next, preprocess::SplitOp{mask});
    commitPipeline(next, doc_->resizeTarget(), QStringLiteral("已添加按通道分离"), QString());
}

} // namespace idc::gui
