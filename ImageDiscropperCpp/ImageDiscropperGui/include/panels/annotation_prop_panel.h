// ============================================================================
// 文件：panels/annotation_prop_panel.h
// 作用：右侧「标注」属性页——颜色选择器、描边粗细、填充开关、
//       文字内容与字号。面板只采集属性意图并发信号，MainWindow 写回 AnnotationBridge
//       （EDIT 模式作用选中项，DRAW 模式作为下一次绘制默认）；面板不含几何 / 光栅化（CONTRIBUTING.md「分层纪律」）。
// 分块依据：每个属性控件对应一个信号，参数即最小信息（QColor / int / bool / QString / double）；
//           syncFromModel 从模型回显（选中项属性，否则当前默认），blockSignals 防回环。
// 说明：Core Annotation 只有单一 color + fill 布尔（填充复用同一颜色），故本面板不提供独立填充色。
// ============================================================================
#pragma once

#include <QWidget>
#include <QColor>

#include "model/annotation_bridge.h"

class QPushButton;
class QSpinBox;
class QCheckBox;
class QLineEdit;
class QGroupBox;

namespace idc::gui {

// ---------------------------------------------------------------------------
// AnnotationPropPanel：标注属性编辑页。
// ---------------------------------------------------------------------------
class AnnotationPropPanel : public QWidget {
    Q_OBJECT
public:
    explicit AnnotationPropPanel(QWidget* parent = nullptr);

    // 绑定标注模型（用于回显当前 / 选中项属性）。
    void setModel(AnnotationBridge* model);
    // 从模型反向同步：颜色 / 粗细 / 填充取 displayXxx，文字 / 字号取 currentXxx（blockSignals 防回环）。
    void syncFromModel();

    // 手柄拖拽进行中：把当前预览的**绝对**缩放系数(1.0=100%、负即翻转)/旋转角(度)实时回显到变换区数值
    // （blockSignals 防回环，不触发 transformApplyRequested）。数值超出 spin 范围时由 QSpinBox 自动钳制。
    void setTransformPreview(double sx, double sy, double rotateDeg) const;

signals:
    void colorPicked(const QColor& c);   // 颜色（描边 + 填充共用）
    void strokeChanged(int width);       // 描边粗细（像素）
    void fillChanged(bool fill);         // 是否填充
    void textChanged(const QString& text);   // 文字内容
    void fontSizeChanged(double size);   // 字号
    // 变换意图（仅作用于当前选中标注）：sx/sy 为缩放增量系数（1.0=不变）、rotateDeg 为旋转角增量（度）。
    // 由变换区 spin 改动直接触发（无「应用」按钮）；翻转无面板按钮（由画布包围盒手柄拖拽越过对边实现）。
    void transformApplyRequested(double sx, double sy, double rotateDeg);

private slots:
    void onPickColor();   // 打开取色器

private:
    void updateSwatch() const;  // 依当前颜色刷新色块按钮背景

    AnnotationBridge* model_{nullptr};
    QColor color_{Qt::black};        // 当前编辑中的颜色（色块显示用）
    QPushButton* colorBtn_{nullptr};
    QSpinBox* strokeSpin_{nullptr};
    QCheckBox* fillCheck_{nullptr};
    QLineEdit* textEdit_{nullptr};
    QSpinBox* fontSpin_{nullptr};
    // ---- 变换区（仅对选中标注生效，无选中时置灰；显示**绝对累积值**、忠实反映底层变换）----
    // 无「应用」按钮：任一 spin 改动即按其单轴换算增量下发（keyboardTracking 关闭，避免逐键触发）。
    QGroupBox* transformGroup_{nullptr};
    QSpinBox* scaleXSpin_{nullptr};      // 水平缩放绝对 %（100=原尺寸、负=翻转）
    QSpinBox* scaleYSpin_{nullptr};      // 垂直缩放绝对 %（100=原尺寸、负=翻转）
    QSpinBox* rotateSpin_{nullptr};      // 旋转绝对角度 °（累积）
    // 上次 syncFromModel 时选中形状的绝对变换基准（供 spin 改动把目标绝对值换算为单轴增量）。
    double curObbSx_{1.0};
    double curObbSy_{1.0};
    double curObbRot_{0.0};
};

} // namespace idc::gui
