// ============================================================================
// 文件：canvas/canvas_touch_input_adapter.h
// 作用：TouchInputAdapter——画布触控输入适配器（GuideLine 阶段 3，SPEC §8.3 手势映射）。
// 设计（行为同构、零行为分支）：
//   - 单指：不接管原始触控事件——Android QPA 会把单指触控合成为 Qt::LeftButton
//     鼠标序列，直接委托内嵌 DesktopInputAdapter 路由（图元抓取 / 框选 / 绘制
//     与桌面完全同码），本类只叠加**双击检测**（短按两连击 = 适应窗口）；
//   - 双指：grabGesture(Qt::PinchGesture)，在 onGestureEvent 里换算
//     捏合缩放（gestureStepZoom，钳制复用桌面链路）与中心位移平移（gesturePanDelta）；
//   - 长按：Android QPA 自动合成 QContextMenuEvent → 桌面右键等价菜单原样可用；
//   - 触控精度：screenOverlayPx()=12（逻辑 px≈dp），视图据此换算手柄/抓边带
//     场景尺寸（≥12pt，SPEC §8.3）；图元与 Coordinator 无需知晓。
// 分块依据：设备→业务的翻译全在此类；双击/pinch 检测态不外泄给视图。
// 说明：仅在 Android/iOS 构建被实例化（canvas_view.cpp 构造按平台换装；iOS 无鼠标、
//       QPA 同样合成触摸为鼠标序列，单指/双击/捏合语义与 Android 一致）；
//       真机验证属阶段 5（GuideLine 风险 1：桌面路径零改动）。
// ============================================================================
#pragma once

#include <QElapsedTimer>
#include <QPoint>

#include "canvas/canvas_input_adapter.h"
#include "canvas/canvas_desktop_input_adapter.h"

namespace idc::gui {

class CanvasView;

// ---------------------------------------------------------------------------
// TouchInputAdapter：触控 → 业务手势翻译（合成鼠标复用桌面路由 + 手势/双击叠加）。
// ---------------------------------------------------------------------------
class TouchInputAdapter final : public ICanvasInputAdapter {
public:
    // 安装挂钩：注册捏合手势（Qt::PinchGesture）。
    void attach(CanvasView& view) override;

    // 抓取带屏幕基准：12 逻辑 px（≈12pt，SPEC §8.3 触控精度）。
    qreal screenOverlayPx() const override;

    // 单指序列（QPA 合成鼠标事件）：双击过滤 + 委托桌面路由。
    void onPress(CanvasView& view, QMouseEvent* event) override;
    void onMove(CanvasView& view, QMouseEvent* event) override;
    void onRelease(CanvasView& view, QMouseEvent* event) override;

    // 触控无滚轮：不消费（桌面滚轮语义在 DesktopInputAdapter，不经此处）。
    void onWheel(CanvasView& view, QWheelEvent* event) override;

    // 长按菜单：QPA 合成的 QContextMenuEvent 与桌面右键同义，直接委托桌面路由。
    void onContextMenu(CanvasView& view, QContextMenuEvent* event) override;

    // 双指捏合：缩放（scaleFactor 增量）+ 平移（中心点位移差）；消费并作废本序列 tap 判定。
    bool onGestureEvent(CanvasView& view, QGestureEvent* event) override;

private:
    DesktopInputAdapter router_;   // 合成鼠标序列的既有路由（行为同构的根）

    // ——双击检测态（阈值常量见 .cpp）——
    QPoint pressPos_;              // 本序列按下位置（视口坐标）
    QPoint lastTapPos_;            // 上一次有效轻点的释放位置
    bool tapCandidate_{false};     // 本序列 press→release 仍可能构成轻点（未大幅移动）
    bool gestureSeen_{false};      // 本序列中出现过双指手势 → 不作数（防误判 tap）
    bool swallowing_{false};       // 双击第二击已消费 → 吞掉本序列剩余合成事件
    QElapsedTimer pressClock_;     // 按下起计时（轻点时长判定）
    QElapsedTimer tapClock_;       // 上次轻点完成起计时（双击间隔判定）
};

} // namespace idc::gui
