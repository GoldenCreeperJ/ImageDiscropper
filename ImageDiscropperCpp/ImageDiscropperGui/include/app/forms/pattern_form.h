// ============================================================================
// 文件：app/forms/pattern_form.h
// 作用：PatternFormWidget——程序化图案参数表单（自 NewCanvasDialog 拆出）：底色（RGBA 经取色
//       对话框，可透明）、前景色（RGBA，仅非纯色图案）、图案类型（11 种）、方向（依类型动态
//       重建条目）、周期/格径、线宽、点半径、噪声种子与配色模式。
//       只做「控件 + 采参 + 图案相关显隐/文案」，像素生成一律交上游（CONTRIBUTING.md「分层纪律」）。
// 分块依据：新建画布 / 添加图片图层两个语境共用同一套图案采参，且与「尺寸 / 附加图片 / 两级
//       落位 / 预览 / 定稿」无耦合——独立组件后 NewCanvasDialog 只做剩余编排（对话框 ~700 行
//       拆至 ~350 行），Rust 第二实现亦可直接对齐本组件边界。
// 说明：任一控件变化发 paramsChanged()，上游据此刷新预览；params() 汇编全尺寸语义的
//       PatternParams（background 由本组件的底色控件供给）。
// ============================================================================
#pragma once

#include <QColor>
#include <QWidget>

#include "generate/pattern.h"

class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;

namespace idc::gui {

// ---------------------------------------------------------------------------
// PatternFormWidget：图案参数表单（底色/前景色/类型/方向/参数/噪声）。
// ---------------------------------------------------------------------------
class PatternFormWidget : public QWidget {
    Q_OBJECT
public:
    explicit PatternFormWidget(QWidget* parent = nullptr);

    // 汇编全尺寸语义的 PatternParams（含背景/前景色；period/lineWidth/dotRadius 全尺寸）。
    void params(generate::PatternParams& out) const;
    // 依图案类型刷新图案相关行显隐 / 方向条目重建 / 周期标签文案（供上游 onParamsChanged 调用；
    // 亦含首次构造后的初始刷新）。
    void updateAvailability();

signals:
    // 任一图案参数控件变化（供上游刷新预览）。
    void paramsChanged();

private slots:
    void onPickBackground();     // 弹出 QColorDialog（ShowAlphaChannel）选底色 RGBA。
    void onPickForeground();     // 弹出 QColorDialog（ShowAlphaChannel）选前景色 RGBA。

private:
    void updateSwatches() const;                                  // 重绘两个色块按钮底色。
    void rebuildOrientationCombo(generate::PatternKind kind) const; // 依图案类型重建方向条目（存 Orientation 枚举于 itemData）。

    // 图案类型 + 方向。
    QComboBox* kindCombo_{nullptr};
    QComboBox* orientationCombo_{nullptr};
    // 颜色（底色 / 前景色均为 RGBA，alpha 经取色对话框设定，不另设独立控件）。
    QPushButton* bgSwatch_{nullptr};
    QPushButton* fgSwatch_{nullptr};
    QLabel* fgLabel_{nullptr};   // 「前景色：」标题（纯色时随行隐藏）。
    QColor bgColor_{QColor(255, 255, 255, 255)};   // 底色 RGBA。
    QColor fgColor_{QColor(0, 0, 0, 255)};         // 前景色 RGBA。
    // 图案参数。
    QSpinBox* periodSpin_{nullptr};
    QSpinBox* lineWidthSpin_{nullptr};
    QSpinBox* dotRadiusSpin_{nullptr};
    QLabel* periodLabel_{nullptr};
    QLabel* lineWidthLabel_{nullptr};
    QLabel* dotRadiusLabel_{nullptr};
    QLabel* orientationLabel_{nullptr};
    // 噪声参数（仅 Noise 图案显隐）。
    QLabel* seedLabel_{nullptr};
    QSpinBox* seedSpin_{nullptr};
    QLabel* noiseModeLabel_{nullptr};
    QComboBox* noiseModeCombo_{nullptr};

    generate::PatternKind shownKind_{static_cast<generate::PatternKind>(-1)};   // 上次生效的图案类型，用于检测切换（重建方向条目、色相波长初值 128）。
};

} // namespace idc::gui
