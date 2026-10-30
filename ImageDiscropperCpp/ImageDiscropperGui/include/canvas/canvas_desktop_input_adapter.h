// ============================================================================
// 文件：canvas/canvas_desktop_input_adapter.h
// 作用：DesktopInputAdapter 声明——桌面（鼠标 / 滚轮 / 右键）输入适配器，
//       ICanvasInputAdapter 的桌面实现（触控单指合成序列亦复用本类作路由）。
// 分块依据：桌面设备语义（按键角色、滚轮刻度、QMenu 右键菜单 UI）全部收拢在本类，
//           使 CanvasView 的事件入口退化为一行转发；手势状态机与信号仍在 CanvasView。
// 说明：手势映射表（桌面键鼠事件 → CanvasView 业务手势）：
//         中键 / 空格+左键按下      → gesturePanBegin            （平移）
//         左键按下（绘制态）        → gestureStrokeBegin         （标注绘制起笔）
//         左键按下（普通）          → dispatchScenePress；无图元接管 → gestureMarqueeBegin（框选）
//         移动                      → 平移 / 绘制拖拽 / 绘制悬停 / 交付图元 + 框选更新 + 光标回报
//         滚轮一格                  → gestureStepZoom(×1.15 或 ÷1.15)（缩放锚定光标下）
//         右键（绘制态 / 普通）     → gestureStrokeFinish / 上下文菜单 → request*()
//         双击（QPA 合成）          → 非绘制态且落点无交互图元＝适应窗口；交互图元/绘制态＝补发两次普通点击
//       本类状态仅「单击延迟判定 / 未配对释放吞掉」两项（双击成立时单击不落地、合成对后的真实释放不重复路由）。
//       **单视图实例**：CanvasView 各持独立 unique_ptr，本类状态（含单一定时器与 heldView_）只服务所属视图，
//       不得跨视图共享实例。
// ============================================================================
#pragma once

#include <QPoint>
#include <QTimer>

#include "canvas/canvas_input_adapter.h"

namespace idc::gui {

// ---------------------------------------------------------------------------
// DesktopInputAdapter：桌面鼠标系输入 → 业务手势翻译（实现见 src/canvas/）。
// ---------------------------------------------------------------------------
class DesktopInputAdapter final : public ICanvasInputAdapter {
public:
    DesktopInputAdapter();

    void onPress(CanvasView& view, QMouseEvent* event) override;
    void onMove(CanvasView& view, QMouseEvent* event) override;
    void onRelease(CanvasView& view, QMouseEvent* event) override;
    // 双击（QPA 合成）：非绘制态空白＝窗口内双击适应窗口；
    //   交互图元/绘制态/越界中速连击＝补发普通点击（合成 press+release 对）。
    void onDoubleClick(CanvasView& view, QMouseEvent* event) override;
    void onWheel(CanvasView& view, QWheelEvent* event) override;
    void onContextMenu(CanvasView& view, QContextMenuEvent* event) override;

    // 取消等待中的单击落地（触控适配器在双击/长按/捏合路径调用：被扣的单击不得再补发）。
    void cancelPendingClick() override;
    // 清「吞下一记释放」标志（触控适配器自吞释放时调用：本层吞掉的释放不得让桌面标志残留到下一击）。
    void clearSwallowNextRelease() { swallowNextRelease_ = false; }
    // 是否扣住未判定的单击（触控适配器分派合成双击用：扣住＝窗口内双击，未扣＝中速连击补发普通点击）。
    bool pendingClickActive() const { return clickHeld_; }

private:
    // 按下/释放的纯路由（不含单击延迟判定）：正常路径与延迟补发共用。
    static void routePress(CanvasView& view, QMouseEvent* event);

    static void routeRelease(CanvasView& view, QMouseEvent* event);
    // 延迟窗口到期：补发被扣住的单击（合成 press+release 对，原位置原修饰键）。
    void flushPendingClick();
    // 把双击事件当普通点击补发（合成 press+release 对）：交互图元上的双击略过适应窗口、
    // 空画布上越界中速连击（首击已落地）两处共用。
    static void routeClickPair(CanvasView& view, const QMouseEvent* event);

    QTimer clickTimer_;                 // 单击延迟判定计时器（窗口＝kDoubleTapGapMs，本应用双击界限）
    bool clickHeld_{false};             // 已扣住一个干净左键按下（等判定单击/双击）
    bool swallowNextRelease_{false};    // 吞掉下一记左键释放：双击/兜底第二击的 press 已被消费（合成对/适应窗口），
                                        //   其随后的真实 release 不再路由（避免未配对释放落到场景）；
                                        //   任何新按下到达时先清此标志（触控层自吞释放后不得残留）。
    QPointF heldLocalPos_{};            // 被扣按下的位置（补发合成事件用）
    QPointF heldGlobalPos_{};
    Qt::KeyboardModifiers heldMods_{};
    CanvasView* heldView_{nullptr};     // 被扣按下所属视图（补发路由目标）
};

} // namespace idc::gui
