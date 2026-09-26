// ============================================================================
// 文件：canvas/canvas_desktop_input_adapter.h
// 作用：DesktopInputAdapter 声明——桌面（鼠标 / 滚轮 / 右键）输入适配器，
//       ICanvasInputAdapter 的当前唯一实现（GuideLine 阶段 2）。
// 分块依据：桌面设备语义（按键角色、滚轮刻度、QMenu 右键菜单 UI）全部收拢在本类，
//           使 CanvasView 的事件入口退化为一行转发；手势状态机与信号仍在 CanvasView。
// 说明：手势映射表（与迁移前 canvas_view.cpp 行为逐条一致，零行为变化）：
//         中键 / 空格+左键按下      → gesturePanBegin            （平移）
//         左键按下（绘制态）        → gestureStrokeBegin         （标注绘制起笔）
//         左键按下（普通）          → dispatchScenePress；无图元接管 → gestureMarqueeBegin（框选）
//         移动                      → 平移 / 绘制拖拽 / 绘制悬停 / 交付图元 + 框选更新 + 光标回报
//         滚轮一格                  → gestureStepZoom(×1.15 或 ÷1.15)（缩放锚定光标下）
//         右键（绘制态 / 普通）     → gestureStrokeFinish / 上下文菜单 → request*()
//       本类无状态：同一实例可服务任意多个 CanvasView。
// ============================================================================
#pragma once

#include "canvas/canvas_input_adapter.h"

namespace idc::gui {

// ---------------------------------------------------------------------------
// DesktopInputAdapter：桌面鼠标系输入 → 业务手势翻译（实现见 src/canvas/）。
// ---------------------------------------------------------------------------
class DesktopInputAdapter final : public ICanvasInputAdapter {
public:
    void onPress(CanvasView& view, QMouseEvent* event) override;
    void onMove(CanvasView& view, QMouseEvent* event) override;
    void onRelease(CanvasView& view, QMouseEvent* event) override;
    void onWheel(CanvasView& view, QWheelEvent* event) override;
    void onContextMenu(CanvasView& view, QContextMenuEvent* event) override;
};

} // namespace idc::gui
