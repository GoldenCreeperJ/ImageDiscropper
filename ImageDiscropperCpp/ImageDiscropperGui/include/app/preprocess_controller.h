// ============================================================================
// 文件：app/preprocess_controller.h
// 作用：PreprocessController——预处理编排控制器：从 MainWindow 拆出 8 个预处理槽、
//       维度变化失效清理（applyWorkingImage）与忙碌对话框（runWithBusyDialog）。
// 分块依据：
//   - 预处理（FR-1 / G-3）是「图像页面板 + 图像菜单」共用的完整数据流，独占一块；
//   - 各操作经 EngineBridge 调 Core pixel_ops，GUI 不自实现像素运算（分层纪律）。
// 说明：图像菜单的动作经 MainWindowUi 的 lambda 直接调用本控制器 public 槽（与面板信号同一入口）。
// ============================================================================
#pragma once

#include <QObject>

#include <functional>

#include "core/image.h"

class QWidget;

namespace idc::gui {

class Document;
class AnnotationBridge;
class StatusBar;
class ImagePanel;

// ---------------------------------------------------------------------------
// PreprocessController：预处理编排（旋转/翻转/缩放/尺寸/黑白/反色/色道分离/重置）。
// ---------------------------------------------------------------------------
class PreprocessController : public QObject {
    Q_OBJECT
public:
    explicit PreprocessController(QObject* parent = nullptr);

    // 依赖注入（setter 模式，与面板一致）。
    void setDocument(Document* doc);
    void setAnnotationBridge(AnnotationBridge* anno);  // 维度变化 clearAll
    void setStatusBar(StatusBar* status);
    void setDialogParent(QWidget* parent);             // 忙碌对话框父窗口（MainWindow）
    void setImagePanel(ImagePanel* panel);
    void connectSignals();                             // ImagePanel 8 信号 → 8 槽

public slots:
    void onRotate(int angleDeg);                 // 旋转 90 的整数倍（-90/90/180）。
    void onFlip(bool horizontal);                // 水平/垂直翻转。
    void onScale(double factor);                 // 按比例缩放。
    void onResize(int newWidth, int newHeight);  // 目标尺寸缩放。
    void onGray();                               // 黑白（灰度）。
    // 色道反色：invR/invG/invB 指示反相哪些通道（未反相的通道保持不变）。
    void onInvert(bool invR, bool invG, bool invB);
    void onSplit(bool keepR, bool keepG, bool keepB); // 色道分离（保留勾选通道）。
    void onResetPreprocess();                    // 重置预处理（恢复原图）。

private:
    // 预处理公共收尾：维度变化时清除失效选区（坐标基于旧尺寸），再写回工作图（触发 imageChanged）。
    void applyWorkingImage(core::Image next, const QString& okMsg);
    // 在模态忙碌对话框（不可取消、阻断其余输入）内同步执行 op：用于缩放/尺寸等可能耗时的重采样。
    void runWithBusyDialog(const QString& text, const std::function<void()>& op);

    Document* doc_{nullptr};
    AnnotationBridge* anno_{nullptr};
    StatusBar* status_{nullptr};
    QWidget* dialogParent_{nullptr};
    ImagePanel* imagePanel_{nullptr};
    // 重采样（缩放/尺寸）进行中标志：防止模态对话框期间的重入（如快捷键再次触发）。
    bool busyResample_{false};
};

} // namespace idc::gui
