// ============================================================================
// 文件：app/controllers/preprocess_controller.h
// 作用：PreprocessController——预处理编排控制器：承载声明式队列的
//       增删移改提交链（commitPipeline：重放 + 几何签名尺寸门）与忙碌对话框（runWithBusyDialog）。
// 分块依据：
//   - 预处理（SPEC §3.1 FR-1）是「图像页面板 + 图像菜单」共用的完整数据流，独占一块；
//   - 队列物化经 EngineBridge::applyPipeline 委托 Core PreprocessPipeline::apply，GUI 不自实现像素运算（分层纪律）。
// 说明：图像菜单的动作经 MainWindowUi 的 lambda 直接调用本控制器 public 槽（与面板信号同一入口）。
// ============================================================================
#pragma once

#include <QObject>

#include <functional>
#include <optional>
#include <string>

#include "canvas/layer.h"           // ImageLayer（图层物化缓存刷新入参：std::vector<ImageLayer>）
#include "core/image.h"
#include "model/document.h"        // ResizeTarget（独立画布尺寸）
#include "preprocess/preprocess_pipeline.h"

class QWidget;

namespace idc::gui {

class Document;
class StatusBar;
class PreprocessPanel;

// ---------------------------------------------------------------------------
// runInBusyDialog：在应用级模态、不可取消的忙碌对话框（不确定进度条）内同步执行 op。
//   用于任何可能在 UI 线程耗时数秒的操作（预处理重采样、全尺寸图案生成等），避免用户误以为
//   卡死而重复点击。preprocess_controller 成员与 NewCanvasDialog 等独立对话框共用此单点实现。
//   parent 为对话框父窗口；fullscreen=true 时铺满主屏（移动端全屏遮罩，SPEC §8.2），否则居中小框。
// ---------------------------------------------------------------------------
void runInBusyDialog(QWidget* parent, bool fullscreen, const QString& text,
                     const std::function<void()>& op);

// ---------------------------------------------------------------------------
// PreprocessController：声明式预处理队列编排（入队 / 编辑 / 删除 / 移动 / 重置）。
// 每次队列编辑：从 original_ 重放整链物化 working_，并按几何签名（geomSig）前后比较做尺寸门
//（尺寸或净旋转象限变 → 仅清框选/多矩形/单元；标注与图片图层恒保留——矢量叠加可落在画布外），再回灌 Document。
// ---------------------------------------------------------------------------
class PreprocessController : public QObject {
    Q_OBJECT
public:
    explicit PreprocessController(QObject* parent = nullptr);

    // 依赖注入（setter 模式，与面板一致）。
    void setDocument(Document* doc);
    void setStatusBar(StatusBar* status);
    void setDialogParent(QWidget* parent);             // 忙碌对话框父窗口（MainWindow）
    /// 忙碌全屏遮罩模式（SPEC §8.2 桌面对话框→移动端全屏遮罩）：
    /// 仅 MobileShell 开启；默认 false 保持桌面居中小对话框不变。
    void setBusyOverlayMode(bool overlay);
    /// 当前是否处于忙碌全屏遮罩形态（SPEC §8.2）：供 SessionController 把同形态传给 NewCanvasDialog 等独立对话框。
    bool busyOverlayMode() const { return busyOverlay_; }
    void setPreprocessPanel(PreprocessPanel* panel);
    void connectSignals();                             // PreprocessPanel 队列意图信号 → 本控制器槽

    // 配置载入：把一份已加载的预处理队列（含 ResizeOp）应用到当前图——从 original_ 重放、
    // 过尺寸门、回灌 Document。由 ConfigController::onLoadConfig 在参数还原后调用。
    void applyLoadedQueue(const preprocess::PreprocessPipeline& pipeline);

    // 多层画布重组（接 Document::layersChanged）：图层栈/画布尺寸声明变时，由 EngineBridge::composeCanvas
    // 按新栈展平得画布底图 → setOriginalImage（失效画布 base_）→ 在新基准上重放画布级预处理产出 working_。
    // 非图层画布（单一载入图）早退，不触碰既有链路。由 MainWindow 将 Document::layersChanged 接入。
    void recomposeLayerCanvas();

    // 导出用「图片图层是否并入」的画布现算（对标标注烧录：导出侧独立于「显示」预览开关）：
    // 依 includeOverlays 重跑 composeCanvas（叠加层含 / 不含）+ 施加当前画布级有效管线，**无副作用**（不改 Document）。
    // 与当前预览态（imageLayersVisible）一致时直接复用 working_ 免二次合成；非图层画布恒返回 working_。
    core::Image composeLayerCanvasForExport(bool includeOverlays) const;

    // 在当前 original_ 基准上重放现有画布级预处理声明（steps_ + resize_）产出 working_：
    //   供「新建画布」（单图态）与「清空图层」（回退单图底）替换底图后补算色彩预处理，与常规提交同走 commitPipeline
    //   （几何签名前后一致→不误清选区/标注）。非图态且无预处理声明时仍会重算（空队列→original_ 副本）。
    void replayCanvasPreprocess();

public slots:
    // ---- 轻操作步骤增删移改（面板步骤列表驱动；仅作用于 steps_，不含 resize）----
    void onStepAppend(const preprocess::PreprocessConfig &cfg);            // 增：追加一步到队尾。
    void onStepReplace(int index, preprocess::PreprocessConfig cfg);// 改：就地替换指定步。
    void onStepRemove(int index);                                  // 删：移除指定步。
    void onStepMove(int index, bool down);                        // 移：上/下移指定步（换序）。
    void onResetPreprocess() const;                                 // 清空当前目标 resize 与队列（画布恢复原图 / 图层清声明）。

    // ---- 独立 resize（面板「画布尺寸」区驱动；恒最先重放、从原图重采样）----
    void onSetResizeTarget(int width, int height);  // 设定目标画布尺寸（绝对）。
    void onScaleResize(double factor);              // 按比例：按当前输出尺寸换算新的绝对目标。
    void onClearResize();                           // 清除独立 resize（回到原图尺寸）。

    // ---- 菜单/快捷键入口：各对应「追加一步轻操作」，委托 commitPipeline。----
    void onRotate(int angleDeg);                 // 追加旋转（-90/90/180）。
    void onFlip(bool horizontal);                // 追加水平/垂直翻转。
    void onGray();                               // 追加黑白（灰度）。
    void onInvert(bool invR, bool invG, bool invB); // 追加按通道反色。
    void onSplit(bool keepR, bool keepG, bool keepB); // 追加按通道分离。

private:
    // 队列编辑公共提交：以「新轻队列 nextSteps + 新 resize nextResize」为结果态→算 geomSig 前后变化
    //（变了则清选区/标注）→ 依 base_（= resize(original)）缓存重放物化 working_ → 回灌 Document。
    // 成本门控：仅当 resize 前缀相对已缓存 base_ 发生变化（baseDirty）且确有 resize 需重采样时，
    // 才弹模态忙碌框；无 resize 的轻编辑（含首步 base=原图副本）只走轻链、免弹框。
    void commitPipeline(const preprocess::PreprocessPipeline& nextSteps,
                        const std::optional<ResizeTarget>& nextResize,
                        const QString& okMsg, const QString& busyText);
    // 提交到「当前预处理目标」（Document::preprocessTarget）：未选中图层→画布（等同 commitPipeline，
    // working_ 物化链路不变）；选中图片图层→写其会话声明并折进 layer.preprocess，交 updateLayer
    // 发 layersChanged 由 recomposeLayerCanvas 重组（逐层预处理由 Core materializeLayer 物化）。
    void commitActive(const preprocess::PreprocessPipeline& nextSteps,
                      const std::optional<ResizeTarget>& nextResize,
                      const QString& okMsg, const QString& busyText);
    // 当前目标会话的声明态只读视图（画布→preprocess_、选中图层→其 layerSessions_ 条目）。
    const preprocess::PreprocessPipeline& activeSteps() const;
    const std::optional<ResizeTarget>& activeResize() const;
    // 在模态忙碌对话框（不可取消、阻断其余输入）内同步执行 op：用于含 resize 的重采样重放。
    void runWithBusyDialog(const QString& text, const std::function<void()>& op) const;

    // ---------------------------------------------------------------------------
    // 图层物化缓存刷新：composeCanvas 每次全量物化所有层（文件层重跑 applyExec、配方层重跑 makePattern），
    //   拖动某层＝所有层预处理重跑。每层「声明态会话 + 物化签名 + 缩放前位图」统一由 Document 持有
    //   （LayerSessionStore，见 model/layer_session_store.h）；本控制器仅驱动刷新，不自持缓存。
    // ---------------------------------------------------------------------------
    // 依当前栈刷新 Document 图层会话库的物化缓存：签名变的层才重跑 materializeLayer（写回 store），命中层复用缓存位图；
    //   同时经 store.prune 剔除已删层。刷新后 store.materialized() 直作 composeCanvas 的 materialized 入参（未命中回退由 Core 现算）。
    void syncLayerMaterialCache(const std::vector<canvas::ImageLayer>& layers) const;

    Document* doc_{nullptr};
    StatusBar* status_{nullptr};
    QWidget* dialogParent_{nullptr};
    PreprocessPanel* preprocessPanel_{nullptr};
    // 重采样（含 resize 的整链重放）进行中标志：防止模态对话框期间的重入（如快捷键再次触发）。
    bool busyResample_{false};
    bool busyOverlay_{false};             // 忙碌全屏遮罩模式（MobileShell 开启；SPEC §8.2）
};

} // namespace idc::gui
