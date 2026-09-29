// ============================================================================
// 文件：app/preprocess_controller.h
// 作用：PreprocessController——预处理编排控制器：从 MainWindow 拆出声明式队列的
//       增删移改提交链（commitPipeline：重放 + 几何签名尺寸门）与忙碌对话框（runWithBusyDialog）。
// 分块依据：
//   - 预处理（FR-1 / G-3）是「图像页面板 + 图像菜单」共用的完整数据流，独占一块；
//   - 队列物化经 EngineBridge::applyPipeline 委托 Core PreprocessPipeline::apply，GUI 不自实现像素运算（分层纪律）。
// 说明：图像菜单的动作经 MainWindowUi 的 lambda 直接调用本控制器 public 槽（与面板信号同一入口）。
// ============================================================================
#pragma once

#include <QObject>

#include <functional>
#include <optional>

#include "core/image.h"
#include "model/document.h"        // ResizeTarget（独立画布尺寸）
#include "preprocess/preprocess_pipeline.h"

class QWidget;

namespace idc::gui {

class Document;
class AnnotationBridge;
class StatusBar;
class ImagePanel;

// ---------------------------------------------------------------------------
// PreprocessController：声明式预处理队列编排（入队 / 编辑 / 删除 / 移动 / 重置）。
// 每次队列编辑：从 original_ 重放整链物化 working_，并按几何签名（geomSig）前后比较做尺寸门
//（尺寸或净旋转象限变 → 清框选/多矩形/单元/标注；否则保留），再回灌 Document。
// ---------------------------------------------------------------------------
class PreprocessController : public QObject {
    Q_OBJECT
public:
    explicit PreprocessController(QObject* parent = nullptr);

    // 依赖注入（setter 模式，与面板一致）。
    void setDocument(Document* doc);
    void setAnnotationBridge(AnnotationBridge* anno);  // 几何失效 clearAll
    void setStatusBar(StatusBar* status);
    void setDialogParent(QWidget* parent);             // 忙碌对话框父窗口（MainWindow）
    /// 忙碌全屏遮罩模式（SPEC §8.2 桌面对话框→移动端全屏遮罩）：
    /// 仅 MobileShell 开启；默认 false 保持桌面居中小对话框不变。
    void setBusyOverlayMode(bool overlay);
    void setImagePanel(ImagePanel* panel);
    void connectSignals();                             // ImagePanel 队列意图信号 → 本控制器槽

    // 配置载入：把一份已加载的预处理队列（含 ResizeOp）应用到当前图——从 original_ 重放、
    // 过尺寸门、回灌 Document。由 MainWindow::onLoadConfig 在参数还原后调用。
    void applyLoadedQueue(const preprocess::PreprocessPipeline& pipeline);

public slots:
    // ---- 轻操作步骤增删移改（面板步骤列表驱动；仅作用于 steps_，不含 resize）----
    void onStepAppend(const preprocess::PreprocessConfig &cfg);            // 增：追加一步到队尾。
    void onStepReplace(int index, preprocess::PreprocessConfig cfg);// 改：就地替换指定步。
    void onStepRemove(int index);                                  // 删：移除指定步。
    void onStepMove(int index, bool down);                        // 移：上/下移指定步（换序）。
    void onResetPreprocess() const;                               // 清空 resize 与队列、恢复原图。

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
    // 在模态忙碌对话框（不可取消、阻断其余输入）内同步执行 op：用于含 resize 的重采样重放。
    void runWithBusyDialog(const QString& text, const std::function<void()>& op) const;

    Document* doc_{nullptr};
    AnnotationBridge* anno_{nullptr};
    StatusBar* status_{nullptr};
    QWidget* dialogParent_{nullptr};
    ImagePanel* imagePanel_{nullptr};
    // 重采样（含 resize 的整链重放）进行中标志：防止模态对话框期间的重入（如快捷键再次触发）。
    bool busyResample_{false};
    bool busyOverlay_{false};             // 忙碌全屏遮罩模式（MobileShell 开启；SPEC §8.2）
};

} // namespace idc::gui
