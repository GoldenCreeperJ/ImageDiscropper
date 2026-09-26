// ============================================================================
// 文件：mobile/mobile_step_pad.h
// 作用：StepPadWidget——8 向步进盘（SPEC §8.3 无键盘补偿：方向键 1px 微调的触控等价物）。
//       3×3 网格：四向 + 四对角共 8 个按钮，中央为倍率切换（×1 / ×10，对应桌面 Shift 大步）。
// 分块依据：
//       - 纯输入采集控件：只发 nudgeRequested(dx,dy) 意图信号，不认识 Document/视图；
//       - 上层（MobileShell）把信号接 CanvasView::requestNudge——与键盘方向键同一条信号链路。
// 说明：按钮恒 ≥44×44pt（SPEC §8.3 可点控件下限）；经 QMenu+QWidgetAction 弹出使用。
// ============================================================================
#pragma once

#include <QWidget>

namespace idc::gui {

// ---------------------------------------------------------------------------
// StepPadWidget：8 向微调步进盘（点按一次发一步，可按住连发由 QToolButton 自带 repeat 提供）。
// ---------------------------------------------------------------------------
class StepPadWidget : public QWidget {
    Q_OBJECT
public:
    explicit StepPadWidget(QWidget* parent = nullptr);

signals:
    // 一步增量（原图像素；倍率为 ×1 或 ×10，含对角组合）。
    void nudgeRequested(int dx, int dy);

private:
    int step_{1};   // 当前倍率（中央按钮切换）
};

} // namespace idc::gui
