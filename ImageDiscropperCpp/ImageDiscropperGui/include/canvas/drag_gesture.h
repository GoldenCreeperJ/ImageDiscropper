// ============================================================================
// 文件：canvas/drag_gesture.h
// 作用：单次「按下→移动→释放」平移手势的纯数据状态机——记 moved（本手势是否收到过非零位移）
//       与累计净位移；end 复位并吐 {wasClick, dx, dy}。点击/拖动判定用显式 moved 标志：
//       与设备像素阈值、视图缩放均无关（场景增量与设备像素不同口径，不可直接比较）。
// 分块依据：只做手势数学、不做送达——结果怎么翻译成信号/返回值由调用方（DraggableCanvasItem 及
//       各图元）决定；setPos 视觉反馈属 QGraphicsItem 侧操作，由 DraggableCanvasItem 在调用本类处完成。
// 说明：零 Qt、零外部依赖（位置以裸 (x, y) 传入），与 canvas/pick_cycle.h 同属
//       「零 Qt 可直编回归」纪律（可被 CTest unit_tests 直接编译）；header-only 故无需进链接清单。
// ============================================================================
#pragma once

namespace idc::gui {

// DragGesture：单次平移手势三态。start 起手势并复位；moveTo 逐帧记非零位移与累计净位移；
//   end 复位并返回 {wasClick, dx, dy}——wasClick＝全程无位移（原地点击），dx/dy＝累计净位移。
class DragGesture {
public:
    struct Result {
        bool wasClick{true};
        double dx{0.0};
        double dy{0.0};
    };

    void start(const double x, const double y) {
        active_ = true;
        moved_ = false;
        lastX_ = x;
        lastY_ = y;
        accX_ = 0.0;
        accY_ = 0.0;
    }

    void moveTo(const double x, const double y) {
        if (!active_) return;
        const double dx = x - lastX_;
        const double dy = y - lastY_;
        if (dx != 0.0 || dy != 0.0) moved_ = true;   // 收到过非零位移即判定为拖动（与缩放无关；纯点击不发 move）
        lastX_ = x;
        lastY_ = y;
        accX_ += dx;
        accY_ += dy;
    }

    Result end() {
        if (!active_) return Result{};   // 未起手势即结算：护栏与 moveTo 对称，返回默认 {wasClick=true, 0, 0}（不触发任何提交）
        const Result r{!moved_, accX_, accY_};
        active_ = false;
        moved_ = false;
        accX_ = 0.0;
        accY_ = 0.0;
        return r;
    }

    bool active() const { return active_; }
    double accX() const { return accX_; }
    double accY() const { return accY_; }

private:
    bool active_{false};
    bool moved_{false};
    double lastX_{0.0};
    double lastY_{0.0};
    double accX_{0.0};
    double accY_{0.0};
};

} // namespace idc::gui
