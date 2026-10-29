// ============================================================================
// 文件：canvas/canvas_input_adapter.h
// 作用：ICanvasInputAdapter——画布输入适配器抽象接口（输入适配器分层）。
//       把原始输入事件（桌面：鼠标 / 滚轮 / 右键；触控：合成鼠标序列 + 手势）翻译为 CanvasView 的
//       业务手势入口（gesture* 系列），使视图本体不含任何设备语义。
// 分块依据：
//   - 本头文件是纯接口与边界声明：「什么键 / 几根手指」只存在于各实现内部；
//   - 唯一调用方是 CanvasView（Qt 事件入口一行转发）；手势执行面（状态机 + 信号）
//     全在 CanvasView；适配器只持有单击延迟判定等**输入判定态**（桌面/触控各自的私有状态）。
// 说明：桌面实现 DesktopInputAdapter；触控实现 TouchInputAdapter（单指走
//       Qt 合成鼠标序列复用桌面路由，叠加双击适应/捏合缩放/抓取带 ≥12pt）；
//       两实现产出的业务手势序列必须等价（SPEC §8.3 行为同构、零行为分支）。
// ============================================================================
#pragma once

#include <QtGlobal>  // qreal

#include "util/gui_nums.h"

class QContextMenuEvent;
class QGestureEvent;
class QMouseEvent;
class QWheelEvent;

namespace idc::gui {

class CanvasView;

// ---------------------------------------------------------------------------
// ICanvasInputAdapter：原始事件 → 业务手势的翻译接口。
// 入口约定：实现方负责在翻译完成后对 event 调用 accept()/ignore()（按语义决定吞或放），
//           并按语义调用 view 的 gesture* / dispatchScene*。
// ---------------------------------------------------------------------------
class ICanvasInputAdapter {
public:
    virtual ~ICanvasInputAdapter() = default;

    virtual void onPress(CanvasView& view, QMouseEvent* event) = 0;
    virtual void onMove(CanvasView& view, QMouseEvent* event) = 0;
    virtual void onRelease(CanvasView& view, QMouseEvent* event) = 0;
    // 双击：适配器路由（桌面＝框选态空白适应窗口、交互图元/其余模式补发两次普通点击；
    //       触控＝窗口内适应窗口、越界中速连击补发普通点击——快速双轻点被 QPA 合成为本事件而非第二次 press）。
    virtual void onDoubleClick(CanvasView& view, QMouseEvent* event) = 0;
    virtual void onWheel(CanvasView& view, QWheelEvent* event) = 0;
    virtual void onContextMenu(CanvasView& view, QContextMenuEvent* event) = 0;

    // 取消等待中的单击落地（桌面：作废被扣的延迟单击；触控：委托桌面路由同作）。
    // 视图在模式切换 / Esc / 空格平移预备等「本单击不再有意义」的时机调用，避免延迟补发
    // 出迟到的单击动作（如框选态切到绘制态后补出一记退化顶点）。
    virtual void cancelPendingClick() {}

    // ——触控扩展点（默认实现即桌面语义，触控适配器按需覆写）——

    // 安装挂钩：视图构造末尾调用，实现可据此注册手势（如 grabGesture）。
    virtual void attach(CanvasView& /*view*/) {}

    // 覆盖层抓取带的屏幕等效尺寸（px）：手柄/抓边条带/点选阈值的屏幕基准
    //（桌面 8px（本项目实现选择，见 util/gui_nums.h nums::canvas，支撑 SPEC §8.2 拖拽能力）；触控 ≥ 12pt≈dp，SPEC §8.3 触控精度）。
    virtual qreal screenOverlayPx() const { return nums::canvas::kDesktopOverlayScreenPx; }

    // 手势事件（捏合缩放/平移等）；返回 true 表示已消费，视图随即停止下发。
    virtual bool onGestureEvent(CanvasView& /*view*/, QGestureEvent* /*event*/) { return false; }
};

} // namespace idc::gui
