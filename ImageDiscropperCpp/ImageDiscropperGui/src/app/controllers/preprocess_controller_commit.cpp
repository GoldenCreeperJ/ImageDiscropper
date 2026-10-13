// ============================================================================
// 文件：app/preprocess_controller_commit.cpp
// 作用：PreprocessController 的「提交链」成员实现（TU 拆分）——编辑公共提交 commitPipeline
//       （geomSig 尺寸门 + base_ 缓存重放 + 回灌 Document）、按目标提交 commitActive
//       （画布→commitPipeline / 选中图层→会话声明折进 layer.preprocess 交重组）与忙碌框
//       （runInBusyDialog 单点实现 + runWithBusyDialog 成员封装）。自 preprocess_controller.cpp
//       原封迁出（同 Document 先例：类不拆、仅分 TU 编译，公有 API 不变）。
// ============================================================================
#include "app/controllers/preprocess_controller.h"

#include <QApplication>
#include <QProgressDialog>
#include <QScreen>

#include <algorithm>
#include <cmath>
#include <type_traits>
#include <utility>
#include <variant>

#include "app/status_bar.h"
#include "canvas/layer.h"
#include "model/annotation_bridge.h"
#include "model/document.h"
#include "model/engine_bridge.h"
#include "model/layer_session_store.h"
#include "model/preprocess_session.h"
#include "preprocess/preprocess_config.h"
#include "i18n/gui_texts.h"
#include "i18n/gui_i18n.h"

namespace idc::gui {


// 编辑公共提交：以「新轻队列 nextSteps + 新 resize nextResize」为结果态：
//   ① geomSig 前后比较判尺寸门（尺寸/净旋转象限变 → 清失效框选；标注/图层/色彩预处理恒保留）；
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
    const auto beforeSig = doc_->geomSig();
    // 编辑后几何签名（结果态有效管线）。完整签名判据：输出尺寸或净旋转象限任一变化即触发门
    //（只看宽高会让 180° 换角这类尺寸不变、内容已转的提交漏清基于旧几何的框选）。
    const auto afterSig = PreprocessSession::makeEffective(nextResize, nextSteps)
                              .geometryOutput(doc_->original().width(), doc_->original().height());
    const bool geomChanged = afterSig.width != beforeSig.width || afterSig.height != beforeSig.height ||
                             afterSig.rotQuad != beforeSig.rotQuad;

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
                    status_->notify(IDC_GTR(status, kPrepFailedEmpty), true);
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
            status_->notify(IDC_GTR(status, kPrepFailedEmpty), true);
            return;
        }
        if (geomChanged) {
            // 尺寸/净旋转象限变：既有框选坐标基于旧几何失效 → 仅清框选（rect/cells）。
            // 标注/图片图层栈/色彩预处理**恒保留**（resize/rotate 口径）：标注为矢量叠加，可存在于画布外
            //（与 setCanvasSize「只清框选」同口径，决策 3）；图层落位随重组链重放同一管线同步变换；色彩步不碰几何。
            doc_->clearRect();
            doc_->clearRects();
            doc_->clearCells();
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
                                      ? IDC_GTR(status, kPrepReplayBusy)
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
    dlg.setWindowTitle(IDC_GTR(label, kDlgTitleBusy));
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

} // namespace idc::gui
