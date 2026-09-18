// ============================================================================
// 文件：panels/image_panel.h
// 作用：右侧「图像处理面板」（FR-1）——旋转、翻转、缩放、图像尺寸、
//       颜色调整（黑白 / 色道反色 / 色道分离）与「重置预处理」。面板只采集用户意图并发信号，
//       真正的像素变换由 MainWindow 经 EngineBridge 调 Core pixel_ops 完成（CONTRIBUTING.md「分层纪律」：GUI 不
//       自实现图像处理逻辑）。
// 分块依据：控件按预处理操作分组（旋转 / 翻转 / 缩放 / 尺寸 / 颜色 / 重置）组织；每类操作对应
//           一个 *Requested 信号，参数即该操作所需的最小信息（角度 / 方向 / 比例 / 目标尺寸 /
//           通道掩码位）。面板不持有真相、不碰 Core，只把交互翻译为意图信号。
// 说明：「颜色选取（取色器）」的落点与标注属性（描边/填充色）强相关，已由右侧「标注」属性页统一提供
//       （见 annotation_prop_panel）；本面板覆盖旋转/翻转/缩放/尺寸/黑白/色道反色/色道分离/重置。
// ============================================================================
#pragma once

#include <QWidget>

class QPushButton;
class QSpinBox;
class QCheckBox;
class QGroupBox;

namespace idc::gui {

class Document;

// ---------------------------------------------------------------------------
// ImagePanel：基础图像处理（预处理）操作面板。
// ---------------------------------------------------------------------------
class ImagePanel : public QWidget {
    Q_OBJECT
public:
    explicit ImagePanel(QWidget* parent = nullptr);

    // 绑定会话状态源：用于读取当前工作图尺寸（尺寸框默认值）与启停控件。
    void setDocument(Document* doc);
    // 从 Document 反向同步：尺寸框回灌当前工作图宽高、按 hasImage/hasPreprocess 启停控件。
    void syncFromDocument();

signals:
    // 旋转：angleDeg 为 90 的整数倍（右转 90、左转 -90、180）。
    void rotateRequested(int angleDeg);
    // 翻转：horizontal=true 水平（左右）、false 垂直（上下）。
    void flipRequested(bool horizontal);
    // 按比例缩放：factor 为倍数（如 0.5 表示缩小到一半）。
    void scaleRequested(double factor);
    // 目标尺寸缩放：直接指定新宽高（像素）。
    void resizeRequested(int newWidth, int newHeight);
    // 黑白（灰度化）。
    void grayRequested();
    // 色道反色：invR/invG/invB 指示反色哪些通道（未选中的保持不变；灰度图整体反相）。
    void invertRequested(bool invR, bool invG, bool invB);
    // 色道分离：keepR/keepG/keepB 指示保留哪些通道（未保留的置零）。
    void splitRequested(bool keepR, bool keepG, bool keepB);
    // 重置预处理：工作图恢复为原图。
    void resetRequested();

private slots:
    void onScaleApply();     // 「按比例缩放」应用按钮。
    void onResizeApply();    // 「目标尺寸」应用按钮。
    void onWidthEdited(int value) const;  // 保持宽高比时，改宽联动算高。

private:
    // 构建各分组控件（每组返回一个 QGroupBox，由构造函数统一排版，避免上帝构造函数）。
    QGroupBox* buildRotateGroup();
    QGroupBox* buildFlipGroup();
    QGroupBox* buildScaleGroup();
    QGroupBox* buildColorGroup();

    Document* doc_{nullptr};

    // 旋转 / 翻转。
    QPushButton* rotLeftBtn_{nullptr};
    QPushButton* rotRightBtn_{nullptr};
    QPushButton* rot180Btn_{nullptr};
    QPushButton* flipHBtn_{nullptr};
    QPushButton* flipVBtn_{nullptr};

    // 缩放（按比例）。
    QSpinBox* scalePct_{nullptr};        // 百分比 1..1000（默认 100）。
    QPushButton* scaleApplyBtn_{nullptr};

    // 图像尺寸（目标宽高）。
    QSpinBox* targetW_{nullptr};
    QSpinBox* targetH_{nullptr};
    QCheckBox* keepRatio_{nullptr};      // 保持宽高比：改宽联动算高。
    QPushButton* resizeApplyBtn_{nullptr};

    // 颜色调整。
    QPushButton* grayBtn_{nullptr};      // 黑白。
    QPushButton* invertBtn_{nullptr};    // 色道反色：反色勾选的通道。
    QCheckBox* chR_{nullptr};            // 通道选择 R（色道反色＝反色它 / 色道分离＝保留它）。
    QCheckBox* chG_{nullptr};            // 通道选择 G（同上）。
    QCheckBox* chB_{nullptr};            // 通道选择 B（同上）。
    QPushButton* splitBtn_{nullptr};     // 色道分离：仅保留勾选通道。

    // 重置预处理。
    QPushButton* resetBtn_{nullptr};
};

} // namespace idc::gui
